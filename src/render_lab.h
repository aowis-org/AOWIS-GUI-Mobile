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

public:
    explicit RenderLab(bool requestedBgfx, QObject *parent = nullptr);
    ~RenderLab() override;

    bool bgfxMode() const;
    QString status() const;
    QString details() const;
    quint64 frames() const;
    void attach(QQuickWindow *window);

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
    bool initialized = false; // Render thread only.
    QQuickWindow *window = nullptr;
    QTimer statusTimer;
};
