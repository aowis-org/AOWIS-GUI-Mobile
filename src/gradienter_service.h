#pragma once

#include "gradienter_math.h"

#include <QAccelerometer>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class QAbstractAnimation;

// Live gravity inclinometer, active only on its page and in the foreground.
// The display is updated on Qt's animation clock, never by QML JavaScript.
class GradienterService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY changed)
    Q_PROPERTY(double rollDegrees READ rollDegrees NOTIFY tiltChanged)
    Q_PROPERTY(double pitchDegrees READ pitchDegrees NOTIFY tiltChanged)
    Q_PROPERTY(double tiltDegrees READ tiltDegrees NOTIFY tiltChanged)
    Q_PROPERTY(bool level READ level NOTIFY tiltChanged)
    Q_PROPERTY(bool referenceSet READ referenceSet NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool sensorActive READ sensorActive NOTIFY changed)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY changed)

public:
    explicit GradienterService(QObject *parent = nullptr);

    bool hasReading() const;
    double rollDegrees() const;
    double pitchDegrees() const;
    double tiltDegrees() const;
    bool level() const;
    bool referenceSet() const;
    QString status() const;
    bool sensorActive() const;
    int sampleCount() const;
    bool pageVisible() const;

    Q_INVOKABLE void setPageVisible(bool visible);
    Q_INVOKABLE void setReference();
    Q_INVOKABLE void resetReference();
    void setApplicationActive(bool active);

signals:
    void changed();
    void tiltChanged();
    void pageVisibleChanged(bool visible);

private:
    enum class State { Paused, Starting, Tracking, Stale, NoSamples, Unavailable, Error };

    void updateActivity();
    void handleReading();
    void handleTimeout();
    void advanceDisplay();
    void updateScreenRotation();
    void changeState(State state);
    AowisGradienter::Angles displayedAxes() const;

    QAccelerometer m_accelerometer;
    QElapsedTimer m_clock;
    QTimer m_watchdog;
    QAbstractAnimation *m_animation = nullptr;
    AowisGradienter::Filter m_filter;
    AowisGradienter::Angles m_latestNative;
    AowisGradienter::Angles m_referenceNative;
    State m_state = State::Paused;
    qint64 m_lastReadingMs = -1;
    qint64 m_startedMs = -1;
    int m_sampleCount = 0;
    int m_screenRotation = 0;
    bool m_hasReference = false;
    bool m_applicationActive = false;
    bool m_pageVisible = false;
};
