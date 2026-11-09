#include "look_around_engine.h"
#include "look_around_field.h"
#include "look_around_vision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
typedef AowisMobileMap::VisionTracker Tracker;
typedef AowisMobileMap::VisionFusion Fusion;
using AowisCompass::multiply;

constexpr double kDegree = AowisCompass::kPi / 180.0;
constexpr double kDip = 65.0;
constexpr double kStrength = 48.0;
constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr double kFocal = 250.0; // about 65 deg horizontal field of view

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

double poseDegrees(const Quaternion &a, const Quaternion &b)
{
    Quaternion e = multiply(conjugate(a), b);
    if (e.w < 0.0)
        e = {-e.x, -e.y, -e.z, -e.w};
    return 2.0 * std::atan2(std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), e.w) / kDegree;
}

double wrapDegrees(double degrees)
{
    return std::remainder(degrees, 360.0);
}

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

// --- A textured world (equirectangular), rendered by a pinhole camera ----

struct World
{
    static constexpr int kW = 2048;
    static constexpr int kH = 1024;
    std::vector<float> texels;

    explicit World(unsigned seed, bool blank)
    {
        this->texels.assign(std::size_t(kW) * kH, 128.0f);
        if (blank)
            return;
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        // Blobs and bars of varied size and contrast: corners to track.
        for (int i = 0; i < 2500; ++i)
        {
            const int cx = int(uniform(random) * kW);
            const int cy = int(uniform(random) * kH);
            const int r = 3 + int(uniform(random) * 14);
            const float value = float(uniform(random) * 255.0);
            const bool bar = uniform(random) < 0.5;
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r * (bar ? 3 : 1); dx <= r * (bar ? 3 : 1); ++dx)
                {
                    const int y = cy + dy;
                    if (y < 0 || y >= kH)
                        continue;
                    const int x = ((cx + dx) % kW + kW) % kW;
                    this->texels[std::size_t(y) * kW + std::size_t(x)] = value;
                }
            }
        }
        // Slight blur (as a lens would).
        std::vector<float> blurred = this->texels;
        for (int y = 1; y < kH - 1; ++y)
            for (int x = 0; x < kW; ++x)
            {
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        sum += this->texels[std::size_t(y + dy) * kW + std::size_t((x + dx + kW) % kW)];
                blurred[std::size_t(y) * kW + std::size_t(x)] = sum / 9.0f;
            }
        this->texels.swap(blurred);
    }

    float look(const Vector3 &d) const
    {
        const double lon = std::atan2(d.x, d.y);
        const double lat = std::asin(std::clamp(d.z, -1.0, 1.0));
        const double u = (lon / (2.0 * AowisCompass::kPi) + 0.5) * kW;
        const double v = (0.5 - lat / AowisCompass::kPi) * (kH - 1);
        const int x0 = int(std::floor(u));
        const int y0 = std::clamp(int(std::floor(v)), 0, kH - 2);
        const double fx = u - std::floor(u);
        const double fy = std::clamp(v - y0, 0.0, 1.0);
        const int x1 = x0 + 1;
        const float a = this->texels[std::size_t(y0) * kW + std::size_t((x0 % kW + kW) % kW)];
        const float b = this->texels[std::size_t(y0) * kW + std::size_t((x1 % kW + kW) % kW)];
        const float c = this->texels[std::size_t(y0 + 1) * kW + std::size_t((x0 % kW + kW) % kW)];
        const float e = this->texels[std::size_t(y0 + 1) * kW + std::size_t((x1 % kW + kW) % kW)];
        return float((a + (b - a) * fx) * (1.0 - fy) + (c + (e - c) * fx) * fy);
    }
};

// Raw camera image (raw image turned 'orientation' clockwise from upright).
void render(const World &world, const Quaternion &pose, int orientation, std::mt19937 &random, double noise,
            std::vector<std::uint8_t> *image)
{
    image->resize(std::size_t(kWidth) * kHeight);
    std::normal_distribution<double> grain(0.0, noise);
    const Quaternion m = Fusion::cameraToDevice(orientation);
    const Quaternion toWorld = multiply(pose, m);
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const Vector3 c{(x - 0.5 * (kWidth - 1)) / kFocal, (y - 0.5 * (kHeight - 1)) / kFocal, 1.0};
            const double n = std::sqrt(c.x * c.x + c.y * c.y + 1.0);
            const Vector3 d = rotate(toWorld, {c.x / n, c.y / n, c.z / n});
            const double value = world.look(d) + (noise > 0.0 ? grain(random) : 0.0);
            (*image)[std::size_t(y) * kWidth + std::size_t(x)] = std::uint8_t(std::clamp(value, 0.0, 255.0));
        }
    }
}

// --- Sensor physics (as tests/look_around_field_tests.cpp) ---------------

Vector3 worldField()
{
    return {0.0, kStrength * std::cos(kDip * kDegree), -kStrength * std::sin(kDip * kDegree)};
}

double ease(double x)
{
    return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : 0.5 - 0.5 * std::cos(AowisCompass::kPi * x);
}

double sweep(double t)
{
    return 40.0 * (ease(t - 1.5) - ease(t - 3.0) + ease(t - 4.5) - ease(t - 6.0));
}

struct Motion
{
    double bearing = 90.0;
    bool tilt = true;
};

Quaternion truthAt(const Motion &m, double t)
{
    return m.tilt ? facing(m.bearing, sweep(t)) : facing(m.bearing + sweep(t), 0.0);
}

Vector3 accelerometer(const Motion &m, double t)
{
    const double h = 0.002;
    const Vector3 r0 = rotate(truthAt(m, t - h), {0.0, 0.0, 0.10});
    const Vector3 r1 = rotate(truthAt(m, t), {0.0, 0.0, 0.10});
    const Vector3 r2 = rotate(truthAt(m, t + h), {0.0, 0.0, 0.10});
    const Vector3 world{(r0.x - 2.0 * r1.x + r2.x) / (h * h), (r0.y - 2.0 * r1.y + r2.y) / (h * h),
                        (r0.z - 2.0 * r1.z + r2.z) / (h * h) + 9.80665};
    return rotate(conjugate(truthAt(m, t)), world);
}

struct Result
{
    // Worst error of the view's turn over half a second against the hand's
    // (what is seen as instability; a slowly corrected north offset is not).
    double turnError = 0.0;
    double sideways = 0.0;
    double upDown = 0.0;
    double worstStep = 0.0;
    int cameraFrames = 0;
    int orientation = -1;
};

// Full pipeline: field (accelerometer + magnetometer physics) and camera
// (30 fps, frames delivered 60 ms after exposure), fused, into the engine.
Result pipeline(const World &world, const Motion &m, int cameraOrientation, bool camera,
                double blankFrom, double blankTo, double cameraClockOffset = 0.0)
{
    std::mt19937 random(7);
    std::normal_distribution<double> noise(0.0, 0.3);
    std::mt19937 grain(9);
    const World blank(0, true);
    Engine engine;
    engine.setHeadingFromCompass(false);
    engine.setGyroTimeoutSeconds(Fusion::kEngineGyroTimeoutSeconds);
    engine.setMotionCorrectionFraction(Fusion::kEngineMotionCorrectionFraction);
    FieldSource source;
    source.setExpectedField(kStrength, kDip);
    Field field;
    Fusion fusion;
    Tracker tracker;
    tracker.setFocalLength(kFocal);
    std::vector<Field::Output> outputs;
    field.setOutputHandler([&outputs](const Field::Output &out) { outputs.push_back(out); });
    struct Pending
    {
        double exposure;
        double arrival;
    };
    std::vector<Pending> frames;
    for (double t = 0.0; t < 7.5; t += 1.0 / 30.0)
        frames.push_back({t, t + 0.06});
    std::size_t nextFrame = 0;
    std::vector<std::uint8_t> image;
    Result result;
    Quaternion previousShown;
    bool hasShown = false;
    double lastFrame = 0.0;
    std::vector<double> frameTimes;
    std::vector<double> frameBearings;
    for (int i = 0; i < 750; ++i)
    {
        const double t = i * 0.01;
        if (i % 2 == 0)
        {
            const double tm = t + 0.004;
            Vector3 f = rotate(conjugate(truthAt(m, tm)), worldField());
            f = {f.x + noise(random), f.y + noise(random), f.z + noise(random)};
            field.addMagneticField(f.x, f.y, f.z, tm);
        }
        const Vector3 a = accelerometer(m, t);
        field.addAccelerometer(a.x, a.y, a.z, t);
        for (const Field::Output &out : outputs)
        {
            FieldSource::Step step;
            if (!source.add(out.pose, out.seconds, out.strength, out.dipDegrees, &step))
                continue;
            Fusion::Output fused;
            if (fusion.addField(step, t + 0.012, &fused))
                engine.addGyro(fused.motion, fused.seconds, t + 0.012);
            if (step.trusted)
                engine.addCompass(step.compass, Fusion::fieldTime(step.seconds), t + 0.012);
        }
        outputs.clear();
        while (camera && nextFrame < frames.size() && frames[nextFrame].arrival <= t)
        {
            const double exposure = frames[nextFrame].exposure;
            const bool dark = exposure >= blankFrom && exposure < blankTo;
            render(dark ? blank : world, truthAt(m, exposure), cameraOrientation, grain, 2.0, &image);
            // Camera timestamps on their own clock: never compared with
            // sensor time.
            const Tracker::Result vision = tracker.process(image.data(), kWidth, kHeight, kWidth, 1,
                                                           exposure + cameraClockOffset);
            fusion.addVision(vision, t);
            if (vision.valid && fusion.orientation() >= 0)
                ++result.cameraFrames;
            ++nextFrame;
        }
        if (t - lastFrame >= 1.0 / 60.0 && engine.hasPose())
        {
            lastFrame = t;
            const Quaternion shown = engine.poseForFrame(t);
            if (t > 2.6)
            {
                // Compared with the truth shortly before (display delay).
                double best = 1e9;
                double bestUpDown = 1e9;
                for (double lag = 0.0; lag <= 0.35; lag += 0.01)
                {
                    const Quaternion truth = truthAt(m, t - lag);
                    const double e = m.tilt ? std::fabs(wrapDegrees(bearingOf(shown) - m.bearing))
                                            : std::fabs(wrapDegrees(bearingOf(shown) - bearingOf(truth)));
                    best = std::min(best, e);
                    bestUpDown = std::min(bestUpDown, std::fabs(pitchOf(shown) - pitchOf(truth)));
                }
                result.sideways = std::max(result.sideways, best);
                result.upDown = std::max(result.upDown, bestUpDown);
                if (hasShown)
                    result.worstStep = std::max(result.worstStep, poseDegrees(previousShown, shown));
            }
            previousShown = shown;
            hasShown = true;
            frameTimes.push_back(t);
            frameBearings.push_back(bearingOf(shown));
        }
    }
    for (std::size_t i = 0; i < frameTimes.size(); ++i)
    {
        if (frameTimes[i] < 3.0)
            continue;
        std::size_t j = i;
        while (j > 0 && frameTimes[i] - frameTimes[j] < 0.5)
            --j;
        const double shownTurn = wrapDegrees(frameBearings[i] - frameBearings[j]);
        double best = 1e9;
        for (double lag = 0.0; lag <= 0.35; lag += 0.01)
        {
            const double truthTurn = wrapDegrees(bearingOf(truthAt(m, frameTimes[i] - lag)) -
                                                 bearingOf(truthAt(m, frameTimes[j] - lag)));
            best = std::min(best, std::fabs(shownTurn - truthTurn));
        }
        result.turnError = std::max(result.turnError, best);
    }
    result.orientation = fusion.orientation();
    return result;
}
}

int main()
{
    const World world(5, false);
    std::mt19937 grain(3);

    for (int orientation : {0, 90, 270})
    {
        // Single steps about each device axis: the tracker's rotation, mapped
        // to the device, is the true increment.
        Tracker tracker;
        tracker.setFocalLength(kFocal);
        std::vector<std::uint8_t> image;
        const Quaternion start = facing(30.0, 5.0);
        render(world, start, orientation, grain, 2.0, &image);
        tracker.process(image.data(), kWidth, kHeight, kWidth, 1, 0.0);
        double worst = 0.0;
        int valid = 0;
        Quaternion pose = start;
        const Quaternion steps[] = {axisAngle(0.0, 1.0, 0.0, 1.5), axisAngle(1.0, 0.0, 0.0, -1.2),
                                    axisAngle(0.0, 0.0, 1.0, 2.0), axisAngle(0.6, 0.8, 0.0, 3.0)};
        double time = 0.0;
        for (const Quaternion &step : steps)
        {
            const Quaternion next = multiply(pose, step);
            render(world, next, orientation, grain, 2.0, &image);
            time += 1.0 / 30.0;
            const Tracker::Result r = tracker.process(image.data(), kWidth, kHeight, kWidth, 1, time);
            if (r.valid)
            {
                ++valid;
                worst = std::max(worst, poseDegrees(Fusion::deviceDelta(r.rotation, orientation), step));
            }
            pose = next;
        }
        std::printf("camera image turned %3d deg: %d/4 steps tracked, worst rotation error %.3f deg\n",
                    orientation, valid, worst);
        expectTrue(valid == 4 && worst < 0.1, "camera rotation steps are measured to a tenth of a degree");
    }

    {
        // A blank view (white wall, sky, darkness) is never "reliable".
        const World blank(0, true);
        Tracker tracker;
        tracker.setFocalLength(kFocal);
        std::vector<std::uint8_t> image;
        int valid = 0;
        for (int i = 0; i < 10; ++i)
        {
            render(blank, facing(0.0, 0.0), 90, grain, 3.0, &image);
            valid += tracker.process(image.data(), kWidth, kHeight, kWidth, 1, i / 30.0).valid ? 1 : 0;
        }
        expectTrue(valid == 0, "a featureless view gives no camera rotation");
    }

    for (int orientation : {0, 90, 180, 270})
    {
        const Result r = pipeline(world, {90.0, true}, orientation, true, -1.0, -1.0);
        std::printf("image turned %3d deg: decided %3d, %d camera updates\n", orientation, r.orientation, r.cameraFrames);
        expectTrue(r.orientation == orientation, "the camera image orientation is found from the accelerometer");
    }

    {
        double fieldOnly = 0.0;
        double withCamera = 0.0;
        for (double bearing : {0.0, 90.0, 180.0, 270.0})
        {
            const Result f = pipeline(world, {bearing, true}, 90, false, -1.0, -1.0);
            const Result c = pipeline(world, {bearing, true}, 90, true, -1.0, -1.0);
            std::printf("up/down sweeps facing %3.0f: sideways shake %.2f deg compass-only, %.2f deg with camera "
                        "(up/down within %.2f / %.2f deg)\n", bearing, f.sideways, c.sideways, f.upDown, c.upDown);
            fieldOnly = std::max(fieldOnly, f.sideways);
            withCamera = std::max(withCamera, c.sideways);
        }
        expectTrue(withCamera < 0.5 * fieldOnly, "with the camera, up/down sweeps shake sideways at most half as much");

        const Result turnField = pipeline(world, {90.0, false}, 90, false, -1.0, -1.0);
        const Result turnCamera = pipeline(world, {90.0, false}, 90, true, -1.0, -1.0);
        std::printf("sideways sweeps: turn error %.2f deg compass-only, %.2f deg with camera "
                    "(north offset up to %.2f / %.2f deg, corrected slowly)\n",
                    turnField.turnError, turnCamera.turnError, turnField.sideways, turnCamera.sideways);
        // A pure turn is already followed well without the camera (the
        // magnetometer's error comes with tilting); the camera must not
        // make it worse beyond the slow north correction made while turning.
        expectTrue(turnCamera.turnError < 1.5, "with the camera, sideways sweeps are followed within 1.5 degrees");
    }

    {
        // Phone lying on a shaken table: the camera sees the table from a few
        // centimetres and reports rotations of up to 1 deg per frame that
        // never happened; gravity shows none. The view must stay as calm as
        // without the camera.
        double shake[2] = {0.0, 0.0};
        int implausible = 0;
        for (int camera = 0; camera < 2; ++camera)
        {
            std::mt19937 random(31);
            std::normal_distribution<double> spurious(0.0, 0.6);
            std::normal_distribution<double> tremor(0.0, 0.02);
            Engine engine;
            engine.setHeadingFromCompass(false);
            engine.setMotionIsGeographic(true);
            engine.setGyroTimeoutSeconds(Fusion::kEngineGyroTimeoutSeconds);
            Fusion fusion;
            fusion.forceOrientation(90);
            FieldSource source;
            Quaternion previous;
            bool hasPrevious = false;
            for (int i = 0; i < 600; ++i)
            {
                const double t = i * 0.01;
                // Lying flat, screen up, field readings with a little noise.
                const Quaternion lying = multiply(axisAngle(0.0, 0.0, 1.0, -90.0 + tremor(random)),
                                                  axisAngle(1.0, 0.0, 0.0, tremor(random)));
                FieldSource::Step step;
                if (i % 2 == 0 && source.add(lying, t, kStrength, kDip, &step))
                {
                    Fusion::Output fused;
                    if (fusion.addField(step, t + 0.01, &fused))
                        engine.addGyro(fused.motion, fused.seconds, t + 0.01);
                    engine.addCompass(step.compass, Fusion::fieldTime(step.seconds), t + 0.01);
                }
                if (camera != 0 && i % 4 == 0)
                {
                    Tracker::Result r;
                    r.valid = true;
                    r.rotation = AowisCompass::normalized(multiply(multiply(
                        axisAngle(1.0, 0.0, 0.0, spurious(random)), axisAngle(0.0, 1.0, 0.0, spurious(random))),
                        axisAngle(0.0, 0.0, 1.0, spurious(random))));
                    fusion.addVision(r, t + 0.01);
                }
                if (i % 2 == 1 && engine.hasPose() && t > 1.0)
                {
                    const Quaternion shown = engine.poseForFrame(t + 0.01);
                    if (hasPrevious)
                        shake[camera] = std::max(shake[camera], poseDegrees(previous, shown));
                    previous = shown;
                    hasPrevious = true;
                }
            }
            implausible = fusion.implausibleFrames();
        }
        std::printf("phone on a shaken table: worst frame step %.3f deg without camera, %.3f deg with (%d frames ignored)\n",
                    shake[0], shake[1], implausible);
        expectTrue(implausible > 50 && shake[1] < 1.5 * shake[0] + 0.02,
                   "close-up camera motion that gravity does not confirm is ignored");
    }

    for (double offset : {0.4, -7.0})
    {
        // The camera clock differs from the sensor clock: the camera still
        // learns, stabilizes and lets the view turn.
        const Result tilt = pipeline(world, {90.0, true}, 90, true, -1.0, -1.0, offset);
        const Result turn = pipeline(world, {90.0, false}, 90, true, -1.0, -1.0, offset);
        std::printf("camera clock %+.1f s: decided %d, up/down shake %.2f deg, sideways turn error %.2f deg\n",
                    offset, turn.orientation, tilt.sideways, turn.turnError);
        expectTrue(turn.orientation == 90 && tilt.sideways < 1.0 && turn.turnError < 1.5,
                   "a camera clock unlike the sensor clock changes nothing");
    }

    {
        // The view goes blank for a second mid-sweep: compass-only motion
        // takes over, the camera takes back, without a jump.
        const Result r = pipeline(world, {90.0, true}, 90, true, 3.3, 4.3);
        std::printf("camera blind 3.3-4.3 s: sideways shake %.2f deg, worst frame step %.2f deg\n",
                    r.sideways, r.worstStep);
        expectTrue(r.worstStep < 2.0 && r.sideways < 3.0, "losing and regaining the camera does not jump the view");
    }

    std::printf("look_around_vision: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
