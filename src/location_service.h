#pragma once

#include "location_snapshot.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPermissions>
#include <QTimer>
#include <QSettings>

class QGeoPositionInfoSource;

// Automatically active foreground positioning. No location is logged,
// transmitted or persisted. Platform permission is requested at first use.
class LocationService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY changed)
    Q_PROPERTY(bool hasFix READ hasFix NOTIFY changed)
    Q_PROPERTY(bool fresh READ fresh NOTIFY changed)
    Q_PROPERTY(bool permissionDenied READ permissionDenied NOTIFY changed)
    Q_PROPERTY(bool providerUnavailable READ providerUnavailable NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double latitude READ latitude NOTIFY changed)
    Q_PROPERTY(double longitude READ longitude NOTIFY changed)
    Q_PROPERTY(bool altitudeAvailable READ altitudeAvailable NOTIFY changed)
    Q_PROPERTY(double altitudeMeters READ altitudeMeters NOTIFY changed)
    Q_PROPERTY(double horizontalAccuracyMeters READ horizontalAccuracyMeters NOTIFY changed)
    Q_PROPERTY(double verticalAccuracyMeters READ verticalAccuracyMeters NOTIFY changed)
    Q_PROPERTY(double speedMetersPerSecond READ speedMetersPerSecond NOTIFY changed)
    Q_PROPERTY(double headingDegrees READ headingDegrees NOTIFY changed)
    Q_PROPERTY(QString timestampUtc READ timestampUtc NOTIFY changed)
    Q_PROPERTY(int fixAgeSeconds READ fixAgeSeconds NOTIFY changed)

public:
    explicit LocationService(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool enabled);
    bool follow() const;
    void setFollow(bool follow);
    bool hasFix() const;
    bool fresh() const;
    bool permissionDenied() const;
    bool providerUnavailable() const;
    QString status() const;
    double latitude() const;
    double longitude() const;
    bool altitudeAvailable() const;
    double altitudeMeters() const;
    double horizontalAccuracyMeters() const;
    double verticalAccuracyMeters() const;
    double speedMetersPerSecond() const;
    double headingDegrees() const;
    QString timestampUtc() const;
    int fixAgeSeconds() const;

    void setApplicationActive(bool active);

signals:
    void changed();

private:
    enum class State { Paused, RequestingPermission, PermissionDenied,
                       WaitingForFix, Tracking, Stale, Unavailable, Error };

    void startIfAllowed();
    void startSource();
    void stopSource();
    void applyPosition(const QGeoPositionInfo &position);
    void handlePermissionResult(const QPermission &permission);
    void changeState(State state);
    void updateFixAge();

    QGeoPositionInfoSource *m_source = nullptr;
    QTimer m_ageTimer;
    QElapsedTimer m_clock;
    LocationSnapshot m_fix;
    qint64 m_fixClockMs = -1;
    State m_state = State::Paused;
    QSettings m_settings{QSettings::NativeFormat, QSettings::UserScope,
                         QStringLiteral("AOWIS"), QStringLiteral("AOWIS-GUI-Mobile")};
    bool m_enabled = true;
    bool m_follow = false;
    bool m_applicationActive = false;
    bool m_permissionPending = false;
};
