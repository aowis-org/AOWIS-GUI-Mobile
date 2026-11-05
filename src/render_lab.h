#pragma once

#include <QObject>
#include <QQuickWindow>
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

public:
    explicit RenderLab(bool requestedBgfx, QObject *parent = nullptr);
    ~RenderLab() override;

    bool bgfxMode() const;
    QString status() const;
    QString details() const;
    quint64 frames() const;
    QString timings() const;
    void attach(QQuickWindow *window);
    Q_INVOKABLE void setView(double zoom, double panX, double panY);

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

    const bool requestedBgfx;
    std::atomic<Phase> phase;
    std::atomic<quint64> renderedFrames{0};
    std::atomic<int> lastWidth{0};
    std::atomic<int> lastHeight{0};
    std::atomic<int64_t> submitMicroseconds{0};
    std::atomic<int64_t> frameIntervalMicroseconds{0};
    std::atomic<int64_t> gpuMicroseconds{-1};
    std::atomic<uint32_t> drawCalls{0};
    int64_t lastFrameTimestampMicroseconds = 0; // Render thread only.
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
