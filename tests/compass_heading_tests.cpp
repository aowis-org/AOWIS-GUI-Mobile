#include "compass_heading.h"
#include "compass_heading_filter.h"
#include "compass_fusion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAILED: " << #condition << " at line " << __LINE__ << '\n'; \
        std::exit(1); \
    } \
} while (false)

namespace
{
constexpr double kDeg = AowisCompass::kPi / 180.0;

// Deterministic Gaussian noise (LCG + Box-Muller), identical on every host.
class Noise
{
public:
    explicit Noise(std::uint64_t seed) : m_state(seed) {}
    double gaussian(double sigma)
    {
        const double u1 = std::max(uniform(), 1e-12);
        const double u2 = uniform();
        return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * AowisCompass::kPi * u2);
    }
private:
    double uniform()
    {
        this->m_state = this->m_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(this->m_state >> 11) / 9007199254740992.0;
    }
    std::uint64_t m_state;
};

struct Quaternion
{
    double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
};

Quaternion multiply(const Quaternion &a, const Quaternion &b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quaternion axisAngle(double ax, double ay, double az, double degrees)
{
    const double half = degrees * kDeg / 2.0;
    return {ax * std::sin(half), ay * std::sin(half), az * std::sin(half), std::cos(half)};
}

// Device pose: compass heading (clockwise from north), then pitch (top edge
// up, about device X) and roll (about device Y), applied in device frame.
double poseHeading(double heading, double pitch, double roll, int screenRotation)
{
    const Quaternion yaw = axisAngle(0.0, 0.0, 1.0, -heading);
    const Quaternion q = multiply(multiply(yaw, axisAngle(1.0, 0.0, 0.0, pitch)),
                                  axisAngle(0.0, 1.0, 0.0, roll));
    return AowisCompass::headingFromRotationVector(q.x, q.y, q.z, q.w, screenRotation);
}

double angularError(double a, double b)
{
    return std::fabs(AowisCompass::shortestDelta(a, b));
}

struct Run
{
    double maxError = 0.0;         // |display - truth| over the evaluation window
    double meanAbsError = 0.0;
    double displayRange = 0.0;     // peak-to-peak displayed angle in window
    int reverseFrames = 0;         // frames moving against a monotonic turn
    double finalDisplay = 0.0;
};

// Plays sensor samples and render frames on one simulated timeline.
Run play(const AowisCompass::HeadingFilterTuning &tuning, double sampleHz, double frameHz,
         double duration, double evaluateFrom, double sigma,
         const std::function<double(double)> &truth, std::uint64_t seed = 7)
{
    AowisCompass::HeadingFilter filter;
    filter.setTuning(tuning);
    Noise noise(seed);
    CHECK(filter.sample(AowisCompass::normalize(truth(0.0)), 0.0));
    Run run;
    double nextSample = 1.0 / sampleHz;
    double previousDisplay = filter.continuousDisplayDegrees();
    double lowest = 1e9;
    double highest = -1e9;
    double sumError = 0.0;
    int evaluated = 0;
    const int frames = static_cast<int>(duration * frameHz);
    for (int frame = 1; frame <= frames; ++frame)
    {
        const double now = frame / frameHz;
        while (nextSample <= now + 1e-9)
        {
            CHECK(filter.sample(AowisCompass::normalize(truth(nextSample) + noise.gaussian(sigma)),
                                nextSample));
            nextSample += 1.0 / sampleHz;
        }
        filter.advanceDisplay(now);
        const double display = filter.continuousDisplayDegrees();
        if (now >= evaluateFrom)
        {
            const double error = angularError(filter.displayDegrees(), AowisCompass::normalize(truth(now)));
            run.maxError = std::max(run.maxError, error);
            sumError += error;
            ++evaluated;
            lowest = std::min(lowest, display);
            highest = std::max(highest, display);
            const double turn = truth(now) - truth(now - 1.0 / frameHz);
            if (turn > 0.0 && display < previousDisplay - 0.02)
                ++run.reverseFrames;
        }
        previousDisplay = display;
    }
    run.meanAbsError = evaluated > 0 ? sumError / evaluated : 0.0;
    run.displayRange = highest - lowest;
    run.finalDisplay = filter.displayDegrees();
    return run;
}

// --- Raw sensor simulation for AOWIS' own fusion ---------------------------
struct Vec
{
    double x = 0.0, y = 0.0, z = 0.0;
};

// World vector into device coordinates: v_device = R^T v_world.
Vec toDevice(const Quaternion &q, const Vec &v)
{
    const Quaternion conjugate{-q.x, -q.y, -q.z, q.w};
    const Quaternion p{v.x, v.y, v.z, 0.0};
    const Quaternion r = multiply(multiply(conjugate, p), q);
    return {r.x, r.y, r.z};
}

Quaternion devicePose(double heading, double pitch, double roll)
{
    return multiply(multiply(axisAngle(0.0, 0.0, 1.0, -heading), axisAngle(1.0, 0.0, 0.0, pitch)),
                    axisAngle(0.0, 1.0, 0.0, roll));
}

struct FusionRun
{
    double maxError = 0.0;
    double meanError = 0.0;
};

// Simulates 100 Hz accelerometer, magnetometer (and gyroscope) streams of a
// hand-held phone, runs fusion + heading filter, compares the displayed
// heading with the true heading. headingAt / tremor are functions of time.
FusionRun playFusion(bool gyroscope, double duration, double evaluateFrom,
                     const std::function<double(double)> &headingAt, double tremorDegrees,
                     double handAcceleration, std::uint64_t seed = 11)
{
    AowisCompass::AttitudeFusion fusion;
    fusion.setGyroscopeAvailable(gyroscope);
    AowisCompass::HeadingFilter filter;
    filter.setTuning(gyroscope ? AowisCompass::kFusedSensorTuning : AowisCompass::kRawCompassTuning);
    Noise noise(seed);
    // Central Europe: ~20 uT north, ~44 uT downwards (inclination ~66 deg).
    const Vec field{0.0, 20.0, -44.0};
    const Vec gravityUp{0.0, 0.0, AowisCompass::AttitudeFusion::kStandardGravity};
    const double dt = 0.01;
    FusionRun run;
    double sum = 0.0;
    int count = 0;
    double nextFrame = 1.0 / 60.0;
    for (int step = 0; step * dt <= duration; ++step)
    {
        const double t = step * dt;
        // Hand tremor: a few degrees of pitch/roll at 3-7 Hz.
        const double pitch = 8.0 + tremorDegrees * std::sin(2.0 * AowisCompass::kPi * 4.3 * t);
        const double roll = -3.0 + tremorDegrees * std::sin(2.0 * AowisCompass::kPi * 6.1 * t + 1.0);
        const Quaternion q = devicePose(headingAt(t), pitch, roll);
        Vec g = toDevice(q, gravityUp);
        // Non-gravity hand acceleration (m/s^2), device frame.
        g.x += handAcceleration * std::sin(2.0 * AowisCompass::kPi * 2.7 * t) + noise.gaussian(0.05);
        g.y += handAcceleration * std::sin(2.0 * AowisCompass::kPi * 3.9 * t + 0.4) + noise.gaussian(0.05);
        g.z += noise.gaussian(0.05);
        Vec m = toDevice(q, field);
        m.x += noise.gaussian(0.6);
        m.y += noise.gaussian(0.6);
        m.z += noise.gaussian(0.6);
        fusion.addAccelerometer(g.x, g.y, g.z, t);
        fusion.addMagneticField(m.x, m.y, m.z, t);
        if (gyroscope)
        {
            // Body rate from the pose change over one step.
            const Quaternion next = devicePose(headingAt(t + dt),
                8.0 + tremorDegrees * std::sin(2.0 * AowisCompass::kPi * 4.3 * (t + dt)),
                -3.0 + tremorDegrees * std::sin(2.0 * AowisCompass::kPi * 6.1 * (t + dt) + 1.0));
            Quaternion delta = multiply(Quaternion{-q.x, -q.y, -q.z, q.w}, next);
            if (delta.w < 0.0)
                delta = {-delta.x, -delta.y, -delta.z, -delta.w};
            const double sinHalf = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
            const double angle = 2.0 * std::atan2(sinHalf, delta.w);
            const double scale = sinHalf > 1e-12 ? angle / (sinHalf * dt) : 0.0;
            // Gyroscope noise ~0.3 deg/s.
            fusion.addGyroscope(delta.x * scale + noise.gaussian(0.005),
                                delta.y * scale + noise.gaussian(0.005),
                                delta.z * scale + noise.gaussian(0.005), t + dt);
        }
        // Output cadence of the sensor thread.
        if (fusion.hasOrientation())
        {
            const AowisCompass::Quaternion o = fusion.orientation();
            const double h = AowisCompass::headingFromRotationVector(o.x, o.y, o.z, o.w, 0);
            if (h >= 0.0)
                filter.sample(h, t);
        }
        while (nextFrame <= t + 1e-9)
        {
            filter.advanceDisplay(nextFrame);
            if (nextFrame >= evaluateFrom)
            {
                const double error = angularError(filter.displayDegrees(),
                                                  AowisCompass::normalize(headingAt(nextFrame)));
                run.maxError = std::max(run.maxError, error);
                sum += error;
                ++count;
            }
            nextFrame += 1.0 / 60.0;
        }
    }
    run.meanError = count > 0 ? sum / count : 0.0;
    return run;
}

void reportFusion(const char *name, const FusionRun &run)
{
    std::cout << name << ": max error " << run.maxError << ", mean error " << run.meanError << '\n';
}

void report(const char *name, const Run &run)
{
    std::cout << name << ": max error " << run.maxError << ", mean error "
              << run.meanAbsError << ", range " << run.displayRange
              << ", reverse frames " << run.reverseFrames << '\n';
}
}

int main()
{
    using AowisCompass::kFusedSensorTuning;
    using AowisCompass::kRawCompassTuning;

    // --- Basic helpers ---------------------------------------------------
    CHECK(AowisCompass::normalize(0.0) == 0.0);
    CHECK(AowisCompass::normalize(360.0) == 0.0);
    CHECK(AowisCompass::normalize(725.0) == 5.0);
    CHECK(AowisCompass::normalize(-10.0) == 350.0);
    CHECK(AowisCompass::normalize(NAN) == -1.0);
    CHECK(std::string(AowisCompass::direction(44.0)) == "NE");
    CHECK(std::string(AowisCompass::direction(359.9)) == "N");
    CHECK(AowisCompass::clampedCalibration(0.5) == 0.5);
    CHECK(AowisCompass::calibrationFromAndroidStatus(3) == 1.0);
    CHECK(AowisCompass::calibrationFromAndroidStatus(0) == 0.0);
    CHECK(AowisCompass::calibrationFromAndroidStatus(-1) == -1.0);

    // --- Rotation vector to heading ---------------------------------------
    CHECK(angularError(AowisCompass::headingFromRotationVector(0, 0, 0, 1, 0), 0.0) < 1e-9);
    for (double heading : {0.0, 37.0, 90.0, 181.5, 271.0, 359.0})
    {
        // Flat, any roll/pitch tremor: the screen-top heading is unchanged.
        CHECK(angularError(poseHeading(heading, 0.0, 0.0, 0), heading) < 1e-6);
        CHECK(angularError(poseHeading(heading, 0.0, 8.0, 0), heading) < 1e-6);
        CHECK(angularError(poseHeading(heading, 6.0, 0.0, 0), heading) < 1e-6);
        // Tilted towards the user and fully upright (camera pointing).
        CHECK(angularError(poseHeading(heading, 45.0, 0.0, 0), heading) < 1e-6);
        CHECK(angularError(poseHeading(heading, 80.0, 0.0, 0), heading) < 1e-6);
        CHECK(angularError(poseHeading(heading, 90.0, 0.0, 0), heading) < 1e-6);
        CHECK(angularError(poseHeading(heading, -30.0, 0.0, 0), heading) < 1e-6);
        // Landscape: screen-top is the device's right edge (+90 deg).
        CHECK(angularError(poseHeading(heading, 0.0, 0.0, 90), heading + 90.0) < 1e-6);
        CHECK(angularError(poseHeading(heading, 0.0, 0.0, 270), heading + 270.0) < 1e-6);
    }
    // Combined small hand tremor of a flat phone stays well below a degree.
    CHECK(angularError(poseHeading(120.0, 6.0, 8.0, 0), 120.0) < 1.0);
    {
        // Upright phone leaning 5 deg sideways (about device Z): the camera
        // still points at 120 deg and the heading must follow the camera.
        const Quaternion q = multiply(multiply(axisAngle(0.0, 0.0, 1.0, -120.0),
                                               axisAngle(1.0, 0.0, 0.0, 88.0)),
                                      axisAngle(0.0, 0.0, 1.0, 5.0));
        const double h = AowisCompass::headingFromRotationVector(q.x, q.y, q.z, q.w, 0);
        std::cout << "upright lean heading " << h << '\n';
        CHECK(angularError(h, 120.0) < 0.5);
    }
    CHECK(AowisCompass::headingFromRotationVector(NAN, 0, 0, 1, 0) == -1.0);
    CHECK(AowisCompass::headingFromRotationVector(0, 0, 0, 0, 0) == -1.0);

    // --- Filter input validation and lifecycle ----------------------------
    AowisCompass::HeadingFilter filter;
    CHECK(!filter.initialized());
    CHECK(!filter.sample(NAN, 0.0));
    CHECK(filter.sample(359.0, 0.0));
    CHECK(filter.initialized());
    CHECK(std::fabs(filter.displayDegrees() - 359.0) < 1e-9);
    CHECK(!filter.sample(190.0, 0.0));
    CHECK(!filter.sample(270.0, -0.1));
    CHECK(!filter.sample(INFINITY, 0.1));
    filter.reset();
    CHECK(!filter.initialized());
    CHECK(!filter.advanceDisplay(3.1));
    CHECK(filter.sample(75.0, 4.0));
    CHECK(std::fabs(filter.displayDegrees() - 75.0) < 0.001);

    const std::function<double(double)> still = [](double) { return 42.0; };

    // --- Precision while still: converge on the true heading -------------
    // Fused sensor, typical 0.3 deg jitter at 100 Hz.
    Run fusedStill = play(kFusedSensorTuning, 100.0, 60.0, 8.0, 2.0, 0.3, still);
    report("fused still", fusedStill);
    CHECK(fusedStill.meanAbsError < 0.12);
    CHECK(fusedStill.maxError < 0.4);
    CHECK(fusedStill.displayRange < 0.7);
    // Raw accelerometer/magnetometer: 1.5 deg jitter at 20 Hz.
    Run rawStill = play(kRawCompassTuning, 20.0, 60.0, 10.0, 3.0, 1.5, still);
    report("raw still", rawStill);
    CHECK(rawStill.meanAbsError < 0.6);
    CHECK(rawStill.maxError < 1.6);

    // The old gate froze any offset below ~2.6 deg forever. A bias that
    // appears while still (e.g. phone set down slightly turned) must be
    // tracked to the new true value.
    const std::function<double(double)> nudge = [](double t) { return t < 2.0 ? 100.0 : 101.8; };
    Run fusedNudge = play(kFusedSensorTuning, 100.0, 60.0, 5.0, 3.0, 0.0, nudge);
    report("fused 1.8 deg nudge", fusedNudge);
    CHECK(fusedNudge.maxError < 0.05);
    Run rawNudge = play(kRawCompassTuning, 20.0, 60.0, 8.0, 5.0, 0.0, nudge);
    report("raw 1.8 deg nudge", rawNudge);
    CHECK(rawNudge.maxError < 0.07);

    // --- Slow turns: continuous, no staircase ------------------------------
    const std::function<double(double)> slow = [](double t) { return 40.0 + 2.0 * t; };
    Run fusedSlow = play(kFusedSensorTuning, 100.0, 60.0, 10.0, 2.0, 0.0, slow);
    report("fused slow 2 deg/s", fusedSlow);
    CHECK(fusedSlow.maxError < 0.5);
    Run rawSlow = play(kRawCompassTuning, 20.0, 60.0, 10.0, 3.0, 0.0, slow);
    report("raw slow 2 deg/s", rawSlow);
    CHECK(rawSlow.maxError < 1.5);

    // --- Normal turns: small lag ------------------------------------------
    const std::function<double(double)> turn90 = [](double t) { return 350.0 + 90.0 * t; };
    Run fusedTurn = play(kFusedSensorTuning, 100.0, 60.0, 4.0, 1.0, 0.3, turn90);
    report("fused 90 deg/s", fusedTurn);
    CHECK(fusedTurn.maxError < 3.0);
    CHECK(fusedTurn.reverseFrames == 0);
    Run fusedTurn50 = play(kFusedSensorTuning, 50.0, 60.0, 4.0, 1.0, 0.3, turn90);
    report("fused 90 deg/s @50Hz", fusedTurn50);
    CHECK(fusedTurn50.maxError < 3.5);
    Run rawTurn = play(kRawCompassTuning, 20.0, 60.0, 4.0, 1.0, 0.0, turn90);
    report("raw 90 deg/s @20Hz", rawTurn);
    CHECK(rawTurn.maxError < 8.0);

    // --- Turn, then stop: settle exactly, no overshoot ---------------------
    const std::function<double(double)> turnStop = [](double t) { return 40.0 + 60.0 * std::min(t, 2.0); };
    Run fusedStop = play(kFusedSensorTuning, 100.0, 60.0, 3.0, 2.5, 0.0, turnStop);
    report("fused stop after 0.5 s", fusedStop);
    CHECK(fusedStop.maxError < 0.1);
    {
        // Overshoot check: display must never pass the final heading.
        AowisCompass::HeadingFilter f;
        CHECK(f.sample(40.0, 0.0));
        double maxDisplay = 0.0;
        double next = 0.01;
        for (int frame = 1; frame <= 240; ++frame)
        {
            const double t = frame / 60.0;
            while (next <= t + 1e-9)
            {
                CHECK(f.sample(turnStop(next), next));
                next += 0.01;
            }
            f.advanceDisplay(t);
            maxDisplay = std::max(maxDisplay, f.continuousDisplayDegrees());
        }
        std::cout << "overshoot " << maxDisplay - 160.0 << '\n';
        CHECK(maxDisplay < 160.0 + 0.3);
        CHECK(!f.moving());
    }

    // --- Frame-rate independence -------------------------------------------
    Run at60 = play(kFusedSensorTuning, 100.0, 60.0, 3.0, 2.9, 0.0, turnStop);
    Run at120 = play(kFusedSensorTuning, 100.0, 120.0, 3.0, 2.9, 0.0, turnStop);
    CHECK(angularError(at60.finalDisplay, at120.finalDisplay) < 0.2);

    // --- Continuous north crossing (unwrapped rose angle) -----------------
    {
        AowisCompass::HeadingFilter wrap;
        CHECK(wrap.sample(350.0, 0.0));
        double nextSample = 0.02;
        double last = wrap.continuousDisplayDegrees();
        for (int frame = 1; frame <= 180; ++frame)
        {
            const double t = frame / 60.0;
            while (nextSample <= t + 1e-8)
            {
                CHECK(wrap.sample(AowisCompass::normalize(350.0 + 45.0 * nextSample), nextSample));
                nextSample += 0.02;
            }
            wrap.advanceDisplay(t);
            const double continuous = wrap.continuousDisplayDegrees();
            CHECK(continuous >= last - 0.02);
            CHECK(continuous - last < 2.0);
            last = continuous;
        }
        CHECK(wrap.continuousDisplayDegrees() > 470.0);
        CHECK(angularError(wrap.displayDegrees(), 125.0) < 2.0);
    }

    // --- Long gap: estimate restarts without a rose jump of 360 deg -------
    {
        AowisCompass::HeadingFilter gap;
        CHECK(gap.sample(10.0, 0.0));
        CHECK(gap.sample(200.0, 5.0));
        CHECK(angularError(gap.targetDegrees(), 200.0) < 1e-9);
        CHECK(std::fabs(gap.continuousTargetDegrees() - gap.continuousDisplayDegrees()) <= 180.0);
    }

    // --- Stationary display does not repaint endlessly -------------------
    {
        AowisCompass::HeadingFilter quiet;
        CHECK(quiet.sample(42.0, 0.0));
        quiet.advanceDisplay(0.0);
        int repaints = 0;
        for (int frame = 1; frame <= 600; ++frame)
        {
            const double t = frame / 60.0;
            CHECK(quiet.sample(42.0, t));
            if (quiet.advanceDisplay(t))
                ++repaints;
        }
        CHECK(repaints == 0);
    }


    // --- AOWIS fusion: TRIAD reproduces poses exactly ---------------------
    for (double heading : {0.0, 45.0, 123.0, 270.0, 359.0})
    {
        for (double pitch : {0.0, 20.0, 60.0, 88.0})
        {
            const Quaternion q = devicePose(heading, pitch, 7.0);
            const Vec g = toDevice(q, {0.0, 0.0, 9.80665});
            const Vec m = toDevice(q, {0.0, 20.0, -44.0});
            AowisCompass::Quaternion measured;
            CHECK(AowisCompass::orientationFromGravityAndField({g.x, g.y, g.z}, {m.x, m.y, m.z}, &measured));
            const double expected = AowisCompass::headingFromRotationVector(q.x, q.y, q.z, q.w, 0);
            const double actual = AowisCompass::headingFromRotationVector(
                measured.x, measured.y, measured.z, measured.w, 0);
            CHECK(angularError(actual, expected) < 1e-6);
        }
    }
    {
        AowisCompass::Quaternion unused;
        CHECK(!AowisCompass::orientationFromGravityAndField({0.0, 0.0, 0.1}, {0.0, 20.0, -44.0}, &unused));
        CHECK(!AowisCompass::orientationFromGravityAndField({0.0, 0.0, 9.8}, {0.0, 0.0, -44.0}, &unused));
    }

    // --- AOWIS fusion: hand-held phone, end to end ------------------------
    const std::function<double(double)> fixed = [](double) { return 73.0; };
    const std::function<double(double)> turning = [](double t) { return 10.0 + 60.0 * std::min(std::max(t - 4.0, 0.0), 3.0); };
    // Still, with 2 deg tremor and 0.8 m/s^2 hand acceleration.
    FusionRun gyroStill = playFusion(true, 10.0, 4.0, fixed, 2.0, 0.8);
    reportFusion("fusion+gyro hand-held still", gyroStill);
    CHECK(gyroStill.meanError < 0.35);
    CHECK(gyroStill.maxError < 0.8);
    FusionRun plainStill = playFusion(false, 10.0, 4.0, fixed, 2.0, 0.8);
    reportFusion("fusion no gyro hand-held still", plainStill);
    CHECK(plainStill.meanError < 1.2);
    CHECK(plainStill.maxError < 3.0);
    // Turning at 60 deg/s, then holding.
    FusionRun gyroTurn = playFusion(true, 10.0, 4.5, turning, 1.0, 0.3);
    reportFusion("fusion+gyro turn 60 deg/s and stop", gyroTurn);
    CHECK(gyroTurn.maxError < 2.0);
    FusionRun gyroSettled = playFusion(true, 10.0, 7.5, turning, 1.0, 0.3);
    reportFusion("fusion+gyro settled", gyroSettled);
    CHECK(gyroSettled.maxError < 0.6);
    FusionRun plainSettled = playFusion(false, 12.0, 9.0, turning, 1.0, 0.3);
    reportFusion("fusion no gyro settled after turn", plainSettled);
    CHECK(plainSettled.maxError < 2.0);

    std::cout << "compass heading tests passed\n";
    return 0;
}
