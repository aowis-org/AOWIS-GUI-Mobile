#pragma once

#include <cmath>

// Magnetic bearings are degrees clockwise from magnetic north. Pure math
// helpers intentionally have no Qt dependency so hardware-free tests can run.
namespace AowisCompass
{
inline constexpr double kPi = 3.14159265358979323846;

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

// Android SENSOR_STATUS_* (0 unreliable .. 3 high accuracy) to 0..1.
inline double calibrationFromAndroidStatus(int status)
{
    if (status < 0)
        return -1.0;
    return clampedCalibration(static_cast<double>(status) / 3.0);
}

// Heading of the *screen-top* direction from an Android rotation vector
// (unit quaternion x, y, z, w mapping device to East/North/Up world axes,
// the same convention as SensorManager.getRotationMatrixFromVector).
//
// screenRotationDegrees is the clockwise angle by which the screen-top is
// turned relative to the device's native top edge (0, 90, 180, 270).
//
// Flat phone: the screen-top edge is projected onto the horizontal plane.
// Upright phone: that projection vanishes, but the back-camera direction
// (-Z) becomes horizontal. The camera direction is weighted by the squared
// upward component of the screen-top axis, so a flat phone stays insensitive
// to roll, an upright phone follows the camera even when leaning sideways,
// and pitching in between hands over smoothly without a switch. Returns -1
// for an invalid quaternion or a degenerate pose.
inline double headingFromRotationVector(double x, double y, double z, double w,
                                        int screenRotationDegrees)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w))
        return -1.0;
    const double norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (norm < 0.5)
        return -1.0;
    x /= norm;
    y /= norm;
    z /= norm;
    w /= norm;

    // Rotation matrix rows: world East, North, Up.
    const double r0 = 1.0 - 2.0 * y * y - 2.0 * z * z;
    const double r1 = 2.0 * x * y - 2.0 * z * w;
    const double r2 = 2.0 * x * z + 2.0 * y * w;
    const double r3 = 2.0 * x * y + 2.0 * z * w;
    const double r4 = 1.0 - 2.0 * x * x - 2.0 * z * z;
    const double r5 = 2.0 * y * z - 2.0 * x * w;
    const double r6 = 2.0 * x * z - 2.0 * y * w;
    const double r7 = 2.0 * y * z + 2.0 * x * w;

    const double rotation = static_cast<double>(screenRotationDegrees) * kPi / 180.0;
    const double s = std::sin(rotation);
    const double c = std::cos(rotation);
    // Screen-top axis in device coordinates is (sin, cos, 0).
    const double topEast = r0 * s + r1 * c;
    const double topNorth = r3 * s + r4 * c;
    const double topUp = r6 * s + r7 * c;
    // Back camera axis (-Z) in world coordinates.
    const double backEast = -r2;
    const double backNorth = -r5;

    // Weights: |top_h|^2 = 1 - up^2 for the screen-top direction and up^2
    // for the camera direction, i.e. they always sum to one. Signed so that
    // the camera term points forward for either pitch direction.
    const double topHorizontal = std::hypot(topEast, topNorth);
    double east = topHorizontal * topEast;
    double north = topHorizontal * topNorth;
    const double backHorizontal = std::hypot(backEast, backNorth);
    if (backHorizontal > 1e-6)
    {
        const double weight = topUp * std::fabs(topUp) / backHorizontal;
        east += weight * backEast;
        north += weight * backNorth;
    }
    if (std::hypot(east, north) < 0.2)
        return -1.0;
    return normalize(std::atan2(east, north) * 180.0 / kPi);
}
}
