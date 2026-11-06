#include "compass_heading.h"
#include "compass_heading_filter.h"

#include <cstdlib>
#include <iostream>
#include <cmath>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAILED: " << #condition << " at line " << __LINE__ << '\n'; \
        std::exit(1); \
    } \
} while (false)

int main()
{
    CHECK(AowisCompass::normalize(0.0) == 0.0);
    CHECK(AowisCompass::normalize(360.0) == 0.0);
    CHECK(AowisCompass::normalize(725.0) == 5.0);
    CHECK(AowisCompass::normalize(-10.0) == 350.0);
    CHECK(AowisCompass::normalize(-720.0) == 0.0);
    CHECK(AowisCompass::normalize(345.0 + 90.0) == 75.0);
    CHECK(AowisCompass::normalize(NAN) == -1.0);
    CHECK(AowisCompass::normalize(INFINITY) == -1.0);
    CHECK(std::string(AowisCompass::direction(0.0)) == "N");
    CHECK(std::string(AowisCompass::direction(44.0)) == "NE");
    CHECK(std::string(AowisCompass::direction(90.0)) == "E");
    CHECK(std::string(AowisCompass::direction(135.0)) == "SE");
    CHECK(std::string(AowisCompass::direction(180.0)) == "S");
    CHECK(std::string(AowisCompass::direction(225.0)) == "SW");
    CHECK(std::string(AowisCompass::direction(270.0)) == "W");
    CHECK(std::string(AowisCompass::direction(315.0)) == "NW");
    CHECK(std::string(AowisCompass::direction(359.9)) == "N");
    CHECK(std::string(AowisCompass::direction(-1.0)) == "N");
    CHECK(std::string(AowisCompass::direction(NAN)) == "--");
    CHECK(AowisCompass::clampedCalibration(-5.0) == 0.0);
    CHECK(AowisCompass::clampedCalibration(0.5) == 0.5);
    CHECK(AowisCompass::clampedCalibration(2.0) == 1.0);
    CHECK(AowisCompass::clampedCalibration(INFINITY) == -1.0);
    // One Euro filter: first fix is shown immediately; no hidden initialization
    // gate may prevent a live sensor bearing from appearing.
    AowisCompass::HeadingFilter filter;
    CHECK(!filter.initialized());
    CHECK(!filter.sample(NAN, 0.0));
    CHECK(filter.sample(359.0, 0.0));
    CHECK(filter.initialized());
    CHECK(std::fabs(filter.displayDegrees() - 359.0) < 1e-9);
    CHECK(!filter.sample(190.0, 0.0)); // duplicate timestamp
    CHECK(!filter.sample(270.0, -0.1)); // out-of-order timestamp
    CHECK(!filter.sample(INFINITY, 0.1));
    CHECK(std::fabs(filter.displayDegrees() - 359.0) < 1e-9);

    // Circular filtering must never rotate across the long arc at north.
    CHECK(filter.sample(1.0, 0.05));
    CHECK(std::fabs(AowisCompass::shortestDelta(359.0, filter.targetDegrees())) < 3.0);
    for (int i = 0; i < 15; ++i)
        filter.advanceDisplay(0.016);
    CHECK(std::fabs(AowisCompass::shortestDelta(filter.displayDegrees(), 1.0)) < 1.5);

    // Static noisy magnetic data: reject jitter instead of following it
    // one-to-one. Warm up and examine the last two seconds.
    filter.reset();
    double noisyMin = 1000.0;
    double noisyMax = -1000.0;
    for (int i = 0; i < 240; ++i)
    {
        const double reading = 42.0 + 1.4 * std::sin(i * 1.73) + 0.65 * std::sin(i * 0.67);
        CHECK(filter.sample(reading, i * 0.05));
        for (int j = 0; j < 3; ++j)
            filter.advanceDisplay(1.0 / 60.0);
        if (i >= 200)
        {
            noisyMin = std::min(noisyMin, filter.displayDegrees());
            noisyMax = std::max(noisyMax, filter.displayDegrees());
        }
    }
    CHECK(noisyMax - noisyMin < 1.5);

    // Continuous rotation including repeated north crossings: smooth forward
    // travel, rather than abruptly jumping a whole revolution backwards.
    filter.reset();
    CHECK(filter.sample(350.0, 0.0));
    double previous = filter.displayDegrees();
    double maximumStep = 0.0;
    for (int i = 1; i <= 120; ++i)
    {
        const double seconds = i * 0.05;
        const double raw = AowisCompass::normalize(350.0 + 90.0 * seconds);
        CHECK(filter.sample(raw, seconds));
        for (int j = 0; j < 3; ++j)
        {
            filter.advanceDisplay(1.0 / 60.0);
            const double step = AowisCompass::shortestDelta(previous, filter.displayDegrees());
            CHECK(step >= -0.05);
            maximumStep = std::max(maximumStep, std::fabs(step));
            previous = filter.displayDegrees();
        }
    }
    CHECK(maximumStep < 12.0);
    CHECK(std::fabs(AowisCompass::shortestDelta(filter.displayDegrees(), 170.0)) < 12.0);

    // Turn sharply, then settle. Fast movements should get a larger One Euro
    // cutoff; display should catch up without an indefinitely long tail.
    filter.reset();
    CHECK(filter.sample(0.0, 0.0));
    for (int i = 1; i <= 12; ++i)
    {
        CHECK(filter.sample(i * 12.0, i * 0.05));
        for (int j = 0; j < 3; ++j)
            filter.advanceDisplay(1.0 / 60.0);
    }
    CHECK(filter.displayDegrees() > 110.0);
    for (int i = 13; i <= 25; ++i)
    {
        CHECK(filter.sample(144.0, i * 0.05));
        for (int j = 0; j < 3; ++j)
            filter.advanceDisplay(1.0 / 60.0);
    }
    CHECK(std::fabs(AowisCompass::shortestDelta(filter.displayDegrees(), 144.0)) < 1.0);

    // Time-based display response must behave similarly at 60Hz and 120Hz.
    AowisCompass::HeadingFilter sixty;
    AowisCompass::HeadingFilter oneTwenty;
    CHECK(sixty.sample(0.0, 0.0));
    CHECK(oneTwenty.sample(0.0, 0.0));
    CHECK(sixty.sample(60.0, 0.05));
    CHECK(oneTwenty.sample(60.0, 0.05));
    for (int i = 0; i < 12; ++i)
        sixty.advanceDisplay(1.0 / 60.0);
    for (int i = 0; i < 24; ++i)
        oneTwenty.advanceDisplay(1.0 / 120.0);
    CHECK(std::fabs(AowisCompass::shortestDelta(sixty.displayDegrees(),
                                                oneTwenty.displayDegrees())) < 0.05);

    // A long sensor pause resets the filter instead of reviving stale bearing.
    CHECK(sixty.sample(250.0, 3.0));
    CHECK(std::fabs(sixty.displayDegrees() - 250.0) < 0.001);
    sixty.reset();
    CHECK(!sixty.initialized());
    CHECK(!sixty.advanceDisplay(0.016));
    CHECK(sixty.sample(75.0, 4.0));
    CHECK(std::fabs(sixty.displayDegrees() - 75.0) < 0.001);

}
