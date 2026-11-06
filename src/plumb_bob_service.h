#pragma once

#include "plumb_bob_math.h"

#include <QAccelerometer>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class QAbstractAnimation;

// Independently owned, foreground/page-scoped accelerometer instrument.
// Sensor math, denoising and display clock are all native C++; QML only draws.
class PlumbBobService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY changed)
    Q_PROPERTY(bool suitable READ suitable NOTIFY changed)
    Q_PROPERTY(double downX READ downX NOTIFY directionChanged)
    Q_PROPERTY(double downY READ downY NOTIFY directionChanged)
    Q_PROPERTY(double edgeDegrees READ edgeDegrees NOTIFY directionChanged)
    Q_PROPERTY(bool plumb READ plumb NOTIFY directionChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool sensorActive READ sensorActive NOTIFY changed)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY changed)

public:
    explicit PlumbBobService(QObject *parent = nullptr);
    bool hasReading() const;
    bool suitable() const;
    double downX() const;
    double downY() const;
    double edgeDegrees() const;
    bool plumb() const;
    QString status() const;
    bool sensorActive() const;
    int sampleCount() const;
    bool pageVisible() const;
    Q_INVOKABLE void setPageVisible(bool visible);
    void setApplicationActive(bool active);

signals:
    void changed();
    void directionChanged();
    void pageVisibleChanged(bool visible);

private:
    enum class State { Paused, Starting, Tracking, TooFlat, Stale, NoSamples, Unavailable, Error };
    void updateActivity();
    void handleReading();
    void handleTimeout();
    void advanceDisplay();
    void updateScreenRotation();
    void changeState(State state);
    AowisPlumbBob::ScreenDirection displayedDirection() const;

    QAccelerometer m_accelerometer;
    QElapsedTimer m_clock;
    QTimer m_watchdog;
    QAbstractAnimation *m_animation = nullptr;
    AowisPlumbBob::Filter m_filter;
    State m_state = State::Paused;
    qint64 m_lastReadingMs = -1;
    qint64 m_startedMs = -1;
    int m_sampleCount = 0;
    int m_screenRotation = 0;
    bool m_applicationActive = false;
    bool m_pageVisible = false;
};
