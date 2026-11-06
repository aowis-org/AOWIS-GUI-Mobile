#include "gradienter_service.h"

#include <QAbstractAnimation>
#include <QAccelerometerReading>
#include <QGuiApplication>
#include <QScreen>
#include <QSensor>

#include <cmath>
#include <functional>
#include <utility>

namespace
{
constexpr qint64 kStaleAfterMs = 3000;
constexpr qint64 kFirstReadingTimeoutMs = 6000;
constexpr double kLevelToleranceDegrees = 0.5;

class GradienterFrameAnimation final : public QAbstractAnimation
{
public:
    GradienterFrameAnimation(std::function<void()> onFrame, QObject *parent)
        : QAbstractAnimation(parent), m_onFrame(std::move(onFrame)) {}
    int duration() const override { return -1; }
protected:
    void updateCurrentTime(int) override { this->m_onFrame(); }
private:
    std::function<void()> m_onFrame;
};
}

GradienterService::GradienterService(QObject *parent) : QObject(parent)
{
    this->m_clock.start();
    this->m_accelerometer.setAxesOrientationMode(QSensor::FixedOrientation);
    this->m_accelerometer.setDataRate(60);
    this->m_accelerometer.setAccelerationMode(QAccelerometer::Gravity);
    this->m_animation = new GradienterFrameAnimation(
        [this]() { this->advanceDisplay(); }, this);
    this->m_watchdog.setInterval(500);
    this->updateScreenRotation();
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen != nullptr)
    {
        connect(screen, &QScreen::orientationChanged, this,
                [this](Qt::ScreenOrientation) { this->updateScreenRotation(); });
    }
    connect(&this->m_accelerometer, &QSensor::readingChanged,
            this, &GradienterService::handleReading);
    connect(&this->m_accelerometer, &QSensor::sensorError, this,
            [this](int) {
                if (this->m_applicationActive && this->m_pageVisible)
                    this->changeState(State::Error);
            });
    connect(&this->m_accelerometer, &QSensor::activeChanged, this,
            [this]() {
                if (this->m_applicationActive && this->m_pageVisible &&
                    !this->m_accelerometer.isActive() && this->m_state != State::Unavailable)
                    this->changeState(State::Error);
            });
    connect(&this->m_watchdog, &QTimer::timeout,
            this, &GradienterService::handleTimeout);
}

void GradienterService::updateScreenRotation()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen != nullptr)
        this->m_screenRotation = screen->angleBetween(
            screen->nativeOrientation(), screen->orientation());
    emit tiltChanged();
}

bool GradienterService::hasReading() const
{
    return this->m_state == State::Tracking && this->m_lastReadingMs >= 0 &&
        (this->m_clock.elapsed() - this->m_lastReadingMs <= kStaleAfterMs);
}

AowisGradienter::Angles GradienterService::displayedAxes() const
{
    return AowisGradienter::screenAxes(this->m_filter.displayed(), this->m_screenRotation);
}

double GradienterService::rollDegrees() const
{
    return hasReading() ? this->displayedAxes().x : 0.0;
}

double GradienterService::pitchDegrees() const
{
    return hasReading() ? this->displayedAxes().y : 0.0;
}

double GradienterService::tiltDegrees() const
{
    return hasReading() ? AowisGradienter::totalTilt(this->displayedAxes()) : 0.0;
}

bool GradienterService::level() const
{
    return hasReading() && this->tiltDegrees() <= kLevelToleranceDegrees;
}

bool GradienterService::referenceSet() const { return this->m_hasReference; }
bool GradienterService::sensorActive() const { return this->m_accelerometer.isActive(); }
int GradienterService::sampleCount() const { return this->m_sampleCount; }
bool GradienterService::pageVisible() const { return this->m_pageVisible; }

QString GradienterService::status() const
{
    switch (this->m_state)
    {
    case State::Paused: return QStringLiteral("Open Gradienter to measure slope");
    case State::Starting: return QStringLiteral("Waiting for gravity sensor");
    case State::Tracking: return hasReading() ? QStringLiteral("Live gravity inclinometer")
                                              : QStringLiteral("Gravity reading has expired");
    case State::Stale: return QStringLiteral("Gravity reading has expired");
    case State::NoSamples: return QStringLiteral("Sensor started, but no gravity readings arrived");
    case State::Unavailable: return QStringLiteral("No accelerometer available on this device");
    case State::Error: return QStringLiteral("Accelerometer could not start or stopped");
    }
    return QStringLiteral("Gradienter unavailable");
}

void GradienterService::changeState(State state)
{
    if (this->m_state == state)
        return;
    this->m_state = state;
    emit tiltChanged();
    emit changed();
}

void GradienterService::setPageVisible(bool visible)
{
    if (this->m_pageVisible == visible)
        return;
    this->m_pageVisible = visible;
    emit pageVisibleChanged(visible);
    this->updateActivity();
}

void GradienterService::setApplicationActive(bool active)
{
    if (this->m_applicationActive == active)
        return;
    this->m_applicationActive = active;
    this->updateActivity();
}

void GradienterService::updateActivity()
{
    if (!this->m_pageVisible || !this->m_applicationActive)
    {
        this->m_accelerometer.stop();
        this->m_animation->stop();
        this->m_watchdog.stop();
        this->m_filter.reset();
        this->m_lastReadingMs = -1;
        this->m_startedMs = -1;
        this->m_sampleCount = 0;
        this->changeState(State::Paused);
        return;
    }
    this->m_filter.reset();
    this->m_lastReadingMs = -1;
    this->m_startedMs = this->m_clock.elapsed();
    this->m_sampleCount = 0;
    this->m_animation->stop();
    if (!this->m_accelerometer.connectToBackend())
    {
        this->changeState(State::Unavailable);
        return;
    }
    this->changeState(State::Starting);
    if (!this->m_accelerometer.start())
    {
        this->changeState(State::Error);
        return;
    }
    this->m_watchdog.start();
}

void GradienterService::handleReading()
{
    if (!this->m_pageVisible || !this->m_applicationActive)
        return;
    const QAccelerometerReading *reading = this->m_accelerometer.reading();
    if (reading == nullptr)
        return;
    AowisGradienter::Angles value;
    if (!AowisGradienter::fromGravity(reading->x(), reading->y(), reading->z(), value))
        return;
    const qint64 now = this->m_clock.elapsed();
    this->m_latestNative = value;
    if (this->m_hasReference)
    {
        value.x -= this->m_referenceNative.x;
        value.y -= this->m_referenceNative.y;
    }
    if (!this->m_filter.sample(value, static_cast<double>(now) / 1000.0))
        return;
    this->m_lastReadingMs = now;
    ++this->m_sampleCount;
    this->changeState(State::Tracking);
    if (this->m_animation->state() != QAbstractAnimation::Running)
        this->m_animation->start();
    emit changed();
    emit tiltChanged();
}

void GradienterService::handleTimeout()
{
    if (this->m_state == State::Starting && this->m_lastReadingMs < 0 &&
        this->m_startedMs >= 0 &&
        this->m_clock.elapsed() - this->m_startedMs > kFirstReadingTimeoutMs)
        this->changeState(State::NoSamples);
    if (this->m_lastReadingMs >= 0 &&
        this->m_clock.elapsed() - this->m_lastReadingMs > kStaleAfterMs)
    {
        this->m_animation->stop();
        this->changeState(State::Stale);
    }
}

void GradienterService::advanceDisplay()
{
    if (!this->hasReading())
    {
        this->m_animation->stop();
        return;
    }
    if (this->m_filter.advance(static_cast<double>(this->m_clock.elapsed()) / 1000.0))
        emit tiltChanged();
}

void GradienterService::setReference()
{
    if (!this->hasReading())
        return;
    this->m_referenceNative = this->m_latestNative;
    this->m_hasReference = true;
    this->m_filter.reset();
    this->m_filter.sample(AowisGradienter::Angles{}, static_cast<double>(this->m_clock.elapsed()) / 1000.0);
    emit changed();
    emit tiltChanged();
}

void GradienterService::resetReference()
{
    if (!this->m_hasReference)
        return;
    this->m_hasReference = false;
    this->m_filter.reset();
    // Rebase from the latest valid raw reading rather than briefly flashing
    // an invented perfectly level result.
    if (this->hasReading())
        this->m_filter.sample(this->m_latestNative,
                              static_cast<double>(this->m_clock.elapsed()) / 1000.0);
    emit changed();
    emit tiltChanged();
}
