#include "compass_service.h"
#include "compass_heading.h"

#include <QCompassReading>
#include <QAbstractAnimation>
#include <functional>
#include <utility>
#include <QGuiApplication>
#include <QScreen>
#include <QSensor>
#include <QtGlobal>

namespace
{
constexpr qint64 kStaleAfterMs = 4000;
constexpr qint64 kFirstReadingTimeoutMs = 6000;

// The Qt animation framework advances this on the GUI animation clock.
// In Qt Quick's threaded render loop this clock is synchronized to frames.
// Unlike a 16ms QTimer, it does not impose an independent cadence on QML.
class CompassFrameAnimation final : public QAbstractAnimation
{
public:
    CompassFrameAnimation(std::function<void()> onFrame, QObject *parent)
        : QAbstractAnimation(parent), m_onFrame(std::move(onFrame)) {}
    int duration() const override { return -1; }
protected:
    void updateCurrentTime(int) override { this->m_onFrame(); }
private:
    std::function<void()> m_onFrame;
};
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
                    emit headingChanged();
                    emit changed();
                });
    }
    // Request high-frequency compass reports where supported. Qt/Android may
    // deliver fewer; the motion observer handles arbitrary sample spacing.
    this->m_compass.setDataRate(60);
    this->m_frameAnimation = new CompassFrameAnimation(
        [this]() { this->advanceHeading(); }, this);
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
        ? AowisCompass::normalize(this->m_headingFilter.displayDegrees() + this->m_displayRotationDegrees)
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

double CompassService::sensorRateHz() const
{
    return this->m_sensorRateHz;
}

double CompassService::displayRateHz() const
{
    return this->m_displayRateHz;
}

bool CompassService::pageVisible() const
{
    return this->m_pageVisible;
}

void CompassService::changeState(State state)
{
    if (this->m_state == state)
        return;
    this->m_state = state;
    emit headingChanged(); // A first fix or loss of fix must update the dial immediately.
    emit changed();
}

void CompassService::setPageVisible(bool visible)
{
    if (this->m_pageVisible == visible)
        return;
    this->m_pageVisible = visible;
    emit pageVisibleChanged(visible);
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
        this->m_frameAnimation->stop();
        this->m_headingFilter.reset();
        this->m_sensorRateHz = 0.0;
        this->m_displayRateHz = 0.0;
        this->m_lastFrameRateCheckMs = -1;
        this->m_framesSinceCheck = 0;
        this->m_lastReadingMs = -1;
        this->m_startMs = -1;
        this->m_sampleCount = 0;
        changeState(State::Paused);
        return;
    }

    // A reading from before backgrounding or a different page must not be
    // presented as live immediately after returning.
    this->m_lastReadingMs = -1;
    this->m_frameAnimation->stop();
    this->m_headingFilter.reset();
    this->m_sensorRateHz = 0.0;
    this->m_displayRateHz = 0.0;
    this->m_lastFrameRateCheckMs = -1;
    this->m_framesSinceCheck = 0;
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
    const qint64 now = this->m_clock.elapsed();
    if (!this->m_headingFilter.sample(degrees, static_cast<double>(now) / 1000.0))
        return;
    if (this->m_lastReadingMs >= 0 && now > this->m_lastReadingMs)
    {
        const double rate = 1000.0 / static_cast<double>(now - this->m_lastReadingMs);
        this->m_sensorRateHz = this->m_sensorRateHz == 0.0
            ? rate : 0.85 * this->m_sensorRateHz + 0.15 * rate;
    }
    ++this->m_sampleCount;
    this->m_calibration = AowisCompass::clampedCalibration(reading->calibrationLevel());
    this->m_lastReadingMs = now;
    changeState(State::Tracking);
    if (this->m_frameAnimation->state() != QAbstractAnimation::Running)
        this->m_frameAnimation->start();
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
    {
        this->m_frameAnimation->stop();
        changeState(State::Stale);
    }
}

void CompassService::advanceHeading()
{
    if (!hasHeading())
    {
        this->m_frameAnimation->stop();
        return;
    }
    const qint64 now = this->m_clock.elapsed();
    if (this->m_lastFrameRateCheckMs < 0)
        this->m_lastFrameRateCheckMs = now;
    ++this->m_framesSinceCheck;
    if (now - this->m_lastFrameRateCheckMs >= 1000)
    {
        this->m_displayRateHz = 1000.0 * static_cast<double>(this->m_framesSinceCheck)
            / static_cast<double>(now - this->m_lastFrameRateCheckMs);
        this->m_lastFrameRateCheckMs = now;
        this->m_framesSinceCheck = 0;
        emit changed(); // Status only, never force a QML repaint on each tick.
    }
    if (this->m_headingFilter.advanceDisplay(static_cast<double>(now) / 1000.0))
        emit headingChanged();
}
