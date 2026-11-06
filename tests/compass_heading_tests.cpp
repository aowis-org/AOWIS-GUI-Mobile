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
    CHECK(filter.sample(1.0, 0.05));
    for (int i = 1; i <= 12; ++i)
        filter.advanceDisplay(0.05 + i / 60.0);
    CHECK(std::fabs(AowisCompass::shortestDelta(filter.displayDegrees(), 1.0)) < 1.2);

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

    CHECK(filter.sample(250.0, 3.0));
    CHECK(std::fabs(filter.displayDegrees() - 250.0) < 0.001);
    filter.reset();
    CHECK(!filter.initialized());
    CHECK(!filter.advanceDisplay(3.1));
    CHECK(filter.sample(75.0, 4.0));
    CHECK(std::fabs(filter.displayDegrees() - 75.0) < 0.001);
}
