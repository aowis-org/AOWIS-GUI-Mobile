#include "look_around_direct.h"

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
typedef AowisMobileMap::LookAroundDirectEngine Engine;
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

double poseDegrees(const Quaternion &a, const Quaternion &b)
{
    const Quaternion e = Engine::shortest(multiply(Engine::conjugate(a), b));
    return 2.0 * std::atan2(std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), e.w) / kDegree;
}

double wrapDegrees(double degrees)
{
    return std::remainder(degrees, 360.0);
}

// Phone upright, camera facing 'bearing' (clockwise from north), tilted up by 'pitch'.
Quaternion facing(double bearing, double pitch)
{
    return multiply(axisAngle(0.0, 0.0, 1.0, -bearing), axisAngle(1.0, 0.0, 0.0, 90.0 + pitch));
}

double bearingOf(const Quaternion &q)
{
    const Quaternion v = rotate(q, {0.0, 0.0, -1.0, 0.0});
    return std::atan2(v.x, v.y) / kDegree;
}

double pitchOf(const Quaternion &q)
{
    const Quaternion v = rotate(q, {0.0, 0.0, -1.0, 0.0});
    return std::asin(std::clamp(v.z, -1.0, 1.0)) / kDegree;
}

double ease(double x)
{
    return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : 0.5 - 0.5 * std::cos(AowisCompass::kPi * x);
}

// Hand sweeps: +40 deg in 1 s, back, again.
double sweep(double t)
{
    return 40.0 * (ease(t - 1.0) - ease(t - 2.5) + ease(t - 4.0) - ease(t - 5.5));
}

struct Delivery
{
    double sensorHz = 100.0;
    double frameHz = 60.0;
    double duration = 7.0;
    int burst = 1;          // samples delivered together
    double latency = 0.008; // after the newest sample of a delivery
    double jitter = 0.010;
    unsigned seed = 3;
};

// Runs one scenario; 'frame' sees each frame time and the shown pose.
void run(Engine &engine, const Delivery &d, const std::function<Quaternion(double)> &truth,
         const std::function<void(double, const Quaternion &)> &frame)
{
    std::mt19937 random(d.seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    struct Event
    {
        double seconds;
        double arrival;
    };
    std::vector<Event> events;
    std::vector<double> pending;
    for (int i = 0; i < int(d.duration * d.sensorHz); ++i)
    {
        pending.push_back(i / d.sensorHz);
        if (int(pending.size()) >= d.burst)
        {
            const double arrival = pending.back() + d.latency + d.jitter * uniform(random);
            for (double seconds : pending)
                events.push_back({seconds, arrival});
            pending.clear();
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const Event &a, const Event &b) { return a.arrival < b.arrival; });
    std::size_t next = 0;
    for (int index = 0; index < int(d.duration * d.frameHz); ++index)
    {
        const double now = index / d.frameHz;
        while (next < events.size() && events[next].arrival <= now)
        {
            engine.addPose(truth(events[next].seconds), events[next].seconds, events[next].arrival);
            ++next;
        }
        if (engine.hasPose())
            frame(now, engine.poseForFrame(now));
    }
}

// RMS of the frame-to-frame change of the per-frame step: visible jerk.
struct Jerk
{
    double previous = std::nan("");
    double previousStep = std::nan("");
    double sum = 0.0;
    int count = 0;
    int backward = 0;

    void add(double value, bool counted, double direction)
    {
        const double step = wrapDegrees(value - this->previous);
        if (counted && !std::isnan(this->previousStep))
        {
            this->sum += (step - this->previousStep) * (step - this->previousStep);
            ++this->count;
            if (direction * step < -1e-9)
                ++this->backward;
        }
        this->previousStep = step;
        this->previous = value;
    }

    double rms() const { return std::sqrt(this->sum / std::max(1, this->count)); }
};
}

int main()
{
    {
        Engine engine;
        expectTrue(!engine.hasPose(), "no pose before the first sample");
        engine.addPose(facing(25.0, 10.0), 1.0, 1.01);
        expectTrue(poseDegrees(engine.poseForFrame(1.02), facing(25.0, 10.0)) < 1e-9,
                   "the first sample is shown as it is");
        engine.addPose(facing(40.0, 10.0), 0.9, 1.03);
        engine.addPose(facing(40.0, 10.0), 1.0, 1.03);
        expectTrue(poseDegrees(engine.targetPose(), facing(25.0, 10.0)) < 1e-9,
                   "older and repeated samples are ignored");
        engine.addPose({0.0, 0.0, 0.0, 0.0}, 1.1, 1.11);
        expectTrue(poseDegrees(engine.targetPose(), facing(25.0, 10.0)) < 1e-9, "invalid samples are ignored");
        engine.reset();
        expectTrue(!engine.hasPose(), "reset requires a fresh sample");
    }

    {
        // Step to a new pose: converges exactly and settles (no dead band).
        Engine engine;
        engine.setMagnification(8.0);
        engine.addPose(facing(0.0, 0.0), 0.0, 0.0);
        engine.poseForFrame(0.0);
        engine.addPose(facing(0.3, -0.2), 0.01, 0.01);
        Quaternion shown;
        for (int i = 1; i < 60 * 10; ++i)
            shown = engine.poseForFrame(i / 60.0);
        expectTrue(poseDegrees(shown, facing(0.3, -0.2)) == 0.0, "at rest the view settles exactly on the sensor");
    }

    for (int burst : {1, 12, 25})
    {
        // Sideways and up/down sweeps are filtered identically, however the
        // samples are delivered.
        Jerk sideways;
        Jerk upDown;
        double lagSideways = 0.0;
        double lagUpDown = 0.0;
        Delivery d;
        d.burst = burst;
        Engine a;
        run(a, d, [](double t) { return facing(sweep(t), 0.0); }, [&](double now, const Quaternion &shown) {
            sideways.add(bearingOf(shown), now > 0.8, 0.0);
            lagSideways = std::max(lagSideways, std::fabs(sweep(now) - bearingOf(shown)));
        });
        Engine b;
        run(b, d, [](double t) { return facing(0.0, sweep(t)); }, [&](double now, const Quaternion &shown) {
            upDown.add(pitchOf(shown), now > 0.8, 0.0);
            lagUpDown = std::max(lagUpDown, std::fabs(sweep(now) - pitchOf(shown)));
        });
        std::printf("delivery in bursts of %2d: jerk sideways %.4f, up/down %.4f deg; worst lag %.2f / %.2f deg\n",
                    burst, sideways.rms(), upDown.rms(), lagSideways, lagUpDown);
        expectTrue(std::fabs(sideways.rms() - upDown.rms()) <= 0.02 * upDown.rms() + 1e-6 &&
                   std::fabs(lagSideways - lagUpDown) < 0.05,
                   "sideways moves exactly like up/down");
    }

    {
        // Direct: a steady 30 deg/s turn, samples every 10 ms, follows within
        // a degree of the newest sample.
        Engine engine;
        Delivery d;
        double worst = 0.0;
        int backward = 0;
        double previous = std::nan("");
        run(engine, d, [](double t) { return facing(30.0 * t, 0.0); }, [&](double now, const Quaternion &shown) {
            const double b = bearingOf(shown);
            if (now > 1.0)
            {
                worst = std::max(worst, std::fabs(wrapDegrees(bearingOf(engine.targetPose()) - b)));
                if (wrapDegrees(b - previous) < 0.0)
                    ++backward;
            }
            previous = b;
        });
        std::printf("steady 30 deg/s: worst distance to the newest sample %.2f deg\n", worst);
        expectTrue(worst < 1.0 && backward == 0, "a turn follows the newest sample within a degree");
    }

    {
        // Bursts of 120 ms: a steady turn still never runs backward.
        Engine engine;
        Delivery d;
        d.burst = 12;
        int backward = 0;
        double previous = std::nan("");
        run(engine, d, [](double t) { return facing(30.0 * t, 0.0); }, [&](double now, const Quaternion &shown) {
            const double b = bearingOf(shown);
            if (now > 1.0 && wrapDegrees(b - previous) < 0.0)
                ++backward;
            previous = b;
        });
        expectTrue(backward == 0, "bursty delivery never makes a turn run backward");
    }

    for (double lens : {1.0, 8.0})
    {
        // Hand tremor (0.15 deg at 9 Hz on every axis) at rest: damped.
        Engine engine;
        engine.setMagnification(lens);
        Delivery d;
        double raw = 0.0;
        double shown = 0.0;
        run(engine, d, [](double t) {
                const double s = 0.15 * std::sin(2.0 * AowisCompass::kPi * 9.0 * t);
                return multiply(facing(10.0 + s, 5.0 - s), axisAngle(0.0, 0.0, 1.0, s));
            }, [&](double now, const Quaternion &pose) {
                if (now > 2.0)
                {
                    raw = std::max(raw, poseDegrees(engine.targetPose(), facing(10.0, 5.0)));
                    const Quaternion v = rotate(pose, {0.0, 0.0, -1.0, 0.0});
                    const Quaternion r = rotate(facing(10.0, 5.0), {0.0, 0.0, -1.0, 0.0});
                    shown = std::max(shown, std::acos(std::clamp(v.x * r.x + v.y * r.y + v.z * r.z, -1.0, 1.0)) / kDegree);
                }
            });
        std::printf("9 Hz tremor at %.0fx: sensor %.3f deg, shown pointing %.3f deg\n", lens, raw, shown);
        expectTrue(shown < raw / 3.0, "hand tremor at rest is damped about threefold");
    }

    {
        // The frame rate does not change the motion much (the filter uses the
        // frame interval).
        double lag[2] = {0.0, 0.0};
        int index = 0;
        for (double hz : {60.0, 120.0})
        {
            Engine engine;
            Delivery d;
            d.frameHz = hz;
            run(engine, d, [](double t) { return facing(30.0 * t, 0.0); }, [&](double now, const Quaternion &shown) {
                if (now > 2.0 && now < 6.0)
                    lag[index] = std::max(lag[index], std::fabs(wrapDegrees(30.0 * now - bearingOf(shown))));
            });
            ++index;
        }
        expectTrue(std::fabs(lag[0] - lag[1]) < 0.25, "60 and 120 Hz frames show the same motion");
    }

    {
        // Equal 2 deg steps about the view axis (roll) and across it (pitch).
        Engine roll;
        Engine pitch;
        const Quaternion start = facing(0.0, 0.0);
        const Quaternion rolled = multiply(start, axisAngle(0.0, 0.0, 1.0, 2.0));
        const Quaternion pitched = multiply(start, axisAngle(1.0, 0.0, 0.0, 2.0));
        roll.addPose(start, 0.0, 0.0);
        pitch.addPose(start, 0.0, 0.0);
        roll.poseForFrame(0.0);
        pitch.poseForFrame(0.0);
        roll.addPose(rolled, 0.01, 0.01);
        pitch.addPose(pitched, 0.01, 0.01);
        double rollPointing = 0.0;
        for (int i = 1; i <= 6; ++i)
        {
            const Quaternion r = roll.poseForFrame(i / 60.0);
            pitch.poseForFrame(i / 60.0);
            const Quaternion v = rotate(r, {0.0, 0.0, -1.0, 0.0});
            const Quaternion s = rotate(start, {0.0, 0.0, -1.0, 0.0});
            rollPointing = std::max(rollPointing,
                std::acos(std::clamp(v.x * s.x + v.y * s.y + v.z * s.z, -1.0, 1.0)) / kDegree);
        }
        expectTrue(poseDegrees(roll.poseForFrame(7 / 60.0), rolled) >
                   1.5 * poseDegrees(pitch.poseForFrame(7 / 60.0), pitched),
                   "roll is smoothed more strongly than pointing");
        expectTrue(rollPointing < 1e-4, "roll smoothing never moves the crosshair");
    }

    expectTrue(Engine::cutoffHz(0.0, 8.0) < Engine::cutoffHz(0.0, 1.0), "rest cut-off falls with magnification");
    expectTrue(Engine::cutoffHz(30.0, 1.0) > Engine::cutoffHz(1.0, 1.0), "cut-off rises with angular speed");
    expectTrue(Engine::cutoffHz(1e6, 8.0) <= Engine::kMaxCutoffHz, "cut-off is bounded");
    expectTrue(std::isfinite(Engine::cutoffHz(0.0, std::nan(""))), "invalid magnification is treated as 1x");

    std::printf("look_around_direct: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
