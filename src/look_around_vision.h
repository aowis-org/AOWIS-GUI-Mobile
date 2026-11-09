#pragma once

#include "compass_fusion.h"
#include "compass_heading.h"
#include "look_around_field.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Camera rotation tracking for Look around on phones without a real
// gyroscope. Qt-free; tested in tests/look_around_vision_tests.cpp.
//
// While the phone only turns in place, distant scene points move in the
// camera image as a pure rotation. VisionTracker follows about 48 corner
// points from frame to frame (pyramidal Lucas-Kanade, with a forward-backward
// check) and estimates the camera's rotation from their directions (RANSAC
// over point pairs, refined on all inliers). Moving people or cars and the
// parallax of near objects are outliers. A frame is "reliable" only with
// enough consistent points; dark, blank, blurred or sky-only views are not.
//
// VisionFusion turns this into a gyroscope-like pose for LookAroundEngine:
// * reliable camera rotation turns the pose's heading (about Up): the axis
//   the magnetometer gets wrong; tilt (up/down, roll) always follows gravity
//   through the field orientation, which is fresher than the camera;
// * otherwise (no reliable camera for kVisionTimeoutSeconds) the field
//   orientation's own motion moves it (LookAroundFieldSource: compass-only).
// * camera frames are applied as rotations between frames when they
//   arrive; the output runs on the motion sensors' time line. Camera
//   timestamps are never compared with sensor timestamps.
// The engine then follows north slowly (setHeadingFromCompass(false)): the
// compass corrects, it does not move the view.
//
// How the camera image is rotated against the phone (sensor orientation 0,
// 90, 180 or 270 deg) is decided by comparing the camera's rotations with
// the field orientation's over the same arrival intervals (tilting or
// turning), so
// wrong or missing driver metadata cannot turn the view the wrong way. The
// camera is not used before that decision.
//
// Camera coordinates: x right, y down in the raw image, z forward. Device:
// Android (X right, Y top edge, Z out of the screen); the back camera looks
// along -Z.
namespace AowisMobileMap
{
class VisionTracker final
{
public:
    typedef AowisCompass::Quaternion Quaternion;

    // Working image width (the input is box-downscaled by an integer factor).
    static constexpr int kTargetWidth = 320;
    static constexpr int kLevels = 3;
    static constexpr int kHalfWindow = 4;          // 9x9 tracking window
    static constexpr int kIterations = 12;
    static constexpr int kGridColumns = 8;
    static constexpr int kGridRows = 6;
    static constexpr std::size_t kRedetectBelow = 40;
    static constexpr double kForwardBackwardPixels = 0.7;
    static constexpr double kMinCornerScore = 12.0; // min eigenvalue, per pixel, 0..255 images
    static constexpr int kRansacIterations = 64;
    static constexpr double kInlierPixels = 1.5;
    static constexpr std::size_t kMinInliers = 12;
    static constexpr double kMinInlierRatio = 0.5;
    static constexpr double kMaxRotationRadians = 0.5;

    struct Result
    {
        bool valid = false;
        // b_now = rotation * b_previous for scene directions in camera
        // coordinates (the inverse of the camera's own rotation).
        Quaternion rotation;
        double fromSeconds = 0.0;
        double toSeconds = 0.0;
        int tracked = 0;
        int inliers = 0;
    };

    void reset()
    {
        this->m_hasPrevious = false;
        this->m_points.clear();
    }

    // Horizontal focal length in pixels of the INPUT image; principal point
    // at the centre.
    void setFocalLength(double inputPixels) { this->m_inputFocal = inputPixels; }

    // Luma plane of one frame (row stride and pixel stride in bytes).
    Result process(const std::uint8_t *luma, int width, int height, int rowStride, int pixelStride,
                   double seconds)
    {
        Result result;
        if (luma == nullptr || width < 32 || height < 24 || !std::isfinite(seconds))
            return result;
        const int factor = std::max(1, (width + kTargetWidth / 2) / kTargetWidth);
        buildLevelZero(luma, width, height, rowStride, pixelStride, factor);
        for (int level = 1; level < kLevels; ++level)
            downsample(this->m_current[level - 1], &this->m_current[level]);
        const double focal = (this->m_inputFocal > 0.0 ? this->m_inputFocal : 0.78 * width) / factor;

        if (this->m_hasPrevious && seconds > this->m_previousSeconds)
        {
            result = trackAndEstimate(focal);
            result.fromSeconds = this->m_previousSeconds;
            result.toSeconds = seconds;
        }
        else
            this->m_points.clear();
        if (this->m_points.size() < kRedetectBelow)
            detect();
        std::swap(this->m_previous, this->m_current);
        this->m_previousSeconds = seconds;
        this->m_hasPrevious = true;
        return result;
    }

    std::size_t pointCount() const { return this->m_points.size(); }

private:
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<float> pixels;

        float at(int x, int y) const { return this->pixels[std::size_t(y) * std::size_t(this->width) + std::size_t(x)]; }

        // Bilinear; false outside.
        bool sample(double x, double y, float *value) const
        {
            if (!(x >= 0.0) || !(y >= 0.0) || x > this->width - 1.001 || y > this->height - 1.001)
                return false;
            const int ix = int(x);
            const int iy = int(y);
            const float fx = float(x - ix);
            const float fy = float(y - iy);
            const float a = at(ix, iy);
            const float b = at(ix + 1, iy);
            const float c = at(ix, iy + 1);
            const float d = at(ix + 1, iy + 1);
            *value = (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
            return true;
        }
    };

    struct Point
    {
        double x = 0.0; // level-0 pixels
        double y = 0.0;
    };

    void buildLevelZero(const std::uint8_t *luma, int width, int height, int rowStride, int pixelStride, int factor)
    {
        Image &image = this->m_current[0];
        image.width = width / factor;
        image.height = height / factor;
        image.pixels.resize(std::size_t(image.width) * std::size_t(image.height));
        const float scale = 1.0f / float(factor * factor);
        for (int y = 0; y < image.height; ++y)
        {
            for (int x = 0; x < image.width; ++x)
            {
                int sum = 0;
                for (int dy = 0; dy < factor; ++dy)
                {
                    const std::uint8_t *row = luma + std::size_t(y * factor + dy) * std::size_t(rowStride);
                    for (int dx = 0; dx < factor; ++dx)
                        sum += row[std::size_t(x * factor + dx) * std::size_t(pixelStride)];
                }
                image.pixels[std::size_t(y) * std::size_t(image.width) + std::size_t(x)] = float(sum) * scale;
            }
        }
    }

    static void downsample(const Image &from, Image *to)
    {
        to->width = from.width / 2;
        to->height = from.height / 2;
        to->pixels.resize(std::size_t(to->width) * std::size_t(to->height));
        for (int y = 0; y < to->height; ++y)
            for (int x = 0; x < to->width; ++x)
                to->pixels[std::size_t(y) * std::size_t(to->width) + std::size_t(x)] =
                    0.25f * (from.at(2 * x, 2 * y) + from.at(2 * x + 1, 2 * y) +
                             from.at(2 * x, 2 * y + 1) + from.at(2 * x + 1, 2 * y + 1));
    }

    // Shi-Tomasi corners: the best per grid cell not already holding a point.
    void detect()
    {
        const Image &image = this->m_current[0];
        const int margin = kHalfWindow * (1 << (kLevels - 1)) + 2;
        const double cellWidth = double(image.width - 2 * margin) / kGridColumns;
        const double cellHeight = double(image.height - 2 * margin) / kGridRows;
        if (cellWidth < 4.0 || cellHeight < 4.0)
            return;
        std::array<bool, kGridColumns * kGridRows> occupied{};
        for (const Point &p : this->m_points)
        {
            const int cx = int((p.x - margin) / cellWidth);
            const int cy = int((p.y - margin) / cellHeight);
            if (cx >= 0 && cx < kGridColumns && cy >= 0 && cy < kGridRows)
                occupied[std::size_t(cy * kGridColumns + cx)] = true;
        }
        for (int cy = 0; cy < kGridRows; ++cy)
        {
            for (int cx = 0; cx < kGridColumns; ++cx)
            {
                if (occupied[std::size_t(cy * kGridColumns + cx)])
                    continue;
                double best = kMinCornerScore;
                Point bestPoint;
                bool found = false;
                const int x0 = margin + int(cx * cellWidth);
                const int y0 = margin + int(cy * cellHeight);
                const int x1 = margin + int((cx + 1) * cellWidth);
                const int y1 = margin + int((cy + 1) * cellHeight);
                for (int y = y0; y < y1; y += 2)
                {
                    for (int x = x0; x < x1; x += 2)
                    {
                        const double score = cornerScore(image, x, y);
                        if (score > best)
                        {
                            best = score;
                            bestPoint = {double(x), double(y)};
                            found = true;
                        }
                    }
                }
                if (found)
                    this->m_points.push_back(bestPoint);
            }
        }
    }

    // Smaller eigenvalue of the structure tensor over a 5x5 window, per pixel.
    static double cornerScore(const Image &image, int x, int y)
    {
        double xx = 0.0;
        double yy = 0.0;
        double xy = 0.0;
        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                const int px = x + dx;
                const int py = y + dy;
                const double gx = 0.5 * (image.at(px + 1, py) - image.at(px - 1, py));
                const double gy = 0.5 * (image.at(px, py + 1) - image.at(px, py - 1));
                xx += gx * gx;
                yy += gy * gy;
                xy += gx * gy;
            }
        }
        const double trace = 0.5 * (xx + yy);
        const double det = xx * yy - xy * xy;
        return (trace - std::sqrt(std::max(0.0, trace * trace - det))) / 25.0;
    }

    // Pyramidal Lucas-Kanade from image 'from' at p to image 'to'; the
    // initial guess is 'guess' (level-0 pixels).
    static bool trackPoint(const std::array<Image, kLevels> &from, const std::array<Image, kLevels> &to,
                           const Point &p, const Point &guess, Point *out)
    {
        double gx = guess.x - p.x;
        double gy = guess.y - p.y;
        double dx = 0.0;
        double dy = 0.0;
        for (int level = kLevels - 1; level >= 0; --level)
        {
            const double scale = 1.0 / double(1 << level);
            const double px = p.x * scale;
            const double py = p.y * scale;
            const Image &a = from[std::size_t(level)];
            const Image &b = to[std::size_t(level)];
            constexpr int size = (2 * kHalfWindow + 1) * (2 * kHalfWindow + 1);
            std::array<float, size> templ{};
            std::array<float, size> ix{};
            std::array<float, size> iy{};
            double gxx = 0.0;
            double gyy = 0.0;
            double gxy = 0.0;
            int index = 0;
            for (int wy = -kHalfWindow; wy <= kHalfWindow; ++wy)
            {
                for (int wx = -kHalfWindow; wx <= kHalfWindow; ++wx, ++index)
                {
                    float c = 0.0f;
                    float l = 0.0f;
                    float r = 0.0f;
                    float u = 0.0f;
                    float d = 0.0f;
                    if (!a.sample(px + wx, py + wy, &c) || !a.sample(px + wx - 1, py + wy, &l) ||
                        !a.sample(px + wx + 1, py + wy, &r) || !a.sample(px + wx, py + wy - 1, &u) ||
                        !a.sample(px + wx, py + wy + 1, &d))
                        return false;
                    templ[std::size_t(index)] = c;
                    ix[std::size_t(index)] = 0.5f * (r - l);
                    iy[std::size_t(index)] = 0.5f * (d - u);
                    gxx += double(ix[std::size_t(index)]) * ix[std::size_t(index)];
                    gyy += double(iy[std::size_t(index)]) * iy[std::size_t(index)];
                    gxy += double(ix[std::size_t(index)]) * iy[std::size_t(index)];
                }
            }
            const double det = gxx * gyy - gxy * gxy;
            if (det < 1e-3 * size * size)
                return false;
            // Guess and accumulated motion at this level.
            double vx = gx * scale + dx;
            double vy = gy * scale + dy;
            for (int iteration = 0; iteration < kIterations; ++iteration)
            {
                double bx = 0.0;
                double by = 0.0;
                index = 0;
                for (int wy = -kHalfWindow; wy <= kHalfWindow; ++wy)
                {
                    for (int wx = -kHalfWindow; wx <= kHalfWindow; ++wx, ++index)
                    {
                        float value = 0.0f;
                        if (!b.sample(px + wx + vx, py + wy + vy, &value))
                            return false;
                        const double diff = double(templ[std::size_t(index)]) - value;
                        bx += diff * ix[std::size_t(index)];
                        by += diff * iy[std::size_t(index)];
                    }
                }
                const double sx = (gyy * bx - gxy * by) / det;
                const double sy = (gxx * by - gxy * bx) / det;
                vx += sx;
                vy += sy;
                if (sx * sx + sy * sy < 1e-4)
                    break;
            }
            if (level > 0)
            {
                // Carry the motion found here (beyond the guess) to the next level.
                dx = 2.0 * (vx - gx * scale);
                dy = 2.0 * (vy - gy * scale);
            }
            else
            {
                out->x = p.x + vx;
                out->y = p.y + vy;
            }
        }
        return true;
    }

    struct Bearing
    {
        double x;
        double y;
        double z;
    };

    Bearing bearing(const Point &p, double focal) const
    {
        const Image &image = this->m_current[0];
        const double x = (p.x - 0.5 * (image.width - 1)) / focal;
        const double y = (p.y - 0.5 * (image.height - 1)) / focal;
        const double n = std::sqrt(x * x + y * y + 1.0);
        return {x / n, y / n, 1.0 / n};
    }

    Result trackAndEstimate(double focal)
    {
        Result result;
        std::vector<Point> from;
        std::vector<Point> to;
        from.reserve(this->m_points.size());
        to.reserve(this->m_points.size());
        for (const Point &p : this->m_points)
        {
            Point forward;
            Point back;
            if (!trackPoint(this->m_previous, this->m_current, p, p, &forward))
                continue;
            if (!trackPoint(this->m_current, this->m_previous, forward, p, &back))
                continue;
            if (std::hypot(back.x - p.x, back.y - p.y) > kForwardBackwardPixels)
                continue;
            from.push_back(p);
            to.push_back(forward);
        }
        result.tracked = int(from.size());
        this->m_points.clear();
        if (from.size() < 2)
            return result;
        std::vector<Bearing> a;
        std::vector<Bearing> b;
        for (std::size_t i = 0; i < from.size(); ++i)
        {
            a.push_back(bearing(from[i], focal));
            b.push_back(bearing(to[i], focal));
        }
        // RANSAC over pairs (deterministic sequence).
        const double threshold = kInlierPixels / focal;
        std::vector<char> best;
        std::size_t bestCount = 0;
        std::uint32_t state = 0x9e3779b9u;
        for (int iteration = 0; iteration < kRansacIterations; ++iteration)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const std::size_t i = state % a.size();
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const std::size_t j = state % a.size();
            if (i == j)
                continue;
            double m[9];
            if (!rotationFromPairs(a[i], a[j], b[i], b[j], m))
                continue;
            std::vector<char> inliers(a.size(), 0);
            std::size_t count = 0;
            for (std::size_t k = 0; k < a.size(); ++k)
            {
                const double rx = m[0] * a[k].x + m[1] * a[k].y + m[2] * a[k].z;
                const double ry = m[3] * a[k].x + m[4] * a[k].y + m[5] * a[k].z;
                const double rz = m[6] * a[k].x + m[7] * a[k].y + m[8] * a[k].z;
                const double ex = ry * b[k].z - rz * b[k].y;
                const double ey = rz * b[k].x - rx * b[k].z;
                const double ez = rx * b[k].y - ry * b[k].x;
                if (std::sqrt(ex * ex + ey * ey + ez * ez) < threshold)
                {
                    inliers[k] = 1;
                    ++count;
                }
            }
            if (count > bestCount)
            {
                bestCount = count;
                best = inliers;
            }
        }
        result.inliers = int(bestCount);
        if (bestCount < kMinInliers || double(bestCount) < kMinInlierRatio * double(a.size()))
        {
            // Unreliable: keep the tracked points for the next frame anyway.
            this->m_points = to;
            return result;
        }
        // Refine on all inliers (Horn: quaternion of the best rotation).
        double s[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        for (std::size_t k = 0; k < a.size(); ++k)
        {
            if (!best[k])
                continue;
            const double av[3] = {a[k].x, a[k].y, a[k].z};
            const double bv[3] = {b[k].x, b[k].y, b[k].z};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    s[r][c] += av[r] * bv[c];
            this->m_points.push_back(to[k]);
        }
        const Quaternion q = hornRotation(s);
        const double angle = 2.0 * std::acos(std::clamp(std::fabs(q.w), 0.0, 1.0));
        if (!(angle < kMaxRotationRadians))
            return result;
        result.rotation = q;
        result.valid = true;
        return result;
    }

    // Rotation M (row-major) with M a1 = b1 and M a2 ~ b2.
    static bool rotationFromPairs(const Bearing &a1, const Bearing &a2, const Bearing &b1, const Bearing &b2, double *m)
    {
        double fa[9];
        double fb[9];
        if (!frame(a1, a2, fa) || !frame(b1, b2, fb))
            return false;
        // M = Fb * Fa^T (columns of F are the frame axes).
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                m[r * 3 + c] = fb[r * 3 + 0] * fa[c * 3 + 0] + fb[r * 3 + 1] * fa[c * 3 + 1] + fb[r * 3 + 2] * fa[c * 3 + 2];
        return true;
    }

    // Orthonormal frame (as matrix columns, row-major storage) from two directions.
    static bool frame(const Bearing &u, const Bearing &v, double *f)
    {
        const double e1[3] = {u.x, u.y, u.z};
        double e2[3] = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const double n = std::sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
        if (n < 1e-4)
            return false;
        e2[0] /= n;
        e2[1] /= n;
        e2[2] /= n;
        const double e3[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (int r = 0; r < 3; ++r)
        {
            f[r * 3 + 0] = e1[r];
            f[r * 3 + 1] = e2[r];
            f[r * 3 + 2] = e3[r];
        }
        return true;
    }

    // Horn's closed form: the unit quaternion maximizing sum b . (R a), from
    // S = sum a b^T, is the eigenvector of the largest eigenvalue of N.
    static Quaternion hornRotation(const double s[3][3])
    {
        const double sxx = s[0][0], sxy = s[0][1], sxz = s[0][2];
        const double syx = s[1][0], syy = s[1][1], syz = s[1][2];
        const double szx = s[2][0], szy = s[2][1], szz = s[2][2];
        double n[4][4] = {
            {sxx + syy + szz, syz - szy, szx - sxz, sxy - syx},
            {syz - szy, sxx - syy - szz, sxy + syx, szx + sxz},
            {szx - sxz, sxy + syx, -sxx + syy - szz, syz + szy},
            {sxy - syx, szx + sxz, syz + szy, -sxx - syy + szz}};
        double v[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        // Cyclic Jacobi.
        for (int sweep = 0; sweep < 30; ++sweep)
        {
            double off = 0.0;
            for (int p = 0; p < 4; ++p)
                for (int q = p + 1; q < 4; ++q)
                    off += n[p][q] * n[p][q];
            if (off < 1e-24)
                break;
            for (int p = 0; p < 4; ++p)
            {
                for (int q = p + 1; q < 4; ++q)
                {
                    if (std::fabs(n[p][q]) < 1e-30)
                        continue;
                    const double theta = 0.5 * std::atan2(2.0 * n[p][q], n[q][q] - n[p][p]);
                    const double c = std::cos(theta);
                    const double sn = std::sin(theta);
                    for (int k = 0; k < 4; ++k)
                    {
                        const double kp = n[k][p];
                        const double kq = n[k][q];
                        n[k][p] = c * kp - sn * kq;
                        n[k][q] = sn * kp + c * kq;
                    }
                    for (int k = 0; k < 4; ++k)
                    {
                        const double pk = n[p][k];
                        const double qk = n[q][k];
                        n[p][k] = c * pk - sn * qk;
                        n[q][k] = sn * pk + c * qk;
                    }
                    for (int k = 0; k < 4; ++k)
                    {
                        const double kp = v[k][p];
                        const double kq = v[k][q];
                        v[k][p] = c * kp - sn * kq;
                        v[k][q] = sn * kp + c * kq;
                    }
                }
            }
        }
        int largest = 0;
        for (int i = 1; i < 4; ++i)
            if (n[i][i] > n[largest][largest])
                largest = i;
        // Eigenvector (w, x, y, z).
        return AowisCompass::normalized({v[1][largest], v[2][largest], v[3][largest], v[0][largest]});
    }

    std::array<Image, kLevels> m_previous;
    std::array<Image, kLevels> m_current;
    std::vector<Point> m_points;
    bool m_hasPrevious = false;
    double m_previousSeconds = 0.0;
    double m_inputFocal = 0.0;
};

class VisionFusion final
{
public:
    typedef AowisCompass::Quaternion Quaternion;
    typedef AowisCompass::Vector3 Vector3;

    // Camera frames only ROTATE the pose (by the rotation measured between
    // two frames) when they arrive; all output to the engine runs on the
    // motion sensors' time line (field steps), and whether the camera is
    // active is judged by arrival time. Camera timestamps are not used at
    // all: on some phones the camera clock differs from the sensor clock,
    // and a shared time line then let the camera block the compass-only
    // motion (no turning) while adding nothing.
    //
    // Without a reliable camera rotation for this long (arrival clock), the
    // compass-only motion moves the pose.
    static constexpr double kVisionTimeoutSeconds = 0.12;
    // For LookAroundEngine::setGyroTimeoutSeconds(): this fusion always
    // supplies motion (camera, else compass-only), so the engine never needs
    // to hand the view to the raw compass.
    static constexpr double kEngineGyroTimeoutSeconds = 1.0;
    // For LookAroundEngine::setMotionCorrectionFraction(): north is corrected
    // by at most this share of the turn while turning (the camera measures
    // turns far better than the compass; 5 % visibly bent each turn).
    static constexpr double kEngineMotionCorrectionFraction = 0.01;
    // Tilt pulled toward gravity (keeps rounding errors from accumulating;
    // the tilt motion itself already comes from gravity).
    static constexpr double kTiltSeconds = 0.3;
    // Image orientation decision: total field rotation needed (radians), and
    // how clearly the best candidate must win.
    static constexpr double kDecisionRotationRadians = 0.6;
    static constexpr double kDecisionAgreement = 0.6;
    // Field pose history (by arrival) for comparing with camera frames.
    static constexpr std::size_t kHistorySize = 128;
    // Plausibility: the camera's tilt motion must match gravity's. Seen
    // from close up (a table, a wall at arm's length) the phone's own small
    // movements look like large rotations; the accelerometer shows no such
    // tilting. A frame whose tilt rate exceeds kTiltRatio x gravity's plus
    // kTiltAllowance (both low-passed over kRateSeconds) is not used.
    static constexpr double kTiltRatio = 2.0;
    static constexpr double kTiltAllowanceDegreesPerSecond = 4.0;
    static constexpr double kRateSeconds = 0.15;
    // The field orientation lags by its (speed-adaptive) low-pass: its
    // readings are dated back by its resting delay (best in simulation of the
    // values tried). Pass compass readings to the engine at fieldTime() too.
    static constexpr double kFieldDelaySeconds = FieldOrientation::kVectorTimeConstantSeconds;

    static double fieldTime(double seconds) { return seconds - kFieldDelaySeconds; }

    struct Output
    {
        Quaternion motion;  // gyroscope-like pose for LookAroundEngine::addGyro
        double seconds = 0.0;
    };

    void reset()
    {
        this->m_hasPose = false;
        this->m_hasFieldMotion = false;
        this->m_lastVision = -1e300;
        this->m_lastFrame = -1e300;
        this->m_historyCount = 0;
        this->m_historyNext = 0;
        this->m_scores = {};
        this->m_magnitudes = {};
        this->m_rotationTotal = 0.0;
        this->m_orientation = this->m_forcedOrientation;
        this->m_lastOutput = -1e300;
        this->m_fieldTiltRate = 0.0;
        this->m_visionTiltRate = 0.0;
        this->m_hasFieldRateTime = false;
        this->m_implausible = 0;
    }

    // Camera frames ignored as implausible so far (diagnostics).
    int implausibleFrames() const { return this->m_implausible; }

    // For tests: skip the decision.
    void forceOrientation(int degrees)
    {
        this->m_forcedOrientation = degrees;
        this->m_orientation = degrees;
    }

    // Image orientation in use (0, 90, 180, 270), or -1 while undecided.
    int orientation() const { return this->m_orientation; }

    // The camera moves the pose (arrival clock).
    bool visionActive(double arrival) const
    {
        return this->m_orientation >= 0 && arrival - this->m_lastVision <= kVisionTimeoutSeconds;
    }

    // One LookAroundFieldSource step, arrived at 'arrival'. Returns true with
    // the pose for the engine at the field time.
    bool addField(const LookAroundFieldSource::Step &step, double arrival, Output *out)
    {
        const Quaternion field = AowisCompass::normalized(step.compass);
        const double t = fieldTime(step.seconds);
        // Sensor clock jumping back (driver resync): new time line; the
        // camera's image orientation is a property of the phone and is kept.
        if (t < this->m_lastOutput - 0.25)
        {
            const int orientation = this->m_orientation;
            reset();
            this->m_orientation = orientation;
        }
        pushHistory(field, arrival);
        if (!this->m_hasPose)
        {
            this->m_pose = field;
            this->m_hasPose = true;
            this->m_poseSeconds = t;
        }
        const bool vision = visionActive(arrival);
        if (this->m_hasFieldMotion)
        {
            // Gravity's tilt rate (for the camera plausibility check).
            const Vector3 f = LookAroundFieldSource::rotationVector(LookAroundFieldSource::shortest(
                AowisCompass::multiply(step.motion, conjugate(this->m_fieldMotion))));
            if (this->m_hasFieldRateTime && arrival > this->m_fieldRateTime)
            {
                const double dt = std::min(arrival - this->m_fieldRateTime, 0.25);
                const double a = -std::expm1(-dt / kRateSeconds);
                this->m_fieldTiltRate += a * (std::hypot(f.x, f.y) / dt - this->m_fieldTiltRate);
            }
            this->m_fieldRateTime = arrival;
            this->m_hasFieldRateTime = true;
        }
        if (this->m_hasFieldMotion)
        {
            // The field source's own increment (turn-gated heading), in the
            // pose's world frame. Tilt (up/down, roll) always comes from here:
            // direct, from gravity. Heading only while the camera is not
            // tracking; while it tracks, the camera turns the heading.
            const Quaternion delta = AowisCompass::multiply(conjugate(this->m_fieldMotion), step.motion);
            Vector3 w = LookAroundFieldSource::rotationVector(LookAroundFieldSource::shortest(
                AowisCompass::multiply(AowisCompass::multiply(this->m_pose, delta), conjugate(this->m_pose))));
            if (vision)
                w.z = 0.0;
            this->m_pose = AowisCompass::normalized(AowisCompass::multiply(
                LookAroundFieldSource::fromRotationVector(w), this->m_pose));
        }
        this->m_fieldMotion = step.motion;
        this->m_hasFieldMotion = true;
        const double dt = std::clamp(t - this->m_poseSeconds, 0.0, 0.25);
        this->m_poseSeconds = std::max(this->m_poseSeconds, t);
        correctTilt(field, dt, kTiltSeconds);
        if (t <= this->m_lastOutput)
            return false;
        out->motion = this->m_pose;
        out->seconds = t;
        this->m_lastOutput = t;
        return true;
    }

    // One camera result, arrived at 'arrival'. A reliable rotation turns the
    // pose's heading (about world Up; shown with the next field step); an
    // unreliable frame hands the heading back to compass-only motion at once.
    void addVision(const VisionTracker::Result &result, double arrival)
    {
        const double previousFrame = this->m_lastFrame;
        this->m_lastFrame = arrival;
        if (!result.valid)
        {
            this->m_lastVision = -1e300;
            return;
        }
        if (!this->m_hasPose)
            return;
        if (this->m_orientation < 0)
        {
            learnOrientation(result, previousFrame, arrival);
            return;
        }
        // Only the turn about world Up: the magnetometer's weak axis. Tilt
        // stays with gravity (fresh); the camera's tilt would arrive late.
        const Quaternion world = AowisCompass::multiply(AowisCompass::multiply(
            this->m_pose, deviceDelta(result.rotation, this->m_orientation)), conjugate(this->m_pose));
        const Vector3 w = LookAroundFieldSource::rotationVector(LookAroundFieldSource::shortest(world));
        // Plausibility: the camera's tilting against gravity's.
        if (previousFrame > -1e299 && arrival > previousFrame)
        {
            const double dt = std::min(arrival - previousFrame, 0.25);
            const double a = -std::expm1(-dt / kRateSeconds);
            this->m_visionTiltRate += a * (std::hypot(w.x, w.y) / dt - this->m_visionTiltRate);
        }
        const double allowance = kTiltAllowanceDegreesPerSecond * AowisCompass::kPi / 180.0;
        if (this->m_visionTiltRate > kTiltRatio * this->m_fieldTiltRate + allowance)
        {
            ++this->m_implausible;
            this->m_lastVision = -1e300;
            return;
        }
        this->m_pose = AowisCompass::normalized(AowisCompass::multiply(
            LookAroundFieldSource::fromRotationVector({0.0, 0.0, w.z}), this->m_pose));
        this->m_lastVision = arrival;
    }

    // Device-frame rotation (pose_new = pose_old * delta) for a tracker
    // rotation, with the raw image turned 'degrees' clockwise from upright.
    static Quaternion deviceDelta(const Quaternion &trackerRotation, int degrees)
    {
        const Quaternion m = cameraToDevice(degrees);
        return AowisCompass::normalized(AowisCompass::multiply(AowisCompass::multiply(m, conjugate(trackerRotation)),
                                                               conjugate(m)));
    }

    // Raw camera coordinates (x right, y down, z forward, image turned
    // 'degrees' clockwise from upright) to device coordinates.
    static Quaternion cameraToDevice(int degrees)
    {
        // Upright image: device X = u, Y = -v, Z = -forward: a half turn about X.
        const Quaternion upright{1.0, 0.0, 0.0, 0.0};
        // Raw to upright: clockwise on screen by 'degrees' = about +z (into the
        // scene) in y-down coordinates.
        const double half = 0.5 * degrees * AowisCompass::kPi / 180.0;
        const Quaternion turn{0.0, 0.0, std::sin(half), std::cos(half)};
        return AowisCompass::multiply(upright, turn);
    }

    static Quaternion conjugate(const Quaternion &q) { return {-q.x, -q.y, -q.z, q.w}; }

private:
    struct Entry
    {
        Quaternion pose;
        double arrival = 0.0;
    };

    void pushHistory(const Quaternion &pose, double arrival)
    {
        if (this->m_historyCount > 0 &&
            arrival < this->m_history[(this->m_historyNext + kHistorySize - 1) % kHistorySize].arrival)
        {
            this->m_historyCount = 0;
            this->m_historyNext = 0;
        }
        this->m_history[this->m_historyNext] = {pose, arrival};
        this->m_historyNext = (this->m_historyNext + 1) % kHistorySize;
        this->m_historyCount = std::min(this->m_historyCount + 1, kHistorySize);
    }

    // Field pose at arrival time t (nearest reading at or before t, the
    // newest after it); false without history covering t.
    bool fieldAt(double t, Quaternion *pose) const
    {
        if (this->m_historyCount == 0)
            return false;
        for (std::size_t i = 1; i <= this->m_historyCount; ++i)
        {
            const Entry &entry = this->m_history[(this->m_historyNext + kHistorySize - i) % kHistorySize];
            if (entry.arrival <= t)
            {
                *pose = entry.pose;
                return true;
            }
        }
        return false;
    }

    // Compare the camera's rotation, for each candidate image orientation,
    // with the field orientation's over the frame's arrival interval (both
    // arrive with similar delays; many frames average the difference out).
    void learnOrientation(const VisionTracker::Result &result, double fromArrival, double toArrival)
    {
        Quaternion from;
        Quaternion to;
        if (!(toArrival - fromArrival < 0.5) || !fieldAt(fromArrival, &from) || !fieldAt(toArrival, &to))
            return;
        const Vector3 field = LookAroundFieldSource::rotationVector(
            LookAroundFieldSource::shortest(AowisCompass::multiply(to, conjugate(from))));
        const double fieldRotation = AowisCompass::length(field);
        if (fieldRotation < 1e-4)
            return;
        for (int k = 0; k < 4; ++k)
        {
            const Quaternion delta = deviceDelta(result.rotation, 90 * k);
            const Quaternion world = AowisCompass::multiply(AowisCompass::multiply(from, delta), conjugate(from));
            const Vector3 v = LookAroundFieldSource::rotationVector(LookAroundFieldSource::shortest(world));
            this->m_scores[std::size_t(k)] += v.x * field.x + v.y * field.y + v.z * field.z;
            this->m_magnitudes[std::size_t(k)] += AowisCompass::length(v) * fieldRotation;
        }
        this->m_rotationTotal += fieldRotation;
        if (this->m_rotationTotal < kDecisionRotationRadians)
            return;
        int best = 0;
        for (int k = 1; k < 4; ++k)
            if (this->m_scores[std::size_t(k)] > this->m_scores[std::size_t(best)])
                best = k;
        const double agreement = this->m_magnitudes[std::size_t(best)] > 0.0
            ? this->m_scores[std::size_t(best)] / this->m_magnitudes[std::size_t(best)] : 0.0;
        if (agreement >= kDecisionAgreement)
            this->m_orientation = 90 * best;
        else
        {
            // Inconclusive (e.g. walking): start over.
            this->m_scores = {};
            this->m_magnitudes = {};
            this->m_rotationTotal = 0.0;
        }
    }

    // Turn the pose so its "down" moves toward the accelerometer's.
    void correctTilt(const Quaternion &field, double dt, double seconds)
    {
        if (dt <= 0.0)
            return;
        // World Up in device coordinates, for the pose (a) and the field (b).
        const Vector3 up{0.0, 0.0, 1.0};
        const Vector3 a = rotateVector(conjugate(this->m_pose), up);
        const Vector3 b = rotateVector(conjugate(field), up);
        Vector3 axis = AowisCompass::cross(b, a);
        const double s = AowisCompass::length(axis);
        const double c = a.x * b.x + a.y * b.y + a.z * b.z;
        const double angle = std::atan2(s, c);
        if (s < 1e-12)
            return;
        axis = {axis.x / s, axis.y / s, axis.z / s};
        const double half = 0.5 * angle * -std::expm1(-dt / seconds);
        // New pose = pose * R with R b = a (R about b x a): the pose's "up"
        // in device coordinates moves from a toward b.
        const Quaternion r{axis.x * std::sin(half), axis.y * std::sin(half), axis.z * std::sin(half), std::cos(half)};
        this->m_pose = AowisCompass::normalized(AowisCompass::multiply(this->m_pose, r));
    }

    static Vector3 rotateVector(const Quaternion &q, const Vector3 &v)
    {
        const Quaternion r = AowisCompass::multiply(AowisCompass::multiply(q, {v.x, v.y, v.z, 0.0}), conjugate(q));
        return {r.x, r.y, r.z};
    }

    bool m_hasPose = false;
    Quaternion m_pose;
    double m_poseSeconds = 0.0;
    bool m_hasFieldMotion = false;
    Quaternion m_fieldMotion;
    double m_lastVision = -1e300;
    double m_lastFrame = -1e300;
    double m_lastOutput = -1e300;
    int m_orientation = -1;
    int m_forcedOrientation = -1;
    std::array<double, 4> m_scores{};
    std::array<double, 4> m_magnitudes{};
    double m_rotationTotal = 0.0;
    double m_fieldTiltRate = 0.0;
    double m_visionTiltRate = 0.0;
    double m_fieldRateTime = 0.0;
    bool m_hasFieldRateTime = false;
    int m_implausible = 0;
    std::array<Entry, kHistorySize> m_history{};
    std::size_t m_historyCount = 0;
    std::size_t m_historyNext = 0;
};
}
