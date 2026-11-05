#include "render_lab.h"
#if AOWIS_LAB_WITH_BGFX
#include "gpu_scene_renderer.h"
#endif

#include <QDebug>
#include <QOpenGLContext>
#include <QSGRendererInterface>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>
#include <chrono>

#if AOWIS_LAB_WITH_BGFX
#include <bgfx/bgfx.h>
#endif

RenderLab::RenderLab(bool requestedBgfx, QObject *parent)
    : QObject(parent)
    , requestedBgfx(requestedBgfx)
    , phase(requestedBgfx ? Phase::AwaitingSceneGraph : Phase::Baseline)
{
    this->statusTimer.setInterval(250);
    QObject::connect(&this->statusTimer, &QTimer::timeout, this, &RenderLab::refreshStatus);
    this->statusTimer.start();
}

RenderLab::~RenderLab()
{
    this->statusTimer.stop();
}

bool RenderLab::bgfxMode() const
{
    return this->requestedBgfx;
}

quint64 RenderLab::frames() const
{
    return this->renderedFrames.load(std::memory_order_relaxed);
}

QString RenderLab::status() const
{
    switch (this->phase.load(std::memory_order_relaxed))
    {
    case Phase::Baseline: return QStringLiteral("QML-only baseline");
    case Phase::AwaitingSceneGraph: return QStringLiteral("Waiting for Qt Quick render thread");
    case Phase::NoEglContext: return QStringLiteral("No external EGL context available");
    case Phase::WrongGraphicsApi: return QStringLiteral("Qt Quick is not using OpenGL");
    case Phase::InitFailed: return QStringLiteral("bgfx::init() failed");
    case Phase::GeometryInitFailed: return QStringLiteral("GPU vertex/shader setup failed (check log)");
    case Phase::Rendering: return QStringLiteral("GPU geometry submitted; inspect actual scene");
    case Phase::Destroyed: return QStringLiteral("Scene graph released");
    }
    return QStringLiteral("Unknown");
}

QString RenderLab::details() const
{
    if (!this->requestedBgfx)
        return QStringLiteral("No bgfx initialized. Compare UI responsiveness with the experimental mode.");
    if (this->phase.load(std::memory_order_relaxed) == Phase::NoEglContext)
        return QStringLiteral("Requires a Qt-owned EGL/OpenGL context; GLX is not supported by this probe.");
    if (this->phase.load(std::memory_order_relaxed) == Phase::Rendering)
        return QStringLiteral("Vertex-color shaders: grid, pipes and nodes. Verify geometry + QML overlay during resize and gestures.");
    return QStringLiteral("Experimental: bgfx may swap a Qt-owned EGL surface. See docs/renderer-lab.md.");
}

QString RenderLab::timings() const
{
    if (!this->requestedBgfx)
        return QStringLiteral("Timing: baseline; no bgfx submission");
    const int64_t submit = this->submitMicroseconds.load(std::memory_order_relaxed);
    const int64_t interval = this->frameIntervalMicroseconds.load(std::memory_order_relaxed);
    const int64_t gpu = this->gpuMicroseconds.load(std::memory_order_relaxed);
    const QString gpuText = gpu < 0 ? QStringLiteral("N/A")
        : QStringLiteral("%1 ms").arg(double(gpu) / 1000.0, 0, 'f', 2);
    return QStringLiteral("bgfx submit CPU: %1 ms  |  frame interval: %2 ms\n"
                          "bgfx GPU (if supported): %3  |  draws: %4")
        .arg(double(submit) / 1000.0, 0, 'f', 2)
        .arg(double(interval) / 1000.0, 0, 'f', 2)
        .arg(gpuText).arg(this->drawCalls.load(std::memory_order_relaxed));
}

QString RenderLab::lifecycle() const
{
    return QStringLiteral("App: %1 | resumes: %2 | pauses: %3\n"
                          "Scenegraph: %4 inits / %5 releases | bgfx inits: %6")
        .arg(this->applicationActive.load() ? QStringLiteral("active") : QStringLiteral("inactive"))
        .arg(this->resumes.load()).arg(this->pauses.load())
        .arg(this->sceneGraphStarts.load()).arg(this->sceneGraphStops.load())
        .arg(this->bgfxInitializations.load());
}

void RenderLab::setApplicationActive(bool active)
{
    const bool wasActive = this->applicationActive.exchange(active);
    if (wasActive == active)
        return;
    if (active)
        this->resumes.fetch_add(1);
    else
        this->pauses.fetch_add(1);
    qInfo("AOWIS render lab: application %s", active ? "active" : "inactive");
    emit statusChanged();
}

void RenderLab::setView(double zoom, double panX, double panY)
{
    // Called by QML on the GUI thread. Atomics are read by the render thread.
    this->viewZoom.store(std::clamp(zoom, 0.3, 3.0), std::memory_order_relaxed);
    this->viewPanX.store(std::clamp(panX, -4096.0, 4096.0), std::memory_order_relaxed);
    this->viewPanY.store(std::clamp(panY, -4096.0, 4096.0), std::memory_order_relaxed);
}

void RenderLab::attach(QQuickWindow *window)
{
    this->window = window;
    if (!this->requestedBgfx)
        return;

    // The signals below execute on the Qt Quick render thread. They must
    // remain DirectConnections, with zero GUI-thread QObject manipulation.
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized,
                     window, [this]() {
                         this->sceneGraphStarts.fetch_add(1);
                         this->phase.store(Phase::AwaitingSceneGraph);
                         qInfo("AOWIS render lab: Qt scenegraph initialized");
                     }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::beforeRenderPassRecording,
                     window, [this]() { this->renderOnRenderThread(); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::sceneGraphInvalidated,
                     window, [this]() {
                         this->sceneGraphStops.fetch_add(1);
                         this->shutdownOnRenderThread();
                         qInfo("AOWIS render lab: Qt scenegraph invalidated");
                     }, Qt::DirectConnection);
}

void RenderLab::initializeOnRenderThread()
{
#if AOWIS_LAB_WITH_BGFX
    if (this->window->rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL)
    {
        this->phase.store(Phase::WrongGraphicsApi);
        return;
    }

    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (context == nullptr)
    {
        this->phase.store(Phase::NoEglContext);
        return;
    }
    const QNativeInterface::QEGLContext *egl =
        context->nativeInterface<QNativeInterface::QEGLContext>();
    if (egl == nullptr || egl->nativeContext() == nullptr)
    {
        this->phase.store(Phase::NoEglContext);
        return;
    }

    const QSize pixels = (this->window->size() * this->window->devicePixelRatio());
    bgfx::Init init;
    init.type = context->isOpenGLES() ? bgfx::RendererType::OpenGLES : bgfx::RendererType::OpenGL;
    init.platformData.context = static_cast<void *>(egl->nativeContext());
    // Current bgfx API (pinned by tools/init_bgfx.sh): window dimensions
    // belong to Init::swapChain; reset now contains device-global flags.
    init.swapChain.width = static_cast<uint32_t>(std::max(1, pixels.width()));
    init.swapChain.height = static_cast<uint32_t>(std::max(1, pixels.height()));
    init.reset = BGFX_RESET_NONE; // Qt owns presentation and vsync.

    // The bgfx context is borrowed from Qt; no second render thread is used.
    // The frame is submitted with BGFX_FRAME_FLUSH to request rendering
    // without a bgfx buffer swap. Qt remains the presentation owner.
    if (!bgfx::init(init))
    {
        this->phase.store(Phase::InitFailed);
        qWarning("AOWIS render lab: bgfx::init failed");
        return;
    }
    this->initialized = true;
    this->bgfxInitializations.fetch_add(1);
    this->lastWidth.store(static_cast<int>(init.swapChain.width));
    this->lastHeight.store(static_cast<int>(init.swapChain.height));
    bgfx::setDebug(BGFX_DEBUG_NONE);
    this->gpuScene = new GpuSceneRenderer();
    if (!this->gpuScene->initialize())
    {
        delete this->gpuScene;
        this->gpuScene = nullptr;
        bgfx::shutdown();
        this->initialized = false;
        this->phase.store(Phase::GeometryInitFailed);
        return;
    }
    this->phase.store(Phase::Rendering);
    qInfo("AOWIS render lab: bgfx initialized with Qt EGL context (backend: %s)",
          context->isOpenGLES() ? "GLES" : "OpenGL");
#else
    this->phase.store(Phase::InitFailed);
#endif
}

void RenderLab::renderOnRenderThread()
{
#if AOWIS_LAB_WITH_BGFX
    const Phase state = this->phase.load();
    if (!this->initialized && state != Phase::AwaitingSceneGraph)
        return;

    // A Qt-owned GL context and render pass are active at this signal.
    // Quarantine all bgfx calls inside the external command state fence.
    this->window->beginExternalCommands();
    if (!this->initialized)
    {
        this->initializeOnRenderThread();
        if (!this->initialized)
        {
            this->window->endExternalCommands();
            return;
        }
    }

    const QSize pixels = (this->window->size() * this->window->devicePixelRatio());
    const int width = std::max(1, pixels.width());
    const int height = std::max(1, pixels.height());

    if (this->lastWidth.load() != width || this->lastHeight.load() != height)
    {
        // Qt resizes the borrowed EGL drawable. With no bgfx-owned native
        // window, passing a SwapChain to reset() would violate bgfx's API
        // contract. Refresh global device state and use the new view size.
        bgfx::reset(BGFX_RESET_NONE);
        this->lastWidth.store(width);
        this->lastHeight.store(height);
    }
    const std::chrono::steady_clock::time_point tick = std::chrono::steady_clock::now();
    const int64_t nowMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        tick.time_since_epoch()).count();
    if (this->lastFrameTimestampMicroseconds != 0)
        this->frameIntervalMicroseconds.store(
            nowMicroseconds - this->lastFrameTimestampMicroseconds, std::memory_order_relaxed);
    this->lastFrameTimestampMicroseconds = nowMicroseconds;

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const double devicePixelRatio = this->window->devicePixelRatio();
    this->gpuScene->render(width, height, devicePixelRatio,
                           this->viewZoom.load(std::memory_order_relaxed),
                           this->viewPanX.load(std::memory_order_relaxed),
                           this->viewPanY.load(std::memory_order_relaxed));
    // Do not present bgfx's borrowed context: Qt Quick owns swap/presentation.
    bgfx::frame(BGFX_FRAME_FLUSH);
    const bgfx::Stats *stats = bgfx::getStats();
    if (stats != nullptr)
    {
        this->drawCalls.store(stats->numDraw, std::memory_order_relaxed);
        if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
        {
            this->gpuMicroseconds.store(
                (stats->gpuTimeEnd - stats->gpuTimeBegin) * INT64_C(1000000)
                  / stats->gpuTimerFreq, std::memory_order_relaxed);
        }
    }
    const int64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    this->submitMicroseconds.store(elapsed, std::memory_order_relaxed);
    this->window->endExternalCommands();
    this->renderedFrames.fetch_add(1, std::memory_order_relaxed);
#endif
}

void RenderLab::shutdownOnRenderThread()
{
#if AOWIS_LAB_WITH_BGFX
    if (this->initialized)
    {
        if (this->gpuScene != nullptr)
        {
            this->gpuScene->release();
            delete this->gpuScene;
            this->gpuScene = nullptr;
        }
        bgfx::shutdown();
        this->initialized = false;
    }
#endif
    this->phase.store(Phase::Destroyed);
    this->lastWidth.store(0);
    this->lastHeight.store(0);
    this->lastFrameTimestampMicroseconds = 0;
    this->submitMicroseconds.store(0);
    this->frameIntervalMicroseconds.store(0);
    this->gpuMicroseconds.store(-1);
    this->drawCalls.store(0);
}

void RenderLab::refreshStatus()
{
    emit statusChanged();
}
