#include "compass_service.h"
#include "compass_heading.h"

#include <QCompassReading>
#include <QGuiApplication>
#include <QScreen>
#include <QSensor>
#include <QtGlobal>

namespace
{
constexpr qint64 kStaleAfterMs = 4000;
constexpr qint64 kFirstReadingTimeoutMs = 6000;
}

CompassService::CompassService(QObject *parent)
    : QObject(parent)
{
    this->m_clock.start();
    // The sensor defines heading for the device's native top edge. When the
    // OS rotates the UI, compensate so the fixed index means screen-top.
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen != nullptr)
    {
        this->m_displayRotationDegrees = screen->angleBetween(
            screen->nativeOrientation(), screen->orientation());
        connect(screen, &QScreen::orientationChanged, this,
                [this, screen](Qt::ScreenOrientation current) {
                    this->m_displayRotationDegrees = screen->angleBetween(
                        screen->nativeOrientation(), current);
                    emit changed();
                });
    }
    this->m_compass.setDataRate(20); // Requested rate; sensor backend may choose another.
    this->m_watchdog.setInterval(1000);
    connect(&this->m_compass, &QSensor::readingChanged,
            this, &CompassService::handleReading);
    connect(&this->m_compass, &QSensor::sensorError, this,
            [this](int) {
                if (this->m_applicationActive && this->m_pageVisible)
                    this->changeState(State::Error);
            });
    connect(&this->m_compass, &QSensor::activeChanged, this,
            [this]() {
                if (this->m_applicationActive && this->m_pageVisible &&
                    !this->m_compass.isActive() && this->m_state != State::Unavailable)
                    this->changeState(State::Error);
            });
    connect(&this->m_watchdog, &QTimer::timeout,
            this, &CompassService::handleTimeout);
}

bool CompassService::hasHeading() const
{
    return this->m_state == State::Tracking &&
           this->m_lastReadingMs >= 0 &&
           (this->m_clock.elapsed() - this->m_lastReadingMs <= kStaleAfterMs);
}

double CompassService::headingDegrees() const
{
    return hasHeading()
        ? AowisCompass::normalize(this->m_headingDegrees + this->m_displayRotationDegrees)
        : -1.0;
}

QString CompassService::direction() const
{
    return QString::fromLatin1(AowisCompass::direction(headingDegrees()));
}

double CompassService::calibrationLevel() const
{
    return hasHeading() ? this->m_calibration : -1.0;
}

QString CompassService::calibrationStatus() const
{
    if (!hasHeading() || this->m_calibration < 0.0)
        return QStringLiteral("Calibration unavailable");
    if (this->m_calibration < 0.35)
        return QStringLiteral("Low calibration · move phone in a figure eight");
    if (this->m_calibration < 0.75)
        return QStringLiteral("Moderate calibration");
    return QStringLiteral("Good calibration");
}

QString CompassService::status() const
{
    switch (this->m_state)
    {
    case State::Paused: return QStringLiteral("Open Compass to read heading");
    case State::Starting: return QStringLiteral("Waiting for magnetic compass sensor");
    case State::Tracking: return hasHeading() ? QStringLiteral("Live magnetic heading")
                                              : QStringLiteral("Compass reading has expired");
    case State::Stale: return QStringLiteral("Compass reading has expired");
    case State::NoSamples: return QStringLiteral("Magnetic sensor started, but no heading samples arrived");
    case State::Unavailable: return QStringLiteral("No compass sensor available on this device");
    case State::Error: return QStringLiteral("Compass sensor could not start or stopped");
    }
    return QStringLiteral("Compass unavailable");
}

bool CompassService::sensorActive() const
{
    return this->m_compass.isActive();
}

int CompassService::sampleCount() const
{
    return this->m_sampleCount;
}

void CompassService::changeState(State state)
{
    if (this->m_state == state)
        return;
    this->m_state = state;
    emit changed();
}

void CompassService::setPageVisible(bool visible)
{
    if (this->m_pageVisible == visible)
        return;
    this->m_pageVisible = visible;
    updateActivity();
}

void CompassService::setApplicationActive(bool active)
{
    if (this->m_applicationActive == active)
        return;
    this->m_applicationActive = active;
    updateActivity();
}

void CompassService::updateActivity()
{
    if (!this->m_pageVisible || !this->m_applicationActive)
    {
        this->m_compass.stop();
        this->m_watchdog.stop();
        this->m_lastReadingMs = -1;
        this->m_startMs = -1;
        this->m_sampleCount = 0;
        changeState(State::Paused);
        return;
    }

    // A reading from before backgrounding or a different page must not be
    // presented as live immediately after returning.
    this->m_lastReadingMs = -1;
    this->m_startMs = this->m_clock.elapsed();
    this->m_sampleCount = 0;
    if (!this->m_compass.connectToBackend())
    {
        changeState(State::Unavailable);
        return;
    }
    changeState(State::Starting);
    if (!this->m_compass.start())
    {
        changeState(State::Error);
        return;
    }
    this->m_watchdog.start();
}

void CompassService::handleReading()
{
    if (!this->m_pageVisible || !this->m_applicationActive)
        return;
    const QCompassReading *reading = this->m_compass.reading();
    if (reading == nullptr)
        return;
    const double degrees = AowisCompass::normalize(reading->azimuth());
    if (degrees < 0.0)
        return;
    this->m_headingDegrees = degrees;
    ++this->m_sampleCount;
    this->m_calibration = AowisCompass::clampedCalibration(reading->calibrationLevel());
    this->m_lastReadingMs = this->m_clock.elapsed();
    changeState(State::Tracking);
    emit changed();
}

void CompassService::handleTimeout()
{
    // A sensor backend can report a successful start yet deliver no samples.
    // Do not leave the UI in "Waiting" forever: identify this failure separately
    // from frame animation, which is never involved in reading the sensor.
    if (this->m_state == State::Starting && this->m_lastReadingMs < 0 &&
        this->m_startMs >= 0 &&
        this->m_clock.elapsed() - this->m_startMs > kFirstReadingTimeoutMs)
        changeState(State::NoSamples);
    if (this->m_lastReadingMs >= 0 &&
        this->m_clock.elapsed() - this->m_lastReadingMs > kStaleAfterMs)
        changeState(State::Stale);
}
