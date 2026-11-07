#pragma once

#include <QString>

#include <cmath>

// Dependency-free helpers of the mobile map: the HUD scale bar and the
// two-finger gesture geometry. Covered by tests/mobile_map_math_tests.cpp.
namespace AowisMobileMap
{
// Great-circle distance on the WGS84 mean sphere (scale bar accuracy).
inline double distanceMeters(double lon0_deg, double lat0_deg, double lon1_deg, double lat1_deg)
{
    constexpr double EarthRadiusM = 6371008.8;
    constexpr double DegToRad = 3.14159265358979323846 / 180.0;
    const double phi0 = lat0_deg * DegToRad;
    const double phi1 = lat1_deg * DegToRad;
    const double half_dphi = (phi1 - phi0) * 0.5;
    const double half_dlambda = (lon1_deg - lon0_deg) * DegToRad * 0.5;
    const double a = std::sin(half_dphi) * std::sin(half_dphi)
        + std::cos(phi0) * std::cos(phi1) * std::sin(half_dlambda) * std::sin(half_dlambda);
    return 2.0 * EarthRadiusM * std::asin(std::sqrt(std::fmin(1.0, std::fmax(0.0, a))));
}

// The longest 1 / 2 / 5 x 10^n metre distance not above maximum_m; 0 when
// there is none (non-finite or below 1 m).
inline double niceScaleDistanceMeters(double maximum_m)
{
    if (!std::isfinite(maximum_m) || maximum_m < 1.0)
        return 0.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(maximum_m)));
    const double steps[] = {5.0, 2.0, 1.0};
    for (const double step : steps)
    {
        if (step * magnitude <= maximum_m)
            return step * magnitude;
    }
    return magnitude;
}

// "500 m", "2 km" (metric, as the scale bar draws it).
inline QString scaleLabel(double meters)
{
    if (meters >= 1000.0)
        return QStringLiteral("%1 km").arg(meters / 1000.0, 0, 'g', 6);
    return QStringLiteral("%1 m").arg(meters, 0, 'g', 6);
}

// Two touch points: their distance and direction (degrees, screen y down).
inline double pointDistance(double x0, double y0, double x1, double y1)
{
    return std::hypot(x1 - x0, y1 - y0);
}

inline double pointAngleDegrees(double x0, double y0, double x1, double y1)
{
    return std::atan2(y1 - y0, x1 - x0) * 180.0 / 3.14159265358979323846;
}

// The smallest signed difference of two angles in degrees (-180, 180].
inline double angleDeltaDegrees(double from_deg, double to_deg)
{
    double delta = std::fmod(to_deg - from_deg, 360.0);
    if (delta <= -180.0)
        delta += 360.0;
    else if (delta > 180.0)
        delta -= 360.0;
    return delta;
}

// Pinch scale as zoom steps: one step doubles the map scale.
inline double zoomStepsForScale(double scale)
{
    if (!std::isfinite(scale) || scale <= 0.0)
        return 0.0;
    return std::log2(scale);
}
}
