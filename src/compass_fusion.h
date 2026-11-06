#pragma once

#include <algorithm>
#include <cmath>

// AOWIS' own attitude fusion from raw Android sensors, used when the phone
// offers no (working) fused TYPE_ROTATION_VECTOR. No Qt dependency, so the
// math is covered by hardware-free tests.
//
// Frames follow Android: device X right, Y top edge, Z out of the screen;
// world X East, Y North, Z Up. The orientation quaternion maps device to
// world vectors, i.e. it is directly usable with headingFromRotationVector().
//
// * Measurement: TRIAD from gravity (accelerometer) and the geomagnetic
//   field, East = M x G, North = G x East: the same construction as
//   SensorManager.getRotationMatrix().
// * With gyroscope: the gyroscope is integrated at its full rate and the
//   orientation is pulled slowly towards the measurement (complementary
//   filter). Hand tremor and short accelerations barely tilt the estimate,
//   while a real turn is followed immediately by the gyroscope.
// * Without gyroscope: gravity and magnetic field are low-pass filtered
//   *before* TRIAD. Qt's compass used each raw accelerometer packet, so the
//   hand's own acceleration tilted the solution and swung the heading.
//
// Implausible magnetic magnitudes (steel, magnets, speakers) and strong
// non-gravity accelerations are not used for correction.
namespace AowisCompass
{
struct Quaternion
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

struct Vector3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vector3 cross(const Vector3 &a, const Vector3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double length(const Vector3 &v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Quaternion multiply(const Quaternion &a, const Quaternion &b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quaternion normalized(const Quaternion &q)
{
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-12) || !std::isfinite(n))
        return {};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// Rotation matrix rows (world East, North, Up in device coordinates) to a
// unit quaternion. Shepperd's method: numerically stable for all poses.
inline Quaternion quaternionFromRows(const Vector3 &east, const Vector3 &north, const Vector3 &up)
{
    const double m00 = east.x, m01 = east.y, m02 = east.z;
    const double m10 = north.x, m11 = north.y, m12 = north.z;
    const double m20 = up.x, m21 = up.y, m22 = up.z;
    const double trace = m00 + m11 + m22;
    Quaternion q;
    if (trace > 0.0)
    {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q.w = (m21 - m12) / s;
        q.x = 0.25 * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25 * s;
        q.z = (m12 + m21) / s;
    }
    else
    {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25 * s;
    }
    return normalized(q);
}

// TRIAD: gravity ("up" as measured by an Android accelerometer at rest) and
// geomagnetic field, both in device coordinates. False if degenerate
// (free fall, or field parallel to gravity).
inline bool orientationFromGravityAndField(const Vector3 &gravity, const Vector3 &field,
                                           Quaternion *orientation)
{
    const double g = length(gravity);
    if (!(g > 1.0) || !std::isfinite(g))
        return false;
    Vector3 east = cross(field, gravity);
    const double e = length(east);
    if (!(e > 1e-6 * g * std::max(1.0, length(field))) || !std::isfinite(e))
        return false;
    east = {east.x / e, east.y / e, east.z / e};
    const Vector3 up{gravity.x / g, gravity.y / g, gravity.z / g};
    const Vector3 north = cross(up, east);
    *orientation = quaternionFromRows(east, north, up);
    return true;
}

// Step a from towards b by fraction t (normalized linear interpolation along
// the shorter arc; accurate for the small steps used here).
inline Quaternion blend(const Quaternion &a, Quaternion b, double t)
{
    const double dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0)
        b = {-b.x, -b.y, -b.z, -b.w};
    return normalized({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y),
                       a.z + t * (b.z - a.z), a.w + t * (b.w - a.w)});
}

class AttitudeFusion final
{
public:
    static constexpr double kStandardGravity = 9.80665;

    void reset()
    {
        this->m_hasGravity = false;
        this->m_hasField = false;
        this->m_hasOrientation = false;
        this->m_lastAccelerometerSeconds = -1.0;
        this->m_lastFieldSeconds = -1.0;
        this->m_lastGyroscopeSeconds = -1.0;
        this->m_lastCorrectionSeconds = -1.0;
        this->m_fieldStrength = -1.0;
    }

    void setGyroscopeAvailable(bool available) { this->m_useGyroscope = available; }
    bool gyroscopeAvailable() const { return this->m_useGyroscope; }
    bool hasOrientation() const { return this->m_hasOrientation; }
    Quaternion orientation() const { return this->m_orientation; }
    // Smoothed |B| in the sensor's unit (Android: microtesla).
    double fieldStrength() const { return this->m_fieldStrength; }

    void addAccelerometer(double x, double y, double z, double seconds)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(seconds))
            return;
        const Vector3 sample{x, y, z};
        if (!this->m_hasGravity)
        {
            this->m_gravity = sample;
            this->m_hasGravity = true;
            this->m_lastAccelerometerSeconds = seconds;
            correct(seconds);
            return;
        }
        const double dt = clampedDelta(seconds, this->m_lastAccelerometerSeconds);
        this->m_lastAccelerometerSeconds = seconds;
        if (dt <= 0.0)
            return;
        // With a gyroscope the complementary filter does the averaging (a
        // pre-filter would only make the measurement lag during turns);
        // without one, gravity itself must be calmed down first.
        const double a = this->m_useGyroscope ? 1.0 : 1.0 - std::exp(-dt / 0.20);
        this->m_gravity = {this->m_gravity.x + a * (x - this->m_gravity.x),
                           this->m_gravity.y + a * (y - this->m_gravity.y),
                           this->m_gravity.z + a * (z - this->m_gravity.z)};
        if (!this->m_useGyroscope)
            correct(seconds);
    }

    void addMagneticField(double x, double y, double z, double seconds)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(seconds))
            return;
        const double strength = std::sqrt(x * x + y * y + z * z);
        if (!this->m_hasField)
        {
            this->m_field = {x, y, z};
            this->m_hasField = true;
            this->m_fieldStrength = strength;
            this->m_lastFieldSeconds = seconds;
            correct(seconds);
            return;
        }
        const double dt = clampedDelta(seconds, this->m_lastFieldSeconds);
        this->m_lastFieldSeconds = seconds;
        if (dt <= 0.0)
            return;
        this->m_fieldStrength += (1.0 - std::exp(-dt / 3.0)) * (strength - this->m_fieldStrength);
        const double a = this->m_useGyroscope ? 1.0 : 1.0 - std::exp(-dt / 0.15);
        this->m_field = {this->m_field.x + a * (x - this->m_field.x),
                         this->m_field.y + a * (y - this->m_field.y),
                         this->m_field.z + a * (z - this->m_field.z)};
        correct(seconds);
    }

    // Angular velocity in rad/s about device axes (Android TYPE_GYROSCOPE,
    // counter-clockwise positive, bias-compensated by the OS).
    void addGyroscope(double x, double y, double z, double seconds)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(seconds))
            return;
        const double dt = this->m_lastGyroscopeSeconds < 0.0
            ? 0.0 : clampedDelta(seconds, this->m_lastGyroscopeSeconds);
        this->m_lastGyroscopeSeconds = seconds;
        if (!this->m_useGyroscope || !this->m_hasOrientation || dt <= 0.0)
            return;
        // q <- q * exp(omega dt / 2): exact for constant rate over dt.
        const double rate = std::sqrt(x * x + y * y + z * z);
        const double angle = rate * dt;
        if (angle < 1e-12)
            return;
        const double s = std::sin(angle / 2.0) / rate;
        const Quaternion step{x * s, y * s, z * s, std::cos(angle / 2.0)};
        this->m_orientation = normalized(multiply(this->m_orientation, step));
    }

private:
    static double clampedDelta(double now, double previous)
    {
        const double dt = now - previous;
        if (!(dt > 0.0))
            return 0.0;
        return std::min(dt, 0.25);
    }

    void correct(double seconds)
    {
        if (!this->m_hasGravity || !this->m_hasField)
            return;
        Quaternion measured;
        if (!orientationFromGravityAndField(this->m_gravity, this->m_field, &measured))
            return;
        if (!this->m_hasOrientation)
        {
            this->m_orientation = measured;
            this->m_hasOrientation = true;
            this->m_lastCorrectionSeconds = seconds;
            return;
        }
        const double dt = clampedDelta(seconds, this->m_lastCorrectionSeconds);
        this->m_lastCorrectionSeconds = seconds;
        if (dt <= 0.0)
            return;
        if (!this->m_useGyroscope)
        {
            // Inputs are already low-passed: take the measurement as is.
            this->m_orientation = measured;
            return;
        }
        // Trust the measurement less while it is visibly disturbed: a
        // shaking hand (|a| != g) or a local magnetic anomaly (|B| jump).
        double tau = 2.0;
        const double gravityError = std::fabs(length(this->m_gravity) - kStandardGravity) / kStandardGravity;
        if (gravityError > 0.08)
            tau *= 1.0 + 12.0 * std::min(1.0, gravityError);
        if (this->m_fieldStrength > 0.0)
        {
            const double fieldError = std::fabs(length(this->m_field) - this->m_fieldStrength) / this->m_fieldStrength;
            if (fieldError > 0.15)
                tau *= 1.0 + 12.0 * std::min(1.0, fieldError);
        }
        this->m_orientation = blend(this->m_orientation, measured, 1.0 - std::exp(-dt / tau));
    }

    bool m_useGyroscope = false;
    bool m_hasGravity = false;
    bool m_hasField = false;
    bool m_hasOrientation = false;
    Vector3 m_gravity;
    Vector3 m_field;
    Quaternion m_orientation;
    double m_fieldStrength = -1.0;
    double m_lastAccelerometerSeconds = -1.0;
    double m_lastFieldSeconds = -1.0;
    double m_lastGyroscopeSeconds = -1.0;
    double m_lastCorrectionSeconds = -1.0;
};
}
