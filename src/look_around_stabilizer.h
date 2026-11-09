#pragma once

#include "compass_fusion.h"
#include "compass_heading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Look-around camera pose from the phone's orientation sensors. Qt-free, so
// the math is covered by hardware-free tests (tests/look_around_stabilizer_tests.cpp).
//
// Smoothness and precision are separated instead of traded against each other
// in one filter:
//
// 1. Motion first, compass second. Where the phone has Android's game
//    rotation vector (gyroscope + accelerometer), every one of its samples
//    advances the view at its own rate. The OS rotation vector (with
//    magnetometer) never paces the view; its samples only adjust a heading
//    offset about Up, with a time constant of seconds. Magnetometer noise and
//    recalibration jumps do not move the view, the mean heading is still the
//    compass heading, and a slow or stalled compass does not slow it down.
//    Each compass sample is compared with the gyroscope pose interpolated to
//    the compass sample's own timestamp, so the few ms between the sensors
//    do not read as a heading offset during a turn. The alignment also slows
//    while turning, against latency inside the OS fusion.
//    Without a game rotation vector the compass pose drives the view.
//
// 2. Speed-adaptive smoothing ("One Euro" on rotations, as for the compass
//    dial). The cut-off rises with the low-passed angular velocity. At rest,
//    tremor is averaged while the estimate still converges on the exact pose:
//    there is no dead band and no persistent offset. In a turn the cut-off
//    opens and the lag stays small. The rest cut-off falls with magnification,
//    because the lens magnifies tremor on screen. The angular velocity is
//    low-passed as a vector, so oscillating tremor cancels instead of being
//    mistaken for a turn.
//
// 3. Roll is smoothed more strongly than pointing. A rotation about the view
//    axis (device Z) does not move the point under the crosshair, so heavier
//    roll smoothing calms the picture at no cost in pointing precision.
//
// Timing uses the sensor timestamps, not GUI-thread delivery time: sensor
// events arrive on the GUI thread in batches.
//
// Frames follow Android: device X right, Y top edge, Z out of the screen; the
// camera looks along -Z. Quaternions map device to world (East, North, Up).
namespace AowisMobileMap
{
struct LookAroundSample
{
    // Latest compass pose: device -> ENU, magnetic north. May repeat; only a
    // newer absoluteSeconds counts as a new compass reading.
    AowisCompass::Quaternion absolute;
    double absoluteSeconds = 0.0;
    // Gyroscope pose: device -> gyroscope frame (Z up, arbitrary heading).
    // A newer relativeSeconds advances the view.
    AowisCompass::Quaternion relative;
    double relativeSeconds = 0.0;
    bool hasRelative = false;
    double magnification = 1.0;
};

class LookAroundStabilizer final
{
public:
    // Heading offset between gyroscope frame and compass. Several seconds:
    // magnetometer noise around 1 Hz is attenuated more than tenfold, while
    // gyroscope drift (a fraction of a degree per minute) leaves no visible
    // error.
    static constexpr double kHeadingTimeConstantSeconds = 2.5;
    // A larger disagreement means the gyroscope frame was reset: snap.
    static constexpr double kHeadingSnapRadians = 25.0 * AowisCompass::kPi / 180.0;
    // A gyroscope pose jumping this far between two samples is a frame reset.
    static constexpr double kRelativeJumpRadians = 20.0 * AowisCompass::kPi / 180.0;
    // Compass samples older than the gyroscope history by more than this
    // are dropped.
    static constexpr double kHistoryToleranceSeconds = 0.02;
    // Longest compass interval credited to one heading correction.
    static constexpr double kMaxAlignmentStepSeconds = 0.25;
    // Longer sensor gaps restart the stream: no smoothing across a pause.
    static constexpr double kMaxGapSeconds = 0.25;
    // Low-pass of the angular velocity that opens the filter.
    static constexpr double kRateCutoffHz = 1.0;
    // Rest cut-off at 1x; divided by the magnification and clamped.
    static constexpr double kRestCutoffHzAt1x = 4.0;
    static constexpr double kMinRestCutoffHz = 0.4;
    static constexpr double kMaxRestCutoffHz = 8.0;
    // Cut-off increase per degree per second of on-screen angular speed
    // (angular speed times magnification).
    static constexpr double kSpeedGainHz = 0.25;
    static constexpr double kMaxCutoffHz = 30.0;
    // Roll cut-off relative to the pointing cut-off.
    static constexpr double kRollCutoffRatio = 0.35;
    // Frame prediction fades in between 2 and 4 deg/s. Hand tremor alone
    // reaches a few deg/s; a lead that followed it would add jerk.
    static constexpr double kPredictionStartDegreesPerSecond = 2.0;
    static constexpr double kPredictionFadeDegreesPerSecond = 2.0;
    // Heading alignment slows to half at this turn rate, a fifth at twice it.
    static constexpr double kAlignmentHoldDegreesPerSecond = 10.0;
    // Gyroscope history for interpolation at compass timestamps (0.16 s at
    // 400 Hz, 0.64 s at 100 Hz).
    static constexpr std::size_t kHistorySize = 64;

    void reset()
    {
        this->m_hasState = false;
        this->m_hasHeadingOffset = false;
        this->m_headingOffset = 0.0;
        this->m_alignmentSeconds = 0.0;
        this->m_rate = {};
        this->m_usedRelative = false;
        this->m_predictionRate = {};
        this->m_hasAbsolute = false;
        this->m_absoluteSeconds = 0.0;
        this->m_hasPendingAbsolute = false;
        clearHistory();
    }

    AowisCompass::Quaternion update(const LookAroundSample &sample)
    {
        const bool relativeSource = sample.hasRelative;
        ingestAbsolute(sample);
        const double motionSeconds = relativeSource ? sample.relativeSeconds : sample.absoluteSeconds;
        const double elapsed = this->m_hasState ? motionSeconds - this->m_seconds : 0.0;
        const bool switched = this->m_hasState && relativeSource != this->m_usedRelative;
        if (this->m_hasState && !switched && elapsed <= 0.0)
        {
            // A compass-only update or a duplicate: no motion step.
            if (relativeSource)
                resolvePendingAbsolute();
            return this->m_filtered;
        }
        const bool continuous = this->m_hasState && elapsed > 0.0 && elapsed <= kMaxGapSeconds;
        const bool timestampEdge = switched && elapsed <= 0.0 && elapsed > -kMaxGapSeconds;
        if (!continuous || switched)
        {
            this->m_hasHeadingOffset = false;
            clearHistory();
        }
        AowisCompass::Quaternion pose = AowisCompass::normalized(sample.absolute);
        if (relativeSource)
        {
            const AowisCompass::Quaternion relative = AowisCompass::normalized(sample.relative);
            if (this->m_historyCount > 0)
            {
                const AowisCompass::Vector3 jump = rotationVector(shortest(AowisCompass::multiply(
                    conjugate(newestRelative().pose), relative)));
                if (AowisCompass::length(jump) > kRelativeJumpRadians)
                {
                    // The OS reset the gyroscope frame: realign from scratch.
                    this->m_hasHeadingOffset = false;
                    clearHistory();
                }
            }
            pushRelative(relative, sample.relativeSeconds);
            resolvePendingAbsolute();
            if (!this->m_hasHeadingOffset && this->m_hasAbsolute)
                snapHeadingOffset(this->m_absolute, relative);
            pose = composeWithOffset(relative);
        }
        this->m_usedRelative = relativeSource;
        this->m_seconds = motionSeconds;
        if (timestampEdge)
        {
            // Source change whose timestamp (now from the other sensor) is a
            // few ms earlier: hold this one sample rather than restarting.
            this->m_previous = pose;
            this->m_predictionRate = {};
            return this->m_filtered;
        }
        if (!continuous)
        {
            this->m_hasState = true;
            this->m_filtered = pose;
            this->m_previous = pose;
            this->m_predictionRate = {};
            this->m_rate = {};
            return pose;
        }
        // The gyroscope pose was dropped or is available again: the filter
        // absorbs the step instead of showing the raw sample, and no motion
        // (hence no prediction) is read into it.
        if (switched)
            this->m_previous = pose;

        // Angular velocity in device coordinates, low-passed as a vector.
        const AowisCompass::Vector3 step = rotationVector(
            shortest(AowisCompass::multiply(conjugate(this->m_previous), pose)));
        // Predict only the part of the latest motion that agrees with the
        // sustained turn. A stop or reversal immediately removes the lead.
        this->m_predictionRate = {};
        const double sustained = AowisCompass::length(this->m_rate);
        constexpr double minimumRate = kPredictionStartDegreesPerSecond * AowisCompass::kPi / 180.0;
        if (sustained > minimumRate && AowisCompass::length(step) < 0.2)
        {
            const double along = (step.x * this->m_rate.x + step.y * this->m_rate.y +
                                  step.z * this->m_rate.z) / (elapsed * sustained);
            const double speed = std::clamp(std::min(along, sustained), 0.0, AowisCompass::kPi);
            const double sustainedDegrees = sustained * 180.0 / AowisCompass::kPi;
            const double fade = std::clamp((sustainedDegrees - kPredictionStartDegreesPerSecond) /
                                           kPredictionFadeDegreesPerSecond, 0.0, 1.0);
            const double scale = fade * speed / sustained;
            this->m_predictionRate = {this->m_rate.x * scale, this->m_rate.y * scale,
                                      this->m_rate.z * scale * kRollCutoffRatio};
        }
        const double rateAlpha = smoothingFactor(kRateCutoffHz, elapsed);
        this->m_rate.x += (step.x / elapsed - this->m_rate.x) * rateAlpha;
        this->m_rate.y += (step.y / elapsed - this->m_rate.y) * rateAlpha;
        this->m_rate.z += (step.z / elapsed - this->m_rate.z) * rateAlpha;
        this->m_previous = pose;

        const double cutoff = cutoffHz(AowisCompass::length(this->m_rate) * 180.0 / AowisCompass::kPi,
                                       sample.magnification);
        const double pointAlpha = smoothingFactor(cutoff, elapsed);
        const double rollAlpha = smoothingFactor(cutoff * kRollCutoffRatio, elapsed);

        // Split the remaining error into twist about the view axis (roll)
        // and swing of the view axis (pointing): error = swing * twist.
        const AowisCompass::Quaternion error =
            shortest(AowisCompass::multiply(conjugate(this->m_filtered), pose));
        AowisCompass::Quaternion twist = AowisCompass::normalized({0.0, 0.0, error.z, error.w});
        if (std::hypot(error.z, error.w) < 1e-9)
            twist = {};
        const AowisCompass::Quaternion swing = AowisCompass::multiply(error, conjugate(twist));
        const AowisCompass::Quaternion correction =
            AowisCompass::multiply(power(swing, pointAlpha), power(twist, rollAlpha));
        this->m_filtered = AowisCompass::normalized(AowisCompass::multiply(this->m_filtered, correction));
        return this->m_filtered;
    }

    bool hasPose() const { return this->m_hasState; }

    // Read-only frame estimate. Sensor fusion always starts the next update
    // from the measured filter state, regardless of display refresh rate.
    AowisCompass::Quaternion poseForFrame(double nowSeconds) const
    {
        const double age = nowSeconds - this->m_seconds;
        if (!this->m_hasState || !std::isfinite(age) || age < 0.0 || age >= 0.050)
            return this->m_filtered;
        // Allow 8 ms for rendering and display. Cap total extrapolation at
        // 20 ms and fade it out for sensor stalls between 20 and 50 ms.
        const double lead = std::min(age + 0.008, 0.020) *
                            std::min(1.0, (0.050 - age) / 0.030);
        const AowisCompass::Quaternion advance = fromRotationVector({
            this->m_predictionRate.x * lead, this->m_predictionRate.y * lead,
            this->m_predictionRate.z * lead});
        return AowisCompass::normalized(AowisCompass::multiply(this->m_filtered, advance));
    }

    // Exposed for tests and diagnostics.
    double headingOffsetRadians() const { return this->m_headingOffset; }
    bool usesRelativePose() const { return this->m_usedRelative; }
    double motionSeconds() const { return this->m_seconds; }

    static double cutoffHz(double angularSpeedDegreesPerSecond, double magnification)
    {
        const double lens = std::isfinite(magnification) && magnification > 0.0 ? magnification : 1.0;
        const double rest = std::clamp(kRestCutoffHzAt1x / lens, kMinRestCutoffHz, kMaxRestCutoffHz);
        const double screenSpeed = std::max(0.0, angularSpeedDegreesPerSecond) * std::max(1.0, lens);
        return std::min(kMaxCutoffHz, rest + kSpeedGainHz * screenSpeed);
    }

    static AowisCompass::Quaternion conjugate(const AowisCompass::Quaternion &q)
    {
        return {-q.x, -q.y, -q.z, q.w};
    }

    static AowisCompass::Quaternion shortest(const AowisCompass::Quaternion &q)
    {
        return q.w < 0.0 ? AowisCompass::Quaternion{-q.x, -q.y, -q.z, -q.w} : q;
    }

    // Axis times angle (radians) of a unit quaternion with w >= 0.
    static AowisCompass::Vector3 rotationVector(const AowisCompass::Quaternion &q)
    {
        const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (s < 1e-12)
            return {2.0 * q.x, 2.0 * q.y, 2.0 * q.z};
        const double scale = 2.0 * std::atan2(s, q.w) / s;
        return {q.x * scale, q.y * scale, q.z * scale};
    }

    static AowisCompass::Quaternion fromRotationVector(const AowisCompass::Vector3 &v)
    {
        const double angle = AowisCompass::length(v);
        if (angle < 1e-12)
            return AowisCompass::normalized({0.5 * v.x, 0.5 * v.y, 0.5 * v.z, 1.0});
        const double scale = std::sin(0.5 * angle) / angle;
        return {v.x * scale, v.y * scale, v.z * scale, std::cos(0.5 * angle)};
    }

    // Fraction t of the rotation q (shortest way).
    static AowisCompass::Quaternion power(const AowisCompass::Quaternion &q, double t)
    {
        const AowisCompass::Vector3 v = rotationVector(shortest(q));
        return fromRotationVector({v.x * t, v.y * t, v.z * t});
    }

    // Rotation of a device-to-ENU pose about Up, in radians.
    static double headingAbout(const AowisCompass::Quaternion &q)
    {
        return 2.0 * std::atan2(q.z, q.w);
    }

private:
    static double smoothingFactor(double cutoffHz, double elapsed)
    {
        return -std::expm1(-2.0 * AowisCompass::kPi * cutoffHz * elapsed);
    }


    struct RelativeEntry
    {
        AowisCompass::Quaternion pose;
        double seconds = 0.0;
    };

    void clearHistory()
    {
        this->m_historyCount = 0;
        this->m_historyNext = 0;
    }

    // index 0: newest
    const RelativeEntry &relativeAt(std::size_t index) const
    {
        return this->m_history[(this->m_historyNext + kHistorySize - 1 - index) % kHistorySize];
    }

    const RelativeEntry &newestRelative() const
    {
        return relativeAt(0);
    }

    void pushRelative(const AowisCompass::Quaternion &pose, double seconds)
    {
        this->m_history[this->m_historyNext].pose = pose;
        this->m_history[this->m_historyNext].seconds = seconds;
        this->m_historyNext = (this->m_historyNext + 1) % kHistorySize;
        this->m_historyCount = std::min(this->m_historyCount + 1, kHistorySize);
    }

    void ingestAbsolute(const LookAroundSample &sample)
    {
        if (!std::isfinite(sample.absoluteSeconds) || sample.absoluteSeconds <= 0.0 ||
            (this->m_hasAbsolute && sample.absoluteSeconds <= this->m_absoluteSeconds))
            return;
        this->m_absolute = AowisCompass::normalized(sample.absolute);
        this->m_absoluteSeconds = sample.absoluteSeconds;
        this->m_hasAbsolute = true;
        this->m_hasPendingAbsolute = true;
    }

    // Gyroscope pose at time t from the history; false if t is not covered.
    bool relativeAtTime(double t, AowisCompass::Quaternion *pose) const
    {
        if (this->m_historyCount == 0 || t > newestRelative().seconds)
            return false;
        for (std::size_t index = 1; index < this->m_historyCount; ++index)
        {
            const RelativeEntry &earlier = relativeAt(index);
            if (earlier.seconds > t)
                continue;
            const RelativeEntry &later = relativeAt(index - 1);
            const double span = later.seconds - earlier.seconds;
            const double fraction = span > 0.0 ? (t - earlier.seconds) / span : 1.0;
            *pose = AowisCompass::normalized(AowisCompass::multiply(earlier.pose, power(
                AowisCompass::multiply(conjugate(earlier.pose), later.pose), fraction)));
            return true;
        }
        const RelativeEntry &oldest = relativeAt(this->m_historyCount - 1);
        if (t < oldest.seconds - kHistoryToleranceSeconds)
            return false;
        *pose = oldest.pose;
        return true;
    }

    void resolvePendingAbsolute()
    {
        if (!this->m_hasPendingAbsolute || this->m_historyCount == 0)
            return;
        AowisCompass::Quaternion relative;
        if (!relativeAtTime(this->m_absoluteSeconds, &relative))
        {
            // Not yet covered: wait for the gyroscope to pass it. Older than
            // the history: drop it.
            if (this->m_absoluteSeconds <= newestRelative().seconds)
                this->m_hasPendingAbsolute = false;
            return;
        }
        this->m_hasPendingAbsolute = false;
        const double measured = measuredOffset(this->m_absolute, relative);
        const double difference = std::remainder(measured - this->m_headingOffset, 2.0 * AowisCompass::kPi);
        const double step = this->m_absoluteSeconds - this->m_alignmentSeconds;
        if (!this->m_hasHeadingOffset || std::fabs(difference) > kHeadingSnapRadians)
        {
            snapHeadingOffset(this->m_absolute, relative);
            this->m_alignmentSeconds = this->m_absoluteSeconds;
            return;
        }
        if (step > 0.0)
        {
            const double turn = AowisCompass::length(this->m_rate) * 180.0 / AowisCompass::kPi
                                / kAlignmentHoldDegreesPerSecond;
            const double weight = -std::expm1(-std::min(step, kMaxAlignmentStepSeconds) /
                                              kHeadingTimeConstantSeconds);
            this->m_headingOffset = std::remainder(
                this->m_headingOffset + difference * weight / (1.0 + turn * turn), 2.0 * AowisCompass::kPi);
        }
        this->m_alignmentSeconds = this->m_absoluteSeconds;
    }

    // absolute ~= Rz(offset) * relative; keep only the rotation about Up.
    static double measuredOffset(const AowisCompass::Quaternion &absolute,
                                 const AowisCompass::Quaternion &relative)
    {
        return headingAbout(shortest(AowisCompass::multiply(absolute, conjugate(relative))));
    }

    void snapHeadingOffset(const AowisCompass::Quaternion &absolute, const AowisCompass::Quaternion &relative)
    {
        this->m_headingOffset = measuredOffset(absolute, relative);
        this->m_hasHeadingOffset = true;
        this->m_alignmentSeconds = this->m_absoluteSeconds;
    }

    AowisCompass::Quaternion composeWithOffset(const AowisCompass::Quaternion &relative) const
    {
        const AowisCompass::Quaternion aboutUp{0.0, 0.0, std::sin(0.5 * this->m_headingOffset),
                                               std::cos(0.5 * this->m_headingOffset)};
        return AowisCompass::normalized(AowisCompass::multiply(aboutUp, relative));
    }

    bool m_hasState = false;
    bool m_usedRelative = false;
    double m_seconds = 0.0;
    AowisCompass::Quaternion m_filtered;
    AowisCompass::Quaternion m_previous;
    AowisCompass::Vector3 m_rate;
    AowisCompass::Vector3 m_predictionRate;

    bool m_hasHeadingOffset = false;
    double m_headingOffset = 0.0;
    double m_alignmentSeconds = 0.0;
    bool m_hasAbsolute = false;
    bool m_hasPendingAbsolute = false;
    AowisCompass::Quaternion m_absolute;
    double m_absoluteSeconds = 0.0;
    std::array<RelativeEntry, kHistorySize> m_history{};
    std::size_t m_historyCount = 0;
    std::size_t m_historyNext = 0;
};
}
