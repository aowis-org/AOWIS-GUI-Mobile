#pragma once

#include "compass_fusion.h"
#include "compass_heading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>

// Look around on phones without a real gyroscope. Qt-free; tested in
// tests/look_around_field_tests.cpp.
//
// Without a gyroscope, every heading the phone offers (its rotation vector,
// and an emulated "game" rotation vector) comes from the magnetometer, with
// "down" from the accelerometer. The Earth's field dips steeply (about 65 deg
// in Central Europe). If the "down" used for the heading is off by e degrees
// about the north-south axis, the heading is off by about e * tan(dip), twice
// e here. Off about the east-west axis it hardly matters. A fusion that
// filters gravity slower than the field (or reads them at different times)
// is off exactly while the phone tilts: tilting up/down then shakes the view
// sideways facing east or west, but not facing north or south.
//
// Two parts:
//
// 1. FieldOrientation (sensor thread) builds the orientation from the raw
//    accelerometer and magnetometer itself: the accelerometer is interpolated
//    to each magnetometer timestamp, and both vectors pass the SAME low-pass
//    at the same instants. Both then lag equally, so the orientation lags a
//    little as a whole but has no tilt-to-heading error from a mismatch.
//
// 2. LookAroundFieldSource (GUI thread) turns that orientation into the two
//    inputs of LookAroundEngine:
//    * a motion pose: the orientation's own increments, except that a change
//      of heading is only passed on while the phone turns about Up rather
//      than tilts (gate: turn^2 / (turn^2 + (kTiltRatio * tilt)^2)). Heading
//      that tilting would produce (the residual of the error above, linear
//      acceleration of the hand) does not move the view;
//    * the orientation itself as the compass. The engine follows it in
//      heading quickly while turning and slowly at rest, and holds it while
//      tilting (turn-gated heading). So the magnetometer only corrects what
//      motion shows; it does not move the view by itself while tilting.
//    The magnetometer is checked against the field expected at the GPS fix
//    (strength and dip, Android GeomagneticField), else against its own
//    recent average. While it is disturbed (steel, cars, magnets) the heading
//    is held: no heading motion, no compass correction.
//
// Frames follow Android: device X right, Y top edge, Z out of the screen;
// world East, North, Up. Accelerometer reads +g up at rest.
namespace AowisMobileMap
{
class FieldOrientation final
{
public:
    typedef AowisCompass::Quaternion Quaternion;
    typedef AowisCompass::Vector3 Vector3;

    // Identical low-pass of gravity and field, speed-adaptive: strong while
    // the phone is (nearly) still, where shake shows; light while it moves,
    // where lag shows. Both vectors always share the same filter, so the
    // adaptation never re-creates the tilt-to-heading error. The speed is
    // the rotation rate of the raw (unfiltered) orientation, low-passed: it
    // responds at once when a turn starts (the output's own rate lagged, so
    // the view stuck, then rushed). Noise of a few deg/s barely opens the
    // filter: the opening grows with (speed / kOpeningDegreesPerSecond)^2.
    static constexpr double kVectorTimeConstantSeconds = 0.20; // at rest
    static constexpr double kMovingTimeConstantSeconds = 0.05;
    static constexpr double kOpeningDegreesPerSecond = 40.0;
    static constexpr double kSpeedTimeConstantSeconds = 0.2;
    // Longer gaps restart the filters.
    static constexpr double kMaxGapSeconds = 0.25;
    // Magnetometer readings waiting for an accelerometer reading after them.
    static constexpr std::size_t kPendingSize = 16;
    // Magnetometer and accelerometer timestamps further apart than this are
    // on different time bases (seen on some vendor HALs): pair by arrival,
    // the magnetometer reading with the newest accelerometer reading.
    static constexpr double kClockMismatchSeconds = 0.5;

    struct Output
    {
        Quaternion pose;            // device -> ENU, magnetic north
        double seconds = 0.0;       // magnetometer timestamp
        double strength = 0.0;      // |B| of the filtered field (sensor unit: uT)
        double dipDegrees = 0.0;    // angle of the field below the horizontal
    };

    void setOutputHandler(std::function<void(const Output &)> handler)
    {
        this->m_handler = std::move(handler);
    }

    void reset()
    {
        this->m_accelCount = 0;
        this->m_pendingCount = 0;
        this->m_hasFilter = false;
        this->m_hasRaw = false;
        this->m_rate = {};
        this->m_speed = 0.0;
    }

    void addAccelerometer(double x, double y, double z, double seconds)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(seconds))
            return;
        // A clock jumping back (driver resync) starts the accelerometer over.
        if (this->m_accelCount > 0 && seconds < this->m_accel[1].seconds - kMaxGapSeconds)
        {
            this->m_accelCount = 0;
            this->m_pendingCount = 0;
        }
        if (this->m_accelCount > 0 && seconds <= this->m_accel[1].seconds)
            return;
        this->m_accel[0] = this->m_accel[1];
        this->m_accel[1] = {{x, y, z}, seconds};
        this->m_accelCount = std::min<std::size_t>(this->m_accelCount + 1, 2);
        // Magnetometer readings now covered by two accelerometer readings.
        std::size_t kept = 0;
        for (std::size_t index = 0; index < this->m_pendingCount; ++index)
        {
            const Reading &field = this->m_pending[index];
            if (field.seconds <= seconds)
                process(field);
            else
                this->m_pending[kept++] = field;
        }
        this->m_pendingCount = kept;
    }

    void addMagneticField(double x, double y, double z, double seconds)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(seconds))
            return;
        const Reading field{{x, y, z}, seconds};
        if (this->m_accelCount > 0 && seconds <= this->m_accel[1].seconds)
        {
            process(field);
            return;
        }
        if (this->m_accelCount > 0 && seconds - this->m_accel[1].seconds > kClockMismatchSeconds)
        {
            this->m_pendingCount = 0;
            this->m_arrivalPairing = true;
            process(field);
            this->m_arrivalPairing = false;
            return;
        }
        if (this->m_pendingCount == kPendingSize)
        {
            std::copy(this->m_pending.begin() + 1, this->m_pending.end(), this->m_pending.begin());
            --this->m_pendingCount;
        }
        this->m_pending[this->m_pendingCount++] = field;
    }

private:
    struct Reading
    {
        Vector3 v;
        double seconds = 0.0;
    };

    // Accelerometer at time t: interpolated between the two newest readings,
    // the older one before them.
    Vector3 accelerometerAt(double t) const
    {
        if (this->m_arrivalPairing)
            return this->m_accel[1].v;
        if (this->m_accelCount < 2 || t <= this->m_accel[0].seconds)
            return this->m_accelCount < 2 ? this->m_accel[1].v : this->m_accel[0].v;
        const double span = this->m_accel[1].seconds - this->m_accel[0].seconds;
        const double f = span > 0.0 ? std::clamp((t - this->m_accel[0].seconds) / span, 0.0, 1.0) : 1.0;
        const Vector3 &a = this->m_accel[0].v;
        const Vector3 &b = this->m_accel[1].v;
        return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
    }

    void process(const Reading &field)
    {
        if (this->m_accelCount == 0)
            return;
        const Vector3 gravity = accelerometerAt(field.seconds);
        const double dt = this->m_hasFilter ? field.seconds - this->m_filterSeconds : 0.0;
        // Speed from the raw orientation.
        Quaternion raw;
        if (AowisCompass::orientationFromGravityAndField(gravity, field.v, &raw))
        {
            if (this->m_hasRaw && dt > 0.0 && dt <= kMaxGapSeconds)
            {
                // Rate as a vector (world frame), low-passed: real motion adds
                // up, noise and tremor (changing direction) cancel.
                Quaternion d = AowisCompass::multiply(raw, {-this->m_lastRaw.x, -this->m_lastRaw.y,
                                                            -this->m_lastRaw.z, this->m_lastRaw.w});
                if (d.w < 0.0)
                    d = {-d.x, -d.y, -d.z, -d.w};
                const double s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                const double scale = s < 1e-12 ? 2.0 : 2.0 * std::atan2(s, d.w) / s;
                const double r = -std::expm1(-dt / kSpeedTimeConstantSeconds);
                this->m_rate.x += r * (d.x * scale / dt - this->m_rate.x);
                this->m_rate.y += r * (d.y * scale / dt - this->m_rate.y);
                this->m_rate.z += r * (d.z * scale / dt - this->m_rate.z);
                this->m_speed = AowisCompass::length(this->m_rate) * 180.0 / AowisCompass::kPi;
            }
            else if (dt > 0.0)
            {
                this->m_rate = {};
                this->m_speed = 0.0;
            }
            this->m_lastRaw = raw;
            this->m_hasRaw = true;
        }
        // Out of order or repeated: skip. Far back (driver resync): restart.
        if (this->m_hasFilter && dt <= 0.0 && dt >= -kMaxGapSeconds)
            return;
        if (!this->m_hasFilter || dt > kMaxGapSeconds || dt < -kMaxGapSeconds)
        {
            this->m_gravity = gravity;
            this->m_field = field.v;
            this->m_hasFilter = true;
        }
        else
        {
            const double speed = this->m_speed / kOpeningDegreesPerSecond;
            const double tau = kMovingTimeConstantSeconds +
                               (kVectorTimeConstantSeconds - kMovingTimeConstantSeconds) / (1.0 + speed * speed);
            const double a = -std::expm1(-dt / tau);
            this->m_gravity = {this->m_gravity.x + a * (gravity.x - this->m_gravity.x),
                               this->m_gravity.y + a * (gravity.y - this->m_gravity.y),
                               this->m_gravity.z + a * (gravity.z - this->m_gravity.z)};
            this->m_field = {this->m_field.x + a * (field.v.x - this->m_field.x),
                             this->m_field.y + a * (field.v.y - this->m_field.y),
                             this->m_field.z + a * (field.v.z - this->m_field.z)};
        }
        this->m_filterSeconds = field.seconds;
        Output out;
        if (!AowisCompass::orientationFromGravityAndField(this->m_gravity, this->m_field, &out.pose))
            return;
        out.seconds = field.seconds;
        out.strength = AowisCompass::length(this->m_field);
        const double g = AowisCompass::length(this->m_gravity);
        const double up = (this->m_field.x * this->m_gravity.x + this->m_field.y * this->m_gravity.y +
                           this->m_field.z * this->m_gravity.z) / (g * out.strength);
        out.dipDegrees = -std::asin(std::clamp(up, -1.0, 1.0)) * 180.0 / AowisCompass::kPi;
        if (this->m_handler)
            this->m_handler(out);
    }

    std::function<void(const Output &)> m_handler;
    std::array<Reading, 2> m_accel{};
    std::size_t m_accelCount = 0;
    std::array<Reading, kPendingSize> m_pending{};
    std::size_t m_pendingCount = 0;
    bool m_hasFilter = false;
    bool m_arrivalPairing = false;
    double m_filterSeconds = 0.0;
    Vector3 m_gravity;
    Vector3 m_field;
    bool m_hasRaw = false;
    Quaternion m_lastRaw;
    Vector3 m_rate;
    double m_speed = 0.0;
};

class LookAroundFieldSource final
{
public:
    typedef AowisCompass::Quaternion Quaternion;
    typedef AowisCompass::Vector3 Vector3;

    // Heading change is passed on with turn^2 / (turn^2 + (ratio * tilt)^2).
    static constexpr double kTiltRatio = 1.5;
    // Disturbed field: strength off by more than this fraction, or dip off
    // by more than this, from the reference.
    static constexpr double kStrengthTolerance = 0.12;
    static constexpr double kDipToleranceDegrees = 5.0;
    // The reference: the field's own average over this time.
    static constexpr double kReferenceTimeConstantSeconds = 3.0;
    static constexpr double kMaxGapSeconds = 0.25;
    // A field "disturbed" this long is the new normal (another place, or
    // a GPS seed the phone does not match): the reference starts over.
    static constexpr double kRelearnSeconds = 5.0;
    // The GPS field seeds the reference only if the phone agrees within the
    // disturbance tolerances: further off, a calibration error and a
    // disturbance cannot be told apart, and the phone's own field is used.
    static constexpr double kSeedStrengthTolerance = kStrengthTolerance;
    static constexpr double kSeedDipToleranceDegrees = kDipToleranceDegrees;

    struct Step
    {
        Quaternion motion;   // for LookAroundEngine::addGyro
        Quaternion compass;  // for LookAroundEngine::addCompass (only if trusted)
        double seconds = 0.0;
        bool trusted = true;
    };

    void reset()
    {
        this->m_hasPrevious = false;
        this->m_hasReference = false;
        this->m_trusted = true;
        this->m_untrustedSeconds = 0.0;
    }

    // Expected field at the GPS fix: strength (uT) and dip (degrees, down
    // positive). Non-finite values: use the field's own average.
    void setExpectedField(double strength, double dipDegrees)
    {
        this->m_expectedStrength = strength;
        this->m_expectedDip = dipDegrees;
    }

    // One FieldOrientation output. False for duplicates and out-of-order.
    bool add(const Quaternion &pose, double seconds, double strength, double dipDegrees, Step *step)
    {
        if (!std::isfinite(seconds))
            return false;
        // A clock jumping back (driver resync): start over instead of
        // rejecting every later reading as old.
        if (this->m_hasPrevious && seconds < this->m_previousSeconds - kMaxGapSeconds)
            this->m_hasPrevious = false;
        if (this->m_hasPrevious && seconds <= this->m_previousSeconds)
            return false;
        const Quaternion q = AowisCompass::normalized(pose);
        const double dt = this->m_hasPrevious ? seconds - this->m_previousSeconds : 0.0;
        this->m_trusted = checkField(strength, dipDegrees, dt);
        if (!this->m_hasPrevious || dt > kMaxGapSeconds)
        {
            this->m_motion = q;
        }
        else
        {
            // World-frame increment: horizontal part tilts, vertical part turns.
            const Vector3 v = rotationVector(shortest(AowisCompass::multiply(q, conjugate(this->m_previous))));
            const double tilt = std::hypot(v.x, v.y);
            const double turn = std::fabs(v.z);
            const double weighted = kTiltRatio * tilt;
            const double gate = !this->m_trusted ? 0.0
                              : turn * turn + weighted * weighted > 0.0
                              ? turn * turn / (turn * turn + weighted * weighted) : 1.0;
            this->m_motion = AowisCompass::normalized(AowisCompass::multiply(
                fromRotationVector({v.x, v.y, v.z * gate}), this->m_motion));
        }
        this->m_previous = q;
        this->m_previousSeconds = seconds;
        this->m_hasPrevious = true;
        step->motion = this->m_motion;
        step->compass = q;
        step->seconds = seconds;
        step->trusted = this->m_trusted;
        return true;
    }

    bool trusted() const { return this->m_trusted; }

    static Quaternion conjugate(const Quaternion &q) { return {-q.x, -q.y, -q.z, q.w}; }

    static Quaternion shortest(const Quaternion &q)
    {
        return q.w < 0.0 ? Quaternion{-q.x, -q.y, -q.z, -q.w} : q;
    }

    static Vector3 rotationVector(const Quaternion &q)
    {
        const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (s < 1e-12)
            return {2.0 * q.x, 2.0 * q.y, 2.0 * q.z};
        const double scale = 2.0 * std::atan2(s, q.w) / s;
        return {q.x * scale, q.y * scale, q.z * scale};
    }

    static Quaternion fromRotationVector(const Vector3 &v)
    {
        const double angle = AowisCompass::length(v);
        if (angle < 1e-12)
            return AowisCompass::normalized({0.5 * v.x, 0.5 * v.y, 0.5 * v.z, 1.0});
        const double scale = std::sin(0.5 * angle) / angle;
        return {v.x * scale, v.y * scale, v.z * scale, std::cos(0.5 * angle)};
    }

private:
    // Disturbance check against the phone's own recent field. The GPS
    // field (Android's model) only seeds that reference, and only when the
    // phone's magnetometer agrees with it: phone magnetometers are
    // often off by more than the disturbance tolerance (calibration, steel
    // in a rugged case), and an absolute check then judged every reading
    // disturbed. A field "disturbed" for kRelearnSeconds is the new normal.
    bool checkField(double strength, double dip, double dt)
    {
        if (!std::isfinite(strength) || !std::isfinite(dip) || strength <= 0.0)
            return false;
        if (!this->m_hasReference)
        {
            const bool gps = std::isfinite(this->m_expectedStrength) && this->m_expectedStrength > 0.0 &&
                             std::isfinite(this->m_expectedDip) &&
                             std::fabs(strength - this->m_expectedStrength) <= kSeedStrengthTolerance * this->m_expectedStrength &&
                             std::fabs(dip - this->m_expectedDip) <= kSeedDipToleranceDegrees;
            this->m_referenceStrength = gps ? this->m_expectedStrength : strength;
            this->m_referenceDip = gps ? this->m_expectedDip : dip;
            this->m_hasReference = true;
            this->m_untrustedSeconds = 0.0;
        }
        bool ok = std::fabs(strength - this->m_referenceStrength) <= kStrengthTolerance * this->m_referenceStrength &&
                  std::fabs(dip - this->m_referenceDip) <= kDipToleranceDegrees;
        this->m_untrustedSeconds = ok ? 0.0 : this->m_untrustedSeconds + std::max(0.0, dt);
        if (!ok && this->m_untrustedSeconds > kRelearnSeconds)
        {
            this->m_referenceStrength = strength;
            this->m_referenceDip = dip;
            this->m_untrustedSeconds = 0.0;
            ok = true;
        }
        // The reference follows only a trusted field, so a disturbance is
        // not learned as normal (except by the relearn above).
        if (ok && dt > 0.0)
        {
            const double a = -std::expm1(-std::min(dt, kMaxGapSeconds) / kReferenceTimeConstantSeconds);
            this->m_referenceStrength += a * (strength - this->m_referenceStrength);
            this->m_referenceDip += a * (dip - this->m_referenceDip);
        }
        return ok;
    }

    bool m_hasPrevious = false;
    Quaternion m_previous;
    double m_previousSeconds = 0.0;
    Quaternion m_motion;
    bool m_trusted = true;
    double m_expectedStrength = std::nan("");
    double m_expectedDip = std::nan("");
    bool m_hasReference = false;
    double m_referenceStrength = 0.0;
    double m_referenceDip = 0.0;
    double m_untrustedSeconds = 0.0;
};
}
