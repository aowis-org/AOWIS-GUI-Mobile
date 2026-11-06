#include "plumb_bob_service.h"

#include <QAbstractAnimation>
#include <QAccelerometerReading>
#include <QGuiApplication>
#include <QScreen>
#include <QSensor>

#include <functional>
#include <utility>

namespace
{
constexpr qint64 kStaleAfterMs = 3000;
constexpr qint64 kFirstReadingTimeoutMs = 6000;

class PlumbBobFrameAnimation final : public QAbstractAnimation
{
public:
    PlumbBobFrameAnimation(std::function<void()> frame, QObject *parent)
        : QAbstractAnimation(parent), m_frame(std::move(frame)) {}
    int duration() const override { return -1; }
protected:
    void updateCurrentTime(int) override { this->m_frame(); }
private:
    std::function<void()> m_frame;
};
}

PlumbBobService::PlumbBobService(QObject *parent) : QObject(parent)
{
    this->m_clock.start();
    this->m_accelerometer.setAxesOrientationMode(QSensor::FixedOrientation);
    this->m_accelerometer.setDataRate(60);
    this->m_accelerometer.setAccelerationMode(QAccelerometer::Gravity);
    this->m_animation = new PlumbBobFrameAnimation([this]() { this->advanceDisplay(); }, this);
    this->m_watchdog.setInterval(500);
    this->updateScreenRotation();
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen != nullptr)
    {
        connect(screen, &QScreen::orientationChanged, this,
                [this](Qt::ScreenOrientation) { this->updateScreenRotation(); });
    }
    connect(&this->m_accelerometer, &QSensor::readingChanged,
            this, &PlumbBobService::handleReading);
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
            this, &PlumbBobService::handleTimeout);
}

void PlumbBobService::updateScreenRotation()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen != nullptr)
        this->m_screenRotation = screen->angleBetween(
            screen->nativeOrientation(), screen->orientation());
    emit directionChanged();
}

bool PlumbBobService::hasReading() const
{
    return this->m_state == State::Tracking && this->m_lastReadingMs >= 0 &&
           this->m_clock.elapsed() - this->m_lastReadingMs <= kStaleAfterMs;
}

bool PlumbBobService::suitable() const { return this->hasReading(); }

AowisPlumbBob::ScreenDirection PlumbBobService::displayedDirection() const
{
    return AowisPlumbBob::screenDirection(this->m_filter.direction(), this->m_screenRotation);
}

double PlumbBobService::downX() const { return this->displayedDirection().x; }
double PlumbBobService::downY() const { return this->displayedDirection().y; }
double PlumbBobService::edgeDegrees() const
{
    return this->hasReading() ? AowisPlumbBob::edgeDeviation(this->displayedDirection()) : 0.0;
}
bool PlumbBobService::plumb() const
{
    return this->hasReading() && AowisPlumbBob::plumb(this->edgeDegrees());
}
bool PlumbBobService::sensorActive() const { return this->m_accelerometer.isActive(); }
int PlumbBobService::sampleCount() const { return this->m_sampleCount; }
bool PlumbBobService::pageVisible() const { return this->m_pageVisible; }

QString PlumbBobService::status() const
{
    switch (this->m_state)
    {
    case State::Paused: return QStringLiteral("Open Plumb Bob to measure vertical alignment");
    case State::Starting: return QStringLiteral("Waiting for gravity sensor");
    case State::Tracking: return this->hasReading() ? QStringLiteral("Live gravity plumb line")
                                                       : QStringLiteral("Gravity reading has expired");
    case State::TooFlat: return QStringLiteral("Hold the phone upright to measure an edge");
    case State::Stale: return QStringLiteral("Gravity reading has expired");
    case State::NoSamples: return QStringLiteral("Sensor started, but no gravity readings arrived");
    case State::Unavailable: return QStringLiteral("No accelerometer available on this device");
    case State::Error: return QStringLiteral("Accelerometer could not start or stopped");
    }
    return QStringLiteral("Plumb Bob unavailable");
}

void PlumbBobService::changeState(State state)
{
    if (this->m_state == state)
        return;
    this->m_state = state;
    emit changed();
    emit directionChanged();
}

void PlumbBobService::setPageVisible(bool visible)
{
    if (this->m_pageVisible == visible)
        return;
    this->m_pageVisible = visible;
    emit pageVisibleChanged(visible);
    this->updateActivity();
}

void PlumbBobService::setApplicationActive(bool active)
{
    if (this->m_applicationActive == active)
        return;
    this->m_applicationActive = active;
    this->updateActivity();
}

void PlumbBobService::updateActivity()
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

void PlumbBobService::handleReading()
{
    if (!this->m_pageVisible || !this->m_applicationActive)
        return;
    const QAccelerometerReading *reading = this->m_accelerometer.reading();
    if (reading == nullptr)
        return;
    AowisPlumbBob::Reading value;
    if (!AowisPlumbBob::fromGravity(reading->x(), reading->y(), reading->z(), value))
        return;
    const qint64 now = this->m_clock.elapsed();
    this->m_lastReadingMs = now;
    ++this->m_sampleCount;
    if (!value.suitable)
    {
        this->m_animation->stop();
        this->m_filter.reset();
        this->changeState(State::TooFlat);
        emit changed();
        return;
    }
    if (!this->m_filter.sample(value, static_cast<double>(now) / 1000.0))
        return;
    this->changeState(State::Tracking);
    if (this->m_animation->state() != QAbstractAnimation::Running)
        this->m_animation->start();
    emit changed();
    emit directionChanged();
}

void PlumbBobService::handleTimeout()
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

void PlumbBobService::advanceDisplay()
{
    if (!this->hasReading())
    {
        this->m_animation->stop();
        return;
    }
    if (this->m_filter.advance(static_cast<double>(this->m_clock.elapsed()) / 1000.0))
        emit directionChanged();
}
