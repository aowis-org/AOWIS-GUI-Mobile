#pragma once

#include <cstdint>
#include <vector>

namespace AowisMobileGpu
{
    // Matches bgfx's Position (3 floats) / Color0 (normalized ABGR8) vertex layout.
    struct Vertex
    {
        float x;
        float y;
        float z;
        uint32_t abgr;
    };

    struct Scene
    {
        std::vector<Vertex> minorGridLines;
        std::vector<Vertex> majorGridLines;
        std::vector<Vertex> pipeTriangles;
        std::vector<Vertex> nodeTriangles;
    };

    struct Camera
    {
        float left;
        float right;
        float top;
        float bottom;
    };

    struct StressProfile
    {
        int side;
        uint64_t nodes;
        uint64_t pipes;
        uint64_t features;
    };

    struct StressTile
    {
        int column;
        int row;
        int stride;
    };

    struct TileSelection
    {
        int stride = 1;
        std::vector<StressTile> tiles;
    };

    struct StressMesh
    {
        std::vector<Vertex> pipes;
        std::vector<Vertex> nodes;
        uint32_t pipeCount = 0;
        uint32_t nodeCount = 0;
    };

    constexpr int kStressTileSide = 32;
    constexpr int kMaximumVisibleTiles = 64;
    constexpr int kMaximumCachedTiles = 96;

    // Original small scene remains a reproducible compatibility baseline.
    Scene createScene();
    Camera makeCamera(int pixelWidth, int pixelHeight, double devicePixelRatio,
                      double zoom, double panX, double panY);

    // Procedural features are deterministic, no million-object CPU scene array.
    // A feature is either a node or one edge to a right/bottom neighbour.
    StressProfile stressProfile(int preset); // 0=demo, 1..4=~1k .. ~1M.
    TileSelection visibleStressTiles(const Camera &camera, const StressProfile &profile);
    StressMesh createStressTile(const StressProfile &profile, const StressTile &tile);
}
