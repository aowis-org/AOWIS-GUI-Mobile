#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQuaternion>
#include <QTimer>
#include <QAbstractAnimation>

#include "compass_heading_filter.h"
#include "compass_sensor_source.h"

// Magnetic orientation, independent of GPS movement heading and map renderer.
// Hardware runs on the Compass page in the foreground.
// Sensors are read through the Android NDK, not Qt Sensors: the OS-fused
// rotation vector where it works, otherwise AOWIS' own fusion of the raw
// gyroscope, accelerometer and magnetometer (see compass_sensor_source.h).
class CompassService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasHeading READ hasHeading NOTIFY changed)
    Q_PROPERTY(bool hasOrientation READ hasOrientation NOTIFY poseChanged)
    Q_PROPERTY(QQuaternion orientationQuaternion READ orientationQuaternion NOTIFY poseChanged)
    // Gyroscope-only orientation paired with orientationQuaternion; a null
    // quaternion when the phone has no game rotation vector.
    Q_PROPERTY(QQuaternion relativeOrientationQuaternion READ relativeOrientationQuaternion NOTIFY poseChanged)
    // Sensor timestamps in seconds (monotonic, not wall clock) of the latest
    // compass reading and of the gyroscope-only orientation. A pose update
    // advances one or both.
    Q_PROPERTY(double orientationTimestampSeconds READ orientationTimestampSeconds NOTIFY poseChanged)
    Q_PROPERTY(double relativeTimestampSeconds READ relativeTimestampSeconds NOTIFY poseChanged)
    // AOWIS' own orientation from accelerometer + magnetometer (time-matched,
    // see look_around_field.h), its magnetometer timestamp, field strength
    // (microtesla) and dip (degrees below the horizontal). Timestamp 0: none yet.
    Q_PROPERTY(QQuaternion fieldOrientationQuaternion READ fieldOrientationQuaternion NOTIFY poseChanged)
    Q_PROPERTY(double fieldTimestampSeconds READ fieldTimestampSeconds NOTIFY poseChanged)
    Q_PROPERTY(double fieldStrength READ fieldStrength NOTIFY poseChanged)
    Q_PROPERTY(double fieldDipDegrees READ fieldDipDegrees NOTIFY poseChanged)
    // The phone lists a gyroscope sensor (it may still be emulated).
    Q_PROPERTY(bool hasGyroscope READ hasGyroscope CONSTANT)
    // Once a second while running: sensor events per second at each stage of
    // the own field orientation (English diagnostics).
    Q_PROPERTY(QString fieldDiagnostics READ fieldDiagnostics NOTIFY fieldDiagnosticsChanged)
    Q_PROPERTY(int screenRotationDegrees READ screenRotationDegrees NOTIFY poseChanged)
    Q_PROPERTY(double headingDegrees READ headingDegrees NOTIFY headingChanged)
    Q_PROPERTY(double roseHeadingDegrees READ roseHeadingDegrees NOTIFY headingChanged)
    Q_PROPERTY(QString direction READ direction NOTIFY headingChanged)
    Q_PROPERTY(double calibrationLevel READ calibrationLevel NOTIFY changed)
    Q_PROPERTY(QString calibrationStatus READ calibrationStatus NOTIFY changed)
    Q_PROPERTY(double headingAccuracyDegrees READ headingAccuracyDegrees NOTIFY changed)
    Q_PROPERTY(QString sourceDescription READ sourceDescription NOTIFY changed)
    Q_PROPERTY(QString hardwareDescription READ hardwareDescription CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool sensorActive READ sensorActive NOTIFY changed)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY changed)
    Q_PROPERTY(double sensorRateHz READ sensorRateHz NOTIFY changed)
    Q_PROPERTY(double displayRateHz READ displayRateHz NOTIFY changed)

public:
    explicit CompassService(QObject *parent = nullptr);

    bool hasHeading() const;
    bool hasOrientation() const;
    QQuaternion orientationQuaternion() const;
    QQuaternion relativeOrientationQuaternion() const;
    double orientationTimestampSeconds() const;
    double relativeTimestampSeconds() const;
    QQuaternion fieldOrientationQuaternion() const;
    double fieldTimestampSeconds() const;
    double fieldStrength() const;
    double fieldDipDegrees() const;
    bool hasGyroscope() const;
    QString fieldDiagnostics() const;
    int screenRotationDegrees() const;
    double headingDegrees() const;
    double roseHeadingDegrees() const;
    QString direction() const;
    double calibrationLevel() const;
    QString calibrationStatus() const;
    double headingAccuracyDegrees() const;
    QString sourceDescription() const;
    QString hardwareDescription() const;
    QString status() const;
    bool sensorActive() const;
    int sampleCount() const;
    double sensorRateHz() const;
    double displayRateHz() const;
    bool pageVisible() const;

    Q_INVOKABLE void setPageVisible(bool visible);
    void setApplicationActive(bool active);

signals:
    void changed();
    void headingChanged();
    void poseChanged();
    void fieldDiagnosticsChanged();
    void pageVisibleChanged(bool visible);

private:
    enum class State { Paused, Starting, Tracking, Stale, NoSamples, Unavailable, Error };

    void updateActivity();
    void handleSample(const CompassSensorSource::Sample &sample);
    bool startSource(CompassSensorSource::Mode best);
    void handleTimeout();
    void advanceHeading();
    void changeState(State state);

    CompassSensorSource m_source;
    CompassSensorSource::Hardware m_hardware;
    QElapsedTimer m_clock;
    QTimer m_watchdog;
    QAbstractAnimation *m_frameAnimation = nullptr;
    AowisCompass::HeadingFilter m_headingFilter;
    State m_state = State::Paused;
    double m_sensorRateHz = 0.0;
    double m_displayRateHz = 0.0;
    qint64 m_lastFrameRateCheckMs = -1;
    unsigned int m_framesSinceCheck = 0;
    int m_displayRotationDegrees = 0;
    double m_calibration = -1.0;
    double m_headingAccuracy = -1.0;
    double m_lastSampleSeconds = -1.0;
    qint64 m_lastReadingMs = -1;
    qint64 m_lastPoseMs = -1;
    QQuaternion m_orientation;
    QQuaternion m_relativeOrientation{0.0f, 0.0f, 0.0f, 0.0f};
    double m_orientationSeconds = 0.0;
    double m_relativeSeconds = 0.0;
    QQuaternion m_fieldOrientation;
    double m_fieldSeconds = 0.0;
    double m_fieldStrength = 0.0;
    double m_fieldDip = 0.0;
    unsigned long long m_fieldReceived = 0;
    CompassSensorSource::Counters m_lastCounters;
    unsigned long long m_lastFieldReceived = 0;
    qint64 m_lastDiagnosticsMs = -1;
    QString m_fieldDiagnostics;
    void updateFieldDiagnostics();
    qint64 m_lastStatusEmitMs = -1;
    qint64 m_startMs = -1;
    int m_sampleCount = 0;
    bool m_applicationActive = false;
    bool m_pageVisible = false;
};
