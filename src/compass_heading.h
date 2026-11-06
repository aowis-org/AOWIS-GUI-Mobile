#pragma once

#include <cmath>

// Magnetic bearings are degrees clockwise from magnetic north. Pure math
// helpers intentionally have no Qt dependency so hardware-free tests can run.
namespace AowisCompass
{
inline double normalize(double degrees)
{
    if (!std::isfinite(degrees))
        return -1.0;
    const double remainder = std::fmod(degrees, 360.0);
    return remainder < 0.0 ? remainder + 360.0 : remainder;
}

inline const char *direction(double degrees)
{
    const double normalized = normalize(degrees);
    if (normalized < 0.0)
        return "--";
    static const char *const directions[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    const int octant = static_cast<int>(std::floor((normalized + 22.5) / 45.0)) % 8;
    return directions[octant];
}

inline double clampedCalibration(double level)
{
    if (!std::isfinite(level))
        return -1.0;
    return std::fmax(0.0, std::fmin(1.0, level));
}
}
