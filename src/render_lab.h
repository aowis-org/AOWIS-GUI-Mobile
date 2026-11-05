#pragma once

#include <QObject>
#include <QQuickWindow>
#include <QMutex>
#include <QString>

#include "performance_samples.h"
#include "lifecycle_watchdog.h"

#include <vector>
#include <QTimer>

#include <atomic>
#include <cstdint>

class RenderLab final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool bgfxMode READ bgfxMode CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString details READ details NOTIFY statusChanged)
    Q_PROPERTY(quint64 frames READ frames NOTIFY statusChanged)
    Q_PROPERTY(QString timings READ timings NOTIFY statusChanged)
    Q_PROPERTY(QString lifecycle READ lifecycle NOTIFY statusChanged)
    Q_PROPERTY(QString recoveryStatus READ recoveryStatus NOTIFY statusChanged)
    Q_PROPERTY(QString sceneStats READ sceneStats NOTIFY statusChanged)
    Q_PROPERTY(bool benchmarkActive READ benchmarkActive NOTIFY statusChanged)
    Q_PROPERTY(QString benchmarkResult READ benchmarkResult NOTIFY statusChanged)

public:
    explicit RenderLab(bool requestedBgfx, QObject *parent = nullptr);
    ~RenderLab() override;

    bool bgfxMode() const;
    QString status() const;
    QString details() const;
    quint64 frames() const;
    QString timings() const;
    QString lifecycle() const;
    QString recoveryStatus() const;
    QString sceneStats() const;
    bool benchmarkActive() const;
    QString benchmarkResult() const;
    void setApplicationActive(bool active);
    void noteSurfaceResize();
    void attach(QQuickWindow *window);
    Q_INVOKABLE void setView(double zoom, double panX, double panY);
    Q_INVOKABLE void setWorkload(int preset);
    Q_INVOKABLE void startBenchmark();
    Q_INVOKABLE void requestGpuRecreation();

signals:
    void statusChanged();

private:
    enum class Phase : int
    {
        Baseline,
        AwaitingSceneGraph,
        NoEglContext,
        WrongGraphicsApi,
        InitFailed,
        GeometryInitFailed,
        Rendering,
        Destroyed
    };

    void initializeOnRenderThread();
    void renderOnRenderThread();
    void shutdownOnRenderThread();
    void refreshStatus();
    void finishBenchmark(bool interrupted);
    void beginRecovery(const QString &reason, int64_t eventUs);
    void inspectRecovery();
    void recordLifecycleEvent(const QString &name, int64_t durationUs = -1);

    const bool requestedBgfx;
    std::atomic<Phase> phase;
    std::atomic<quint64> renderedFrames{0};
    std::atomic<int> lastWidth{0};
    std::atomic<int> lastHeight{0};
    std::atomic<int64_t> submitMicroseconds{0};
    std::atomic<int64_t> frameIntervalMicroseconds{0};
    std::atomic<int64_t> gpuMicroseconds{-1};
    std::atomic<uint32_t> drawCalls{0};
    std::atomic<int> workload{0};
    std::atomic<uint32_t> visibleTiles{0};
    std::atomic<uint32_t> cachedTiles{0};
    std::atomic<uint32_t> tileUploads{0};
    std::atomic<uint32_t> lodStride{1};
    std::atomic<uint32_t> nodesDrawn{0};
    std::atomic<uint32_t> pipesDrawn{0};
    std::atomic<uint64_t> gpuVertexBytes{0};
    std::atomic<uint32_t> sceneGraphStarts{0};
    std::atomic<uint32_t> sceneGraphStops{0};
    std::atomic<uint32_t> bgfxInitializations{0};
    std::atomic<uint32_t> resumes{0};
    std::atomic<uint32_t> pauses{0};
    std::atomic<int64_t> lastCompletedFrameUs{0};
    std::atomic<uint32_t> lastFrameGpuGeneration{0};
    uint32_t explicitRecreateStartingGeneration = 0; // GUI thread only.
    std::atomic<int64_t> lastSceneGraphInvalidationUs{0};
    std::atomic<bool> invalidationWasActive{false};
    std::atomic<uint32_t> absentGlContextOnRelease{0};
    AowisMobileLifecycle::Watchdog recoveryWatchdog; // GUI thread only.
    QString recoveryReason; // GUI thread only.
    QString lastRecoveryMessage = QStringLiteral("Awaiting first active frame");
    uint32_t observedInvalidations = 0; // GUI thread only.
    uint32_t recoveries = 0; // GUI thread only.
    uint32_t recoveryTimeouts = 0; // GUI thread only.
    uint32_t lateRecoveries = 0; // GUI thread only.
    int64_t worstRecoveryUs = 0; // GUI thread only.
    std::atomic<bool> applicationActive{false};
    int64_t lastFrameTimestampMicroseconds = 0; // Render thread only.
    mutable QMutex benchmarkMutex;
    bool capturing = false; // Protected by benchmarkMutex.
    uint64_t captureToken = 0; // GUI thread only.
    std::atomic<bool> captureInvalidated{false};
    std::atomic<bool> recreateGpuRequested{false};
    uint64_t captureStartRssKb = 0; // Mutex-protected.
    int64_t captureStartUs = 0; // Protected by benchmarkMutex.
    std::vector<AowisMobileBench::FrameSample> captureSamples; // Mutex-protected.
    QString lastBenchmarkResult; // GUI thread only.
    std::atomic<double> viewZoom{1.0};
    std::atomic<double> viewPanX{0.0};
    std::atomic<double> viewPanY{0.0};
#if AOWIS_LAB_WITH_BGFX
    class GpuSceneRenderer *gpuScene = nullptr; // Owned on render thread.
#endif
    bool initialized = false; // Render thread only.
    QQuickWindow *window = nullptr;
    QTimer statusTimer;
};
