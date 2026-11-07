#include "mobile_map_math.h"
#include "look_around_stabilizer.h"

#include <cmath>
#include <cstdio>

namespace
{
int g_checks = 0;
int g_failures = 0;

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

bool near(double value, double expected, double tolerance)
{
    return std::fabs(value - expected) <= tolerance;
}
}

int main()
{
    using namespace AowisMobileMap;
    // One degree of latitude is about 111.2 km on the mean sphere.
    expectTrue(near(distanceMeters(0.0, 0.0, 0.0, 1.0), 111195.0, 5.0), "one degree of latitude");
    expectTrue(near(distanceMeters(8.5, 47.4, 8.5, 47.4), 0.0, 1e-9), "zero distance");
    expectTrue(distanceMeters(-179.9, 0.0, 179.9, 0.0) < 23000.0, "short way across the antimeridian");

    expectTrue(niceScaleDistanceMeters(0.5) == 0.0, "no scale below one metre");
    expectTrue(niceScaleDistanceMeters(std::nan("")) == 0.0, "no scale for NaN");
    expectTrue(niceScaleDistanceMeters(7.0) == 5.0, "7 m -> 5 m");
    expectTrue(niceScaleDistanceMeters(380.0) == 200.0, "380 m -> 200 m");
    expectTrue(niceScaleDistanceMeters(1000.0) == 1000.0, "1 km stays");
    expectTrue(niceScaleDistanceMeters(9999.0) == 5000.0, "9999 m -> 5 km");
    expectTrue(scaleLabel(200.0) == QStringLiteral("200 m"), "metre label");
    expectTrue(scaleLabel(5000.0) == QStringLiteral("5 km"), "kilometre label");
    expectTrue(scaleLabel(1500.0) == QStringLiteral("1.5 km"), "fractional kilometre label");

    expectTrue(near(pointDistance(0.0, 0.0, 3.0, 4.0), 5.0, 1e-12), "point distance");
    expectTrue(near(pointAngleDegrees(0.0, 0.0, 0.0, 1.0), 90.0, 1e-12), "screen y down is +90 degrees");
    expectTrue(near(angleDeltaDegrees(170.0, -170.0), 20.0, 1e-12), "angle delta wraps forward");
    expectTrue(near(angleDeltaDegrees(-170.0, 170.0), -20.0, 1e-12), "angle delta wraps backward");
    expectTrue(near(zoomStepsForScale(2.0), 1.0, 1e-12), "doubling is one zoom step");
    expectTrue(near(zoomStepsForScale(0.5), -1.0, 1e-12), "halving is minus one step");
    expectTrue(zoomStepsForScale(0.0) == 0.0, "degenerate pinch does nothing");

    const QQuaternion still;
    const QQuaternion tremor = QQuaternion::fromAxisAndAngle(0, 1, 0, 0.2f);
    const double dt = 1.0 / 60.0;
    const QQuaternion zoomed = stabilizeLookAround(still, tremor, 8.0, dt);
    expectTrue(stabilizeLookAround(still, tremor, 1.0, dt) == tremor, "normal zoom tracks raw pose");
    expectTrue(zoomed.vector().length() < 0.1f * tremor.vector().length(), "8x damps small movements");
    const QQuaternion turn = QQuaternion::fromAxisAndAngle(0, 1, 0, 30);
    QQuaternion estimate;
    for (int frame = 0; frame < 12; ++frame)
        estimate = stabilizeLookAround(estimate, turn, 8.0, dt);
    expectTrue((estimate.conjugated() * turn).vector().length() < 0.018f, "deliberate turn follows within two degrees");
    expectTrue(stabilizeLookAround(still, turn, 8.0, 0.5) == turn, "new sensor stream starts immediately");
    expectTrue((zoomed.conjugated() * stabilizeLookAround(still, -tremor, 8.0, dt)).vector().length()
               < 0.00001f, "equivalent quaternion signs take the same short path");

    std::printf("mobile_map_math: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
