#include "look_around_engine.h"
#include "look_around_field.h"

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
typedef AowisCompass::Vector3 Vector3;
typedef AowisMobileMap::LookAroundEngine Engine;
typedef AowisMobileMap::LookAroundFieldSource FieldSource;
typedef AowisMobileMap::FieldOrientation Field;
using AowisCompass::multiply;

constexpr double kDegree = AowisCompass::kPi / 180.0;
constexpr double kDip = 65.0;
constexpr double kStrength = 48.0;

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

Quaternion conjugate(const Quaternion &q)
{
    return {-q.x, -q.y, -q.z, q.w};
}

Vector3 rotate(const Quaternion &q, const Vector3 &v)
{
    const Quaternion r = multiply(multiply(q, {v.x, v.y, v.z, 0.0}), conjugate(q));
    return {r.x, r.y, r.z};
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
    const Vector3 v = rotate(q, {0.0, 0.0, -1.0});
    return std::atan2(v.x, v.y) / kDegree;
}

double pitchOf(const Quaternion &q)
{
    const Vector3 v = rotate(q, {0.0, 0.0, -1.0});
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

struct Motion
{
    double bearing = 0.0;
    bool tilt = true; // up/down sweeps; else sideways sweeps
};

Quaternion truthAt(const Motion &m, double t)
{
    return m.tilt ? facing(m.bearing, sweep(t)) : facing(m.bearing + sweep(t), 0.0);
}

// World-frame field: north and down by the dip.
Vector3 worldField()
{
    return {0.0, kStrength * std::cos(kDip * kDegree), -kStrength * std::sin(kDip * kDegree)};
}

// Accelerometer (device frame, +g up) including the hand's own acceleration:
// the phone turns about a wrist 10 cm behind its screen.
Vector3 accelerometer(const Motion &m, double t)
{
    const double h = 0.002;
    const Quaternion q0 = truthAt(m, t - h);
    const Quaternion q1 = truthAt(m, t);
    const Quaternion q2 = truthAt(m, t + h);
    const Vector3 r0 = rotate(q0, {0.0, 0.0, 0.10});
    const Vector3 r1 = rotate(q1, {0.0, 0.0, 0.10});
    const Vector3 r2 = rotate(q2, {0.0, 0.0, 0.10});
    const Vector3 linear{(r0.x - 2.0 * r1.x + r2.x) / (h * h), (r0.y - 2.0 * r1.y + r2.y) / (h * h),
                         (r0.z - 2.0 * r1.z + r2.z) / (h * h)};
    const Vector3 world{linear.x, linear.y, linear.z + 9.80665};
    return rotate(conjugate(q1), world);
}

Vector3 magnetometer(const Motion &m, double t)
{
    return rotate(conjugate(truthAt(m, t)), worldField());
}

struct Result
{
    double sideways = 0.0; // worst view bearing error
    double upDown = 0.0;   // worst view pitch error
};

// Phone-style fusion: gravity filtered slower than the field (as an OS
// fusion without gyroscope may do), the view driven by it directly.
Result phoneStyle(const Motion &m)
{
    std::mt19937 random(7);
    std::normal_distribution<double> noise(0.0, 0.3);
    Engine engine;
    Vector3 g;
    Vector3 b;
    bool started = false;
    Result result;
    double last = 0.0;
    for (int i = 0; i < 700; ++i)
    {
        const double t = i * 0.01;
        const Vector3 a = accelerometer(m, t);
        Vector3 f = magnetometer(m, t);
        f = {f.x + noise(random), f.y + noise(random), f.z + noise(random)};
        if (!started)
        {
            g = a;
            b = f;
            started = true;
        }
        const double ag = -std::expm1(-0.01 / 0.20);
        const double af = -std::expm1(-0.01 / 0.05);
        g = {g.x + ag * (a.x - g.x), g.y + ag * (a.y - g.y), g.z + ag * (a.z - g.z)};
        b = {b.x + af * (f.x - b.x), b.y + af * (f.y - b.y), b.z + af * (f.z - b.z)};
        Quaternion pose;
        if (AowisCompass::orientationFromGravityAndField(g, b, &pose))
            engine.addCompass(pose, t, t + 0.01);
        if (t - last >= 1.0 / 60.0 && engine.hasPose())
        {
            last = t;
            const Quaternion shown = engine.poseForFrame(t + 0.01);
            if (t > 0.8)
            {
                double best = 1e9;
                for (double lag = 0.0; lag <= 0.35; lag += 0.01)
                {
                    const Quaternion truth = truthAt(m, t - lag);
                    best = std::min(best, m.tilt ? std::fabs(wrapDegrees(bearingOf(shown) - m.bearing))
                                                 : std::fabs(wrapDegrees(bearingOf(shown) - bearingOf(truth))));
                }
                result.sideways = std::max(result.sideways, best);
            }
        }
    }
    return result;
}

// AOWIS field pipeline: FieldOrientation -> LookAroundFieldSource -> engine.
Result fieldPipeline(const Motion &m, double expectedStrength, double expectedDip,
                     const std::function<Vector3(double, const Vector3 &)> &disturb, int *untrusted)
{
    std::mt19937 random(7);
    std::normal_distribution<double> noise(0.0, 0.3);
    Engine engine;
    engine.setMotionIsGeographic(true);
    FieldSource source;
    source.setExpectedField(expectedStrength, expectedDip);
    Field field;
    std::vector<Field::Output> outputs;
    field.setOutputHandler([&outputs](const Field::Output &out) { outputs.push_back(out); });
    Result result;
    double lastFrame = 0.0;
    if (untrusted != nullptr)
        *untrusted = 0;
    // Accelerometer at 100 Hz, magnetometer at 50 Hz, 4 ms apart.
    for (int i = 0; i < 700; ++i)
    {
        const double t = i * 0.01;
        if (i % 2 == 0)
        {
            const double tm = t + 0.004;
            Vector3 f = magnetometer(m, tm);
            f = {f.x + noise(random), f.y + noise(random), f.z + noise(random)};
            f = disturb(tm, f);
            field.addMagneticField(f.x, f.y, f.z, tm);
        }
        const Vector3 a = accelerometer(m, t);
        field.addAccelerometer(a.x, a.y, a.z, t);
        for (const Field::Output &out : outputs)
        {
            FieldSource::Step step;
            if (!source.add(out.pose, out.seconds, out.strength, out.dipDegrees, &step))
                continue;
            engine.addGyro(step.motion, step.seconds, step.seconds + 0.012);
            if (step.trusted)
                engine.addCompass(step.compass, step.seconds, step.seconds + 0.012);
            else if (untrusted != nullptr)
                ++*untrusted;
        }
        outputs.clear();
        if (t - lastFrame >= 1.0 / 60.0 && engine.hasPose())
        {
            lastFrame = t;
            const Quaternion shown = engine.poseForFrame(t + 0.012);
            if (t > 0.8)
            {
                // Compared with the hand at the best-matching moment shortly
                // before (the filters' delay, up to 0.35 s).
                double bestSideways = 1e9;
                double bestUpDown = 1e9;
                for (double lag = 0.0; lag <= 0.35; lag += 0.01)
                {
                    const Quaternion truth = truthAt(m, t - lag);
                    bestSideways = std::min(bestSideways, m.tilt ? std::fabs(wrapDegrees(bearingOf(shown) - m.bearing))
                                                                 : std::fabs(wrapDegrees(bearingOf(shown) - bearingOf(truth))));
                    bestUpDown = std::min(bestUpDown, std::fabs(pitchOf(shown) - pitchOf(truth)));
                }
                result.sideways = std::max(result.sideways, bestSideways);
                if (m.tilt)
                    result.upDown = std::max(result.upDown, bestUpDown);
            }
        }
    }
    return result;
}

Vector3 undisturbed(double, const Vector3 &f)
{
    return f;
}
}

int main()
{
    {
        // Static pose: the field orientation is the true one.
        Field field;
        Field::Output last;
        int count = 0;
        field.setOutputHandler([&last, &count](const Field::Output &out) {
            last = out;
            ++count;
        });
        const Motion m{37.0, true};
        for (int i = 0; i < 100; ++i)
        {
            const double t = i * 0.01;
            const Vector3 f = rotate(conjugate(facing(37.0, 10.0)), worldField());
            field.addMagneticField(f.x, f.y, f.z, t + 0.004);
            const Vector3 a = rotate(conjugate(facing(37.0, 10.0)), {0.0, 0.0, 9.80665});
            field.addAccelerometer(a.x, a.y, a.z, t);
        }
        (void)m;
        expectTrue(count > 90, "every magnetometer reading covered by the accelerometer gives a pose");
        expectTrue(std::fabs(wrapDegrees(bearingOf(last.pose) - 37.0)) < 1e-6 &&
                   std::fabs(pitchOf(last.pose) - 10.0) < 1e-6, "a resting phone gives its true pose");
        expectTrue(std::fabs(last.strength - kStrength) < 1e-6 && std::fabs(last.dipDegrees - kDip) < 1e-6,
                   "field strength and dip are measured");
    }

    {
        // Vendor HAL with the magnetometer on another time base (+1000 s):
        // poses still come out (paired by arrival).
        Field field;
        int count = 0;
        field.setOutputHandler([&count](const Field::Output &) { ++count; });
        for (int i = 0; i < 100; ++i)
        {
            const double t = i * 0.01;
            const Vector3 a = rotate(conjugate(facing(10.0, 0.0)), {0.0, 0.0, 9.80665});
            field.addAccelerometer(a.x, a.y, a.z, t);
            const Vector3 f = rotate(conjugate(facing(10.0, 0.0)), worldField());
            field.addMagneticField(f.x, f.y, f.z, t + 1000.0);
        }
        expectTrue(count > 90, "magnetometer and accelerometer on different time bases still give poses");
    }

    {
        // A sensor clock jumping back by 5 s mid-session (driver resync):
        // poses keep coming, the field source and the engine continue.
        Field field;
        FieldSource source;
        Engine engine;
        int outputs = 0;
        int steps = 0;
        double lastStep = 0.0;
        field.setOutputHandler([&](const Field::Output &out) {
            ++outputs;
            FieldSource::Step step;
            if (source.add(out.pose, out.seconds, out.strength, out.dipDegrees, &step))
            {
                ++steps;
                lastStep = step.seconds;
                engine.addGyro(step.motion, step.seconds, step.seconds + 0.01);
                engine.addCompass(step.compass, step.seconds, step.seconds + 0.01);
            }
        });
        for (int i = 0; i < 200; ++i)
        {
            const double t = i * 0.01 - (i >= 100 ? 5.0 : 0.0);
            const Vector3 a = rotate(conjugate(facing(10.0, 0.0)), {0.0, 0.0, 9.80665});
            field.addAccelerometer(a.x, a.y, a.z, t);
            const Vector3 f = rotate(conjugate(facing(10.0, 0.0)), worldField());
            field.addMagneticField(f.x, f.y, f.z, t + 0.004);
        }
        expectTrue(outputs > 180 && steps > 180 && lastStep < 0.0 && engine.hasPose() && engine.coreSeconds() < 0.0,
                   "a sensor clock jumping back keeps poses coming through field source and engine");
    }

    double phoneWorst[4];
    double ourWorst[4];
    double ourUpDown = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        const double bearing = 90.0 * index;
        phoneWorst[index] = phoneStyle({bearing, true}).sideways;
        const Result ours = fieldPipeline({bearing, true}, kStrength, kDip, undisturbed, nullptr);
        ourWorst[index] = ours.sideways;
        ourUpDown = std::max(ourUpDown, ours.upDown);
        std::printf("up/down sweeps facing %3.0f deg: sideways shake %.2f deg phone-style fusion, %.2f deg AOWIS field "
                    "(up/down follows within %.2f deg)\n", bearing, phoneWorst[index], ourWorst[index], ours.upDown);
    }
    expectTrue(phoneWorst[1] > 3.0 * phoneWorst[0] && phoneWorst[3] > 3.0 * phoneWorst[2],
               "the model reproduces the effect: phone-style fusion shakes facing east/west, not north/south");
    expectTrue(*std::max_element(ourWorst, ourWorst + 4) < 2.5,
               "up/down sweeps shake sideways at most by hand acceleration and sensor noise, in any direction");
    expectTrue(ourWorst[1] < 0.125 * phoneWorst[1] && ourWorst[3] < 0.125 * phoneWorst[3],
               "facing east/west the shake is reduced at least eightfold");
    expectTrue(ourUpDown < 4.0, "up/down still follows the hand");

    for (double bearing : {0.0, 90.0})
    {
        // Sideways sweeps: the heading follows.
        const Result ours = fieldPipeline({bearing, false}, kStrength, kDip, undisturbed, nullptr);
        const Result phone = phoneStyle({bearing, false});
        std::printf("sideways sweeps from %3.0f deg: worst heading error %.2f deg AOWIS field, %.2f deg phone-style\n",
                    bearing, ours.sideways, phone.sideways);
        // (Only camera stabilization's fallback now: with the camera, the
        // heading comes from the camera.)
        expectTrue(ours.sideways < 6.0 && ours.sideways < phone.sideways + 1.5,
                   "sideways sweeps are followed about as well as by the phone-style fusion");
    }

    {
        // A steel railing: from 3.0 to 4.5 s the field is 30 % stronger and
        // bent 20 deg. The heading is held, not swung.
        int untrusted = 0;
        const Result held = fieldPipeline({90.0, true}, kStrength, kDip, [](double t, const Vector3 &f) {
                if (t < 3.0 || t > 4.5)
                    return f;
                const Vector3 bent = rotate(axisAngle(0.0, 0.0, 1.0, 20.0), f);
                return Vector3{1.3 * bent.x, 1.3 * bent.y, 1.3 * bent.z};
            }, &untrusted);
        std::printf("disturbed field (GPS reference): %d readings held, worst sideways %.2f deg\n", untrusted, held.sideways);
        expectTrue(untrusted > 50 && held.sideways < 2.5, "a disturbed field is detected against the GPS field and ignored");

        // Without GPS: the field's own average is the reference.
        const Result self = fieldPipeline({90.0, true}, std::nan(""), std::nan(""), [](double t, const Vector3 &f) {
                if (t < 3.0 || t > 4.5)
                    return f;
                const Vector3 bent = rotate(axisAngle(0.0, 0.0, 1.0, 20.0), f);
                return Vector3{1.3 * bent.x, 1.3 * bent.y, 1.3 * bent.z};
            }, &untrusted);
        std::printf("disturbed field (own reference): %d readings held, worst sideways %.2f deg\n", untrusted, self.sideways);
        expectTrue(untrusted > 50 && self.sideways < 2.5, "without GPS a disturbance is detected against the recent field");
    }

    for (double scale : {0.75, 1.3})
    {
        // The phone's magnetometer reads 25 % low (or 30 % high) against the
        // GPS field model, from the start. The view must move at once
        // (before: no compass reading ever passed the check and nothing was
        // shown at all), and north settles on the phone's own field.
        int untrusted = 0;
        const double expected = kStrength / scale;
        const Result r = fieldPipeline({90.0, true}, expected, kDip, undisturbed, &untrusted);
        std::printf("magnetometer %.0f %% of the GPS model: %d readings held, worst sideways %.2f deg, up/down within %.2f deg\n",
                    100.0 * scale, untrusted, r.sideways, r.upDown);
        expectTrue(untrusted == 0 && r.upDown < 4.0 && r.sideways < 2.5,
                   "a magnetometer off the GPS model still drives the view");
    }

    {
        // Before any compass reading is accepted, geographic motion is shown
        // already; a game rotation vector (arbitrary heading) is not.
        Engine geographic;
        geographic.setMotionIsGeographic(true);
        Engine game;
        for (int i = 0; i < 30; ++i)
        {
            geographic.addGyro(facing(20.0, 0.0), i * 0.01, i * 0.01 + 0.01);
            game.addGyro(facing(20.0, 0.0), i * 0.01, i * 0.01 + 0.01);
        }
        expectTrue(geographic.hasPose() && !game.hasPose(),
                   "geographic motion is shown without waiting for the compass");
    }

    {
        // Without GPS, a permanently different field is re-learned.
        FieldSource source;
        FieldSource::Step step;
        source.add(facing(0.0, 0.0), 0.0, 48.0, 65.0, &step);
        for (int i = 1; i < 800; ++i)
            source.add(facing(0.0, 0.0), i * 0.01, 60.0, 65.0, &step);
        expectTrue(step.trusted, "a lasting new field is accepted after a few seconds");
    }

    std::printf("look_around_field: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
