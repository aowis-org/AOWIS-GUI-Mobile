#include "location_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGeoPositionInfoSource>
#include <QtGlobal>

namespace
{
constexpr qint64 kStaleAfterMilliseconds = 15000;
constexpr int kRequestedUpdateIntervalMilliseconds = 2000;
}

LocationService::LocationService(QObject *parent)
    : QObject(parent)
{
    this->m_clock.start();
    this->m_ageTimer.setInterval(1000);
    connect(&this->m_ageTimer, &QTimer::timeout, this, &LocationService::updateFixAge);
}

bool LocationService::hasFix() const { return this->m_fix.valid; }
bool LocationService::fresh() const
{
    return this->m_state == State::Tracking && this->m_applicationActive;
}
bool LocationService::permissionDenied() const
{
    return this->m_state == State::PermissionDenied;
}

bool LocationService::providerUnavailable() const
{
    return this->m_state == State::Unavailable;
}

QString LocationService::status() const
{
    switch (this->m_state)
    {
    case State::Paused: return tr("Paused while app is in background");
    case State::RequestingPermission: return tr("Waiting for location permission");
    case State::PermissionDenied: return tr("Location permission denied; enable it in Android Settings");
    case State::WaitingForFix: return tr("Searching for position");
    case State::Tracking: return tr("Position available");
    case State::Stale: return tr("Last position is stale; waiting for a new fix");
    case State::Unavailable: return tr("No location provider available; check location services");
    case State::Error: return tr("Location service error; try again");
    }
    return tr("Unknown location state");
}
double LocationService::latitude() const { return this->m_fix.latitude; }
double LocationService::longitude() const { return this->m_fix.longitude; }
bool LocationService::altitudeAvailable() const { return this->m_fix.altitudeAvailable; }
double LocationService::altitudeMeters() const { return this->m_fix.altitudeMeters; }
double LocationService::horizontalAccuracyMeters() const { return this->m_fix.horizontalAccuracyMeters; }
double LocationService::verticalAccuracyMeters() const { return this->m_fix.verticalAccuracyMeters; }
double LocationService::speedMetersPerSecond() const { return this->m_fix.speedMetersPerSecond; }
double LocationService::headingDegrees() const { return this->m_fix.headingDegrees; }
QString LocationService::timestampUtc() const { return this->m_fix.timestampUtc; }
int LocationService::fixAgeSeconds() const
{
    if (!this->m_fix.valid)
        return -1;
    return static_cast<int>(qMax<qint64>(0, this->m_clock.elapsed() - this->m_fixClockMs) / 1000);
}

void LocationService::changeState(State state)
{
    if (this->m_state == state)
        return;
    this->m_state = state;
    emit changed();
}

void LocationService::stopSource()
{
    if (this->m_source != nullptr)
        this->m_source->stopUpdates();
    this->m_ageTimer.stop();
}

void LocationService::setApplicationActive(bool active)
{
    if (this->m_applicationActive == active)
        return;
    this->m_applicationActive = active;
    if (!active)
    {
        stopSource();
        changeState(State::Paused);
    }
    else
        startIfAllowed();
}

void LocationService::startIfAllowed()
{
    if (!this->m_applicationActive)
        return;
    if (this->m_permissionPending)
    {
        changeState(State::RequestingPermission);
        return;
    }
#if QT_CONFIG(permissions)
    QLocationPermission permission;
    permission.setAvailability(QLocationPermission::WhenInUse);
    permission.setAccuracy(QLocationPermission::Precise);
    const Qt::PermissionStatus status = QCoreApplication::instance()->checkPermission(permission);
    if (status == Qt::PermissionStatus::Undetermined)
    {
        this->m_permissionPending = true;
        changeState(State::RequestingPermission);
        QCoreApplication::instance()->requestPermission(permission, this,
            &LocationService::handlePermissionResult);
        return;
    }
    if (status != Qt::PermissionStatus::Granted)
    {
        stopSource();
        changeState(State::PermissionDenied);
        return;
    }
#endif
    startSource();
}

void LocationService::handlePermissionResult(const QPermission &permission)
{
    this->m_permissionPending = false;
    if (!this->m_applicationActive)
        return;
    if (permission.status() != Qt::PermissionStatus::Granted)
    {
        stopSource();
        changeState(State::PermissionDenied);
        return;
    }
    startSource();
}

void LocationService::startSource()
{
    if (this->m_source == nullptr)
    {
        this->m_source = QGeoPositionInfoSource::createDefaultSource(this);
        if (this->m_source == nullptr)
        {
            changeState(State::Unavailable);
            return;
        }
        connect(this->m_source, &QGeoPositionInfoSource::positionUpdated,
                this, &LocationService::applyPosition);
        connect(this->m_source, &QGeoPositionInfoSource::errorOccurred, this,
            [this](QGeoPositionInfoSource::Error error) {
                if (!this->m_applicationActive ||
                    error == QGeoPositionInfoSource::NoError)
                    return;
                if (error == QGeoPositionInfoSource::AccessError)
                {
                    stopSource();
                    changeState(State::PermissionDenied);
                }
                else if (error == QGeoPositionInfoSource::UpdateTimeoutError)
                    changeState(this->m_fix.valid ? State::Stale : State::WaitingForFix);
                else
                    changeState(State::Error);
            });
    }
    const int minimumInterval = this->m_source->minimumUpdateInterval();
    this->m_source->setUpdateInterval(qMax(kRequestedUpdateIntervalMilliseconds, minimumInterval));
    this->m_source->startUpdates();
    this->m_ageTimer.start();
    // A position received before a pause remains a last-known fix, but must
    // not immediately be represented as a fresh location after resuming.
    changeState(this->m_fix.valid ? State::Stale : State::WaitingForFix);
}

void LocationService::applyPosition(const QGeoPositionInfo &position)
{
    if (!this->m_applicationActive)
        return;
    const LocationSnapshot snapshot = LocationSnapshot::fromPosition(position);
    if (!snapshot.valid)
        return;
    this->m_fix = snapshot;
    const qint64 reportedAge = qMax<qint64>(0,
        position.timestamp().msecsTo(QDateTime::currentDateTimeUtc()));
    this->m_fixClockMs = this->m_clock.elapsed() - reportedAge;
    changeState(reportedAge > kStaleAfterMilliseconds ? State::Stale : State::Tracking);
    emit changed();
}

void LocationService::updateFixAge()
{
    if (!this->m_applicationActive)
        return;
    if (this->m_fix.valid && this->m_state == State::Tracking &&
        this->m_clock.elapsed() - this->m_fixClockMs > kStaleAfterMilliseconds)
        changeState(State::Stale);
    if (this->m_fix.valid)
        emit changed();
}
