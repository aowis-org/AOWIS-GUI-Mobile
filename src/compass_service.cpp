#include "compass_service.h"
#include "compass_heading.h"

#include <QAbstractAnimation>
#include <functional>
#include <utility>
#include <QGuiApplication>
#include <QScreen>
#include <QtGlobal>

namespace
{
constexpr qint64 kStaleAfterMs = 4000;
constexpr qint64 kFirstReadingTimeoutMs = 6000;
// The OS rotation vector should report within a fraction of a second; if it
// stays silent, switch to AOWIS' own fusion quickly rather than waiting.
constexpr qint64 kRotationVectorTimeoutMs = 2500;
// Status text (sample count, rates) is refreshed at a readable cadence; the
// heading itself is pushed on every display frame via headingChanged().
constexpr qint64 kStatusRefreshMs = 250;

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
    this->m_hardware = CompassSensorSource::probeHardware();
    this->m_source.setSampleHandler(
        [this](const CompassSensorSource::Sample &sample) {
            handleSample(sample);
        });
    this->m_frameAnimation = new CompassFrameAnimation(
        [this]() { advanceHeading(); }, this);
    this->m_watchdog.setInterval(1000);
    connect(&this->m_watchdog, &QTimer::timeout,
            this, &CompassService::handleTimeout);
}

bool CompassService::hasHeading() const
{
    return this->m_state == State::Tracking &&
           this->m_lastReadingMs >= 0 &&
           (this->m_clock.elapsed() - this->m_lastReadingMs <= kStaleAfterMs);
}

// Screen orientation is already applied per sample (it selects the
// screen-top axis of the orientation), so the filter state is the
// screen-top heading.
double CompassService::headingDegrees() const
{
    return hasHeading() ? this->m_headingFilter.displayDegrees() : -1.0;
}

double CompassService::roseHeadingDegrees() const
{
    // The rose must receive an *unwrapped* angle. Normalizing here would
    // cause a visible 359 -> 0 degree jump on every north crossing.
    return hasHeading() ? this->m_headingFilter.continuousDisplayDegrees() : -1.0;
}

QString CompassService::direction() const
{
    return tr(AowisCompass::direction(headingDegrees()));
}

double CompassService::calibrationLevel() const
{
    return hasHeading() ? this->m_calibration : -1.0;
}

QString CompassService::calibrationStatus() const
{
    QString text;
    if (!hasHeading() || this->m_calibration < 0.0)
        text = tr("Calibration unavailable");
    else if (this->m_calibration < 0.35)
        text = tr("Low calibration · move phone in a figure eight");
    else if (this->m_calibration < 0.75)
        text = tr("Moderate calibration");
    else
        text = tr("Good calibration");
    const double accuracy = headingAccuracyDegrees();
    if (accuracy > 0.0)
        text = tr("%1 · estimated accuracy ±%2°").arg(text).arg(qRound(accuracy));
    return text;
}

double CompassService::headingAccuracyDegrees() const
{
    return hasHeading() ? this->m_headingAccuracy : -1.0;
}

QString CompassService::sourceDescription() const
{
    switch (this->m_source.mode())
    {
    case CompassSensorSource::Mode::RotationVector:
        return tr("Source: Android fused rotation vector");
    case CompassSensorSource::Mode::FusionWithGyroscope:
        return tr("Source: AOWIS fusion (gyroscope + accelerometer + magnetometer)");
    case CompassSensorSource::Mode::FusionWithoutGyroscope:
        return tr("Source: AOWIS fusion (accelerometer + magnetometer, no gyroscope)");
    case CompassSensorSource::Mode::None:
        break;
    }
    return QString();
}

QString CompassService::hardwareDescription() const
{
    const QString yes = QStringLiteral("✓");
    const QString no = QStringLiteral("✗");
    return tr("Phone sensors: rotation vector %1 · gyroscope %2 · accelerometer %3 · magnetometer %4")
        .arg(this->m_hardware.rotationVector ? yes : no)
        .arg(this->m_hardware.gyroscope ? yes : no)
        .arg(this->m_hardware.accelerometer ? yes : no)
        .arg(this->m_hardware.magnetometer ? yes : no);
}

QString CompassService::status() const
{
    switch (this->m_state)
    {
    case State::Paused: return tr("Open Compass to read heading");
    case State::Starting: return tr("Waiting for magnetic compass sensor");
    case State::Tracking: return hasHeading() ? tr("Live magnetic heading")
                                              : tr("Compass reading has expired");
    case State::Stale: return tr("Compass reading has expired");
    case State::NoSamples: return tr("Magnetic sensor started, but no heading samples arrived");
    case State::Unavailable: return tr("No compass sensor available on this device");
    case State::Error: return tr("Compass sensor could not start or stopped");
    }
    return tr("Compass unavailable");
}

bool CompassService::sensorActive() const
{
    return this->m_source.active();
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
        this->m_source.stop();
        this->m_watchdog.stop();
        this->m_frameAnimation->stop();
        this->m_headingFilter.reset();
        this->m_sensorRateHz = 0.0;
        this->m_displayRateHz = 0.0;
        this->m_lastFrameRateCheckMs = -1;
        this->m_framesSinceCheck = 0;
        this->m_lastReadingMs = -1;
        this->m_lastStatusEmitMs = -1;
        this->m_lastSampleSeconds = -1.0;
        this->m_headingAccuracy = -1.0;
        this->m_calibration = -1.0;
        this->m_startMs = -1;
        this->m_sampleCount = 0;
        changeState(State::Paused);
        return;
    }

    // A reading from before backgrounding or a different page must not be
    // presented as live immediately after returning.
    this->m_lastReadingMs = -1;
    this->m_lastStatusEmitMs = -1;
    this->m_frameAnimation->stop();
    this->m_headingFilter.reset();
    this->m_sensorRateHz = 0.0;
    this->m_displayRateHz = 0.0;
    this->m_lastFrameRateCheckMs = -1;
    this->m_framesSinceCheck = 0;
    this->m_lastSampleSeconds = -1.0;
    this->m_headingAccuracy = -1.0;
    this->m_calibration = -1.0;
    this->m_startMs = this->m_clock.elapsed();
    this->m_sampleCount = 0;
    this->m_source.stop();
    if (!startSource(CompassSensorSource::Mode::RotationVector))
    {
        emit changed();
        changeState(State::Unavailable);
        return;
    }
    this->m_watchdog.start();
}

bool CompassService::startSource(CompassSensorSource::Mode best)
{
    this->m_headingFilter.reset();
    this->m_startMs = this->m_clock.elapsed();
    if (!this->m_source.start(best))
        return false;
    // The OS rotation vector and the gyroscope fusion are both stabilised;
    // without a gyroscope the heading needs heavier averaging.
    this->m_headingFilter.setTuning(
        this->m_source.mode() == CompassSensorSource::Mode::FusionWithoutGyroscope
            ? AowisCompass::kRawCompassTuning : AowisCompass::kFusedSensorTuning);
    changeState(State::Starting);
    emit changed();
    return true;
}

void CompassService::handleSample(const CompassSensorSource::Sample &sample)
{
    if (!this->m_pageVisible || !this->m_applicationActive)
        return;
    const double heading = AowisCompass::headingFromRotationVector(
        sample.x, sample.y, sample.z, sample.w, this->m_displayRotationDegrees);
    if (heading < 0.0)
        return; // Degenerate pose (e.g. tipped far past flat): no fake value.
    this->m_headingAccuracy = sample.headingAccuracyDegrees;
    double calibration = AowisCompass::calibrationFromAndroidStatus(sample.magneticStatus);
    if (calibration < 0.0 && sample.headingAccuracyDegrees > 0.0)
        calibration = sample.headingAccuracyDegrees <= 10.0 ? 1.0
                    : sample.headingAccuracyDegrees <= 25.0 ? 0.5 : 0.2;
    const double sampleSeconds = sample.seconds;
    if (!this->m_headingFilter.sample(heading, sampleSeconds))
        return;
    if (this->m_lastSampleSeconds >= 0.0 && sampleSeconds > this->m_lastSampleSeconds)
    {
        const double rate = 1.0 / (sampleSeconds - this->m_lastSampleSeconds);
        this->m_sensorRateHz = this->m_sensorRateHz == 0.0
            ? rate : 0.95 * this->m_sensorRateHz + 0.05 * rate;
    }
    this->m_lastSampleSeconds = sampleSeconds;
    ++this->m_sampleCount;
    this->m_calibration = calibration;
    const qint64 now = this->m_clock.elapsed();
    const bool firstSample = this->m_lastReadingMs < 0;
    this->m_lastReadingMs = now;
    changeState(State::Tracking);
    if (this->m_frameAnimation->state() != QAbstractAnimation::Running)
        this->m_frameAnimation->start();
    if (firstSample || this->m_lastStatusEmitMs < 0 ||
        now - this->m_lastStatusEmitMs >= kStatusRefreshMs)
    {
        this->m_lastStatusEmitMs = now;
        emit changed();
    }
}

void CompassService::handleTimeout()
{
    // A sensor backend can report a successful start yet deliver no samples.
    // Do not leave the UI in "Waiting" forever: identify this failure separately
    // from frame animation, which is never involved in reading the sensor.
    if (this->m_state == State::Starting && this->m_lastReadingMs < 0 &&
        this->m_startMs >= 0 &&
        this->m_source.mode() == CompassSensorSource::Mode::RotationVector &&
        this->m_clock.elapsed() - this->m_startMs > kRotationVectorTimeoutMs)
    {
        // Rotation vector listed but silent (seen on some vendor builds):
        // continue with AOWIS' own fusion of the raw sensors.
        this->m_source.stop();
        if (!startSource(CompassSensorSource::Mode::FusionWithGyroscope))
            changeState(State::NoSamples);
        return;
    }
    if (this->m_state == State::Starting && this->m_lastReadingMs < 0 &&
        this->m_startMs >= 0 &&
        this->m_clock.elapsed() - this->m_startMs > kFirstReadingTimeoutMs)
    {
        changeState(State::NoSamples);
    }
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
