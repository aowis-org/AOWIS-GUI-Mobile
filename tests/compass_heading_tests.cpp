#include "compass_heading.h"
#include "compass_heading_filter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAILED: " << #condition << " at line " << __LINE__ << '\n'; \
        std::exit(1); \
    } \
} while (false)

namespace
{
struct Playback
{
    AowisCompass::HeadingFilter filter;
    double frameInterval = 1.0 / 60.0;
    double sampleInterval = 1.0 / 20.0;
    double displayedStepMaximum = 0.0;
    double movingStepMinimum = 1000.0;
    double maxMovingError = 0.0;
    double lateNoiseMin = 10000.0;
    double lateNoiseMax = -10000.0;
    double finalHeading = 0.0;

    void run(double duration, double startHeading, double velocity, double noise)
    {
        CHECK(this->filter.sample(startHeading, 0.0));
        double previous = this->filter.displayDegrees();
        double nextSensor = this->sampleInterval;
        for (int frame = 1; frame * this->frameInterval <= duration + 0.0000001; ++frame)
        {
            const double now = frame * this->frameInterval;
            // Feed the sensor events that occurred before this render frame.
            while (nextSensor <= now + 1e-8)
            {
                const double raw = AowisCompass::normalize(startHeading + velocity * nextSensor
                    + noise * (std::sin(nextSensor * 49.0) + 0.55 * std::sin(nextSensor * 117.0)));
                CHECK(this->filter.sample(raw, nextSensor));
                nextSensor += this->sampleInterval;
            }
            this->filter.advanceDisplay(now);
            const double heading = this->filter.displayDegrees();
            const double increment = AowisCompass::shortestDelta(previous, heading);
            this->displayedStepMaximum = std::max(this->displayedStepMaximum, std::fabs(increment));
            if (velocity > 30.0 && now > 0.75 && now < duration - 0.2)
            {
                this->movingStepMinimum = std::min(this->movingStepMinimum, increment);
                const double actual = AowisCompass::normalize(startHeading + velocity * now);
                this->maxMovingError = std::max(this->maxMovingError,
                    std::fabs(AowisCompass::shortestDelta(heading, actual)));
            }
            if (velocity == 0.0 && now > duration - 1.0)
            {
                const double relative = AowisCompass::shortestDelta(startHeading, heading);
                this->lateNoiseMin = std::min(this->lateNoiseMin, relative);
                this->lateNoiseMax = std::max(this->lateNoiseMax, relative);
            }
            previous = heading;
            this->finalHeading = heading;
        }
    }
};
}

int main()
{
    CHECK(AowisCompass::normalize(0.0) == 0.0);
    CHECK(AowisCompass::normalize(360.0) == 0.0);
    CHECK(AowisCompass::normalize(725.0) == 5.0);
    CHECK(AowisCompass::normalize(-10.0) == 350.0);
    CHECK(AowisCompass::normalize(NAN) == -1.0);
    CHECK(std::string(AowisCompass::direction(44.0)) == "NE");
    CHECK(std::string(AowisCompass::direction(359.9)) == "N");
    CHECK(AowisCompass::clampedCalibration(0.5) == 0.5);

    AowisCompass::HeadingFilter filter;
    CHECK(!filter.initialized());
    CHECK(!filter.sample(NAN, 0.0));
    CHECK(filter.sample(359.0, 0.0));
    CHECK(filter.initialized());
    CHECK(std::fabs(filter.displayDegrees() - 359.0) < 1e-9);
    CHECK(!filter.sample(190.0, 0.0));
    CHECK(!filter.sample(270.0, -0.1));
    CHECK(!filter.sample(INFINITY, 0.1));
    // Isolated packets are not enough to distinguish noise from motion.
    // A sustained clockwise north crossing must rotate continuously forward.
    for (int i = 1; i <= 10; ++i)
        CHECK(filter.sample(AowisCompass::normalize(359.0 + i), i * 0.05));
    for (int i = 1; i <= 30; ++i)
        filter.advanceDisplay(0.5 + i / 60.0);
    CHECK(filter.continuousDisplayDegrees() > 360.0);
    CHECK(filter.continuousDisplayDegrees() < 372.0);
    CHECK(std::fabs(AowisCompass::shortestDelta(filter.displayDegrees(), 9.0)) < 3.0);

    // Constant 90 degrees/s at 20Hz input must produce continuous 60Hz
    // movement. The previous filter visibly stalled between packets.
    Playback twenty;
    twenty.run(5.0, 350.0, 90.0, 0.0);
    std::cout << "20Hz moving: min step " << twenty.movingStepMinimum
              << " max error " << twenty.maxMovingError
              << " max step " << twenty.displayedStepMaximum << '\n';
    CHECK(twenty.movingStepMinimum > 0.25);
    CHECK(twenty.displayedStepMaximum < 6.0);
    CHECK(twenty.maxMovingError < 13.0);

    // A backend limited to 10Hz still needs to be smooth between samples.
    Playback ten;
    ten.sampleInterval = 0.1;
    ten.run(5.0, 350.0, 90.0, 0.0);
    CHECK(ten.movingStepMinimum > 0.1);
    CHECK(ten.maxMovingError < 16.0);

    // Native high-rate sensor updates should not cause extra oscillation.
    Playback sixty;
    sixty.sampleInterval = 1.0 / 60.0;
    sixty.run(5.0, 350.0, 90.0, 0.0);
    CHECK(sixty.movingStepMinimum > 0.25);
    CHECK(sixty.maxMovingError < 12.0);

    // Frame cadence independence at 60 and 120fps.
    Playback oneTwenty;
    oneTwenty.frameInterval = 1.0 / 120.0;
    oneTwenty.run(5.0, 350.0, 90.0, 0.0);
    std::cout << "60vs120 " << twenty.finalHeading << " " << oneTwenty.finalHeading << "\n";
    CHECK(std::fabs(AowisCompass::shortestDelta(twenty.finalHeading,
                                                oneTwenty.finalHeading)) < 2.0);

    // Stationary jitter should be damped rather than predicted as rotations.
    Playback still;
    still.run(8.0, 42.0, 0.0, 1.0);
    std::cout << "stationary noise range: " << still.lateNoiseMax - still.lateNoiseMin << '\n';
    CHECK(still.lateNoiseMax - still.lateNoiseMin < 2.0);

    // Stopping the sensor freezes bounded prediction rather than spinning.
    filter.reset();
    CHECK(filter.sample(0.0, 0.0));
    for (int i = 1; i <= 8; ++i)
    {
        CHECK(filter.sample(i * 6.0, i * 0.05));
        filter.advanceDisplay(i * 0.05);
    }
    for (int i = 1; i <= 90; ++i)
        filter.advanceDisplay(0.4 + i / 60.0);
    const double settled = filter.displayDegrees();
    filter.advanceDisplay(2.4);
    CHECK(std::fabs(AowisCompass::shortestDelta(settled, filter.displayDegrees())) < 0.1);

    // The display angle is unwrapped: the UI must never see 359 -> 0 and
    // rotate an entire revolution backwards across north.
    AowisCompass::HeadingFilter wrap;
    CHECK(wrap.sample(350.0, 0.0));
    double nextSample = 0.05;
    double lastContinuous = wrap.continuousDisplayDegrees();
    for (int frame = 1; frame <= 180; ++frame)
    {
        const double t = frame / 60.0;
        while (nextSample <= t + 1e-8)
        {
            CHECK(wrap.sample(AowisCompass::normalize(350.0 + 45.0 * nextSample), nextSample));
            nextSample += 0.05;
        }
        wrap.advanceDisplay(t);
        const double continuous = wrap.continuousDisplayDegrees();
        CHECK(continuous >= lastContinuous - 0.02);
        CHECK(continuous - lastContinuous < 2.0);
        lastContinuous = continuous;
    }
    CHECK(wrap.continuousDisplayDegrees() > 360.0);

    // Reproduces the reported electric-motor / shaver-like vibration:
    // a stationary phone with substantial alternating magnetic fluctuations
    // must not repeatedly move the rendered rose by subpixel angles.
    for (double sampleHz : {20.0, 60.0})
    {
        AowisCompass::HeadingFilter noisyStill;
        CHECK(noisyStill.sample(42.0, 0.0));
        double latest = 42.0;
        double highest = 42.0;
        double lowest = 42.0;
        double next = 1.0 / sampleHz;
        for (int frame = 1; frame <= 600; ++frame)
        {
            const double t = frame / 60.0;
            while (next <= t + 1e-8)
            {
                const double noise = 1.1 * (0.75 * std::sin(next * 129.0)
                                             + 0.55 * std::sin(next * 239.0)
                                             + 0.3 * std::sin(next * 19.0));
                CHECK(noisyStill.sample(42.0 + noise, next));
                next += 1.0 / sampleHz;
            }
            noisyStill.advanceDisplay(t);
            latest = noisyStill.displayDegrees();
            if (t > 2.0)
            {
                highest = std::max(highest, latest);
                lowest = std::min(lowest, latest);
            }
        }
        std::cout << "Stationary noise range at " << sampleHz << "Hz: "
                  << (highest - lowest) << " degrees\n";
        CHECK(highest - lowest < 0.18);
    }

    // Under a genuine, noisy turn, a continuous observer must never hunt
    // alternately clockwise/counter-clockwise at each magnetic sample.
    for (double sampleHz : {20.0, 60.0})
    {
        AowisCompass::HeadingFilter turning;
        CHECK(turning.sample(22.0, 0.0));
        double lastAngle = turning.continuousDisplayDegrees();
        double next = 1.0 / sampleHz;
        double reversals = 0.0;
        int reversedFrames = 0;
        for (int frame = 1; frame <= 300; ++frame)
        {
            const double t = frame / 60.0;
            while (next <= t + 1e-8)
            {
                const double noise = 1.3 * (0.75 * std::sin(next * 129.0)
                                             + 0.55 * std::sin(next * 239.0)
                                             + 0.3 * std::sin(next * 19.0));
                CHECK(turning.sample(AowisCompass::normalize(22.0 + 40.0 * next + noise), next));
                next += 1.0 / sampleHz;
            }
            turning.advanceDisplay(t);
            const double angle = turning.continuousDisplayDegrees();
            if (t > 0.8 && angle < lastAngle - 0.02)
            {
                reversals += lastAngle - angle;
                ++reversedFrames;
            }
            lastAngle = angle;
        }
        std::cout << "Noisy 40deg/s turn at " << sampleHz << "Hz: "
                  << reversedFrames << " reverse frames, " << reversals << " deg reversed\n";
        CHECK(reversedFrames == 0);
        CHECK(reversals < 0.01);
        CHECK(std::fabs(turning.continuousDisplayDegrees() - 222.0) < 6.0);
    }

    // A realistic failure case is a phone rotating, then stopping while the
    // magnetometer continues alternating by up to ~2 degrees at 20Hz. The
    // observer must return to its held-needle mode rather than "idling".
    AowisCompass::HeadingFilter turnThenStop;
    CHECK(turnThenStop.sample(40.0, 0.0));
    double nextStopSample = 0.05;
    double lateMinimum = 1000.0;
    double lateMaximum = -1000.0;
    for (int frame = 1; frame <= 900; ++frame)
    {
        const double t = frame / 60.0;
        while (nextStopSample <= t + 1e-8)
        {
            const double measuredTurn = 40.0 + 40.0 * std::min(5.0, nextStopSample);
            const double magneticNoise = 2.0 *
                (0.75 * std::sin(nextStopSample * 129.0)
                 + 0.55 * std::sin(nextStopSample * 239.0)
                 + 0.3 * std::sin(nextStopSample * 19.0));
            CHECK(turnThenStop.sample(AowisCompass::normalize(measuredTurn + magneticNoise),
                                      nextStopSample));
            nextStopSample += 0.05;
        }
        turnThenStop.advanceDisplay(t);
        if (t > 12.0)
        {
            lateMinimum = std::min(lateMinimum, turnThenStop.continuousDisplayDegrees());
            lateMaximum = std::max(lateMaximum, turnThenStop.continuousDisplayDegrees());
        }
    }
    CHECK(!turnThenStop.moving());
    CHECK(lateMaximum - lateMinimum < 0.18);

    // Even a slow physical turn must eventually release the stillness gate.
    AowisCompass::HeadingFilter slow;
    CHECK(slow.sample(40.0, 0.0));
    for (int frame = 1; frame <= 240; ++frame)
    {
        const double t = frame / 60.0;
        CHECK(slow.sample(40.0 + 2.0 * t, t));
        slow.advanceDisplay(t);
    }
    CHECK(slow.displayDegrees() > 45.0);
    CHECK(slow.displayDegrees() < 49.0);

    CHECK(filter.sample(250.0, 3.0));
    CHECK(std::fabs(filter.displayDegrees() - 250.0) < 0.001);
    filter.reset();
    CHECK(!filter.initialized());
    CHECK(!filter.advanceDisplay(3.1));
    CHECK(filter.sample(75.0, 4.0));
    CHECK(std::fabs(filter.displayDegrees() - 75.0) < 0.001);
}
