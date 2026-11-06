#pragma once

#include <QCompass>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// Magnetic orientation, independent of GPS movement heading and map renderer.
// The hardware is activated only while the Compass page is in the foreground.
class CompassService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasHeading READ hasHeading NOTIFY changed)
    Q_PROPERTY(double headingDegrees READ headingDegrees NOTIFY changed)
    Q_PROPERTY(QString direction READ direction NOTIFY changed)
    Q_PROPERTY(double calibrationLevel READ calibrationLevel NOTIFY changed)
    Q_PROPERTY(QString calibrationStatus READ calibrationStatus NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool sensorActive READ sensorActive NOTIFY changed)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY changed)

public:
    explicit CompassService(QObject *parent = nullptr);

    bool hasHeading() const;
    double headingDegrees() const;
    QString direction() const;
    double calibrationLevel() const;
    QString calibrationStatus() const;
    QString status() const;
    bool sensorActive() const;
    int sampleCount() const;

    Q_INVOKABLE void setPageVisible(bool visible);
    void setApplicationActive(bool active);

signals:
    void changed();

private:
    enum class State { Paused, Starting, Tracking, Stale, NoSamples, Unavailable, Error };

    void updateActivity();
    void handleReading();
    void handleTimeout();
    void changeState(State state);

    QCompass m_compass;
    QElapsedTimer m_clock;
    QTimer m_watchdog;
    State m_state = State::Paused;
    double m_headingDegrees = -1.0;
    int m_displayRotationDegrees = 0;
    double m_calibration = -1.0;
    qint64 m_lastReadingMs = -1;
    qint64 m_startMs = -1;
    int m_sampleCount = 0;
    bool m_applicationActive = false;
    bool m_pageVisible = false;
};
