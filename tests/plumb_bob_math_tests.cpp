#include "plumb_bob_math.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using AowisPlumbBob::Filter;
    using AowisPlumbBob::Reading;
    using AowisPlumbBob::ScreenDirection;
    Reading value;
    assert(AowisPlumbBob::fromGravity(0, 9.81, 0, value));
    assert(value.suitable);
    assert(std::abs(value.downX) < 1e-6);
    assert(std::abs(value.downY - 1) < 1e-6);
    assert(AowisPlumbBob::plumb(AowisPlumbBob::edgeDeviation({value.downX, value.downY}), value.faceLean));

    assert(AowisPlumbBob::fromGravity(0, -9.81, 0, value));
    assert(value.suitable);
    assert(AowisPlumbBob::edgeDeviation({value.downX, value.downY}) < 0.01);

    assert(AowisPlumbBob::fromGravity(9.81, 0, 0, value));
    assert(value.suitable);
    assert(std::abs(AowisPlumbBob::edgeDeviation({value.downX, value.downY}) - 90.0) < 1e-5);

    assert(AowisPlumbBob::fromGravity(0, 0, 9.81, value));
    assert(!value.suitable);
    assert(std::abs(value.faceLean - 90) < 1e-6);

    assert(AowisPlumbBob::fromGravity(0, 9.81 / std::sqrt(2),
                                     9.81 / std::sqrt(2), value));
    assert(value.suitable);
    assert(std::abs(value.faceLean - 45) < .0001);
    assert(!AowisPlumbBob::plumb(0, value.faceLean));
    assert(!AowisPlumbBob::plumb(1.0, 0));
    assert(AowisPlumbBob::plumb(0.6, 0.6));

    assert(!AowisPlumbBob::fromGravity(NAN, 1, 0, value));
    assert(!AowisPlumbBob::fromGravity(0, 0, 0, value));
    assert(!AowisPlumbBob::fromGravity(0, 0, 60, value));
    const ScreenDirection rotated = AowisPlumbBob::screenDirection({0, 1}, 90);
    assert(std::abs(rotated.x - 1) < 1e-6);
    assert(std::abs(rotated.y) < 1e-6);
    const ScreenDirection reversed = AowisPlumbBob::screenDirection({0, 1}, 180);
    assert(std::abs(reversed.y + 1) < 1e-6);
    assert(std::abs(AowisPlumbBob::shortestDelta(179, -179) - 2) < 1e-6);

    Filter filter;
    assert(AowisPlumbBob::fromGravity(0, 9.81, 0, value));
    assert(filter.sample(value, 0.0));
    assert(filter.initialized());
    assert(!filter.advance(0.016));
    assert(std::abs(filter.direction().x) < 1e-6);
    for (int i = 1; i <= 200; ++i)
    {
        const double noise = (i % 2 ? -0.007 : 0.007); // ~0.04 deg
        assert(AowisPlumbBob::fromGravity(noise, 9.81, 0, value));
        assert(filter.sample(value, i * .02));
        filter.advance(i * .02 + .016);
    }
    assert(std::abs(filter.direction().x) < .003);
    // Deliberate rotation eventually overcomes the stationary-noise gate.
    for (int i = 201; i <= 300; ++i)
    {
        const double angle = (i - 200) * 0.3 * AowisPlumbBob::kPi / 180;
        assert(AowisPlumbBob::fromGravity(-9.81 * std::sin(angle),
                                         9.81 * std::cos(angle), 0, value));
        assert(filter.sample(value, i * .02));
        filter.advance(i * .02 + .016);
    }
    assert(filter.direction().x > .35);
    filter.reset();
    assert(!filter.initialized());
    assert(!filter.advance(20));
    std::cout << "Plumb bob gravity and smoothing tests passed\n";
}
