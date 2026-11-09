#pragma once

#include "compass_fusion.h"
#include "compass_heading.h"

#include <algorithm>
#include <cmath>

// Direct look-around engine. Qt-free; tested in tests/look_around_direct_tests.cpp.
//
// One sensor, newest sample, no buffering:
//
// * The view follows ONE input: the OS rotation vector (device -> ENU,
//   magnetic north), the fused orientation a sky app shows. No second
//   gyroscope stream, no heading offset between two sensors, no hand-over,
//   no alignment: nothing in the engine treats the heading differently from
//   the tilt.
// * The newest sample is the target. Sensor timestamps only order the
//   samples; how the samples are batched or delayed changes nothing in the
//   engine: there is no delay buffer, no interpolation in sensor time and no
//   timeout.
// * The filter runs once per displayed frame, over the frame interval, toward
//   the target. It only takes the hand shake out: a speed-adaptive ("One
//   Euro") low-pass on rotations. At rest the cut-off is low (tremor at
//   8-12 Hz is damped several-fold), and it rises with the on-screen angular
//   speed, so a turn follows within a fraction of a degree. The angular speed
//   is low-passed as a vector, so oscillating tremor cancels instead of
//   opening the filter. The rest cut-off falls with magnification. Roll
//   (about the view axis) is smoothed more; it never moves the crosshair.
// * Every axis is filtered identically: sideways equals up/down.
//
// Frames follow Android: device X right, Y top edge, Z out of the screen; the
// camera looks along -Z. Poses map device to world (East, North, Up).
namespace AowisMobileMap
{
class LookAroundDirectEngine final
{
public:
    typedef AowisCompass::Quaternion Quaternion;
    typedef AowisCompass::Vector3 Vector3;

    // Cut-off at rest at 1x; divided by the magnification, clamped.
    static constexpr double kRestCutoffHzAt1x = 2.0;
    static constexpr double kMinRestCutoffHz = 0.3;
    // Cut-off increase per degree per second of on-screen angular speed
    // (angular speed times magnification), and its ceiling.
    static constexpr double kSpeedGainHz = 0.2;
    static constexpr double kMaxCutoffHz = 25.0;
    // Low-pass of the angular velocity that opens the filter.
    static constexpr double kRateCutoffHz = 1.0;
    // Roll cut-off relative to the pointing cut-off.
    static constexpr double kRollCutoffRatio = 0.35;
    // Longest frame interval credited to one filter step (a stalled frame
    // does not throw the filter onto the target).
    static constexpr double kMaxFrameSeconds = 0.1;
    // Closer than this to a resting target: show the target exactly, so
    // frames stop once the view has settled.
    static constexpr double kSettleRadians = 1e-5;

    void reset()
    {
        this->m_hasTarget = false;
        this->m_hasFiltered = false;
        this->m_rate = {};
        this->m_samples = 0;
        this->m_samplesAtFrame = 0;
    }

    void setMagnification(double magnification)
    {
        this->m_magnification = std::isfinite(magnification) && magnification > 0.0 ? magnification : 1.0;
    }

    // OS rotation vector sample at sensor time 'seconds', delivered at
    // 'arrival' (the clock of frames). Older or repeated samples are ignored.
    void addPose(const Quaternion &pose, double seconds, double arrival)
    {
        if (!std::isfinite(seconds) || !std::isfinite(arrival))
            return;
        if (this->m_hasTarget && seconds <= this->m_targetSeconds)
            return;
        const double norm = std::sqrt(pose.x * pose.x + pose.y * pose.y + pose.z * pose.z + pose.w * pose.w);
        if (!std::isfinite(norm) || norm < 0.5)
            return;
        this->m_target = AowisCompass::normalized(pose);
        this->m_targetSeconds = seconds;
        this->m_targetArrival = arrival;
        this->m_hasTarget = true;
        ++this->m_samples;
    }

    bool hasPose() const { return this->m_hasTarget; }

    // Pose to show for a frame at 'now' (the clock of 'arrival').
    Quaternion poseForFrame(double now)
    {
        if (!this->m_hasTarget)
            return {};
        this->m_samplesAtFrame = this->m_samples;
        if (!this->m_hasFiltered || !std::isfinite(now))
        {
            this->m_filtered = this->m_target;
            this->m_previousTarget = this->m_target;
            this->m_rate = {};
            this->m_lastFrame = now;
            this->m_hasFiltered = std::isfinite(now);
            return this->m_filtered;
        }
        const double dt = std::min(now - this->m_lastFrame, kMaxFrameSeconds);
        if (!(dt > 0.0))
            return this->m_filtered;
        this->m_lastFrame = now;

        // Angular velocity of the target, low-passed as a vector.
        const Vector3 motion = rotationVector(shortest(
            AowisCompass::multiply(conjugate(this->m_previousTarget), this->m_target)));
        this->m_previousTarget = this->m_target;
        const double rateAlpha = smoothingFactor(kRateCutoffHz, dt);
        this->m_rate.x += (motion.x / dt - this->m_rate.x) * rateAlpha;
        this->m_rate.y += (motion.y / dt - this->m_rate.y) * rateAlpha;
        this->m_rate.z += (motion.z / dt - this->m_rate.z) * rateAlpha;

        this->m_cutoff = cutoffHz(AowisCompass::length(this->m_rate) * 180.0 / AowisCompass::kPi,
                                  this->m_magnification);
        const double pointAlpha = smoothingFactor(this->m_cutoff, dt);
        const double rollAlpha = smoothingFactor(this->m_cutoff * kRollCutoffRatio, dt);

        // Remaining error, split into twist about the view axis (roll) and
        // swing of the view axis (pointing): error = swing * twist.
        const Quaternion error = shortest(AowisCompass::multiply(conjugate(this->m_filtered), this->m_target));
        if (AowisCompass::length(rotationVector(error)) < kSettleRadians &&
            AowisCompass::length(this->m_rate) < kSettleRadians)
        {
            this->m_filtered = this->m_target;
            return this->m_filtered;
        }
        Quaternion twist = AowisCompass::normalized({0.0, 0.0, error.z, error.w});
        if (std::hypot(error.z, error.w) < 1e-9)
            twist = {};
        const Quaternion swing = AowisCompass::multiply(error, conjugate(twist));
        this->m_filtered = AowisCompass::normalized(AowisCompass::multiply(this->m_filtered,
            AowisCompass::multiply(power(swing, pointAlpha), power(twist, rollAlpha))));
        return this->m_filtered;
    }

    // Diagnostics and tests.
    Quaternion targetPose() const { return this->m_target; }
    // Time from the newest sample's arrival to 'now'.
    double sampleAgeSeconds(double now) const { return this->m_hasTarget ? now - this->m_targetArrival : 0.0; }
    double cutoffHzNow() const { return this->m_cutoff; }
    double angularSpeedDegrees() const { return AowisCompass::length(this->m_rate) * 180.0 / AowisCompass::kPi; }
    // Samples received up to the last frame (count per frame = difference).
    long long samplesAtFrame() const { return this->m_samplesAtFrame; }

    static double cutoffHz(double angularSpeedDegreesPerSecond, double magnification)
    {
        const double lens = std::isfinite(magnification) && magnification > 0.0 ? magnification : 1.0;
        const double rest = std::max(kMinRestCutoffHz, kRestCutoffHzAt1x / lens);
        const double screenSpeed = std::max(0.0, angularSpeedDegreesPerSecond) * std::max(1.0, lens);
        return std::min(kMaxCutoffHz, rest + kSpeedGainHz * screenSpeed);
    }

    static Quaternion conjugate(const Quaternion &q)
    {
        return {-q.x, -q.y, -q.z, q.w};
    }

    static Quaternion shortest(const Quaternion &q)
    {
        return q.w < 0.0 ? Quaternion{-q.x, -q.y, -q.z, -q.w} : q;
    }

    // Axis times angle (radians) of a unit quaternion with w >= 0.
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

    // Fraction t of the rotation q (shortest way).
    static Quaternion power(const Quaternion &q, double t)
    {
        const Vector3 v = rotationVector(shortest(q));
        return fromRotationVector({v.x * t, v.y * t, v.z * t});
    }

private:
    static double smoothingFactor(double cutoffHz, double elapsed)
    {
        return -std::expm1(-2.0 * AowisCompass::kPi * cutoffHz * elapsed);
    }

    double m_magnification = 1.0;
    bool m_hasTarget = false;
    bool m_hasFiltered = false;
    Quaternion m_target;
    double m_targetSeconds = 0.0;
    double m_targetArrival = 0.0;
    Quaternion m_previousTarget;
    Quaternion m_filtered;
    Vector3 m_rate;
    double m_lastFrame = 0.0;
    double m_cutoff = 0.0;
    long long m_samples = 0;
    long long m_samplesAtFrame = 0;
};
}
