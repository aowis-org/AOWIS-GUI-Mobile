#include "render_lab.h"
#include "probe_scene.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QSGRendererInterface>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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
    case Phase::Rendering: return QStringLiteral("bgfx frame calls active (visual test required)");
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
        return QStringLiteral("Verify bgfx color/text AND QML overlays remain visible. Frame count alone is NOT proof of correct composition.");
    return QStringLiteral("Experimental: bgfx may swap a Qt-owned EGL surface. See docs/renderer-lab.md.");
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
                         this->phase.store(Phase::AwaitingSceneGraph);
                     }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::beforeRenderPassRecording,
                     window, [this]() { this->renderOnRenderThread(); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::sceneGraphInvalidated,
                     window, [this]() { this->shutdownOnRenderThread(); }, Qt::DirectConnection);
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
    this->lastWidth.store(static_cast<int>(init.swapChain.width));
    this->lastHeight.store(static_cast<int>(init.swapChain.height));
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    this->phase.store(Phase::Rendering);
    qInfo("AOWIS render lab: bgfx initialized with Qt EGL context");
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
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(std::min(width, 65535)),
                      static_cast<uint16_t>(std::min(height, 65535)));
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x102f45ff, 1.0f, 0);
    bgfx::touch(0);
    bgfx::dbgTextClear();
    const double zoom = this->viewZoom.load(std::memory_order_relaxed);
    const double panX = this->viewPanX.load(std::memory_order_relaxed);
    const double panY = this->viewPanY.load(std::memory_order_relaxed);
    // Leave the translucent/opaque QML panels outside the diagnostic content.
    bgfx::dbgTextPrintf(2, 5, 0x0b, "BGFX diagnostic scene - QML handles pan / zoom / tap");
    const int columns = std::clamp(width / 8 - 4, 12, 95);
    const int rows = std::clamp(height / 20 - 15, 6, 28);
    const std::vector<std::string> scene =
        AowisMobileProbe::createScene(columns, rows, zoom, panX, panY);
    for (size_t row = 0; row < scene.size(); ++row)
    {
        // Background palette index zero avoids opaque blue/yellow text bands.
        bgfx::dbgTextPrintf(2, static_cast<uint16_t>(8 + row), 0x0a,
                            "%s", scene[row].c_str());
    }
    // Pinned bgfx supports FLUSH: execute commands without presenting the
    // backbuffer. The Qt Quick scene graph presents after this callback.
    bgfx::frame(BGFX_FRAME_FLUSH);
    this->window->endExternalCommands();
    this->renderedFrames.fetch_add(1, std::memory_order_relaxed);
#endif
}

void RenderLab::shutdownOnRenderThread()
{
#if AOWIS_LAB_WITH_BGFX
    if (this->initialized)
    {
        bgfx::shutdown();
        this->initialized = false;
    }
#endif
    this->phase.store(Phase::Destroyed);
    this->lastWidth.store(0);
    this->lastHeight.store(0);
}

void RenderLab::refreshStatus()
{
    emit statusChanged();
}
