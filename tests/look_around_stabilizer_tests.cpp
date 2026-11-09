#include "look_around_stabilizer.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int g_checks = 0;
int g_failures = 0;

typedef AowisCompass::Quaternion Quaternion;
typedef AowisMobileMap::LookAroundStabilizer Stabilizer;
typedef AowisMobileMap::LookAroundSample Sample;

constexpr double kDegree = AowisCompass::kPi / 180.0;
constexpr double kStep = 0.01; // 100 Hz sensor

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

Quaternion axisAngle(double x, double y, double z, double degrees)
{
    const double half = 0.5 * degrees * kDegree;
    return {x * std::sin(half), y * std::sin(half), z * std::sin(half), std::cos(half)};
}

using AowisCompass::multiply;

Quaternion rotate(const Quaternion &q, const Quaternion &vector)
{
    return multiply(multiply(q, vector), Stabilizer::conjugate(q));
}

// Angle between the camera axes (device -Z) of two poses.
double viewDegrees(const Quaternion &a, const Quaternion &b)
{
    const Quaternion va = rotate(a, {0.0, 0.0, -1.0, 0.0});
    const Quaternion vb = rotate(b, {0.0, 0.0, -1.0, 0.0});
    const double dot = va.x * vb.x + va.y * vb.y + va.z * vb.z;
    return std::acos(std::clamp(dot, -1.0, 1.0)) / kDegree;
}

// Complete rotation angle between two poses.
double poseDegrees(const Quaternion &a, const Quaternion &b)
{
    const Quaternion e = Stabilizer::shortest(multiply(Stabilizer::conjugate(a), b));
    return 2.0 * std::atan2(std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), e.w) / kDegree;
}

// Phone upright, camera facing north.
Quaternion horizon()
{
    return axisAngle(1.0, 0.0, 0.0, 90.0);
}

// Compass pose only: it drives the view.
Sample sample(const Quaternion &absolute, double seconds, double magnification)
{
    Sample s;
    s.absolute = absolute;
    s.absoluteSeconds = seconds;
    s.magnification = magnification;
    return s;
}

// Compass and gyroscope pose with their own timestamps.
Sample sample(const Quaternion &absolute, double absoluteSeconds,
              const Quaternion &relative, double relativeSeconds, double magnification)
{
    Sample s = sample(absolute, absoluteSeconds, magnification);
    s.relative = relative;
    s.relativeSeconds = relativeSeconds;
    s.hasRelative = true;
    return s;
}

Sample sample(const Quaternion &absolute, const Quaternion &relative, double seconds, double magnification)
{
    return sample(absolute, seconds, relative, seconds, magnification);
}

double tremor(double seconds)
{
    return 0.1 * std::sin(2.0 * AowisCompass::kPi * 9.0 * seconds);
}
}

int main()
{
    {
        Stabilizer stabilizer;
        const Quaternion first = axisAngle(0.0, 0.0, 1.0, 37.0);
        expectTrue(poseDegrees(stabilizer.update(sample(first, 1.0, 8.0)), first) < 1e-9,
                   "first sample is used immediately");
        const Quaternion later = axisAngle(0.0, 0.0, 1.0, 80.0);
        expectTrue(poseDegrees(stabilizer.update(sample(later, 1.5, 8.0)), later) < 1e-9,
                   "a sensor gap restarts without smoothing across it");
        expectTrue(poseDegrees(stabilizer.update(sample(first, 1.5, 8.0)), later) < 1e-9,
                   "a repeated timestamp keeps the previous estimate");
    }

    {
        Stabilizer stabilizer;
        Quaternion out;
        const Quaternion start = horizon();
        const Quaternion moved = multiply(axisAngle(0.0, 0.0, 1.0, 2.0), start);
        stabilizer.update(sample(start, 0.0, 8.0));
        for (int i = 1; i <= 400; ++i)
            out = stabilizer.update(sample(moved, i * kStep, 8.0));
        expectTrue(poseDegrees(out, moved) < 0.001, "at rest the estimate converges exactly: no dead band");
    }

    {
        Stabilizer stabilizer;
        double worst = 0.0;
        for (int i = 0; i < 600; ++i)
        {
            const double t = i * kStep;
            const Quaternion measured = multiply(horizon(), axisAngle(1.0, 0.0, 0.0, tremor(t)));
            stabilizer.update(sample(measured, t, 8.0));
            const Quaternion out = stabilizer.poseForFrame(t + 0.010);
            if (i > 300)
                worst = std::max(worst, viewDegrees(out, horizon()));
        }
        expectTrue(worst < 0.025, "8x damps 9 Hz hand tremor to a quarter or less");
    }

    {
        // The old error-driven filter trailed a 4 deg/s pan at 8x by ~1 deg.
        Stabilizer stabilizer;
        double worst = 0.0;
        for (int i = 0; i < 600; ++i)
        {
            const double t = i * kStep;
            const Quaternion truth = multiply(axisAngle(0.0, 0.0, 1.0, 4.0 * t), horizon());
            const Quaternion out = stabilizer.update(sample(truth, t, 8.0));
            if (i > 300)
                worst = std::max(worst, viewDegrees(out, truth));
        }
        expectTrue(worst < 0.15, "slow 8x pan follows within 0.15 deg");
    }

    {
        Stabilizer stabilizer;
        double worst = 0.0;
        for (int i = 0; i < 300; ++i)
        {
            const double t = i * kStep;
            const Quaternion truth = multiply(axisAngle(0.0, 0.0, 1.0, 120.0 * t), horizon());
            const Quaternion out = stabilizer.update(sample(truth, t, 1.0));
            if (i > 100)
                worst = std::max(worst, viewDegrees(out, truth));
        }
        expectTrue(worst < 0.5, "fast 1x turn follows within half a degree");
    }

    {
        // Game rotation vector: arbitrary heading reference, slow drift.
        // Rotation vector: correct on average, magnetometer noise on top.
        Stabilizer stabilizer;
        std::mt19937 random(3);
        std::normal_distribution<double> white(0.0, 0.3);
        double worstRaw = 0.0;
        double worstOut = 0.0;
        for (int i = 0; i < 6000; ++i)
        {
            const double t = i * kStep;
            const Quaternion truth = multiply(axisAngle(0.0, 0.0, 1.0, 20.0 * std::sin(0.3 * t)), horizon());
            const double noise = 0.5 * std::sin(2.0 * AowisCompass::kPi * t) + white(random);
            const Quaternion absolute = multiply(axisAngle(0.0, 0.0, 1.0, noise), truth);
            const Quaternion relative = multiply(axisAngle(0.0, 0.0, 1.0, -73.0 + 0.5 / 60.0 * t), truth);
            const Quaternion out = stabilizer.update(sample(absolute, relative, t, 8.0));
            if (i > 1000)
            {
                worstRaw = std::max(worstRaw, viewDegrees(absolute, truth));
                worstOut = std::max(worstOut, viewDegrees(out, truth));
            }
        }
        expectTrue(stabilizer.usesRelativePose(), "game rotation vector drives the pose");
        expectTrue(worstOut < 0.2 * worstRaw, "magnetometer heading noise is attenuated fivefold or more");
        expectTrue(worstOut < 0.25, "heading stays on the compass mean despite gyroscope frame offset");
    }

    {
        Stabilizer stabilizer;
        const Quaternion truth = horizon();
        Quaternion out;
        for (int i = 0; i < 100; ++i)
            out = stabilizer.update(sample(truth, multiply(axisAngle(0.0, 0.0, 1.0, 40.0), truth), i * kStep, 1.0));
        expectTrue(poseDegrees(out, truth) < 1e-6, "first relative sample is aligned to the compass heading");
        // The OS resets the game rotation vector's heading reference.
        const Quaternion reset = multiply(axisAngle(0.0, 0.0, 1.0, -120.0), truth);
        out = stabilizer.update(sample(truth, reset, 1.0, 1.0));
        expectTrue(poseDegrees(out, truth) < 0.5, "a gyroscope frame reset snaps instead of swinging the view");
    }

    {
        // Equal 2 deg steps about the view axis (roll) and across it (pitch).
        Stabilizer roll;
        Stabilizer pitch;
        const Quaternion start = horizon();
        const Quaternion rolled = multiply(start, axisAngle(0.0, 0.0, 1.0, 2.0));
        const Quaternion pitched = multiply(start, axisAngle(1.0, 0.0, 0.0, 2.0));
        roll.update(sample(start, 0.0, 4.0));
        pitch.update(sample(start, 0.0, 4.0));
        Quaternion rollOut;
        Quaternion pitchOut;
        double rollPointing = 0.0;
        for (int i = 1; i <= 30; ++i)
        {
            rollOut = roll.update(sample(rolled, i * kStep, 4.0));
            pitchOut = pitch.update(sample(pitched, i * kStep, 4.0));
            rollPointing = std::max(rollPointing, viewDegrees(rollOut, start));
        }
        expectTrue(poseDegrees(rollOut, rolled) > 1.5 * poseDegrees(pitchOut, pitched),
                   "roll is smoothed more strongly than pointing");
        expectTrue(rollPointing < 1e-4, "roll smoothing never moves the crosshair");
    }

    {
        Stabilizer a;
        Stabilizer b;
        const Quaternion start = horizon();
        const Quaternion next = multiply(axisAngle(0.0, 1.0, 0.0, 0.3), start);
        const Quaternion negated{-next.x, -next.y, -next.z, -next.w};
        a.update(sample(start, 0.0, 8.0));
        b.update(sample(start, 0.0, 8.0));
        expectTrue(poseDegrees(a.update(sample(next, kStep, 8.0)), b.update(sample(negated, kStep, 8.0))) < 1e-9,
                   "equivalent quaternion signs take the same short path");
    }

    for (double lens : {1.0, 8.0})
    {
        Stabilizer filter;
        Quaternion measured;
        Quaternion filtered;
        const double speed = lens == 1.0 ? 90.0 : 4.0;
        const double t = 1.0;
        for (int i = 0; i <= 100; ++i)
        {
            measured = multiply(axisAngle(0, 0, 1, speed * i * kStep), horizon());
            filtered = filter.update(sample(measured, i * kStep, lens));
        }
        const Quaternion displayed = filter.poseForFrame(t + 0.008);
        const Quaternion truth = multiply(axisAngle(0, 0, 1, speed * (t + 0.016)), horizon());
        const double baseError = viewDegrees(filtered, truth);
        const double frameError = viewDegrees(displayed, truth);
        std::printf("%.0fx pan %.0f deg/s: filtered %.4f deg, predicted %.4f deg\n",
                    lens, speed, baseError, frameError);
        expectTrue(frameError < 0.8 * baseError, "frame prediction reduces tracking error during a pan");
        expectTrue(poseDegrees(filter.poseForFrame(t + 0.060), filtered) < 1e-9,
                   "a stalled stream cannot extrapolate");
        expectTrue(poseDegrees(filter.poseForFrame(t - 0.001), filtered) < 1e-9,
                   "future sensor timestamps cannot extrapolate");
        expectTrue(poseDegrees(displayed, filter.poseForFrame(t + 0.008)) < 1e-9,
                   "sampling other frames does not alter the estimate");
        filtered = filter.update(sample(measured, t + kStep, lens));
        expectTrue(poseDegrees(filter.poseForFrame(t + 0.018), filtered) < 1e-9,
                   "a stop cancels prediction on its first sample");
        measured = multiply(axisAngle(0, 0, 1, speed * (t - kStep)), horizon());
        filtered = filter.update(sample(measured, t + 2 * kStep, lens));
        expectTrue(poseDegrees(filter.poseForFrame(t + 0.028), filtered) < 1e-9,
                   "a reversal cannot continue the previous prediction");
        filtered = filter.update(sample(measured, measured, t + 3 * kStep, lens));
        expectTrue(poseDegrees(filter.poseForFrame(t + 0.038), filtered) < 1e-9,
                   "a source switch clears prediction");
        filter.reset();
        expectTrue(!filter.hasPose(), "resume and mode entry require a fresh sample");
    }

    {
        // Slow 8x pan with tremor: a lead would follow the tremor.
        Stabilizer stabilizer;
        double lead = 0.0;
        for (int i = 0; i < 600; ++i)
        {
            const double t = i * kStep;
            const Quaternion truth = multiply(axisAngle(0.0, 0.0, 1.0, 1.5 * t), horizon());
            const Quaternion measured = multiply(truth, axisAngle(1.0, 0.0, 0.0, tremor(t)));
            const Quaternion filtered = stabilizer.update(sample(measured, t, 8.0));
            if (i > 300)
                lead = std::max(lead, poseDegrees(stabilizer.poseForFrame(t + 0.010), filtered));
        }
        expectTrue(lead < 1e-6, "no frame prediction below tremor-scale turn rates");
    }

    for (double skewMs : {5.0, 10.0})
    {
        // The game rotation vector sample is older than the compass sample:
        // during a turn that reads as a heading offset of rate * skew.
        Stabilizer stabilizer;
        double worst = 0.0;
        for (int i = 0; i < 900; ++i)
        {
            const double t = i * kStep;
            const double gyroTime = t - skewMs * 1e-3;
            const double yaw = t < 3.0 ? 0.0 : t < 6.0 ? 90.0 * (t - 3.0) : 270.0;
            const double gyroYaw = gyroTime < 3.0 ? 0.0 : gyroTime < 6.0 ? 90.0 * (gyroTime - 3.0) : 270.0;
            const Quaternion absolute = multiply(axisAngle(0.0, 0.0, 1.0, yaw), horizon());
            const Quaternion relative = multiply(axisAngle(0.0, 0.0, 1.0, gyroYaw - 40.0), horizon());
            stabilizer.update(sample(absolute, t, relative, gyroTime, 8.0));
            if (t > 3.0)
                worst = std::max(worst, std::fabs(std::remainder(
                    stabilizer.headingOffsetRadians() / kDegree - 40.0, 360.0)));
        }
        expectTrue(worst < 0.1, "compass/gyroscope skew does not read as a heading offset");
    }

    {
        // One compass sample without a paired gyroscope pose, carrying 1.5 deg
        // of magnetometer noise, must not be shown raw.
        Stabilizer stabilizer;
        const Quaternion truth = horizon();
        for (int i = 0; i < 300; ++i)
            stabilizer.update(sample(truth, truth, i * kStep, 8.0));
        const Quaternion noisy = multiply(axisAngle(0.0, 0.0, 1.0, 1.5), truth);
        double worst = poseDegrees(stabilizer.update(sample(noisy, 3.0, 8.0)), truth);
        for (int i = 1; i <= 50; ++i)
            worst = std::max(worst, poseDegrees(stabilizer.update(sample(truth, truth, 3.0 + i * kStep, 8.0)), truth));
        expectTrue(worst < 0.2, "a missed sensor pairing is smoothed, not shown raw");
        // Alternating pairing must not freeze the view.
        const Quaternion turned = multiply(axisAngle(0.0, 0.0, 1.0, 5.0), truth);
        Quaternion out;
        for (int i = 1; i <= 400; ++i)
        {
            const double t = 4.0 + i * kStep;
            out = i % 2 == 0 ? stabilizer.update(sample(turned, turned, t, 8.0))
                             : stabilizer.update(sample(turned, t, 8.0));
        }
        expectTrue(poseDegrees(out, turned) < 0.05, "alternating sensor pairing still converges");
    }

    {
        // Gyroscope at 200 Hz, compass at 25 Hz and then stalled for a
        // second: the view advances on every gyroscope sample regardless.
        Stabilizer stabilizer;
        const Quaternion offset = axisAngle(0.0, 0.0, 1.0, -30.0);
        double compassSeconds = 0.0;
        Quaternion compass = horizon();
        int advances = 0;
        double worst = 0.0;
        double previousMotion = -1.0;
        for (int i = 0; i < 600; ++i)
        {
            const double t = i * 0.005;
            const Quaternion truth = multiply(axisAngle(0.0, 0.0, 1.0, t < 1.0 ? 0.0 : 20.0 * (t - 1.0)), horizon());
            if (i % 8 == 0 && (t < 1.5 || t > 2.5))
            {
                compass = truth;
                compassSeconds = t;
            }
            const Quaternion out = stabilizer.update(sample(compass, compassSeconds, multiply(offset, truth), t, 1.0));
            if (stabilizer.motionSeconds() > previousMotion)
                ++advances;
            previousMotion = stabilizer.motionSeconds();
            if (t > 1.5 && t < 2.5)
                worst = std::max(worst, viewDegrees(out, truth));
        }
        expectTrue(advances == 600, "every gyroscope sample advances the view");
        expectTrue(worst < 0.5, "a stalled compass neither stops nor misleads the view");
    }

    {
        Stabilizer stabilizer;
        const Quaternion truth = horizon();
        for (int i = 0; i < 100; ++i)
            stabilizer.update(sample(truth, truth, i * kStep, 8.0));
        const Quaternion before = stabilizer.poseForFrame(-1.0);
        const double motion = stabilizer.motionSeconds();
        const Quaternion noisy = multiply(axisAngle(0.0, 0.0, 1.0, 2.0), truth);
        const Quaternion out = stabilizer.update(sample(noisy, 0.995, truth, 0.99, 8.0));
        expectTrue(stabilizer.motionSeconds() == motion && poseDegrees(out, before) < 1e-9,
                   "a compass-only reading never steps the view");
    }

    expectTrue(Stabilizer::cutoffHz(0.0, 8.0) < Stabilizer::cutoffHz(0.0, 1.0), "rest cut-off falls with magnification");
    expectTrue(Stabilizer::cutoffHz(10.0, 8.0) > Stabilizer::cutoffHz(1.0, 8.0), "cut-off rises with angular speed");
    expectTrue(Stabilizer::cutoffHz(1e6, 8.0) <= Stabilizer::kMaxCutoffHz, "cut-off is bounded");
    expectTrue(std::isfinite(Stabilizer::cutoffHz(0.0, std::nan(""))), "invalid magnification is treated as 1x");

    std::printf("look_around_stabilizer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
