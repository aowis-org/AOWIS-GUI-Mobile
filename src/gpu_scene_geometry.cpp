#include "gpu_scene_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
    struct Point
    {
        float x;
        float y;
    };

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kStressSpacing = 14.0f;

    uint32_t rgba(uint8_t red, uint8_t green, uint8_t blue)
    {
        return UINT32_C(0xff000000) | (uint32_t(blue) << 16)
             | (uint32_t(green) << 8) | uint32_t(red);
    }

    void push(std::vector<AowisMobileGpu::Vertex> &out, Point point, uint32_t color)
    {
        out.push_back({point.x, point.y, 0.0f, color});
    }

    void segment(std::vector<AowisMobileGpu::Vertex> &out, Point a, Point b,
                 float thickness, uint32_t color)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.0001f)
            return;
        const float nx = -dy * thickness / (length * 2.0f);
        const float ny = dx * thickness / (length * 2.0f);
        const Point first = {a.x + nx, a.y + ny};
        const Point second = {a.x - nx, a.y - ny};
        const Point third = {b.x + nx, b.y + ny};
        const Point fourth = {b.x - nx, b.y - ny};
        push(out, first, color);
        push(out, second, color);
        push(out, third, color);
        push(out, third, color);
        push(out, second, color);
        push(out, fourth, color);
    }

    void disk(std::vector<AowisMobileGpu::Vertex> &out, Point center,
              float radius, uint32_t color)
    {
        constexpr int sides = 20;
        for (int part = 0; part < sides; ++part)
        {
            const float theta = 2.0f * kPi * float(part) / float(sides);
            const float nextTheta = 2.0f * kPi * float(part + 1) / float(sides);
            push(out, center, color);
            push(out, {center.x + radius * std::cos(theta),
                       center.y + radius * std::sin(theta)}, color);
            push(out, {center.x + radius * std::cos(nextTheta),
                       center.y + radius * std::sin(nextTheta)}, color);
        }
    }

    void square(std::vector<AowisMobileGpu::Vertex> &out, Point center,
                float radius, uint32_t color)
    {
        const Point a = {center.x - radius, center.y - radius};
        const Point b = {center.x + radius, center.y - radius};
        const Point c = {center.x - radius, center.y + radius};
        const Point d = {center.x + radius, center.y + radius};
        push(out, a, color); push(out, b, color); push(out, c, color);
        push(out, c, color); push(out, b, color); push(out, d, color);
    }

    float stressPosition(int index, int side)
    {
        return (float(index) - float(side - 1) * 0.5f) * kStressSpacing;
    }

}

AowisMobileGpu::Scene AowisMobileGpu::createScene()
{
    Scene scene;
    const uint32_t minorColor = rgba(40, 79, 95);
    const uint32_t majorColor = rgba(60, 112, 129);
    // Two vertices per line. Entire grid is created once on the CPU, then
    // transformed by the GPU when the camera moves or zooms.
    for (int index = -128; index <= 128; ++index)
    {
        std::vector<Vertex> &target = index % 4 == 0
            ? scene.majorGridLines : scene.minorGridLines;
        const uint32_t color = index % 4 == 0 ? majorColor : minorColor;
        const float coordinate = float(index) * 48.0f;
        push(target, {coordinate, -6144.0f}, color);
        push(target, {coordinate, 6144.0f}, color);
        push(target, {-6144.0f, coordinate}, color);
        push(target, {6144.0f, coordinate}, color);
    }

    // Fixed model-space positions for a small hydraulic-style demonstration.
    constexpr Point junctions[] = {
        {-195.0f, 85.0f}, {-110.0f, 12.0f}, {-15.0f, 42.0f},
        {91.0f, -24.0f}, {202.0f, -94.0f}, {48.0f, 142.0f},
        {-30.0f, -95.0f}, {136.0f, 91.0f}
    };
    const uint32_t pipeColor = rgba(71, 190, 203);
    const uint32_t secondaryColor = rgba(209, 156, 88);
    const uint32_t nodeOutline = rgba(9, 35, 47);
    const uint32_t nodeFill = rgba(160, 238, 219);
    constexpr int primarySegments[][2] = {{0,1}, {1,2}, {2,3}, {3,4}, {2,5}};
    constexpr int secondarySegments[][2] = {{1,6}, {3,7}};
    for (const int (&pair)[2] : primarySegments)
        segment(scene.pipeTriangles, junctions[pair[0]], junctions[pair[1]], 5.0f, pipeColor);
    for (const int (&pair)[2] : secondarySegments)
        segment(scene.pipeTriangles, junctions[pair[0]], junctions[pair[1]], 4.0f, secondaryColor);
    for (const Point &junction : junctions)
    {
        disk(scene.nodeTriangles, junction, 10.0f, nodeOutline);
        disk(scene.nodeTriangles, junction, 6.0f, nodeFill);
    }
    return scene;
}

AowisMobileGpu::Camera AowisMobileGpu::makeCamera(
    int pixelWidth, int pixelHeight, double devicePixelRatio,
    double zoom, double panX, double panY)
{
    const double scale = std::clamp(zoom, 0.3, 3.0);
    const double dpr = std::clamp(devicePixelRatio, 0.25, 8.0);
    const double x = std::clamp(panX, -4096.0, 4096.0) * dpr;
    const double y = std::clamp(panY, -4096.0, 4096.0) * dpr;
    const double halfWidth = double(std::max(1, pixelWidth)) / 2.0;
    const double halfHeight = double(std::max(1, pixelHeight)) / 2.0;
    return {float((-halfWidth - x) / scale), float((halfWidth - x) / scale),
            float((-halfHeight - y) / scale), float((halfHeight - y) / scale)};
}


AowisMobileGpu::StressProfile AowisMobileGpu::stressProfile(int preset)
{
    // The numbers refer to nodes + edges, not only nodes or uploaded vertices.
    const int side = preset == 1 ? 19 : preset == 2 ? 58 :
                     preset == 3 ? 183 : preset == 4 ? 577 : 0;
    const uint64_t nodes = uint64_t(side) * uint64_t(side);
    const uint64_t pipes = side > 0 ? 2 * uint64_t(side) * uint64_t(side - 1) : 0;
    return {side, nodes, pipes, nodes + pipes};
}

AowisMobileGpu::TileSelection AowisMobileGpu::visibleStressTiles(
    const Camera &camera, const StressProfile &profile)
{
    TileSelection result;
    if (profile.side < 1)
        return result;

    const double first = double(stressPosition(0, profile.side));
    // Include a neighbour around screen edges for links crossing the boundary.
    const int minColumn = std::clamp(int(std::floor((double(camera.left) - first) / kStressSpacing)) - 1,
                                     0, profile.side - 1);
    const int maxColumn = std::clamp(int(std::ceil((double(camera.right) - first) / kStressSpacing)) + 1,
                                     0, profile.side - 1);
    const int minRow = std::clamp(int(std::floor((double(camera.top) - first) / kStressSpacing)) - 1,
                                  0, profile.side - 1);
    const int maxRow = std::clamp(int(std::ceil((double(camera.bottom) - first) / kStressSpacing)) + 1,
                                  0, profile.side - 1);
    // No overlap => no GPU work at all (including when far outside the model).
    if (camera.right < first || camera.left > stressPosition(profile.side - 1, profile.side) ||
        camera.bottom < first || camera.top > stressPosition(profile.side - 1, profile.side))
        return result;

    for (int stride = 1; stride <= 1024; stride *= 2)
    {
        const int tileWidth = kStressTileSide * stride;
        const int beginX = minColumn / tileWidth;
        const int endX = maxColumn / tileWidth;
        const int beginY = minRow / tileWidth;
        const int endY = maxRow / tileWidth;
        const int count = (endX - beginX + 1) * (endY - beginY + 1);
        if (count > kMaximumVisibleTiles && stride < 1024)
            continue;
        result.stride = stride;
        result.tiles.reserve(size_t(count));
        for (int row = beginY; row <= endY; ++row)
            for (int column = beginX; column <= endX; ++column)
                result.tiles.push_back({column, row, stride});
        return result;
    }
    return result;
}

AowisMobileGpu::StressMesh AowisMobileGpu::createStressTile(
    const StressProfile &profile, const StressTile &tile)
{
    StressMesh mesh;
    if (profile.side == 0 || tile.stride < 1 || tile.column < 0 || tile.row < 0)
        return mesh;
    const int beginColumn = tile.column * kStressTileSide * tile.stride;
    const int beginRow = tile.row * kStressTileSide * tile.stride;
    if (beginColumn >= profile.side || beginRow >= profile.side)
        return mesh;
    const int endColumn = std::min(profile.side, beginColumn + kStressTileSide * tile.stride);
    const int endRow = std::min(profile.side, beginRow + kStressTileSide * tile.stride);
    mesh.nodes.reserve(6 * kStressTileSide * kStressTileSide);
    mesh.pipes.reserve(12 * kStressTileSide * kStressTileSide);
    const uint32_t mainPipe = rgba(70, 187, 202);
    const uint32_t secondaryPipe = rgba(205, 159, 89);
    const uint32_t nodeColor = rgba(152, 232, 214);
    for (int row = beginRow; row < endRow; row += tile.stride)
    {
        for (int column = beginColumn; column < endColumn; column += tile.stride)
        {
            const Point position = {stressPosition(column, profile.side),
                                    stressPosition(row, profile.side)};
            square(mesh.nodes, position, 2.6f * std::sqrt(float(tile.stride)), nodeColor);
            ++mesh.nodeCount;
            // All edges are deterministic. No random generator, no duplicated
            // edges at tile boundaries, and no CPU work outside visible tiles.
            if (column + tile.stride < profile.side)
            {
                segment(mesh.pipes, position,
                        {stressPosition(column + tile.stride, profile.side), position.y},
                        2.0f, mainPipe);
                ++mesh.pipeCount;
            }
            if (row + tile.stride < profile.side)
            {
                segment(mesh.pipes, position,
                        {position.x, stressPosition(row + tile.stride, profile.side)},
                        2.0f, secondaryPipe);
                ++mesh.pipeCount;
            }
        }
    }
    return mesh;
}
