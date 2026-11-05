#include "render_lab.h"
#include "gpu_scene_geometry.h"
#if AOWIS_LAB_WITH_BGFX
#include "gpu_scene_renderer.h"
#endif

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QMutexLocker>
#include <QList>
#include <QOpenGLContext>
#include <QSGRendererInterface>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <vector>

namespace
{
    int64_t steadyMicroseconds()
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    }

    QString formatMs(int64_t microseconds)
    {
        return microseconds < 0 ? QStringLiteral("N/A")
            : QString::number(double(microseconds) / 1000.0, 'f', 2);
    }

    uint64_t rssKilobytes()
    {
        QFile file(QStringLiteral("/proc/self/status"));
        if (!file.open(QIODevice::ReadOnly))
            return 0;
        while (!file.atEnd())
        {
            const QByteArray line = file.readLine();
            if (line.startsWith("VmRSS:"))
            {
                const QList<QByteArray> fields = line.simplified().split(' ');
                return fields.size() >= 2 ? fields.at(1).toULongLong() : 0;
            }
        }
        return 0;
    }
}


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

QString RenderLab::recoveryStatus() const
{
    const int64_t nowUs = steadyMicroseconds();
    const QString progress = this->recoveryWatchdog.pending()
        ? QStringLiteral("waiting %1 ms").arg(this->recoveryWatchdog.ageUs(nowUs) / 1000)
        : QStringLiteral("idle");
    return QStringLiteral("Recovery: %1 | OK %2 / stalled %3 / late %4\n"
                          "Worst: %5 ms | release without GL: %6 | %7\n"
                          "%8")
        .arg(progress).arg(this->recoveries).arg(this->recoveryTimeouts)
        .arg(this->lateRecoveries).arg(formatMs(this->worstRecoveryUs))
        .arg(this->absentGlContextOnRelease.load(std::memory_order_relaxed))
        .arg(this->lastRecoveryMessage)
        .arg(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             + QStringLiteral("/lifecycle/events.csv"));
}

void RenderLab::recordLifecycleEvent(const QString &event, int64_t durationUs)
{
    // GUI thread only; persist individual events so a subsequent process crash
    // does not erase previous observations. All event strings are constants.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/lifecycle");
    if (!QDir().mkpath(directory))
    {
        qWarning("AOWIS lifecycle: cannot create event directory");
        return;
    }
    QFile file(directory + QStringLiteral("/events.csv"));
    const bool first = !file.exists() || file.size() == 0;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        qWarning("AOWIS lifecycle: cannot write event log");
        return;
    }
    QTextStream stream(&file);
    if (first)
        stream << "utc,event,duration_us,app_active,exposed,frames,sg_init,sg_release,"
                  "bgfx_init,missing_gl_release,preset,tiles,vertex_bytes,rss_kb,width,height\n";
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ','
           << event << ',' << durationUs << ',' << (this->applicationActive.load() ? 1 : 0) << ','
           << (this->window != nullptr && this->window->isExposed() ? 1 : 0) << ','
           << this->renderedFrames.load() << ',' << this->sceneGraphStarts.load() << ','
           << this->sceneGraphStops.load() << ',' << this->bgfxInitializations.load() << ','
           << this->absentGlContextOnRelease.load() << ',' << this->workload.load() << ','
           << this->cachedTiles.load() << ',' << this->gpuVertexBytes.load() << ','
           << rssKilobytes() << ',' << (this->window != nullptr ? this->window->width() : 0)
           << ',' << (this->window != nullptr ? this->window->height() : 0) << '\n';
    stream.flush();
    if (!file.flush())
        qWarning("AOWIS lifecycle: event log flush failed");
    qInfo().noquote() << "AOWIS lifecycle:" << event
                      << "duration_us=" << durationUs
                      << "bgfx_inits=" << this->bgfxInitializations.load()
                      << "frames=" << this->renderedFrames.load();
}

void RenderLab::beginRecovery(const QString &reason, int64_t eventUs)
{
    if (!this->requestedBgfx)
        return;
    this->recoveryReason = reason;
    this->recoveryWatchdog.arm(eventUs);
    this->lastRecoveryMessage = QStringLiteral("Expecting a new bgfx frame after %1").arg(reason);
    this->recordLifecycleEvent(QStringLiteral("watch_") + reason);
}

void RenderLab::inspectRecovery()
{
    if (!this->requestedBgfx)
        return;
    // sceneGraphInvalidated runs on Qt's render thread. Consume its atomic
    // timestamp on the GUI thread, without touching GUI objects there.
    const uint32_t releases = this->sceneGraphStops.load(std::memory_order_relaxed);
    if (releases != this->observedInvalidations)
    {
        this->observedInvalidations = releases;
        const int64_t timestampUs = this->lastSceneGraphInvalidationUs.load();
        if (timestampUs != 0 && this->invalidationWasActive.load())
            this->beginRecovery(QStringLiteral("scene_graph_recreate"), timestampUs);
        this->recordLifecycleEvent(QStringLiteral("scene_graph_released"));
    }
    int64_t lastFrameUs = this->lastCompletedFrameUs.load(std::memory_order_acquire);
    if (this->recoveryReason == QStringLiteral("explicit_gpu_recreate") &&
        this->lastFrameGpuGeneration.load(std::memory_order_relaxed)
            <= this->explicitRecreateStartingGeneration)
        lastFrameUs = 0; // Old-generation frames do not count as recovery.
    const AowisMobileLifecycle::Outcome outcome = this->recoveryWatchdog.poll(
        steadyMicroseconds(), lastFrameUs,
        this->applicationActive.load(), this->window != nullptr && this->window->isExposed());
    if (outcome == AowisMobileLifecycle::Outcome::Recovered ||
        outcome == AowisMobileLifecycle::Outcome::LateRecovered)
    {
        if (outcome == AowisMobileLifecycle::Outcome::LateRecovered)
            ++this->lateRecoveries;
        else
            ++this->recoveries;
        const int64_t duration = this->recoveryWatchdog.lastDurationUs();
        this->worstRecoveryUs = std::max(this->worstRecoveryUs, duration);
        this->lastRecoveryMessage = QStringLiteral("%1: frame recovered in %2 ms")
            .arg(this->recoveryReason).arg(formatMs(duration));
        this->recordLifecycleEvent(outcome == AowisMobileLifecycle::Outcome::LateRecovered
                                   ? QStringLiteral("late_recovery") : QStringLiteral("recovered"), duration);
    }
    else if (outcome == AowisMobileLifecycle::Outcome::TimedOut)
    {
        ++this->recoveryTimeouts;
        this->lastRecoveryMessage = QStringLiteral("STALLED after %1; no bgfx frame in 5 seconds")
            .arg(this->recoveryReason);
        this->recordLifecycleEvent(QStringLiteral("timeout"));
    }
}

void RenderLab::noteSurfaceResize()
{
    if (!this->requestedBgfx || !this->applicationActive.load())
        return;
    // Width/height can change separately during rotation. Defer checking
    // until geometry has settled, while retaining the first transition time.
    if (this->recoveryReason == QStringLiteral("resize") && this->recoveryWatchdog.pending())
        return;
    this->beginRecovery(QStringLiteral("resize"), steadyMicroseconds());
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
    if (!active)
    {
        this->finishBenchmark(true);
        this->recoveryWatchdog.cancel();
    }
    else
        this->beginRecovery(QStringLiteral("resume"), steadyMicroseconds());
    this->recordLifecycleEvent(active ? QStringLiteral("app_active") : QStringLiteral("app_inactive"));
    qInfo("AOWIS Mobile: application %s", active ? "active" : "inactive");
    emit statusChanged();
}

void RenderLab::setView(double zoom, double panX, double panY)
{
    // Called by QML on the GUI thread. Atomics are read by the render thread.
    this->viewZoom.store(std::clamp(zoom, 0.3, 3.0), std::memory_order_relaxed);
    this->viewPanX.store(std::clamp(panX, -4096.0, 4096.0), std::memory_order_relaxed);
    this->viewPanY.store(std::clamp(panY, -4096.0, 4096.0), std::memory_order_relaxed);
}

QString RenderLab::sceneStats() const
{
    const int preset = this->workload.load(std::memory_order_relaxed);
    if (preset == 0)
        return QStringLiteral("Scene: small GPU demonstration (8 nodes / 7 pipes)");
    const AowisMobileGpu::StressProfile profile = AowisMobileGpu::stressProfile(preset);
    const uint64_t kb = rssKilobytes();
    const QString rss = kb == 0 ? QStringLiteral("N/A")
        : QStringLiteral("%1 MiB").arg(double(kb) / 1024.0, 0, 'f', 1);
    return QStringLiteral("Network: %1 nodes + %2 pipes = %3 total\n"
                          "Visible: %4 nodes / %5 pipes | %6 tiles | LOD 1:%7\n"
                          "GPU cache: %8 tiles / %9 MiB vertex data | uploads/frame: %10 | RSS: %11")
        .arg(QString::number(static_cast<qulonglong>(profile.nodes))).arg(QString::number(static_cast<qulonglong>(profile.pipes)))
        .arg(QString::number(static_cast<qulonglong>(profile.features)))
        .arg(this->nodesDrawn.load()).arg(this->pipesDrawn.load())
        .arg(this->visibleTiles.load()).arg(this->lodStride.load())
        .arg(this->cachedTiles.load())
        .arg(double(this->gpuVertexBytes.load()) / (1024.0 * 1024.0), 0, 'f', 1)
        .arg(this->tileUploads.load()).arg(rss);
}

bool RenderLab::benchmarkActive() const
{
    QMutexLocker locker(&this->benchmarkMutex);
    return this->capturing;
}

QString RenderLab::benchmarkResult() const
{
    return this->lastBenchmarkResult;
}

void RenderLab::requestGpuRecreation()
{
    // This is a deliberate RESOURCE lifecycle test while Qt's context is
    // valid. It does not pretend to simulate an externally lost EGL context.
    if (!this->requestedBgfx || !this->applicationActive.load() ||
        this->phase.load() != Phase::Rendering ||
        this->recreateGpuRequested.exchange(true))
        return;
    this->finishBenchmark(true);
    this->explicitRecreateStartingGeneration = this->bgfxInitializations.load();
    this->beginRecovery(QStringLiteral("explicit_gpu_recreate"), steadyMicroseconds());
    if (this->window != nullptr)
        this->window->update();
}

void RenderLab::setWorkload(int preset)
{
    if (preset < 0 || preset > 4)
        return;
    {
        QMutexLocker locker(&this->benchmarkMutex);
        if (this->capturing)
            return; // Workload must remain fixed for a benchmark sample.
    }
    this->workload.store(preset, std::memory_order_relaxed);
    this->lastBenchmarkResult.clear();
    emit statusChanged();
}

void RenderLab::startBenchmark()
{
    if (!this->requestedBgfx || !this->applicationActive.load() ||
        this->phase.load() != Phase::Rendering)
        return;
    {
        QMutexLocker locker(&this->benchmarkMutex);
        if (this->capturing)
            return;
        this->capturing = true;
        this->captureSamples.clear();
        this->captureSamples.reserve(1200);
        this->captureStartUs = steadyMicroseconds();
        this->captureStartRssKb = rssKilobytes();
        this->captureInvalidated.store(false);
    }
    this->lastBenchmarkResult = QStringLiteral("Recording 15 seconds; keep workload unchanged");
    const uint64_t token = ++this->captureToken;
    QTimer::singleShot(15000, this, [this, token]() {
        if (this->captureToken == token)
            this->finishBenchmark(false);
    });
    emit statusChanged();
}

void RenderLab::finishBenchmark(bool interrupted)
{
    std::vector<AowisMobileBench::FrameSample> samples;
    uint64_t startRssKb = 0;
    {
        QMutexLocker locker(&this->benchmarkMutex);
        if (!this->capturing)
            return;
        this->capturing = false;
        samples.swap(this->captureSamples);
        startRssKb = this->captureStartRssKb;
    }
    if (interrupted)
    {
        this->lastBenchmarkResult = QStringLiteral("Capture interrupted by application pause; discarded");
        emit statusChanged();
        return;
    }
    if (samples.size() < 10)
    {
        this->lastBenchmarkResult = QStringLiteral("Not enough frames captured; no report saved");
        emit statusChanged();
        return;
    }

    const uint64_t endRssKb = rssKilobytes();
    const std::vector<int64_t> intervals = AowisMobileBench::intervals(samples);
    const std::vector<int64_t> submits = AowisMobileBench::submissions(samples);
    const std::vector<int64_t> gpus = AowisMobileBench::gpuTimes(samples);
    const int64_t median = AowisMobileBench::percentile(intervals, 0.5);
    const int64_t p95 = AowisMobileBench::percentile(intervals, 0.95);
    const int64_t p99 = AowisMobileBench::percentile(intervals, 0.99);
    const int64_t cpu95 = AowisMobileBench::percentile(submits, 0.95);
    const int64_t gpu95 = AowisMobileBench::percentile(gpus, 0.95);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/benchmarks");
    if (!QDir().mkpath(directory))
    {
        this->lastBenchmarkResult = QStringLiteral("Cannot create benchmark output directory");
        emit statusChanged();
        return;
    }
    const QString name = QStringLiteral("network-%1-%2.csv")
        .arg(this->workload.load())
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const QString output = directory + QStringLiteral("/") + name;
    QSaveFile file(output);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        this->lastBenchmarkResult = QStringLiteral("Cannot open CSV report for writing");
        emit statusChanged();
        return;
    }
    QTextStream stream(&file);
    const AowisMobileGpu::StressProfile profile = AowisMobileGpu::stressProfile(this->workload.load());
    stream << "# AOWIS Mobile GPU benchmark, 15 s, steady_clock; CPU/GPU timing as reported by bgfx\n";
    stream << "# Device: " << QSysInfo::prettyProductName() << "\n";
    stream << "# Preset: " << this->workload.load() << "; logical nodes=" << static_cast<qulonglong>(profile.nodes)
           << "; logical pipes=" << static_cast<qulonglong>(profile.pipes)
           << "; total=" << static_cast<qulonglong>(profile.features) << "\n";
    stream << "# Visible-only GPU tiles; bounded tile cache; adaptive LOD; background captures discarded\n";
    stream << "# Process RSS beginning KB=" << startRssKb
           << "; end KB=" << endRssKb << " (may include Qt/driver allocations)\n";
    stream << "# p50_interval_us=" << median << "; p95_interval_us=" << p95
           << "; p99_interval_us=" << p99 << "; p95_cpu_submit_us=" << cpu95
           << "; p95_gpu_us=" << gpu95 << "\n";
    stream << "elapsed_us,interval_us,submit_us,gpu_us,draws,visible_tiles,uploads,lod_stride,"
              "nodes_drawn,pipes_drawn,vertex_buffer_bytes\n";
    for (const AowisMobileBench::FrameSample &sample : samples)
        stream << sample.elapsedUs << ',' << sample.frameIntervalUs << ',' << sample.submitUs
               << ',' << sample.gpuUs << ',' << sample.draws << ',' << sample.tiles
               << ',' << sample.uploads << ',' << sample.stride << ','
               << sample.nodesDrawn << ',' << sample.pipesDrawn << ','
               << sample.vertexBytes << '\n';
    stream.flush();
    if (!file.commit())
    {
        this->lastBenchmarkResult = QStringLiteral("Failed to save CSV report");
        emit statusChanged();
        return;
    }
    this->lastBenchmarkResult = QStringLiteral("%1 frames | interval p50/p95/p99: %2 / %3 / %4 ms\n"
        "CPU p95: %5 ms | GPU p95: %6 ms | CSV: %7")
        .arg(qulonglong(samples.size())).arg(formatMs(median)).arg(formatMs(p95))
        .arg(formatMs(p99)).arg(formatMs(cpu95)).arg(formatMs(gpu95)).arg(output);
    qInfo().noquote() << "AOWIS Mobile benchmark:" << this->lastBenchmarkResult;
    emit statusChanged();
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
                         this->invalidationWasActive.store(this->applicationActive.load());
                         this->lastSceneGraphInvalidationUs.store(steadyMicroseconds());
                         this->sceneGraphStops.fetch_add(1);
                         if (QOpenGLContext::currentContext() == nullptr)
                             this->absentGlContextOnRelease.fetch_add(1);
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
    if (this->recreateGpuRequested.exchange(false))
    {
        // Only the Qt Quick render thread calls bgfx::shutdown/init, and only
        // while Qt's EGL context is current. This tests reinitialization of
        // the shader programs and entire retained tile cache on demand.
        this->shutdownOnRenderThread();
        this->phase.store(Phase::AwaitingSceneGraph);
        qInfo("AOWIS lifecycle: explicit GPU resource recreation");
    }
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
    const int64_t nowMicroseconds = steadyMicroseconds();
    if (this->lastFrameTimestampMicroseconds != 0)
        this->frameIntervalMicroseconds.store(
            nowMicroseconds - this->lastFrameTimestampMicroseconds, std::memory_order_relaxed);
    this->lastFrameTimestampMicroseconds = nowMicroseconds;

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const double devicePixelRatio = this->window->devicePixelRatio();
    const GpuSceneMetrics sceneMetrics = this->gpuScene->render(
        width, height, devicePixelRatio,
        this->viewZoom.load(std::memory_order_relaxed),
        this->viewPanX.load(std::memory_order_relaxed),
        this->viewPanY.load(std::memory_order_relaxed),
        this->workload.load(std::memory_order_relaxed));
    this->visibleTiles.store(sceneMetrics.visibleTiles, std::memory_order_relaxed);
    this->cachedTiles.store(sceneMetrics.residentTiles, std::memory_order_relaxed);
    this->tileUploads.store(sceneMetrics.uploadsThisFrame, std::memory_order_relaxed);
    this->lodStride.store(sceneMetrics.stride, std::memory_order_relaxed);
    this->nodesDrawn.store(sceneMetrics.sampledNodes, std::memory_order_relaxed);
    this->pipesDrawn.store(sceneMetrics.sampledPipes, std::memory_order_relaxed);
    this->gpuVertexBytes.store(sceneMetrics.gpuVertexBytes, std::memory_order_relaxed);
    // Do not present bgfx's borrowed context: Qt Quick owns swap/presentation.
    bgfx::frame(BGFX_FRAME_FLUSH);
    this->gpuMicroseconds.store(-1, std::memory_order_relaxed);
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
    this->lastFrameGpuGeneration.store(this->bgfxInitializations.load(), std::memory_order_relaxed);
    this->lastCompletedFrameUs.store(steadyMicroseconds(), std::memory_order_release);
    {
        QMutexLocker locker(&this->benchmarkMutex);
        if (this->capturing)
        {
            AowisMobileBench::FrameSample sample;
            sample.elapsedUs = nowMicroseconds - this->captureStartUs;
            sample.frameIntervalUs = this->frameIntervalMicroseconds.load();
            sample.submitUs = elapsed;
            sample.gpuUs = this->gpuMicroseconds.load();
            sample.draws = this->drawCalls.load();
            sample.tiles = sceneMetrics.visibleTiles;
            sample.uploads = sceneMetrics.uploadsThisFrame;
            sample.stride = sceneMetrics.stride;
            sample.nodesDrawn = sceneMetrics.sampledNodes;
            sample.pipesDrawn = sceneMetrics.sampledPipes;
            sample.vertexBytes = sceneMetrics.gpuVertexBytes;
            this->captureSamples.push_back(sample);
        }
    }
#endif
}

void RenderLab::shutdownOnRenderThread()
{
    this->captureInvalidated.store(true);
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
    this->visibleTiles.store(0);
    this->cachedTiles.store(0);
    this->tileUploads.store(0);
    this->lodStride.store(1);
    this->nodesDrawn.store(0);
    this->pipesDrawn.store(0);
    this->gpuVertexBytes.store(0);
}

void RenderLab::refreshStatus()
{
    this->inspectRecovery();
    if (this->captureInvalidated.exchange(false))
        this->finishBenchmark(true);
    emit statusChanged();
}
