#include "look_around_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace
{
int g_checks = 0;
int g_failures = 0;

typedef AowisCompass::Quaternion Quaternion;
typedef AowisMobileMap::LookAroundEngine Engine;
using AowisCompass::multiply;

constexpr double kDegree = AowisCompass::kPi / 180.0;

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

Quaternion rotate(const Quaternion &q, const Quaternion &vector)
{
    return multiply(multiply(q, vector), Engine::conjugate(q));
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
    const Quaternion e = Engine::shortest(multiply(Engine::conjugate(a), b));
    return 2.0 * std::atan2(std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), e.w) / kDegree;
}

// Compass bearing of the camera axis, degrees clockwise from north.
double bearing(const Quaternion &q)
{
    const Quaternion v = rotate(q, {0.0, 0.0, -1.0, 0.0});
    return std::atan2(v.x, v.y) / kDegree;
}

double wrapDegrees(double degrees)
{
    return std::remainder(degrees, 360.0);
}

// Phone upright, camera facing north, then turned clockwise by 'bearingDegrees'.
Quaternion facing(double bearingDegrees)
{
    return multiply(axisAngle(0.0, 0.0, 1.0, -bearingDegrees), axisAngle(1.0, 0.0, 0.0, 90.0));
}

// The game rotation vector: the true pose in a frame turned by -30 deg.
Quaternion gyroOf(const Quaternion &truth)
{
    return multiply(axisAngle(0.0, 0.0, 1.0, -30.0), truth);
}

struct Delivery
{
    double gyroHz = 100.0;
    double compassHz = 100.0;
    double frameHz = 60.0;
    double duration = 6.0;
    // Gyroscope events are flushed in batches of 1-3, each batch arriving
    // latency + jitter * U(0,1) after its newest event.
    double latency = 0.006;
    double jitter = 0.015;
    double compassSkew = 0.003;      // compass timestamps relative to the gyro grid
    double clockOffset = 0.0;        // arrival/frame clock minus sensor clock
    double compassStallStart = -1.0; // compass silent in [start, end)
    double compassStallEnd = -1.0;
    double gyroStallStart = -1.0;    // gyroscope silent in [start, end)
    double gyroStallEnd = -1.0;
    bool gyro = true;
    unsigned seed = 11;
};

struct Event
{
    double seconds;
    double arrival;
    bool compass;
};

// Runs the engine over a scenario; 'frame' sees each frame time (sensor
// clock) and the pose shown.
void run(Engine &engine, const Delivery &d, const std::function<Quaternion(double)> &truth,
         const std::function<Quaternion(double, const Quaternion &)> &compassReading,
         const std::function<void(double, const Quaternion &)> &frame)
{
    std::mt19937 random(d.seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::vector<Event> events;
    if (d.gyro)
    {
        std::vector<double> batch;
        int batchSize = 1 + int(uniform(random) * 3.0);
        for (double t = 0.0; t < d.duration; t += 1.0 / d.gyroHz)
        {
            if (t >= d.gyroStallStart && t < d.gyroStallEnd)
                continue;
            batch.push_back(t);
            if (int(batch.size()) >= batchSize)
            {
                const double arrival = batch.back() + d.latency + d.jitter * uniform(random);
                for (double seconds : batch)
                    events.push_back({seconds, arrival, false});
                batch.clear();
                batchSize = 1 + int(uniform(random) * 3.0);
            }
        }
    }
    for (double t = d.compassSkew; t < d.duration; t += 1.0 / d.compassHz)
    {
        if (t >= d.compassStallStart && t < d.compassStallEnd)
            continue;
        events.push_back({t, t + 0.008 + 0.01 * uniform(random), true});
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const Event &a, const Event &b) { return a.arrival < b.arrival; });
    std::size_t next = 0;
    for (int index = 0; index < int(d.duration * d.frameHz); ++index)
    {
        const double now = index / d.frameHz;
        while (next < events.size() && events[next].arrival <= now)
        {
            const Event &event = events[next++];
            const Quaternion pose = truth(event.seconds);
            if (event.compass)
                engine.addCompass(compassReading(event.seconds, pose), event.seconds,
                                  event.arrival + d.clockOffset);
            else
                engine.addGyro(gyroOf(pose), event.seconds, event.arrival + d.clockOffset);
        }
        if (engine.hasPose())
            frame(now, engine.poseForFrame(now + d.clockOffset));
    }
}

Quaternion exactCompass(double, const Quaternion &pose)
{
    return pose;
}

// Smooth hand stop: constant speed, then a cosine deceleration.
double panDegrees(double t, double start, double end, double speed, double brake)
{
    if (t < start)
        return 0.0;
    if (t < end)
        return speed * (t - start);
    const double s = t - end;
    if (brake <= 0.0)
        return speed * (end - start);
    if (s >= brake)
        return speed * (end - start + 0.5 * brake);
    return speed * (end - start + 0.5 * (s + brake / AowisCompass::kPi * std::sin(AowisCompass::kPi * s / brake)));
}

struct FrameStats
{
    std::vector<double> steps;
    double previous = std::nan("");
    double worstLag = 0.0;
    double overshoot = 0.0;
    int backward = 0;

    double jitter() const
    {
        double mean = 0.0;
        for (double step : this->steps)
            mean += step;
        mean /= double(this->steps.size());
        double variance = 0.0;
        for (double step : this->steps)
            variance += (step - mean) * (step - mean);
        return std::sqrt(variance / double(this->steps.size())) / std::fabs(mean);
    }
};
}

int main()
{
    {
        Engine engine;
        engine.addGyro(gyroOf(facing(10.0)), 0.00, 0.01);
        engine.addGyro(gyroOf(facing(10.0)), 0.01, 0.02);
        expectTrue(!engine.hasPose(), "no pose before the compass gives north");
        engine.addCompass(facing(10.0), 0.012, 0.02);
        engine.addGyro(gyroOf(facing(10.0)), 0.02, 0.03);
        expectTrue(engine.hasPose() && engine.usesGyro(), "gyroscope drives once north is known");
        expectTrue(std::fabs(wrapDegrees(bearing(engine.corePose()) - 10.0)) < 1e-6,
                   "first alignment takes the compass heading exactly");
    }

    for (double rate : {100.0, 200.0})
    {
        // Steady 20 deg/s pan under batched, jittery delivery.
        Engine engine;
        Delivery d;
        d.gyroHz = rate;
        d.frameHz = rate == 100.0 ? 60.0 : 120.0;
        FrameStats stats;
        run(engine, d, [](double t) { return facing(panDegrees(t, 1.0, 5.0, 20.0, 0.0)); }, exactCompass,
            [&stats](double now, const Quaternion &shown) {
                const double b = bearing(shown);
                if (now > 2.0 && now < 4.9)
                {
                    const double step = wrapDegrees(b - stats.previous);
                    stats.steps.push_back(step);
                    if (step < 0.0)
                        ++stats.backward;
                }
                stats.previous = b;
            });
        std::printf("pan 20 deg/s, %.0f Hz sensor: frame step jitter %.2f %%\n", rate, 100.0 * stats.jitter());
        expectTrue(stats.jitter() < 0.03 && stats.backward == 0,
                   "batched, jittery samples still give evenly advancing frames");
    }

    {
        // Same motion at 100 and 400 Hz: the fixed-step core gives the same path.
        Engine slow;
        Engine fast;
        Delivery d;
        d.duration = 3.0;
        d.jitter = 0.0;
        const std::function<Quaternion(double)> truth = [](double t) {
            return multiply(facing(panDegrees(t, 0.5, 2.0, 30.0, 0.2)), axisAngle(1.0, 0.0, 0.0, 3.0 * std::sin(4.0 * t)));
        };
        d.gyroHz = 100.0;
        run(slow, d, truth, exactCompass, [](double, const Quaternion &) {});
        d.gyroHz = 400.0;
        run(fast, d, truth, exactCompass, [](double, const Quaternion &) {});
        const double common = std::min(slow.coreSeconds(), fast.coreSeconds()) - 0.05;
        expectTrue(poseDegrees(slow.corePoseAt(common), fast.corePoseAt(common)) < 0.05,
                   "the core gives the same motion at 100 and 400 Hz sensor rates");
    }

    {
        // Frames only read: extra queries, or another frame rate, leave the
        // core as it is.
        Engine plain;
        Engine queried;
        Engine slowFrames;
        Delivery d;
        d.duration = 2.0;
        const std::function<Quaternion(double)> truth = [](double t) { return facing(panDegrees(t, 0.5, 2.0, 15.0, 0.0)); };
        run(plain, d, truth, exactCompass, [](double, const Quaternion &) {});
        run(queried, d, truth, exactCompass, [&queried](double now, const Quaternion &) {
            for (int i = 0; i < 5; ++i)
                queried.peekFrame(now + 0.001 * i);
        });
        d.frameHz = 7.0;
        run(slowFrames, d, truth, exactCompass, [](double, const Quaternion &) {});
        expectTrue(queried.coreSeconds() == plain.coreSeconds() &&
                   poseDegrees(queried.corePose(), plain.corePose()) < 1e-12,
                   "frame queries never change the core");
        const double common = std::min(slowFrames.coreSeconds(), plain.coreSeconds()) - 0.05;
        expectTrue(poseDegrees(slowFrames.corePoseAt(common), plain.corePoseAt(common)) < 1e-9,
                   "the frame rate never changes the core");
    }

    {
        // Sensor and frame clocks may differ by any constant offset.
        Engine plain;
        Engine offset;
        Delivery d;
        d.duration = 3.0;
        std::vector<Quaternion> a;
        std::vector<Quaternion> b;
        const std::function<Quaternion(double)> truth = [](double t) { return facing(panDegrees(t, 1.0, 3.0, 20.0, 0.0)); };
        run(plain, d, truth, exactCompass, [&a](double, const Quaternion &q) { a.push_back(q); });
        d.clockOffset = 1000.0;
        run(offset, d, truth, exactCompass, [&b](double, const Quaternion &q) { b.push_back(q); });
        double worst = 0.0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            worst = std::max(worst, viewDegrees(a[i], b[i]));
        expectTrue(a.size() == b.size() && worst < 0.05, "a clock offset between sensor and frame clocks cancels");
    }

    {
        Engine engine;
        engine.setMagnification(8.0);
        Delivery d;
        d.duration = 9.0;
        run(engine, d, [](double t) { return t < 1.0 ? facing(0.0) : facing(2.0); }, exactCompass,
            [](double, const Quaternion &) {});
        expectTrue(viewDegrees(engine.corePose(), facing(2.0)) < 0.001,
                   "at rest the estimate converges exactly: no dead band");
    }

    {
        Engine engine;
        engine.setMagnification(8.0);
        Delivery d;
        double worst = 0.0;
        run(engine, d, [](double t) {
                return multiply(facing(0.0), axisAngle(1.0, 0.0, 0.0, 0.1 * std::sin(2.0 * AowisCompass::kPi * 9.0 * t)));
            }, exactCompass,
            [&worst](double now, const Quaternion &shown) {
                if (now > 2.0)
                    worst = std::max(worst, viewDegrees(shown, facing(0.0)));
            });
        expectTrue(worst < 0.025, "8x damps 9 Hz hand tremor to a quarter or less");
    }

    {
        // Slow 8x pan, measured against the true pose when the frame is seen.
        Engine engine;
        engine.setMagnification(8.0);
        Delivery d;
        double worst = 0.0;
        run(engine, d, [](double t) { return facing(panDegrees(t, 1.0, 5.0, 4.0, 0.0)); }, exactCompass,
            [&worst](double now, const Quaternion &shown) {
                if (now > 2.5 && now < 4.9)
                    worst = std::max(worst, std::fabs(wrapDegrees(bearing(shown) - panDegrees(now + 0.008, 1.0, 5.0, 4.0, 0.0))));
            });
        std::printf("8x pan 4 deg/s: worst lag %.3f deg\n", worst);
        expectTrue(worst < 0.35, "slow 8x pan follows within 0.35 deg");
    }

    for (double brake : {0.15, 0.0})
    {
        // 20 deg/s at 1x, then a natural (150 ms) or abrupt stop.
        Engine engine;
        Delivery d;
        double overshoot = 0.0;
        const double final = panDegrees(10.0, 1.0, 3.0, 20.0, brake);
        run(engine, d, [brake](double t) { return facing(panDegrees(t, 1.0, 3.0, 20.0, brake)); }, exactCompass,
            [&overshoot, final](double now, const Quaternion &shown) {
                if (now > 3.0)
                    overshoot = std::max(overshoot, wrapDegrees(bearing(shown) - final));
            });
        std::printf("stop from 20 deg/s, %.2f s brake: overshoot %.3f deg\n", brake, overshoot);
        expectTrue(overshoot < (brake > 0.0 ? 0.08 : 0.25), "a stop does not visibly overshoot");
    }

    for (double skew : {-0.008, 0.004, 0.009})
    {
        // Compass and gyroscope a few ms apart during fast turns.
        Engine engine;
        engine.setHeadingFromCompass(false);
        Delivery d;
        d.compassSkew = skew;
        d.jitter = 0.0;
        double worst = 0.0;
        run(engine, d, [](double t) { return facing(panDegrees(t, 1.0, 4.0, 90.0, 0.0)); }, exactCompass,
            [&engine, &worst](double, const Quaternion &) {
                worst = std::max(worst, std::fabs(wrapDegrees(engine.targetHeadingOffsetRadians() / kDegree - 30.0)));
            });
        expectTrue(worst < 0.05, "compass/gyroscope skew does not read as a heading offset");
    }

    {
        // Slow-north mode: magnetometer noise barely moves the heading offset.
        Engine engine;
        engine.setHeadingFromCompass(false);
        Delivery d;
        d.duration = 20.0;
        std::mt19937 random(5);
        std::normal_distribution<double> white(0.0, 0.3);
        double worst = 0.0;
        run(engine, d, [](double) { return facing(0.0); },
            [&random, &white](double t, const Quaternion &pose) {
                return multiply(axisAngle(0.0, 0.0, 1.0, 0.5 * std::sin(2.0 * AowisCompass::kPi * t) + white(random)), pose);
            },
            [&worst](double now, const Quaternion &shown) {
                if (now > 5.0)
                    worst = std::max(worst, viewDegrees(shown, facing(0.0)));
            });
        expectTrue(worst < 0.15, "magnetometer noise is attenuated more than fivefold");
    }

    {
        // Slow-north mode: the compass disagrees by 1 deg from t = 2 s. At
        // rest at 8x the correction creeps very slowly; once panning it is
        // made quickly.
        Engine engine;
        engine.setHeadingFromCompass(false);
        engine.setMagnification(8.0);
        Delivery d;
        d.duration = 12.0;
        double atRest = 0.0;
        double panned = 0.0;
        run(engine, d, [](double t) { return facing(panDegrees(t, 8.0, 12.0, 10.0, 0.0)); },
            [](double t, const Quaternion &pose) { return t < 2.0 ? pose : multiply(axisAngle(0.0, 0.0, 1.0, -1.0), pose); },
            [&engine, &atRest, &panned](double now, const Quaternion &) {
                const double corrected = -wrapDegrees(engine.headingOffsetRadians() / kDegree - 30.0);
                if (now < 8.0)
                    atRest = corrected;
                else
                    panned = corrected;
            });
        std::printf("1 deg compass correction at 8x: %.3f deg after 6 s at rest, %.3f deg after 4 s of panning\n",
                    atRest, panned);
        expectTrue(atRest > 0.05 && atRest < 0.4, "at rest north is corrected by a barely visible creep");
        expectTrue(panned > 0.9, "while panning north is corrected quickly");
    }

    {
        // Compass silent for a second: the view keeps moving on the gyroscope.
        Engine engine;
        Delivery d;
        d.compassStallStart = 2.0;
        d.compassStallEnd = 3.0;
        double worst = 0.0;
        double coreAtStallStart = -1.0;
        double coreAtStallEnd = -1.0;
        run(engine, d, [](double t) { return facing(panDegrees(t, 1.0, 5.0, 20.0, 0.0)); }, exactCompass,
            [&](double now, const Quaternion &shown) {
                if (now > 2.1 && now < 3.0)
                {
                    if (coreAtStallStart < 0.0)
                        coreAtStallStart = engine.coreSeconds();
                    coreAtStallEnd = engine.coreSeconds();
                    worst = std::max(worst, std::fabs(wrapDegrees(bearing(shown) - panDegrees(now + 0.008, 1.0, 5.0, 20.0, 0.0))));
                }
            });
        const bool advancing = coreAtStallEnd - coreAtStallStart > 0.8;
        expectTrue(advancing && engine.usesGyro(), "a stalled compass does not stop the view");
        expectTrue(worst < 1.2, "a stalled compass does not mislead the view");
    }

    {
        // The OS resets the game rotation vector frame by 120 deg about Up.
        Engine engine;
        double worstStep = 0.0;
        Quaternion previous;
        bool hasPrevious = false;
        for (int i = 0; i < 300; ++i)
        {
            const double t = i * 0.01;
            const Quaternion truth = facing(5.0 * t);
            const Quaternion frame = t < 1.5 ? axisAngle(0.0, 0.0, 1.0, -30.0) : axisAngle(0.0, 0.0, 1.0, 90.0);
            engine.addGyro(multiply(frame, truth), t, t + 0.01);
            if (i % 5 == 0)
                engine.addCompass(truth, t + 0.002, t + 0.012);
            if (engine.hasPose())
            {
                const Quaternion shown = engine.poseForFrame(t + 0.01);
                if (hasPrevious)
                    worstStep = std::max(worstStep, viewDegrees(shown, previous));
                previous = shown;
                hasPrevious = true;
            }
        }
        expectTrue(worstStep < 0.2, "a gyroscope frame reset keeps the view continuous");
    }

    {
        // No game rotation vector: the compass drives the view, still smoothly.
        Engine engine;
        Delivery d;
        d.gyro = false;
        FrameStats stats;
        run(engine, d, [](double t) { return facing(panDegrees(t, 1.0, 5.0, 20.0, 0.0)); }, exactCompass,
            [&stats](double now, const Quaternion &shown) {
                const double b = bearing(shown);
                if (now > 2.0 && now < 4.9)
                    stats.steps.push_back(wrapDegrees(b - stats.previous));
                stats.previous = b;
            });
        expectTrue(!engine.usesGyro() && stats.jitter() < 0.05, "without a gyroscope the compass drives smoothly");
    }

    for (int gyroInput = 0; gyroInput < 2; ++gyroInput)
    {
        // No real gyroscope: the heading of the driving input (the compass,
        // or a vendor-emulated game rotation vector) is magnetometer noise of
        // 0.5 deg rms; tilt is clean. Rest, a 30 deg/s pan, rest.
        std::mt19937 random(21);
        std::normal_distribution<double> white(0.0, 0.5);
        const std::function<Quaternion(const Quaternion &)> noisy = [&random, &white](const Quaternion &pose) {
            return multiply(axisAngle(0.0, 0.0, 1.0, white(random)), pose);
        };
        Engine engine;
        Delivery d;
        d.gyro = gyroInput != 0;
        d.duration = 6.0;
        FrameStats rest;
        FrameStats pan;
        double lag = 0.0;
        int lagFrames = 0;
        double worstPitch = 0.0;
        const std::function<Quaternion(double)> truth = [](double t) { return facing(panDegrees(t, 3.0, 5.5, 30.0, 0.15)); };
        const std::function<Quaternion(double, const Quaternion &)> compass =
            [&noisy, gyroInput](double, const Quaternion &pose) { return gyroInput != 0 ? pose : noisy(pose); };
        std::function<Quaternion(double)> driving = truth;
        if (gyroInput != 0)
            driving = [&noisy, &truth](double t) { return noisy(truth(t)); };
        run(engine, d, driving, compass, [&](double now, const Quaternion &shown) {
            const double b = bearing(shown);
            if (now > 1.5 && now < 3.0)
                rest.steps.push_back(wrapDegrees(b - rest.previous));
            if (now > 4.0 && now < 5.4)
            {
                pan.steps.push_back(wrapDegrees(b - pan.previous));
                lag += panDegrees(now, 3.0, 5.5, 30.0, 0.15) - b;
                ++lagFrames;
            }
            if (now > 1.0)
            {
                const Quaternion v = rotate(shown, {0.0, 0.0, -1.0, 0.0});
                worstPitch = std::max(worstPitch, std::fabs(std::asin(std::clamp(v.z, -1.0, 1.0)) / kDegree));
            }
            rest.previous = b;
            pan.previous = b;
        });
        double restRms = 0.0;
        for (double step : rest.steps)
            restRms += step * step;
        restRms = std::sqrt(restRms / double(rest.steps.size()));
        double panMean = 0.0;
        for (double step : pan.steps)
            panMean += step;
        panMean /= double(pan.steps.size());
        double panRms = 0.0;
        for (double step : pan.steps)
            panRms += (step - panMean) * (step - panMean);
        panRms = std::sqrt(panRms / double(pan.steps.size()));
        std::printf("noisy heading (0.5 deg, %s): rest step %.3f deg rms, pan step jitter %.3f deg rms, "
                    "lag %.2f deg, noise estimate %.2f deg\n", gyroInput != 0 ? "game rotation vector" : "compass",
                    restRms, panRms, lag / lagFrames, engine.headingNoiseDegrees());
        expectTrue(engine.headingNoiseDegrees() > 0.4 && engine.headingNoiseDegrees() < 0.6,
                   "the heading noise of the driving input is measured");
        expectTrue(restRms < 0.12, "a noisy heading at rest is calmed (unfiltered: about 0.2 deg per frame)");
        expectTrue(panRms < 0.3, "a noisy heading pans evenly (unfiltered: about 0.6 deg per frame)");
        expectTrue(lag / lagFrames < 1.0, "the heading tracker adds no lag to a steady turn");
        expectTrue(worstPitch < 1e-6, "heading noise suppression never touches tilt");
    }

    {
        // The game rotation vector arrives in 120 ms bursts, the compass
        // promptly: the view is handed to the compass and back with every
        // burst. Sideways sweeps must then be exactly as smooth as up/down
        // sweeps (before: heading jumps of several degrees per hand-back).
        double jerk[2] = {0.0, 0.0};
        int switches = 0;
        for (int yaw = 0; yaw < 2; ++yaw)
        {
            Engine engine;
            std::mt19937 random(5);
            std::uniform_real_distribution<double> uniform(0.0, 1.0);
            const std::function<double(double)> sweep = [](double t) {
                const std::function<double(double)> ease = [](double x) {
                    return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : 0.5 - 0.5 * std::cos(AowisCompass::kPi * x);
                };
                return 40.0 * (ease(t - 1.5) - ease(t - 3.0) + ease(t - 4.5) - ease(t - 6.0));
            };
            const std::function<Quaternion(double)> truth = [&sweep, yaw](double t) {
                return yaw != 0 ? facing(sweep(t)) : multiply(facing(0.0), axisAngle(1.0, 0.0, 0.0, sweep(t)));
            };
            std::vector<Event> events;
            std::vector<double> burst;
            for (int i = 0; i < 750; ++i)
            {
                burst.push_back(i * 0.01);
                if (burst.size() == 12)
                {
                    const double arrival = burst.back() + 0.006 + 0.01 * uniform(random);
                    for (double seconds : burst)
                        events.push_back({seconds, arrival, false});
                    burst.clear();
                }
                events.push_back({0.003 + i * 0.01, 0.009 + i * 0.01 + 0.01 * uniform(random), true});
            }
            std::stable_sort(events.begin(), events.end(),
                             [](const Event &a, const Event &b) { return a.arrival < b.arrival; });
            std::size_t next = 0;
            double previous = std::nan("");
            double previousStep = std::nan("");
            double sum = 0.0;
            int count = 0;
            for (int frame = 0; frame < 450; ++frame)
            {
                const double now = frame / 60.0;
                while (next < events.size() && events[next].arrival <= now)
                {
                    const Event &event = events[next++];
                    if (event.compass)
                        engine.addCompass(truth(event.seconds), event.seconds, event.arrival);
                    else
                        engine.addGyro(gyroOf(truth(event.seconds)), event.seconds, event.arrival);
                }
                if (!engine.hasPose())
                    continue;
                const Quaternion v = rotate(engine.poseForFrame(now), {0.0, 0.0, -1.0, 0.0});
                const double angle = yaw != 0 ? std::atan2(v.x, v.y) / kDegree
                                              : std::asin(std::clamp(v.z, -1.0, 1.0)) / kDegree;
                const double step = wrapDegrees(angle - previous);
                if (now > 1.2 && !std::isnan(previousStep))
                {
                    sum += (step - previousStep) * (step - previousStep);
                    ++count;
                }
                previousStep = step;
                previous = angle;
            }
            jerk[yaw] = std::sqrt(sum / count);
            switches = engine.modeSwitches();
        }
        std::printf("120 ms gyroscope bursts: frame-to-frame step change %.3f deg up/down, %.3f deg sideways, "
                    "%d hand-overs\n", jerk[0], jerk[1], switches);
        expectTrue(switches > 50, "the burst scenario hands over to the compass and back");
        expectTrue(jerk[1] < 1.1 * jerk[0] + 0.01, "sideways is as smooth as up/down under gyroscope bursts");
    }

    {
        // Heading from the compass, tilt from the gyroscope. Hand sweeps of
        // +-40 deg, sideways and up/down.
        const std::function<double(double)> sweep = [](double t) {
            const std::function<double(double)> ease = [](double x) {
                return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : 0.5 - 0.5 * std::cos(AowisCompass::kPi * x);
            };
            return 40.0 * (ease(t - 1.5) - ease(t - 3.0) + ease(t - 4.5) - ease(t - 6.0));
        };
        Delivery d;
        d.duration = 7.0;

        // 1. Up/down is exactly what the previous mode showed.
        std::vector<double> pitchNew;
        std::vector<double> pitchOld;
        for (int compassHeading = 0; compassHeading < 2; ++compassHeading)
        {
            Engine engine;
            engine.setHeadingFromCompass(compassHeading != 0);
            std::vector<double> &out = compassHeading != 0 ? pitchNew : pitchOld;
            run(engine, d, [&sweep](double t) { return multiply(facing(0.0), axisAngle(1.0, 0.0, 0.0, sweep(t))); },
                exactCompass, [&out](double, const Quaternion &shown) {
                    const Quaternion v = rotate(shown, {0.0, 0.0, -1.0, 0.0});
                    out.push_back(std::asin(std::clamp(v.z, -1.0, 1.0)) / kDegree);
                });
        }
        double pitchDifference = 0.0;
        for (std::size_t i = 0; i < std::min(pitchNew.size(), pitchOld.size()); ++i)
            pitchDifference = std::max(pitchDifference, std::fabs(pitchNew[i] - pitchOld[i]));
        expectTrue(pitchNew.size() == pitchOld.size() && pitchDifference < 0.01,
                   "up/down is unchanged by taking the heading from the compass");

        // 2. The game rotation vector turns only 85 % of a sideways turn and
        // the compass tilt is noisy: sideways follows the compass, up/down
        // the gyroscope.
        double worstHeading[2] = {0.0, 0.0};
        double worstPitch = 0.0;
        for (int compassHeading = 0; compassHeading < 2; ++compassHeading)
        {
            Engine engine;
            engine.setHeadingFromCompass(compassHeading != 0);
            std::mt19937 random(13);
            std::normal_distribution<double> tilt(0.0, 0.4);
            std::mt19937 order(17);
            std::uniform_real_distribution<double> uniform(0.0, 1.0);
            std::vector<Event> events;
            for (int i = 0; i < 700; ++i)
            {
                events.push_back({i * 0.01, i * 0.01 + 0.006 + 0.01 * uniform(order), false});
                events.push_back({0.003 + i * 0.01, 0.009 + i * 0.01 + 0.01 * uniform(order), true});
            }
            std::stable_sort(events.begin(), events.end(),
                             [](const Event &a, const Event &b) { return a.arrival < b.arrival; });
            std::size_t next = 0;
            for (int frame = 0; frame < 420; ++frame)
            {
                const double now = frame / 60.0;
                while (next < events.size() && events[next].arrival <= now)
                {
                    const Event &event = events[next++];
                    const double turn = sweep(event.seconds);
                    if (event.compass)
                        engine.addCompass(multiply(facing(turn), axisAngle(1.0, 0.0, 0.0, tilt(random))),
                                          event.seconds, event.arrival);
                    else
                        engine.addGyro(gyroOf(facing(0.85 * turn)), event.seconds, event.arrival);
                }
                if (!engine.hasPose() || now < 1.0)
                    continue;
                const Quaternion shown = engine.poseForFrame(now);
                worstHeading[compassHeading] = std::max(worstHeading[compassHeading],
                    std::fabs(wrapDegrees(bearing(shown) - sweep(now))));
                if (compassHeading != 0)
                {
                    const Quaternion v = rotate(shown, {0.0, 0.0, -1.0, 0.0});
                    worstPitch = std::max(worstPitch, std::fabs(std::asin(std::clamp(v.z, -1.0, 1.0)) / kDegree));
                }
            }
        }
        // 3. Up/down sweeps; the compass heading wobbles while tilting (its
        // gravity estimate is disturbed by the hand's acceleration): up to
        // 3 deg, in proportion to the tilt rate, plus 0.3 deg noise. The view
        // must not turn sideways.
        double worstSideways = 0.0;
        double worstUpDownLag = 0.0;
        {
            Engine engine;
            std::mt19937 random(23);
            std::normal_distribution<double> noise(0.0, 0.3);
            Delivery wobble;
            wobble.duration = 7.0;
            const std::function<Quaternion(double)> truth = [&sweep](double t) {
                return multiply(facing(0.0), axisAngle(1.0, 0.0, 0.0, sweep(t)));
            };
            run(engine, wobble, truth,
                [&sweep, &random, &noise](double t, const Quaternion &pose) {
                    const double tiltRate = (sweep(t + 0.005) - sweep(t - 0.005)) / 0.01;
                    return multiply(axisAngle(0.0, 0.0, 1.0, 0.05 * tiltRate + noise(random)), pose);
                },
                [&](double now, const Quaternion &shown) {
                    if (now < 1.0)
                        return;
                    worstSideways = std::max(worstSideways, std::fabs(wrapDegrees(bearing(shown))));
                    const Quaternion v = rotate(shown, {0.0, 0.0, -1.0, 0.0});
                    worstUpDownLag = std::max(worstUpDownLag,
                        std::fabs(std::asin(std::clamp(v.z, -1.0, 1.0)) / kDegree - sweep(now)));
                });
        }
        std::printf("up/down sweeps, compass heading wobbling up to 3 deg: view turns sideways %.2f deg\n",
                    worstSideways);
        expectTrue(worstSideways < 0.4, "compass heading wobble while tilting does not shake the view sideways");
        std::printf("game rotation vector turning 85 %%: worst heading error %.2f deg slow-north, %.2f deg "
                    "compass heading; compass tilt noise shown %.3f deg\n",
                    worstHeading[0], worstHeading[1], worstPitch);
        expectTrue(worstHeading[1] < 2.5 && worstHeading[1] < 0.4 * worstHeading[0],
                   "sideways follows the compass heading, not a mis-turning gyroscope");
        expectTrue(worstPitch < 1e-6, "compass tilt noise never reaches up/down");
    }

    {
        // A real gyroscope (and a clean compass) is passed through unchanged.
        std::mt19937 random(8);
        std::normal_distribution<double> tremor(0.0, 0.005);
        Engine engine;
        Delivery d;
        d.duration = 4.0;
        run(engine, d, [&random, &tremor](double t) {
                return multiply(axisAngle(0.0, 0.0, 1.0, tremor(random)), facing(panDegrees(t, 1.0, 3.0, 60.0, 0.0)));
            }, exactCompass, [](double, const Quaternion &) {});
        expectTrue(std::fabs(engine.headingCorrectionDegrees()) < 1e-6 && engine.headingNoiseDegrees() < 0.05,
                   "gyroscope-grade input bypasses the heading tracker");
    }

    {
        double alpha = 0.0;
        double beta = 0.0;
        Engine::trackerGains(1e9, &alpha, &beta);
        expectTrue(alpha == 1.0 && beta == 0.0, "no measured noise: the tracker is the identity");
        Engine::trackerGains(0.1, &alpha, &beta);
        expectTrue(alpha > 0.0 && alpha < 0.5 && beta > 0.0 && beta < alpha, "heavy noise: low tracker gains");
    }

    {
        // The gyroscope drops out for 0.5 s while the compass reads 1 deg off.
        Engine engine;
        engine.setMagnification(4.0);
        Delivery d;
        d.gyroStallStart = 2.0;
        d.gyroStallEnd = 2.5;
        double worstStep = 0.0;
        double previous = std::nan("");
        bool usedCompass = false;
        run(engine, d, [](double) { return facing(0.0); },
            [](double, const Quaternion &pose) { return multiply(axisAngle(0.0, 0.0, 1.0, -1.0), pose); },
            [&](double, const Quaternion &shown) {
                usedCompass = usedCompass || !engine.usesGyro();
                const double b = bearing(shown);
                if (!std::isnan(previous))
                    worstStep = std::max(worstStep, std::fabs(wrapDegrees(b - previous)));
                previous = b;
            });
        expectTrue(usedCompass && engine.usesGyro(), "gyroscope dropout hands over to the compass and back");
        expectTrue(worstStep < 0.1, "the hand-over does not jump");
    }

    {
        // Equal 2 deg steps about the view axis (roll) and across it (pitch).
        Engine roll;
        Engine pitch;
        roll.setMagnification(4.0);
        pitch.setMagnification(4.0);
        const Quaternion start = facing(0.0);
        const Quaternion rolled = multiply(start, axisAngle(0.0, 0.0, 1.0, 2.0));
        const Quaternion pitched = multiply(start, axisAngle(1.0, 0.0, 0.0, 2.0));
        double rollPointing = 0.0;
        for (int i = 0; i <= 40; ++i)
        {
            const double t = i * 0.01;
            roll.addCompass(i < 10 ? start : rolled, t, t);
            pitch.addCompass(i < 10 ? start : pitched, t, t);
            if (roll.hasPose())
                rollPointing = std::max(rollPointing, viewDegrees(roll.corePose(), start));
        }
        expectTrue(poseDegrees(roll.corePose(), rolled) > 1.5 * poseDegrees(pitch.corePose(), pitched),
                   "roll is smoothed more strongly than pointing");
        expectTrue(rollPointing < 1e-4, "roll smoothing never moves the crosshair");
    }

    {
        Engine engine;
        for (int i = 0; i < 50; ++i)
        {
            engine.addGyro(gyroOf(facing(0.0)), i * 0.01, i * 0.01);
            engine.addCompass(facing(0.0), i * 0.01, i * 0.01);
        }
        const double core = engine.coreSeconds();
        const Quaternion pose = engine.corePose();
        engine.addGyro(gyroOf(facing(40.0)), 0.30, 0.6);
        engine.addGyro(gyroOf(facing(40.0)), 0.49, 0.6);
        expectTrue(engine.coreSeconds() == core && poseDegrees(engine.corePose(), pose) < 1e-12,
                   "duplicate and out-of-order samples are ignored");
        const Quaternion negated = gyroOf(facing(0.0));
        engine.addGyro({-negated.x, -negated.y, -negated.z, -negated.w}, 0.50, 0.6);
        expectTrue(viewDegrees(engine.corePose(), facing(0.0)) < 1e-6, "equivalent quaternion signs are the same pose");
        engine.addGyro(gyroOf(facing(60.0)), 1.50, 1.6);
        expectTrue(viewDegrees(engine.corePose(), facing(60.0)) < 1e-6, "a long pause restarts without smoothing across it");
        engine.reset();
        expectTrue(!engine.hasPose(), "reset requires fresh samples");
    }

    for (double hz : {60.0, 58.9, 120.0})
    {
        // Frames shown exactly one refresh apart, prepared up to +-4.5 ms
        // early or late (measured on Android: 11 to 21 ms intervals at 60 Hz).
        AowisMobileMap::LookAroundFrameClock clock;
        std::mt19937 random(9);
        std::uniform_real_distribution<double> jitter(-0.0045, 0.0045);
        const double period = 1.0 / hz;
        double worstStep = 0.0;
        double worstOffset = 0.0;
        double previous = 0.0;
        for (int frame = 0; frame < 600; ++frame)
        {
            const double vsync = 10.0 + frame * period;
            const double paced = clock.frameTime(vsync + jitter(random) * std::min(1.0, period / 0.0167));
            if (frame > 120)
            {
                worstStep = std::max(worstStep, std::fabs((paced - previous) / period - 1.0));
                worstOffset = std::max(worstOffset, std::fabs(paced - vsync));
            }
            previous = paced;
        }
        std::printf("frame clock %.1f Hz: worst step error %.2f %%, worst offset %.2f ms\n",
                    hz, 100.0 * worstStep, 1e3 * worstOffset);
        expectTrue(worstStep < 0.03, "jittery frame preparation gives evenly paced frame times");
        expectTrue(worstOffset < 0.003, "paced frame times stay on the presentation grid");
    }

    {
        // A real dropped frame: the stream then runs one period later.
        AowisMobileMap::LookAroundFrameClock clock;
        const double period = 1.0 / 60.0;
        double previous = 0.0;
        double worstStep = 0.0;
        double offset = 0.0;
        for (int frame = 0; frame < 400; ++frame)
        {
            const double vsync = frame * period + (frame >= 100 ? period : 0.0);
            const double paced = clock.frameTime(vsync);
            if (frame > 10)
                worstStep = std::max(worstStep, (paced - previous) / period);
            previous = paced;
            offset = paced - vsync;
        }
        std::printf("dropped frame: worst paced step %.3f periods, final offset %.3f ms\n", worstStep, 1e3 * offset);
        expectTrue(worstStep < 1.2 && std::fabs(offset) < 0.001, "a dropped frame is absorbed smoothly");
        const double resumed = clock.frameTime(previous + 2.0);
        expectTrue(std::fabs(resumed - (previous + 2.0)) < 0.002, "after a pause the clock restarts at the wall clock");
    }

    {
        // End to end: a steady pan, frames shown on a 60 Hz grid but
        // prepared with jitter. Sampling the raw preparation time makes every
        // shown step uneven; the frame clock keeps them even.
        double jitters[2] = {0.0, 0.0};
        for (int paced = 0; paced < 2; ++paced)
        {
            Engine engine;
            AowisMobileMap::LookAroundFrameClock clock;
            std::mt19937 random(4);
            std::uniform_real_distribution<double> jitter(-0.0045, 0.0045);
            FrameStats stats;
            double nextGyro = 0.0;
            double nextCompass = 0.003;
            for (int frame = 0; frame < 300; ++frame)
            {
                const double vsync = frame / 60.0;
                const double prepared = vsync + jitter(random);
                // Samples arrive 10 ms after their sensor time.
                while (nextGyro + 0.010 <= prepared)
                {
                    engine.addGyro(gyroOf(facing(20.0 * nextGyro)), nextGyro, nextGyro + 0.010);
                    nextGyro += 0.01;
                }
                while (nextCompass + 0.010 <= prepared)
                {
                    engine.addCompass(facing(20.0 * nextCompass), nextCompass, nextCompass + 0.010);
                    nextCompass += 0.01;
                }
                const double time = paced != 0 ? clock.frameTime(prepared) : prepared;
                if (!engine.hasPose())
                    continue;
                const double b = bearing(engine.poseForFrame(time));
                if (frame > 120)
                    stats.steps.push_back(wrapDegrees(b - stats.previous));
                stats.previous = b;
            }
            jitters[paced] = stats.jitter();
        }
        std::printf("pan on a 60 Hz screen, prepared +-4.5 ms: step jitter %.1f %% raw, %.1f %% paced\n",
                    100.0 * jitters[0], 100.0 * jitters[1]);
        expectTrue(jitters[0] > 0.08, "raw preparation time reproduces the stutter");
        expectTrue(jitters[1] < 0.03, "the frame clock removes it");
    }

    expectTrue(Engine::cutoffHz(0.0, 8.0) < Engine::cutoffHz(0.0, 1.0), "rest cut-off falls with magnification");
    expectTrue(Engine::cutoffHz(10.0, 8.0) > Engine::cutoffHz(1.0, 8.0), "cut-off rises with angular speed");
    expectTrue(Engine::cutoffHz(1e6, 8.0) <= Engine::kMaxCutoffHz, "cut-off is bounded");
    expectTrue(std::isfinite(Engine::cutoffHz(0.0, std::nan(""))), "invalid magnification is treated as 1x");

    std::printf("look_around_engine: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
