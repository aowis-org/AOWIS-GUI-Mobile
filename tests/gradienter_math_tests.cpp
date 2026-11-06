#include "gradienter_math.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace
{
bool close(double lhs, double rhs, double tolerance = 1.0e-6)
{
    return std::abs(lhs - rhs) < tolerance;
}
}

int main()
{
    using AowisGradienter::Angles;
    Angles angles;
    assert(AowisGradienter::fromGravity(0, 0, 9.81, angles));
    assert(close(angles.x, 0) && close(angles.y, 0));
    assert(AowisGradienter::fromGravity(0, 0, -9.81, angles));
    assert(close(angles.x, 0) && close(angles.y, 0));
    assert(AowisGradienter::fromGravity(9.81, 0, 0, angles));
    assert(close(angles.x, 90) && close(angles.y, 0));
    assert(AowisGradienter::fromGravity(0, -9.81, 0, angles));
    assert(close(angles.y, -90));
    assert(AowisGradienter::fromGravity(1.7, -2.3, 9.4, angles));
    assert(angles.x > 0 && angles.y < 0);
    assert(!AowisGradienter::fromGravity(0, 0, 0, angles));
    assert(!AowisGradienter::fromGravity(std::numeric_limits<double>::quiet_NaN(), 0, 9.81, angles));
    assert(!AowisGradienter::fromGravity(0, 0, 70, angles));

    const Angles original{3.2, -5.1};
    const Angles p90 = AowisGradienter::screenAxes(original, 90);
    const Angles p270 = AowisGradienter::screenAxes(original, -90);
    assert(close(p90.x, -5.1) && close(p90.y, -3.2));
    assert(close(p270.x, 5.1) && close(p270.y, 3.2));
    const Angles p180 = AowisGradienter::screenAxes(p90, 90);
    assert(close(p180.x, -3.2) && close(p180.y, 5.1));
    assert(close(AowisGradienter::totalTilt(original), AowisGradienter::totalTilt(p90)));

    // One-second stationary ±0.3° sensor jitter should be nearly invisible.
    AowisGradienter::Filter filter;
    assert(filter.sample(Angles{0, 0}, 0));
    for (int i = 1; i <= 120; ++i)
    {
        const double t = i / 120.0;
        if (i % 2 == 0)
            assert(filter.sample(Angles{0.3, -0.3}, t));
        else
            assert(filter.sample(Angles{-0.3, 0.3}, t));
        filter.advance(t + 0.0001);
    }
    assert(std::abs(filter.displayed().x) < 0.06);
    assert(std::abs(filter.displayed().y) < 0.06);

    // Real steady tilt must eventually be followed with no overshoot.
    for (int i = 1; i <= 100; ++i)
    {
        const double t = 1.01 + i * 0.02;
        assert(filter.sample(Angles{8, -6}, t));
        filter.advance(t + 0.0001);
        assert(filter.displayed().x <= 8.0001);
        assert(filter.displayed().y >= -6.0001);
    }
    assert(filter.displayed().x > 7.8);
    assert(filter.displayed().y < -5.8);

    // Page reopening / zero-reference resets must not retain stale motion.
    filter.reset();
    assert(!filter.hasSample());
    assert(filter.sample(Angles{2, -3}, 9.0));
    assert(close(filter.displayed().x, 2));
    assert(close(filter.displayed().y, -3));
    assert(!filter.sample(Angles{8, 8}, 9.0));
    return 0;
}
