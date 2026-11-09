#pragma once

#include "compass_fusion.h"
#include "compass_heading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Look-around motion engine. Qt-free, so it is covered by hardware-free
// tests (tests/look_around_engine_tests.cpp).
//
// The engine is independent of every input and output rate:
//
// * Inputs keep their sensor timestamps: the gyroscope pose (Android game
//   rotation vector) and the compass pose (OS rotation vector), at whatever
//   rate and in whatever batches they arrive.
// * The core advances in fixed 2 ms steps of sensor time, lazily: whenever
//   input arrives it runs every step up to the newest gyroscope timestamp.
//   No timer, no thread; the same inputs always give the same motion, and the
//   smoothing behaves identically at 100 Hz and 400 Hz sensor rates.
// * Each frame asks for the pose at its own time. The core history is
//   interpolated at a moment shortly in the past, so frames advance evenly
//   however the samples were batched, and a forward lead along the smoothed
//   trajectory compensates part of that delay. The core runs ahead of the
//   shown moment, so a stop is seen before it reaches the screen and the lead
//   eases out instead of overshooting.
//
// Heading from the compass while turning, tilt from the gyroscope (default).
// Each compass reading is compared with the gyroscope pose interpolated to the
// reading's own timestamp; the rotation about Up between them (the heading
// offset) is kept per reading and interpolated in sensor time like the poses.
// The view is the gyroscope pose turned by that offset: its tilt (up/down,
// roll) is the gyroscope's. The offset follows the compass with a time
// constant set by the gyroscope's turn rate about Up: quickly while turning
// sideways (the heading is then the compass's), slowly while tilting or at
// rest, where the compass heading wobbles (the hand's acceleration disturbs
// its gravity estimate) and the gyroscope alone moves the view. No slow alignment, no creep; a hand-over to
// the compass and back does not change the heading at all. Every compass
// reading the gyroscope history covers is used, however the samples are
// batched. (Phones whose game rotation vector turns sideways worse than
// their rotation vector, or not at all in step with it, show the latter's
// heading.) setHeadingFromCompass(false) restores the mode below.
//
// Motion first, compass second (setHeadingFromCompass(false)). Gyroscope samples move the view. Compass
// readings only steer a heading offset about Up: each reading is compared with
// the gyroscope pose interpolated to its own timestamp, low-passed over
// seconds, and the applied offset follows at a limited rate that rides on the
// user's own motion (a few percent of the turn rate), so north is fixed while
// panning rather than by a visible creep at rest. Without a gyroscope pose the
// compass pose drives the view directly.
//
// Heading noise. On phones without a gyroscope the rotation about Up can only
// come from the magnetometer, also inside a "game" rotation vector the vendor
// emulates; tilt still comes cleanly from gravity. Such a heading jitters by
// tenths of a degree to degrees from sample to sample. The engine measures
// that noise on the driving input (second differences of the heading,
// minus a floor that real gyroscopes and hand acceleration stay below) and,
// only when it is present, passes the heading through a constant-velocity
// (alpha-beta) tracker whose gains follow the measured noise: the jitter is
// averaged out, while a steady turn is followed without lag. Tilt is never
// touched. With a real gyroscope the tracker is transparent.
//
// The core smoothing is a speed-adaptive ("One Euro") filter on rotations:
// at rest tremor is averaged and the estimate converges on the exact pose (no
// dead band); while turning the cut-off opens. The rest cut-off falls with
// magnification. Roll (about the view axis, device Z) is smoothed more
// strongly; it does not move the point under the crosshair.
//
// Frames follow Android: device X right, Y top edge, Z out of the screen; the
// camera looks along -Z. Poses map device to world (East, North, Up).
namespace AowisMobileMap
{
class LookAroundEngine final
{
public:
    typedef AowisCompass::Quaternion Quaternion;
    typedef AowisCompass::Vector3 Vector3;

    // --- Core ----------------------------------------------------------
    static constexpr double kStepSeconds = 0.002;
    // Longer input gaps restart the motion: no smoothing across a pause.
    static constexpr double kMaxGapSeconds = 0.25;
    // The compass drives the view once the gyroscope is this far behind it.
    // Some phones deliver the game rotation vector in bursts longer than
    // this; the compass then covers each gap, and the hand-back realigns north
    // between readings of the same sensor time (see addGyro()).
    static constexpr double kGyroTimeoutSeconds = 0.1;
    // A gyroscope pose jumping this far between two samples is a reset of its
    // frame by the OS; the heading offset is carried across it.
    static constexpr double kGyroJumpRadians = 20.0 * AowisCompass::kPi / 180.0;

    // --- Smoothing -----------------------------------------------------
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

    // --- Compass north -------------------------------------------------
    // Low-pass of the measured offset: magnetometer noise around 1 Hz is
    // attenuated more than tenfold.
    static constexpr double kHeadingTimeConstantSeconds = 2.5;
    // Longest compass interval credited to one heading update.
    static constexpr double kMaxAlignmentStepSeconds = 0.25;
    // Applied offset rate: at rest (1x, divided by the magnification) and in
    // proportion to the turn rate, where a correction is not noticed.
    static constexpr double kRestCorrectionDegreesPerSecond = 0.3;
    static constexpr double kMotionCorrectionFraction = 0.05;
    // Compass readings older than the gyroscope history by more than this
    // are dropped.
    static constexpr double kHistoryToleranceSeconds = 0.02;
    // Heading offset time constant: at rest or while only tilting, and the
    // floor reached while turning sideways fast. In between it falls with
    // (turn rate / kHeadingTurnDegreesPerSecond)^2.
    static constexpr double kHeadingRestSeconds = 1.0;
    static constexpr double kHeadingTurnSeconds = 0.04;
    static constexpr double kHeadingTurnDegreesPerSecond = 8.0;
    // Tilting (up/down, roll) holds north: the time constant is stretched by
    // 1 + (tilt rate / this)^2 / (1 + (turn rate / kHeadingTurn...)^2), so a
    // pure tilt freezes it while a diagonal move still follows the compass.
    static constexpr double kHeadingTiltDegreesPerSecond = 4.0;
    // Window over which the gyroscope turn rate about Up is measured.
    static constexpr double kTurnRateWindowSeconds = 0.05;

    // --- Heading noise -------------------------------------------------
    // Time constant of the heading noise estimate.
    static constexpr double kNoiseTimeConstantSeconds = 1.0;
    // Sample-to-sample heading noise below this is left alone: real
    // gyroscopes (hundredths of a degree) and the hand's own acceleration at
    // 100 Hz stay below it.
    static constexpr double kNoiseFloorDegrees = 0.05;
    // Process noise of the tracker: angular acceleration of a hand turning
    // the phone. Higher follows starts and stops faster, but averages less.
    static constexpr double kHandAccelerationDegreesPerSecond2 = 200.0;
    // A tracker further off than this (a wild sensor) restarts on the input.
    static constexpr double kMaxHeadingCorrectionDegrees = 8.0;

    // --- Display -------------------------------------------------------
    // Rendering plus scan-out after the frame time.
    static constexpr double kDisplayLatencySeconds = 0.008;
    static constexpr double kLatencyWindowSeconds = 0.5;
    // A changing delay changes the displayed speed (2 % at 20 ms/s).
    static constexpr double kDelayRisePerSecond = 0.02;
    static constexpr double kDelayFallPerSecond = 0.005;
    static constexpr double kMaxDelaySeconds = 0.08;
    static constexpr double kVelocityWindowSeconds = 0.025;
    // Past the newest core state (a latency spike), extrapolate briefly.
    static constexpr double kMaxExtrapolationSeconds = 0.03;
    // Lead fades in between 2 and 4 deg/s: tremor alone reaches a few deg/s.
    static constexpr double kLeadStartDegreesPerSecond = 2.0;
    static constexpr double kLeadFadeDegreesPerSecond = 2.0;
    // Share of the display delay compensated by the lead. 1.0 removes the
    // lag of a steady pan but overshoots a stop by up to the lead; 0.6 halves
    // the lag and keeps a natural stop's overshoot to a few hundredths of a
    // degree at slow pans (simulated, tests/look_around_engine_tests.cpp).
    static constexpr double kDefaultLeadFraction = 0.6;
    // The lead's stop response is eased: a stop arrives in sensor batches.
    static constexpr double kLeadEaseSeconds = 0.04;

    static constexpr std::size_t kInputHistorySize = 64;
    static constexpr std::size_t kOutputHistorySize = 128;

    // true (default): heading from the compass at every reading, tilt from
    // the gyroscope. false: gyroscope motion, compass north low-passed and
    // applied slowly.
    void setHeadingFromCompass(bool enabled)
    {
        this->m_headingFromCompass = enabled;
    }

    void reset()
    {
        this->m_gyro.clear();
        this->m_compass.clear();
        this->m_core.clear();
        this->m_latency.clear();
        this->m_mode = Mode::None;
        this->m_hasCore = false;
        this->m_rebase = false;
        this->m_hasOffset = false;
        this->m_appliedOffset = 0.0;
        this->m_targetOffset = 0.0;
        this->m_alignmentSeconds = 0.0;
        this->m_hasPendingCompass = false;
        this->m_hasDelay = false;
        this->m_hasFrame = false;
        this->m_agreement = 1.0;
        this->m_rate = {};
        this->m_headingTracker.reset();
        this->m_compassTracker.reset();
        this->m_modeSwitches = 0;
        this->m_offsets.clear();
        this->m_lastResolvedCompass = -1e300;
    }

    void setMagnification(double magnification)
    {
        this->m_magnification = std::isfinite(magnification) && magnification > 0.0 ? magnification : 1.0;
    }


    void setLeadFraction(double fraction)
    {
        this->m_leadFraction = std::clamp(fraction, 0.0, 1.5);
    }

    // Gyroscope pose (device -> gyroscope frame, Z up, arbitrary heading) at
    // sensor time 'seconds', delivered at 'arrival' (the clock of frames).
    void addGyro(const Quaternion &pose, double seconds, double arrival)
    {
        if (!std::isfinite(seconds) || !std::isfinite(arrival))
            return;
        const Quaternion q = AowisCompass::normalized(pose);
        if (this->m_gyro.count > 0)
        {
            const Sample &last = this->m_gyro.newest();
            if (seconds <= last.seconds)
                return;
            if (seconds - last.seconds > kMaxGapSeconds)
            {
                this->m_gyro.clear();
                this->m_headingTracker.reset();
            }
            else if (AowisCompass::length(rotationVector(shortest(
                         AowisCompass::multiply(conjugate(last.pose), q)))) > kGyroJumpRadians)
            {
                // The OS reset the gyroscope frame. Keep the view continuous:
                // turn the offset by the jump about Up (the user cannot turn
                // that far in one sample).
                const double jump = headingAbout(shortest(AowisCompass::multiply(last.pose, conjugate(q))));
                this->m_appliedOffset = wrap(this->m_appliedOffset + jump);
                this->m_targetOffset = wrap(this->m_targetOffset + jump);
                this->m_gyro.clear();
                this->m_headingTracker.reset();
                this->m_offsets.clear();
                this->m_rebase = true;
            }
        }
        if (this->m_mode != Mode::Gyro)
            this->m_headingTracker.reset();
        this->m_gyro.push(q, seconds, arrival, this->m_headingTracker.add(q, seconds));
        if (this->m_mode != Mode::Gyro)
        {
            const bool wasRunning = this->m_mode != Mode::None;
            if (wasRunning)
                ++this->m_modeSwitches;
            this->m_mode = Mode::Gyro;
            this->m_latency.clear();
            this->m_hasOffset = false;
            this->m_rebase = this->m_rebase || wasRunning;
            this->m_hasPendingCompass = this->m_compass.count > 0;
            this->m_offsets.clear();
            this->m_lastResolvedCompass = -1e300;
        }
        this->m_latency.push({}, seconds, arrival);
        if (this->m_headingFromCompass)
            resolveCompassReadings();
        else if (this->m_hasPendingCompass)
            resolveCompass();
        if (!this->m_headingFromCompass && !this->m_hasOffset && this->m_compass.count > 0)
        {
            // First alignment: compass and gyroscope at the same sensor time
            // (either interpolated to the other), else the newest of both;
            // refined by every later reading. Comparing the newest of both
            // (as before) paired readings up to a gyroscope burst apart: on
            // every hand-back from the compass, a heading jump of turn rate
            // times burst length. Tilting was unaffected (only the heading is
            // aligned): sideways yanky, up and down smooth.
            const Sample &reading = this->m_compass.newest();
            Quaternion compassAt;
            Quaternion gyroAt;
            double measured = 0.0;
            if (this->m_compass.interpolate(seconds, kHistoryToleranceSeconds, &compassAt))
                measured = measuredOffset(compassAt, q);
            else if (this->m_gyro.interpolate(reading.seconds, kHistoryToleranceSeconds, &gyroAt))
                measured = measuredOffset(reading.pose, gyroAt);
            else
                measured = measuredOffset(reading.pose, q);
            this->m_appliedOffset = measured;
            this->m_targetOffset = measured;
            this->m_alignmentSeconds = this->m_compass.newest().seconds;
            this->m_hasOffset = true;
        }
        advance(seconds);
    }

    // Compass pose (device -> ENU, magnetic north) at sensor time 'seconds'.
    void addCompass(const Quaternion &pose, double seconds, double arrival)
    {
        if (!std::isfinite(seconds) || !std::isfinite(arrival))
            return;
        if (this->m_compass.count > 0 && seconds <= this->m_compass.newest().seconds)
            return;
        const Quaternion q = AowisCompass::normalized(pose);
        const bool gyroCurrent = this->m_gyro.count > 0 &&
                                 seconds - this->m_gyro.newest().seconds <= kGyroTimeoutSeconds;
        if (this->m_compass.count > 0 && seconds - this->m_compass.newest().seconds > kMaxGapSeconds)
        {
            this->m_compass.clear();
            this->m_compassTracker.reset();
        }
        // The compass heading has its own noise tracker (transparent on a
        // clean compass); it runs on in both modes, so a hand-over keeps it.
        const double compassHeading = this->m_compassTracker.add(q, seconds);
        if (gyroCurrent)
        {
            this->m_compass.push(q, seconds, arrival, compassHeading);
            this->m_hasPendingCompass = true;
            if (this->m_headingFromCompass)
                resolveCompassReadings();
            else
                resolveCompass();
            return;
        }
        // No current gyroscope: the compass drives the view.
        if (this->m_mode != Mode::Compass)
        {
            this->m_rebase = this->m_mode != Mode::None;
            if (this->m_rebase)
                ++this->m_modeSwitches;
            this->m_mode = Mode::Compass;
            this->m_latency.clear();
            this->m_gyro.clear();
            this->m_headingTracker.reset();
        }
        this->m_compass.push(q, seconds, arrival, compassHeading);
        this->m_latency.push({}, seconds, arrival);
        advance(seconds);
    }

    bool hasPose() const { return this->m_hasCore; }

    // Pose to show for a frame drawn at 'now' (the clock of 'arrival').
    // Only the display delay adapts; the core is never changed by frames.
    Quaternion poseForFrame(double now)
    {
        if (!this->m_hasCore)
            return {};
        const double target = targetDelay(now);
        if (!this->m_hasDelay)
        {
            this->m_delay = target;
            this->m_hasDelay = true;
        }
        else if (this->m_hasFrame)
        {
            const double dt = std::clamp(now - this->m_lastFrame, 0.0, 0.1);
            if (target > this->m_delay)
                this->m_delay = std::min(target, this->m_delay + kDelayRisePerSecond * dt);
            else
                this->m_delay = std::max(target, this->m_delay - kDelayFallPerSecond * dt);
        }
        // The lead's stop response is eased over a few frames: a stop shows
        // up in batches, and a stepwise lead would make it jerky.
        const double raw = leadAgreement(now);
        const double frameStep = this->m_hasFrame ? std::clamp(now - this->m_lastFrame, 0.0, 0.1) : 1.0;
        this->m_agreement += (raw - this->m_agreement) * -std::expm1(-frameStep / kLeadEaseSeconds);
        this->m_lastFrame = now;
        this->m_hasFrame = true;
        return displayPose(now);
    }

    // Read-only frame pose at 'now' with the current delay (tests).
    Quaternion peekFrame(double now) const
    {
        return this->m_hasCore ? displayPose(now) : Quaternion{};
    }

    // Diagnostics and tests.
    bool usesGyro() const { return this->m_mode == Mode::Gyro; }
    double coreSeconds() const { return this->m_coreSeconds; }
    Quaternion corePose() const { return this->m_filtered; }
    // Core state at sensor time t within the last 0.25 s (older: the oldest
    // kept state).
    Quaternion corePoseAt(double t) const { return this->m_hasCore ? coreAt(t) : Quaternion{}; }
    double headingOffsetRadians() const
    {
        if (this->m_headingFromCompass)
            return this->m_offsets.count > 0 ? wrap(this->m_offsets.newest().heading) : 0.0;
        return this->m_appliedOffset;
    }
    double targetHeadingOffsetRadians() const { return this->m_targetOffset; }
    double displayDelaySeconds() const { return this->m_delay - minimumLatency(this->m_lastFrame); }
    // Measured sample-to-sample heading noise of the driving input, before the
    // floor is subtracted (degrees), and the correction currently applied.
    double headingNoiseDegrees() const
    {
        return this->m_mode == Mode::Compass || (this->m_headingFromCompass && this->m_mode == Mode::Gyro)
            ? this->m_compassTracker.noiseDegrees() : this->m_headingTracker.noiseDegrees();
    }
    double gyroHeadingNoiseDegrees() const { return this->m_headingTracker.noiseDegrees(); }
    int modeSwitches() const { return this->m_modeSwitches; }
    double headingCorrectionDegrees() const
    {
        return this->m_mode == Mode::Compass || (this->m_headingFromCompass && this->m_mode == Mode::Gyro)
            ? this->m_compassTracker.correctionDegrees() : this->m_headingTracker.correctionDegrees();
    }

    // Steady-state gains of a constant-velocity (alpha-beta) tracker for the
    // tracking index lambda = process noise * dt^2 / measurement noise
    // (Kalata). lambda -> infinity gives alpha -> 1: the input unchanged.
    static void trackerGains(double lambda, double *alpha, double *beta)
    {
        if (!(lambda < 1e4))
        {
            *alpha = 1.0;
            *beta = 0.0;
            return;
        }
        const double root = std::sqrt(lambda * lambda + 8.0 * lambda);
        *alpha = std::clamp(-(lambda * lambda + 8.0 * lambda - (lambda + 4.0) * root) / 8.0, 0.0, 1.0);
        *beta = std::clamp((lambda * lambda + 4.0 * lambda - lambda * root) / 4.0, 0.0, 2.0);
    }

    static double cutoffHz(double angularSpeedDegreesPerSecond, double magnification)
    {
        const double lens = std::isfinite(magnification) && magnification > 0.0 ? magnification : 1.0;
        const double rest = std::clamp(kRestCutoffHzAt1x / lens, kMinRestCutoffHz, kMaxRestCutoffHz);
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

    static Quaternion slerp(const Quaternion &a, const Quaternion &b, double t)
    {
        return AowisCompass::normalized(AowisCompass::multiply(a, power(AowisCompass::multiply(conjugate(a), b), t)));
    }

    // Rotation of a device-to-ENU pose about Up, in radians.
    static double headingAbout(const Quaternion &q)
    {
        return 2.0 * std::atan2(q.z, q.w);
    }

private:
    enum class Mode { None, Gyro, Compass };

    struct Sample
    {
        Quaternion pose;
        double seconds = 0.0;
        double arrival = 0.0;
        // Heading correction (radians about Up, applied on the left).
        double heading = 0.0;
    };

    template <std::size_t Size>
    struct History
    {
        std::array<Sample, Size> entries{};
        std::size_t count = 0;
        std::size_t next = 0;

        void clear()
        {
            this->count = 0;
            this->next = 0;
        }

        void push(const Quaternion &pose, double seconds, double arrival, double heading = 0.0)
        {
            this->entries[this->next].pose = pose;
            this->entries[this->next].seconds = seconds;
            this->entries[this->next].arrival = arrival;
            this->entries[this->next].heading = heading;
            this->next = (this->next + 1) % Size;
            this->count = std::min(this->count + 1, Size);
        }

        const Sample &at(std::size_t index) const // 0: newest
        {
            return this->entries[(this->next + Size - 1 - index) % Size];
        }

        const Sample &newest() const { return at(0); }
        const Sample &oldest() const { return at(this->count - 1); }

        // Pose at time t between samples; false outside the history (beyond
        // 'tolerance' before the oldest sample, or after the newest).
        bool interpolate(double t, double tolerance, Quaternion *pose, double *heading = nullptr) const
        {
            if (this->count == 0 || t > newest().seconds)
                return false;
            for (std::size_t index = 1; index < this->count; ++index)
            {
                const Sample &earlier = at(index);
                if (earlier.seconds > t)
                    continue;
                const Sample &later = at(index - 1);
                const double span = later.seconds - earlier.seconds;
                const double fraction = span > 0.0 ? (t - earlier.seconds) / span : 1.0;
                *pose = slerp(earlier.pose, later.pose, fraction);
                if (heading != nullptr)
                    *heading = earlier.heading + (later.heading - earlier.heading) * fraction;
                return true;
            }
            if (t < oldest().seconds - tolerance)
                return false;
            *pose = oldest().pose;
            if (heading != nullptr)
                *heading = oldest().heading;
            return true;
        }
    };

    // Heading noise estimate and constant-velocity tracker on the driving
    // input. The heading is integrated from the vertical (world Up) part of
    // each sample-to-sample rotation, so it has no singularity looking up or
    // down. add() returns the correction to apply about Up to that sample.
    class HeadingTracker
    {
    public:
        void reset()
        {
            this->m_count = 0;
            this->m_correction = 0.0;
        }

        double add(const Quaternion &pose, double seconds)
        {
            if (this->m_count == 0 || !(seconds > this->m_seconds))
            {
                if (this->m_count == 0)
                {
                    this->m_raw = 0.0;
                    this->m_tracked = 0.0;
                    this->m_velocity = 0.0;
                    this->m_increment = 0.0;
                    this->m_correction = 0.0;
                    this->m_count = 1;
                }
                this->m_pose = pose;
                this->m_seconds = seconds;
                return this->m_correction;
            }
            const double dt = seconds - this->m_seconds;
            const double increment = rotationVector(shortest(AowisCompass::multiply(pose, conjugate(this->m_pose)))).z;
            this->m_pose = pose;
            this->m_seconds = seconds;
            this->m_raw += increment;
            if (this->m_count >= 2)
            {
                // Second difference: white noise of sigma gives variance
                // 6 sigma^2; a steady turn gives none.
                const double second = increment - this->m_increment * dt / this->m_dt;
                const double weight = -std::expm1(-dt / kNoiseTimeConstantSeconds);
                if (this->m_count == 2)
                    this->m_noiseVariance = second * second / 6.0;
                else
                    this->m_noiseVariance += (second * second / 6.0 - this->m_noiseVariance) * weight;
            }
            this->m_increment = increment;
            this->m_dt = dt;
            this->m_count = std::min(this->m_count + 1, 3);

            const double floor = kNoiseFloorDegrees * AowisCompass::kPi / 180.0;
            const double noise = std::sqrt(std::max(0.0, this->m_count >= 3
                ? this->m_noiseVariance - floor * floor : 0.0));
            const double process = kHandAccelerationDegreesPerSecond2 * AowisCompass::kPi / 180.0;
            double alpha = 1.0;
            double beta = 0.0;
            trackerGains(noise > 0.0 ? process * dt * dt / noise : 1e9, &alpha, &beta);
            const double predicted = this->m_tracked + this->m_velocity * dt;
            const double residual = this->m_raw - predicted;
            this->m_tracked = predicted + alpha * residual;
            this->m_velocity = beta > 0.0 ? this->m_velocity + beta * residual / dt : increment / dt;
            const double limit = kMaxHeadingCorrectionDegrees * AowisCompass::kPi / 180.0;
            if (std::fabs(this->m_tracked - this->m_raw) > limit)
            {
                this->m_tracked = this->m_raw;
                this->m_velocity = increment / dt;
            }
            // Keep the integrated heading small; only differences matter.
            if (std::fabs(this->m_raw) > 1000.0)
            {
                this->m_tracked -= this->m_raw;
                this->m_raw = 0.0;
            }
            this->m_correction = this->m_tracked - this->m_raw;
            return this->m_correction;
        }

        double noiseDegrees() const
        {
            return this->m_count >= 3 ? std::sqrt(std::max(0.0, this->m_noiseVariance)) * 180.0 / AowisCompass::kPi : 0.0;
        }

        double correctionDegrees() const { return this->m_correction * 180.0 / AowisCompass::kPi; }

    private:
        int m_count = 0;
        Quaternion m_pose;
        double m_seconds = 0.0;
        double m_dt = 0.0;
        double m_increment = 0.0;
        double m_raw = 0.0;
        double m_tracked = 0.0;
        double m_velocity = 0.0;
        double m_noiseVariance = 0.0;
        double m_correction = 0.0;
    };

    static double wrap(double radians)
    {
        return std::remainder(radians, 2.0 * AowisCompass::kPi);
    }

    static double smoothingFactor(double cutoffHz, double elapsed)
    {
        return -std::expm1(-2.0 * AowisCompass::kPi * cutoffHz * elapsed);
    }

    // compass ~= Rz(offset) * gyro; keep only the rotation about Up.
    static double measuredOffset(const Quaternion &compass, const Quaternion &gyro)
    {
        return headingAbout(shortest(AowisCompass::multiply(compass, conjugate(gyro))));
    }

    static Quaternion aboutUp(double radians)
    {
        return {0.0, 0.0, std::sin(0.5 * radians), std::cos(0.5 * radians)};
    }

    // Heading from the compass: every compass reading not used yet that the
    // gyroscope history covers gives the heading offset at its own time.
    // Readings ahead of the gyroscope wait; readings older than its history
    // are skipped.
    void resolveCompassReadings()
    {
        if (this->m_compass.count == 0 || this->m_gyro.count == 0)
            return;
        for (std::size_t index = this->m_compass.count; index-- > 0;)
        {
            const Sample &reading = this->m_compass.at(index);
            if (reading.seconds <= this->m_lastResolvedCompass)
                continue;
            if (reading.seconds > this->m_gyro.newest().seconds)
                break;
            Quaternion gyro;
            double heading = 0.0;
            if (this->m_gyro.interpolate(reading.seconds, kHistoryToleranceSeconds, &gyro, &heading))
            {
                // Against the gyroscope as shown (with its noise correction),
                // so the view meets the compass heading at this reading.
                const double measured = measuredOffset(AowisCompass::multiply(aboutUp(reading.heading), reading.pose),
                                                       AowisCompass::multiply(aboutUp(heading), gyro));
                // Unwrapped, so interpolation never runs the long way round.
                double offset = measured;
                if (this->m_offsets.count > 0)
                {
                    const Sample &last = this->m_offsets.newest();
                    const double dt = std::clamp(reading.seconds - last.seconds, 0.0, kMaxGapSeconds);
                    double turnRate = 0.0;
                    double tiltRate = 0.0;
                    gyroRates(reading.seconds, &turnRate, &tiltRate);
                    const double turn = turnRate / kHeadingTurnDegreesPerSecond;
                    const double tilt = tiltRate / kHeadingTiltDegreesPerSecond;
                    const double tau = (kHeadingTurnSeconds +
                                        (kHeadingRestSeconds - kHeadingTurnSeconds) / (1.0 + turn * turn)) *
                                       (1.0 + tilt * tilt / (1.0 + turn * turn));
                    offset = last.heading + wrap(measured - last.heading) * -std::expm1(-dt / tau);
                }
                this->m_offsets.push({}, reading.seconds, reading.arrival, offset);
            }
            this->m_lastResolvedCompass = reading.seconds;
        }
        this->m_hasPendingCompass = false;
    }

    // Gyroscope rates at sensor time t over kTurnRateWindowSeconds (degrees
    // per second): turn about Up and tilt about horizontal axes. 0 if not
    // covered.
    void gyroRates(double t, double *turn, double *tilt) const
    {
        *turn = 0.0;
        *tilt = 0.0;
        Quaternion now;
        Quaternion before;
        if (!this->m_gyro.interpolate(t, kHistoryToleranceSeconds, &now) ||
            !this->m_gyro.interpolate(t - kTurnRateWindowSeconds, kTurnRateWindowSeconds, &before))
            return;
        const Vector3 v = rotationVector(shortest(AowisCompass::multiply(now, conjugate(before))));
        const double scale = 180.0 / AowisCompass::kPi / kTurnRateWindowSeconds;
        *turn = std::fabs(v.z) * scale;
        *tilt = std::hypot(v.x, v.y) * scale;
    }

    // Heading offset at sensor time t: interpolated between readings, held
    // before the first and after the newest.
    bool offsetAt(double t, double *offset) const
    {
        if (this->m_offsets.count == 0)
            return false;
        Quaternion unused;
        if (!this->m_offsets.interpolate(t, 1e9, &unused, offset))
            *offset = this->m_offsets.newest().heading;
        return true;
    }

    // Compare the newest compass reading with the gyroscope pose at the
    // reading's own timestamp and steer the target heading offset.
    void resolveCompass()
    {
        if (!this->m_hasPendingCompass || this->m_compass.count == 0 || this->m_gyro.count == 0)
            return;
        const Sample &reading = this->m_compass.newest();
        Quaternion gyro;
        if (!this->m_gyro.interpolate(reading.seconds, kHistoryToleranceSeconds, &gyro))
        {
            // Not covered yet: wait for the gyroscope to pass it. Older than
            // the history: drop it.
            if (reading.seconds <= this->m_gyro.newest().seconds)
                this->m_hasPendingCompass = false;
            return;
        }
        this->m_hasPendingCompass = false;
        const double measured = measuredOffset(reading.pose, gyro);
        if (!this->m_hasOffset)
        {
            this->m_appliedOffset = measured;
            this->m_targetOffset = measured;
            this->m_hasOffset = true;
            this->m_alignmentSeconds = reading.seconds;
            return;
        }
        const double step = std::min(reading.seconds - this->m_alignmentSeconds, kMaxAlignmentStepSeconds);
        if (step > 0.0)
            this->m_targetOffset = wrap(this->m_targetOffset + wrap(measured - this->m_targetOffset) *
                                        -std::expm1(-step / kHeadingTimeConstantSeconds));
        this->m_alignmentSeconds = std::max(this->m_alignmentSeconds, reading.seconds);
    }

    // Unsmoothed device-to-ENU pose at sensor time t from the driving input.
    bool measurement(double t, Quaternion *pose) const
    {
        if (this->m_mode == Mode::Gyro)
        {
            Quaternion gyro;
            double heading = 0.0;
            if (this->m_headingFromCompass)
            {
                double offset = 0.0;
                if (!offsetAt(t, &offset) || !this->m_gyro.interpolate(t, kMaxGapSeconds, &gyro, &heading))
                    return false;
                *pose = AowisCompass::normalized(AowisCompass::multiply(aboutUp(offset + heading), gyro));
                return true;
            }
            if (!this->m_hasOffset || !this->m_gyro.interpolate(t, kMaxGapSeconds, &gyro, &heading))
                return false;
            *pose = AowisCompass::normalized(AowisCompass::multiply(aboutUp(this->m_appliedOffset + heading), gyro));
            return true;
        }
        if (this->m_mode == Mode::Compass)
        {
            Quaternion compass;
            double heading = 0.0;
            if (!this->m_compass.interpolate(t, kMaxGapSeconds, &compass, &heading))
                return false;
            *pose = AowisCompass::normalized(AowisCompass::multiply(aboutUp(heading), compass));
            return true;
        }
        return false;
    }

    void advance(double watermark)
    {
        if (!this->m_hasCore || watermark - this->m_coreSeconds > kMaxGapSeconds)
        {
            Quaternion pose;
            if (!measurement(watermark, &pose))
                return;
            this->m_hasCore = true;
            this->m_coreSeconds = watermark;
            this->m_filtered = pose;
            this->m_previous = pose;
            this->m_rate = {};
            this->m_rebase = false;
            this->m_core.clear();
            this->m_core.push(pose, watermark, 0.0);
            return;
        }
        while (this->m_coreSeconds + kStepSeconds <= watermark + 1e-9)
        {
            const double t = this->m_coreSeconds + kStepSeconds;
            if (!step(t))
                break;
            this->m_coreSeconds = t;
            this->m_core.push(this->m_filtered, t, 0.0);
        }
    }

    bool step(double t)
    {
        // North: the applied offset follows the target at a limited rate,
        // faster while the user turns.
        if (!this->m_headingFromCompass && this->m_mode == Mode::Gyro && this->m_hasOffset)
        {
            const double turn = AowisCompass::length(this->m_rate);
            const double limit = (kRestCorrectionDegreesPerSecond * AowisCompass::kPi / 180.0 / this->m_magnification
                                  + kMotionCorrectionFraction * turn) * kStepSeconds;
            const double remaining = wrap(this->m_targetOffset - this->m_appliedOffset);
            this->m_appliedOffset = wrap(this->m_appliedOffset + std::clamp(remaining, -limit, limit));
        }
        Quaternion pose;
        if (!measurement(t, &pose))
            return false;
        if (this->m_rebase)
        {
            // Source change or gyroscope reset: read no motion from the step;
            // the filter absorbs any difference smoothly.
            this->m_previous = pose;
            this->m_rebase = false;
        }
        const Vector3 motion = rotationVector(shortest(AowisCompass::multiply(conjugate(this->m_previous), pose)));
        this->m_previous = pose;
        const double rateAlpha = smoothingFactor(kRateCutoffHz, kStepSeconds);
        this->m_rate.x += (motion.x / kStepSeconds - this->m_rate.x) * rateAlpha;
        this->m_rate.y += (motion.y / kStepSeconds - this->m_rate.y) * rateAlpha;
        this->m_rate.z += (motion.z / kStepSeconds - this->m_rate.z) * rateAlpha;

        const double cutoff = cutoffHz(AowisCompass::length(this->m_rate) * 180.0 / AowisCompass::kPi,
                                       this->m_magnification);
        const double pointAlpha = smoothingFactor(cutoff, kStepSeconds);
        const double rollAlpha = smoothingFactor(cutoff * kRollCutoffRatio, kStepSeconds);
        // Split the remaining error into twist about the view axis (roll)
        // and swing of the view axis (pointing): error = swing * twist.
        const Quaternion error = shortest(AowisCompass::multiply(conjugate(this->m_filtered), pose));
        Quaternion twist = AowisCompass::normalized({0.0, 0.0, error.z, error.w});
        if (std::hypot(error.z, error.w) < 1e-9)
            twist = {};
        const Quaternion swing = AowisCompass::multiply(error, conjugate(twist));
        this->m_filtered = AowisCompass::normalized(AowisCompass::multiply(this->m_filtered,
            AowisCompass::multiply(power(swing, pointAlpha), power(twist, rollAlpha))));
        return true;
    }

    // --- Display ---------------------------------------------------------

    double minimumLatency(double now) const
    {
        if (this->m_latency.count == 0)
            return 0.0;
        double best = this->m_latency.newest().arrival - this->m_latency.newest().seconds;
        for (std::size_t index = 1; index < this->m_latency.count; ++index)
        {
            const Sample &entry = this->m_latency.at(index);
            if (entry.arrival < now - kLatencyWindowSeconds)
                break;
            best = std::min(best, entry.arrival - entry.seconds);
        }
        return best;
    }

    // Largest delivery latency in the window plus one input interval and one
    // core step: a core state for now - delay has then always been computed.
    // latency = arrival - sensor time includes any clock offset, so now -
    // delay is directly a sensor time.
    double targetDelay(double now) const
    {
        if (this->m_latency.count == 0)
            return 0.0;
        double worst = this->m_latency.newest().arrival - this->m_latency.newest().seconds;
        for (std::size_t index = 1; index < this->m_latency.count; ++index)
        {
            const Sample &entry = this->m_latency.at(index);
            if (entry.arrival < now - kLatencyWindowSeconds)
                break;
            worst = std::max(worst, entry.arrival - entry.seconds);
        }
        double interval = 0.01;
        if (this->m_latency.count > 1)
            interval = (this->m_latency.newest().seconds - this->m_latency.oldest().seconds) /
                       double(this->m_latency.count - 1);
        const double minimum = minimumLatency(now);
        return std::clamp(worst + interval + kStepSeconds, minimum, minimum + kMaxDelaySeconds);
    }

    Quaternion coreAt(double t) const
    {
        const Sample &last = this->m_core.newest();
        if (t <= last.seconds)
        {
            Quaternion pose;
            if (this->m_core.interpolate(t, 1e9, &pose))
                return pose;
            return last.pose;
        }
        // Latency spike: extrapolate briefly along the recent motion.
        Quaternion earlier;
        if (!this->m_core.interpolate(last.seconds - kVelocityWindowSeconds, 1e9, &earlier))
            return last.pose;
        const double ahead = std::min(t - last.seconds, kMaxExtrapolationSeconds);
        return AowisCompass::normalized(AowisCompass::multiply(last.pose,
            power(AowisCompass::multiply(conjugate(earlier), last.pose), ahead / kVelocityWindowSeconds)));
    }

    Quaternion displayPose(double now) const
    {
        const double t = now - this->m_delay;
        const Quaternion base = coreAt(t);
        if (this->m_leadFraction <= 0.0)
            return base;
        const Vector3 motion = rotationVector(shortest(
            AowisCompass::multiply(conjugate(coreAt(t - kVelocityWindowSeconds)), base)));
        const double speed = AowisCompass::length(motion) / kVelocityWindowSeconds;
        const double fade = std::clamp((speed * 180.0 / AowisCompass::kPi - kLeadStartDegreesPerSecond) /
                                       kLeadFadeDegreesPerSecond, 0.0, 1.0);
        // Age of the shown state when it reaches the screen. Only latency
        // differences are known; the smallest one is the true latency if the
        // clocks agree (Android: both CLOCK_BOOTTIME), otherwise assume 5 ms.
        const double minimum = minimumLatency(now);
        const double floor = minimum >= 0.0 && minimum <= 0.1 ? minimum : 0.005;
        const double age = this->m_delay - minimum + floor + kDisplayLatencySeconds;
        const double lead = this->m_leadFraction * fade * this->m_agreement * age / kVelocityWindowSeconds;
        return AowisCompass::normalized(AowisCompass::multiply(base,
            fromRotationVector({motion.x * lead, motion.y * lead, motion.z * lead})));
    }

    // The core already runs ahead of the shown moment. How much of the shown
    // motion its newest state still has: 1 while panning steadily, falling to
    // 0 as a stop arrives, before the stop reaches the screen.
    double leadAgreement(double now) const
    {
        const double t = now - this->m_delay;
        const Quaternion base = coreAt(t);
        const Vector3 motion = rotationVector(shortest(
            AowisCompass::multiply(conjugate(coreAt(t - kVelocityWindowSeconds)), base)));
        const double newestSeconds = this->m_core.newest().seconds;
        const Vector3 ahead = rotationVector(shortest(AowisCompass::multiply(
            conjugate(coreAt(newestSeconds - kVelocityWindowSeconds)), this->m_core.newest().pose)));
        const double squared = motion.x * motion.x + motion.y * motion.y + motion.z * motion.z;
        if (squared < 1e-18)
            return 1.0;
        return std::clamp((ahead.x * motion.x + ahead.y * motion.y + ahead.z * motion.z) / squared, 0.0, 1.0);
    }

    History<kInputHistorySize> m_gyro;
    History<kInputHistorySize> m_compass;
    History<kInputHistorySize> m_latency;
    History<kOutputHistorySize> m_core;
    Mode m_mode = Mode::None;
    double m_magnification = 1.0;
    double m_leadFraction = kDefaultLeadFraction;
    double m_agreement = 1.0;

    bool m_hasCore = false;
    bool m_rebase = false;
    double m_coreSeconds = 0.0;
    Quaternion m_filtered;
    Quaternion m_previous;
    Vector3 m_rate;

    bool m_hasOffset = false;
    bool m_hasPendingCompass = false;
    double m_appliedOffset = 0.0;
    double m_targetOffset = 0.0;
    double m_alignmentSeconds = 0.0;

    bool m_hasDelay = false;
    bool m_hasFrame = false;
    double m_delay = 0.0;
    double m_lastFrame = 0.0;

    HeadingTracker m_headingTracker;
    HeadingTracker m_compassTracker;
    bool m_headingFromCompass = true;
    // Heading offset (compass - gyroscope about Up, unwrapped, in 'heading')
    // at each compass reading's sensor time.
    History<kInputHistorySize> m_offsets;
    double m_lastResolvedCompass = -1e300;
    // Hand-overs between gyroscope and compass (diagnostics).
    int m_modeSwitches = 0;
};
// Presentation clock for frames. With a vsync-locked swap the screen shows
// frames exactly one refresh period apart, but the moment a frame is prepared
// jitters by several milliseconds (event dispatch, Qt, compositor: measured
// on Android 11 to 21 ms between 60 Hz frames). Sampling motion at that raw
// moment makes every shown step jitter by the same ratio (about 20 % at
// 60 Hz), which is seen as stutter.
//
// This clock is a second-order phase-locked loop on the frame stream: each
// frame advances by one period; the difference to the wall clock corrects the
// phase by kPhaseGain and the period by kPeriodGain of it. Preparation jitter
// is attenuated about twentyfold, the period follows the panel's true rate
// (58.9 Hz measured on an Android "60 Hz" device) without being estimated from
// noisy intervals, and a real dropped frame is absorbed within a second.
// (Rounding each interval to whole periods, as the renderer's MapFramePacer
// does, turns a late frame followed by an early one into "two periods, then
// none": a hitch, although the screen showed each frame once.)
class LookAroundFrameClock final
{
public:
    static constexpr double kPhaseGain = 0.05;
    // About kPhaseGain^2 / 4: a critically damped loop.
    static constexpr double kPeriodGain = 0.0006;
    // Intervals measured before locking (half a second at 60 Hz); their
    // total over the frames they span starts the period.
    static constexpr std::size_t kStartIntervals = 30;
    static constexpr double kMinimumPeriodSeconds = 1.0 / 500.0;
    static constexpr double kMaximumPeriodSeconds = 1.0 / 20.0;
    static constexpr double kLongGapSeconds = 0.25;
    // Further off than this (a stall, a pause): start over at the wall clock.
    static constexpr double kResyncPeriods = 3.0;

    void reset()
    {
        this->m_hasFrame = false;
        this->m_count = 0;
        this->m_period = 0.0;
    }

    // 'wall' is when the frame is prepared; returns its presentation time on
    // the same clock.
    double frameTime(double wall)
    {
        if (!std::isfinite(wall))
            return this->m_paced;
        if (!this->m_hasFrame)
        {
            this->m_hasFrame = true;
            this->m_lastWall = wall;
            this->m_paced = wall;
            return this->m_paced;
        }
        const double measured = wall - this->m_lastWall;
        this->m_lastWall = wall;
        if (measured <= 0.0)
            return this->m_paced;
        if (measured > kLongGapSeconds)
            return restart(wall);
        if (this->m_period <= 0.0)
        {
            // Not locked yet: follow the wall clock and collect intervals.
            if (measured >= kMinimumPeriodSeconds && measured <= kMaximumPeriodSeconds)
                this->m_start[this->m_count++] = measured;
            if (this->m_count == kStartIntervals)
            {
                // Total time over frames shown, a dropped frame counting as
                // the periods it spans. Preparation jitter telescopes out of
                // the sum (dropping extreme intervals instead would bias it).
                std::array<double, kStartIntervals> sorted = this->m_start;
                std::sort(sorted.begin(), sorted.end());
                const double median = sorted[kStartIntervals / 2];
                double total = 0.0;
                double frames = 0.0;
                for (double interval : sorted)
                {
                    total += interval;
                    frames += std::max(1.0, std::round(interval / median));
                }
                this->m_period = total / frames;
                this->m_nominal = this->m_period;
            }
            this->m_paced = wall;
            return this->m_paced;
        }
        const double predicted = this->m_paced + this->m_period;
        const double error = wall - predicted;
        if (std::fabs(error) > kResyncPeriods * this->m_period)
            return restart(wall);
        this->m_paced = predicted + kPhaseGain * error;
        this->m_period = std::clamp(this->m_period + kPeriodGain * error,
                                    0.75 * this->m_nominal, 1.33 * this->m_nominal);
        return this->m_paced;
    }

    // The locked refresh period; 0 before locking.
    double periodSeconds() const { return this->m_period; }

private:
    double restart(double wall)
    {
        this->m_count = 0;
        this->m_period = 0.0;
        this->m_paced = wall;
        return wall;
    }

    std::array<double, kStartIntervals> m_start{};
    std::size_t m_count = 0;
    bool m_hasFrame = false;
    double m_lastWall = 0.0;
    double m_paced = 0.0;
    double m_period = 0.0;
    double m_nominal = 0.0;
};
}
