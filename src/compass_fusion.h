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
//   Both use the same filter, and MagneticHeadingGuard keeps the heading
//   while the magnetometer is visibly wrong (tilting a cheap phone).
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

inline Quaternion inverseRotation(const Quaternion &q)
{
    return {-q.x, -q.y, -q.z, q.w};
}

inline Vector3 rotateDeviceToWorld(const Quaternion &q, const Vector3 &v)
{
    const Quaternion r = multiply(multiply(q, {v.x, v.y, v.z, 0.0}), inverseRotation(q));
    return {r.x, r.y, r.z};
}

// Angle (degrees) between two vectors; 0 if either is zero.
inline double angleBetweenDegrees(const Vector3 &a, const Vector3 &b)
{
    const double n = length(a) * length(b);
    if (!(n > 0.0))
        return 0.0;
    const double c = (a.x * b.x + a.y * b.y + a.z * b.z) / n;
    return std::acos(std::clamp(c, -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
}

// Holds the heading while the magnetometer is visibly wrong, for fusions
// without a gyroscope (where nothing else knows the heading).
//
// Measured on cheap phones (Ulefone Armor X13, recordings in
// benchmarks/lookaround/): while the phone tilts up or down, the reported
// field direction lags, then catches up in jumps of 10-25 deg; its angle
// below the horizontal ("dip") swings between about 20 and 89 deg while the
// true dip is 66. Near 90 the horizontal part of the field vanishes and the
// heading swings by up to 180 deg although the phone only tilted. Tilt from
// the accelerometer stays right throughout.
//
// So the orientation is updated in two parts:
// * tilt: always from the measurement (the previous orientation, rotated
//   by the smallest rotation that puts the measured gravity on Up);
// * heading: from the measurement only as far as the field looks right:
//   fully while its dip is within kDipToleranceDegrees of the reference,
//   not at all beyond kDipRejectDegrees, linearly in between: the heading
//   follows the measurement's own CHANGES that much, at most as fast as a
//   hand turns (kMaxTurnDegreesPerSecond). The offset that builds up (held
//   changes, jumps) is caught up smoothly: at rest
//   (gravity and field both steady) with kRestCatchSeconds; while moving
//   with a field that has looked right for kSettleSeconds with
//   kCatchSeconds (not for about 180 deg: the signature of a vanished
//   horizontal field); else not at all.
// The reference dip is learned at rest, and follows the trusted field.
// Until the first rest the guard passes the measurement through.
class MagneticHeadingGuard final
{
public:
    static constexpr double kDipToleranceDegrees = 8.0;
    static constexpr double kDipRejectDegrees = 16.0;
    // Faster than this, a heading change is a jump (or the hold catching up).
    static constexpr double kMaxTurnDegreesPerSecond = 360.0;
    static constexpr double kFieldIntervalSeconds = 0.03;
    static constexpr double kFlipDegrees = 135.0;
    static constexpr double kRestAngleDegrees = 3.0;
    static constexpr double kRestSeconds = 0.3;
    static constexpr double kRestCatchSeconds = 0.4;
    static constexpr double kSettleSeconds = 0.3;
    static constexpr double kCatchSeconds = 0.4;
    static constexpr double kReferenceSeconds = 3.0;
    static constexpr double kRestReferenceSeconds = 0.5;
    // A dip "wrong" this long is the new normal (another place, a case).
    static constexpr double kRelearnSeconds = 5.0;
    static constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;

    void reset()
    {
        this->m_hasOrientation = false;
        this->m_hasReference = false;
        this->m_hasAnchor = false;
        this->m_stillSeconds = 0.0;
        this->m_wrongSeconds = 0.0;
        this->m_rightSeconds = 0.0;
        this->m_trust = 1.0;
    }

    bool hasReference() const { return this->m_hasReference; }
    double referenceDipDegrees() const { return this->m_referenceDip; }
    double trust() const { return this->m_trust; }
    bool atRest() const { return this->m_stillSeconds >= kRestSeconds; }

    // gravity, field: device-frame vectors the measurement was built from
    // (TRIAD); measured: that measurement; dt: seconds since the previous
    // call (0 for the first). Returns the guarded orientation.
    Quaternion apply(const Vector3 &gravity, const Vector3 &field, const Quaternion &measured, double dt)
    {
        const double g = length(gravity);
        const double f = length(field);
        if (!(g > 0.0) || !(f > 0.0) || !std::isfinite(dt))
            return measured;
        dt = std::max(0.0, dt);
        const double dip = -std::asin(std::clamp((gravity.x * field.x + gravity.y * field.y + gravity.z * field.z) / (g * f),
                                                 -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
        // Rest: gravity and field both within kRestAngleDegrees of an anchor.
        if (!this->m_hasAnchor || angleBetweenDegrees(gravity, this->m_anchorGravity) > kRestAngleDegrees ||
            angleBetweenDegrees(field, this->m_anchorField) > kRestAngleDegrees)
        {
            this->m_anchorGravity = gravity;
            this->m_anchorField = field;
            this->m_hasAnchor = true;
            this->m_stillSeconds = 0.0;
        }
        else
        {
            this->m_stillSeconds += dt;
        }
        const bool rest = atRest();
        if (!this->m_hasOrientation)
        {
            this->m_orientation = measured;
            this->m_previousMeasured = measured;
            this->m_hasOrientation = true;
            if (rest)
                learn(dip, 0.0, true);
            return measured;
        }
        double weight = 1.0;
        if (rest)
        {
            learn(dip, dt, false);
            this->m_wrongSeconds = 0.0;
        }
        else if (this->m_hasReference)
        {
            const double error = std::fabs(dip - this->m_referenceDip);
            weight = error <= kDipToleranceDegrees ? 1.0
                   : error >= kDipRejectDegrees ? 0.0
                   : (kDipRejectDegrees - error) / (kDipRejectDegrees - kDipToleranceDegrees);
            this->m_wrongSeconds = weight > 0.5 ? 0.0 : this->m_wrongSeconds + dt;
            if (this->m_wrongSeconds > kRelearnSeconds)
            {
                learn(dip, 0.0, true);
                this->m_wrongSeconds = 0.0;
                weight = 1.0;
            }
            else if (weight > 0.5 && dt > 0.0)
            {
                this->m_referenceDip += -std::expm1(-dt / kReferenceSeconds) * (dip - this->m_referenceDip);
            }
        }
        this->m_rightSeconds = weight >= 0.999 ? this->m_rightSeconds + dt : 0.0;

        // Tilt: the previous orientation, turned by the smallest rotation
        // putting the measured gravity on Up. What remains between it and
        // the measurement is a turn about Up.
        Quaternion held;
        double turn = 0.0;
        if (!tiltAndTurn(this->m_orientation, gravity, measured, &held, &turn))
        {
            // Upside down against the previous pose: start over.
            this->m_orientation = measured;
            this->m_previousMeasured = measured;
            return measured;
        }
        // The measurement's own heading change since the previous reading
        // (the same split): followed, up to what a hand turns, as far as
        // the field looks right. A change while the field looks wrong is
        // not followed; what built up that way (or by a jump) is caught up
        // smoothly, only where that is safe.
        Quaternion unused;
        double change = 0.0;
        if (!tiltAndTurn(this->m_previousMeasured, gravity, measured, &unused, &change))
            change = 0.0;
        this->m_previousMeasured = measured;
        // Per reading: the field arrives slower than gravity (50 vs 120 Hz
        // on cheap phones), so its change comes in one call of a few ms.
        const double step = kMaxTurnDegreesPerSecond * kRadiansPerDegree * std::max(dt, kFieldIntervalSeconds);
        this->m_trust = weight;
        double applied = 0.0;
        if (!this->m_hasReference)
        {
            applied = turn;
        }
        else
        {
            applied = weight * std::clamp(change, -step, step);
            const double offset = std::remainder(turn - applied, 2.0 * kPiValue);
            double catchUp = 0.0;
            if (rest)
                catchUp = -std::expm1(-dt / kRestCatchSeconds);
            else if (this->m_rightSeconds >= kSettleSeconds && std::fabs(offset) < kFlipDegrees * kRadiansPerDegree)
                catchUp = -std::expm1(-dt / kCatchSeconds);
            applied += catchUp * offset;
        }
        const double half = 0.5 * applied;
        this->m_orientation = normalized(multiply({0.0, 0.0, std::sin(half), std::cos(half)}, held));
        return this->m_orientation;
    }

private:
    static constexpr double kPiValue = 3.14159265358979323846;

    // held: from, tilted so that gravity (device frame) maps to Up; turn:
    // the angle (radians, -pi..pi) about Up from held to 'to', which maps
    // gravity to Up as well. False if 'from' has gravity pointing down.
    static bool tiltAndTurn(const Quaternion &from, const Vector3 &gravity, const Quaternion &to,
                            Quaternion *held, double *turn)
    {
        const Vector3 up = rotateDeviceToWorld(from, gravity);
        const double u = length(up);
        if (!(u > 0.0))
            return false;
        const double d = up.z / u;
        if (!(1.0 + d > 1e-6))
            return false;
        const Quaternion swing = normalized({up.y / u, -up.x / u, 0.0, 1.0 + d});
        *held = normalized(multiply(swing, from));
        const Quaternion e = multiply(to, inverseRotation(*held));
        *turn = std::remainder(2.0 * std::atan2(e.z, e.w), 2.0 * kPiValue);
        return true;
    }

    void learn(double dip, double dt, bool restart)
    {
        if (restart || !this->m_hasReference)
            this->m_referenceDip = dip;
        else
            this->m_referenceDip += -std::expm1(-dt / kRestReferenceSeconds) * (dip - this->m_referenceDip);
        this->m_hasReference = true;
    }

    bool m_hasOrientation = false;
    Quaternion m_orientation;
    Quaternion m_previousMeasured;
    bool m_hasReference = false;
    double m_referenceDip = 0.0;
    bool m_hasAnchor = false;
    Vector3 m_anchorGravity;
    Vector3 m_anchorField;
    double m_stillSeconds = 0.0;
    double m_wrongSeconds = 0.0;
    double m_rightSeconds = 0.0;
    double m_trust = 1.0;
};

class AttitudeFusion final
{
public:
    static constexpr double kStandardGravity = 9.80665;
    // Without gyroscope: low-pass of gravity and field (identical).
    static constexpr double kNoGyroscopeFilterSeconds = 0.20;

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
        this->m_guard.reset();
    }

    void setGyroscopeAvailable(bool available) { this->m_useGyroscope = available; }
    bool gyroscopeAvailable() const { return this->m_useGyroscope; }
    bool hasOrientation() const { return this->m_hasOrientation; }
    Quaternion orientation() const { return this->m_orientation; }
    // Smoothed |B| in the sensor's unit (Android: microtesla).
    double fieldStrength() const { return this->m_fieldStrength; }
    // Without gyroscope: how far the heading follows the magnetometer now
    // (1 full, 0 held), and the learned dip (NaN before the first rest).
    double headingTrust() const { return this->m_useGyroscope ? 1.0 : this->m_guard.trust(); }
    double referenceDipDegrees() const
    {
        return this->m_guard.hasReference() ? this->m_guard.referenceDipDegrees() : std::nan("");
    }

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
        const double a = this->m_useGyroscope ? 1.0 : 1.0 - std::exp(-dt / kNoGyroscopeFilterSeconds);
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
        // Same low-pass as gravity: a field filtered faster than gravity
        // turns every tilt into a heading swing (see look_around_field.h).
        const double a = this->m_useGyroscope ? 1.0 : 1.0 - std::exp(-dt / kNoGyroscopeFilterSeconds);
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
            this->m_orientation = this->m_useGyroscope
                ? measured : this->m_guard.apply(this->m_gravity, this->m_field, measured, 0.0);
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
            // Inputs are already low-passed: tilt as measured, heading as far
            // as the magnetometer looks right.
            this->m_orientation = this->m_guard.apply(this->m_gravity, this->m_field, measured, dt);
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

    MagneticHeadingGuard m_guard;
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
