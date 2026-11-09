// Replays a Look-around raw sensor recording (sensors-<utc>.csv, written by
// the app; fetched by tools/pull_look_around.sh) through the CURRENT motion
// code, as the map would run it:
//
//   phone   - the phone's own orientation: OS rotation vector (+ game rotation
//             vector), else AOWIS' own fusion of the raw sensors
//             (compass_fusion.h), as with camera stabilization off;
//   field   - the own accelerometer + magnetometer orientation alone
//             (look_around_field.h; camera stabilization's fallback);
//   camera  - camera stabilization with the recorded camera results
//             (look_around_vision.h), if the recording has any.
//
// Frames are shown at 60 Hz on the arrival clock, as on the phone. Writes
// <recording>-replay.csv (per frame: bearing and pitch of each path) and
// prints a summary: per path the frame-to-frame jitter while the phone is
// held still (what is seen as shake), the same while moving, the worst
// single frame step (what is seen as a jump), and the lag of up/down behind
// the phone path. Qt-free; built by tools/replay_look_around.sh.
#include "compass_fusion.h"
#include "look_around_engine.h"
#include "look_around_field.h"
#include "look_around_vision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
typedef AowisCompass::Quaternion Quaternion;
typedef AowisCompass::Vector3 Vector3;
using namespace AowisMobileMap;

constexpr double kDegree = AowisCompass::kPi / 180.0;

struct Record
{
    char kind = 0;
    double seconds = 0.0;
    double arrival = 0.0;
    double v[6] = {0, 0, 0, 0, 0, 0};
};

Quaternion conjugate(const Quaternion &q)
{
    return {-q.x, -q.y, -q.z, q.w};
}

Vector3 rotate(const Quaternion &q, const Vector3 &v)
{
    const Quaternion r = AowisCompass::multiply(AowisCompass::multiply(q, {v.x, v.y, v.z, 0.0}), conjugate(q));
    return {r.x, r.y, r.z};
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

double wrap(double degrees)
{
    return std::remainder(degrees, 360.0);
}

struct Path
{
    const char *name;
    LookAroundEngine engine;
    std::vector<double> bearing;
    std::vector<double> pitch;
};

// Frame-to-frame jitter: rms of the change of the per-frame step (bearing and
// pitch combined), over frames where 'select' holds.
double jitter(const Path &path, const std::vector<char> &select)
{
    double sum = 0.0;
    int count = 0;
    for (std::size_t i = 2; i < path.bearing.size(); ++i)
    {
        if (!select[i] || std::isnan(path.bearing[i - 2]) || std::isnan(path.bearing[i]))
            continue;
        const double b = wrap(path.bearing[i] - path.bearing[i - 1]) - wrap(path.bearing[i - 1] - path.bearing[i - 2]);
        const double p = (path.pitch[i] - path.pitch[i - 1]) - (path.pitch[i - 1] - path.pitch[i - 2]);
        sum += b * b + p * p;
        ++count;
    }
    return count > 0 ? std::sqrt(sum / count) : std::nan("");
}

// Largest frame-to-frame step of the view (degrees, bearing scaled by
// cos(pitch)), with the camera within 80 deg of the horizon: what is seen
// as a jump.
double worstStep(const Path &path)
{
    double worst = 0.0;
    for (std::size_t i = 1; i < path.bearing.size(); ++i)
    {
        if (std::isnan(path.bearing[i - 1]) || std::isnan(path.bearing[i]) || std::fabs(path.pitch[i]) > 80.0)
            continue;
        const double b = wrap(path.bearing[i] - path.bearing[i - 1]) * std::cos(path.pitch[i] * kDegree);
        worst = std::max(worst, std::hypot(b, path.pitch[i] - path.pitch[i - 1]));
    }
    return worst;
}

// Lag (seconds) of a path's pitch behind the reference path's, by best
// alignment over shifts of whole frames.
double lagBehind(const Path &path, const Path &reference, double frameSeconds)
{
    double best = 1e18;
    int bestShift = 0;
    for (int shift = -6; shift <= 30; ++shift)
    {
        double sum = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < path.pitch.size(); ++i)
        {
            const long j = long(i) - shift;
            if (j < 0 || j >= long(reference.pitch.size()) || std::isnan(path.pitch[i]) || std::isnan(reference.pitch[std::size_t(j)]))
                continue;
            const double d = path.pitch[i] - reference.pitch[std::size_t(j)];
            sum += d * d;
            ++count;
        }
        if (count > 100 && sum / count < best)
        {
            best = sum / count;
            bestShift = shift;
        }
    }
    return bestShift * frameSeconds;
}
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s sensors-<utc>.csv\n", argv[0]);
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input)
    {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    std::vector<Record> records;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#' || line.compare(0, 4, "kind") == 0)
            continue;
        std::stringstream fields(line);
        std::string cell;
        Record record;
        int column = 0;
        while (std::getline(fields, cell, ','))
        {
            if (column == 0)
                record.kind = cell.empty() ? 0 : cell[0];
            else if (column == 1)
                record.seconds = std::atof(cell.c_str());
            else if (column == 2)
                record.arrival = std::atof(cell.c_str());
            else if (column - 3 < 6)
                record.v[column - 3] = std::atof(cell.c_str());
            ++column;
        }
        if (record.kind != 0)
            records.push_back(record);
    }
    std::stable_sort(records.begin(), records.end(),
                     [](const Record &a, const Record &b) { return a.arrival < b.arrival; });
    if (records.size() < 100)
    {
        std::fprintf(stderr, "%s: too few records (%zu)\n", argv[1], records.size());
        return 1;
    }

    bool hasRotationVector = false;
    bool hasGame = false;
    bool hasGyro = false;
    int cameraResults = 0;
    for (const Record &r : records)
    {
        hasRotationVector = hasRotationVector || r.kind == 'R';
        hasGame = hasGame || r.kind == 'G';
        hasGyro = hasGyro || r.kind == 'Y';
        cameraResults += r.kind == 'C' ? 1 : 0;
    }

    Path phone{"phone", {}, {}, {}};
    Path field{"field", {}, {}, {}};
    Path camera{"camera", {}, {}, {}};
    field.engine.setMotionIsGeographic(true);
    field.engine.setHeadingFromCompass(false);
    field.engine.setGyroTimeoutSeconds(VisionFusion::kEngineGyroTimeoutSeconds);
    field.engine.setMotionCorrectionFraction(VisionFusion::kEngineMotionCorrectionFraction);
    camera.engine.setMotionIsGeographic(true);
    camera.engine.setHeadingFromCompass(false);
    camera.engine.setGyroTimeoutSeconds(VisionFusion::kEngineGyroTimeoutSeconds);
    camera.engine.setMotionCorrectionFraction(VisionFusion::kEngineMotionCorrectionFraction);

    AowisCompass::AttitudeFusion attitude;
    attitude.setGyroscopeAvailable(hasGyro);
    FieldOrientation orientation;
    std::vector<FieldOrientation::Output> outputs;
    orientation.setOutputHandler([&outputs](const FieldOrientation::Output &out) { outputs.push_back(out); });
    LookAroundFieldSource fieldSource;
    LookAroundFieldSource cameraSource;
    VisionFusion fieldFusion;
    VisionFusion cameraFusion;
    LookAroundFrameClock clock;

    const double frameSeconds = 1.0 / 60.0;
    double nextFrame = records.front().arrival + frameSeconds;
    std::vector<double> frameTimes;
    std::size_t index = 0;
    while (index < records.size())
    {
        const double now = nextFrame;
        while (index < records.size() && records[index].arrival <= now)
        {
            const Record &r = records[index++];
            if (r.kind == 'A')
            {
                orientation.addAccelerometer(r.v[0], r.v[1], r.v[2], r.seconds);
                attitude.addAccelerometer(r.v[0], r.v[1], r.v[2], r.seconds);
            }
            else if (r.kind == 'M')
            {
                orientation.addMagneticField(r.v[0], r.v[1], r.v[2], r.seconds);
                attitude.addMagneticField(r.v[0], r.v[1], r.v[2], r.seconds);
                if (!hasRotationVector && attitude.hasOrientation())
                    phone.engine.addCompass(attitude.orientation(), r.seconds, r.arrival);
            }
            else if (r.kind == 'Y')
                attitude.addGyroscope(r.v[0], r.v[1], r.v[2], r.seconds);
            else if (r.kind == 'R')
                phone.engine.addCompass({r.v[0], r.v[1], r.v[2], r.v[3]}, r.seconds, r.arrival);
            else if (r.kind == 'G' && hasRotationVector)
                phone.engine.addGyro({r.v[0], r.v[1], r.v[2], r.v[3]}, r.seconds, r.arrival);
            else if (r.kind == 'C')
            {
                VisionTracker::Result result;
                result.valid = r.v[0] > 0.5;
                result.inliers = int(r.v[1]);
                result.rotation = {r.v[2], r.v[3], r.v[4], r.v[5]};
                result.toSeconds = r.seconds;
                cameraFusion.addVision(result, r.arrival);
            }
            for (const FieldOrientation::Output &out : outputs)
            {
                LookAroundFieldSource::Step step;
                if (fieldSource.add(out.pose, out.seconds, out.strength, out.dipDegrees, &step))
                {
                    VisionFusion::Output fused;
                    if (fieldFusion.addField(step, r.arrival, &fused))
                        field.engine.addGyro(fused.motion, fused.seconds, r.arrival);
                    if (step.trusted)
                        field.engine.addCompass(step.compass, VisionFusion::fieldTime(step.seconds), r.arrival);
                }
                if (cameraResults > 0 && cameraSource.add(out.pose, out.seconds, out.strength, out.dipDegrees, &step))
                {
                    VisionFusion::Output fused;
                    if (cameraFusion.addField(step, r.arrival, &fused))
                        camera.engine.addGyro(fused.motion, fused.seconds, r.arrival);
                    if (step.trusted)
                        camera.engine.addCompass(step.compass, VisionFusion::fieldTime(step.seconds), r.arrival);
                }
            }
            outputs.clear();
        }
        const double paced = clock.frameTime(now);
        frameTimes.push_back(now);
        Path *paths[3] = {&phone, &field, &camera};
        for (Path *path : paths)
        {
            if (path->engine.hasPose())
            {
                const Quaternion shown = path->engine.poseForFrame(paced);
                path->bearing.push_back(bearingOf(shown));
                path->pitch.push_back(pitchOf(shown));
            }
            else
            {
                path->bearing.push_back(std::nan(""));
                path->pitch.push_back(std::nan(""));
            }
        }
        nextFrame += frameSeconds;
    }

    // Still vs moving, from the phone path's speed (degrees per second over
    // a quarter second).
    const std::size_t frames = frameTimes.size();
    std::vector<char> still(frames, 0);
    std::vector<char> moving(frames, 0);
    const std::size_t span = 15;
    bool phoneShown = false;
    for (double b : phone.bearing)
        phoneShown = phoneShown || !std::isnan(b);
    const Path &reference = phoneShown ? phone : field;
    for (std::size_t i = span; i < frames; ++i)
    {
        if (std::isnan(reference.bearing[i]) || std::isnan(reference.bearing[i - span]))
            continue;
        const double turn = std::hypot(wrap(reference.bearing[i] - reference.bearing[i - span]),
                                       reference.pitch[i] - reference.pitch[i - span]) / (span * frameSeconds);
        still[i] = turn < 3.0 ? 1 : 0;
        moving[i] = turn > 15.0 ? 1 : 0;
    }

    const std::string output = std::string(argv[1]).substr(0, std::string(argv[1]).size() - 4) + "-replay.csv";
    std::FILE *file = std::fopen(output.c_str(), "w");
    if (file != nullptr)
    {
        std::fprintf(file, "seconds,still,phone_bearing,phone_pitch,field_bearing,field_pitch,camera_bearing,camera_pitch\n");
        for (std::size_t i = 0; i < frames; ++i)
            std::fprintf(file, "%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", frameTimes[i] - frameTimes.front(), still[i],
                         phone.bearing[i], phone.pitch[i], field.bearing[i], field.pitch[i],
                         camera.bearing[i], camera.pitch[i]);
        std::fclose(file);
    }

    int stillFrames = 0;
    int movingFrames = 0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        stillFrames += still[i];
        movingFrames += moving[i];
    }
    std::printf("%s\n", argv[1]);
    std::printf("  %.0f s, sensors: %s%s%s, camera results %d, frames %zu (still %d, moving %d)\n",
                frameTimes.back() - frameTimes.front(), hasRotationVector ? "rotation vector, " : "no rotation vector, ",
                hasGame ? "game rotation vector, " : "", hasGyro ? "gyroscope" : "no gyroscope", cameraResults, frames,
                stillFrames, movingFrames);
    const Path *paths[3] = {&phone, &field, &camera};
    for (const Path *path : paths)
    {
        if (path == &camera && cameraResults == 0)
            continue;
        std::printf("  %-6s jitter still %.4f deg, jitter moving %.4f deg, worst frame step %.2f deg,"
                    " up/down lag behind phone %+.0f ms\n",
                    path->name, jitter(*path, still), jitter(*path, moving), worstStep(*path),
                    path == &phone ? 0.0 : 1000.0 * lagBehind(*path, phone, frameSeconds));
    }
    std::printf("  per frame: %s\n", output.c_str());
    return 0;
}
