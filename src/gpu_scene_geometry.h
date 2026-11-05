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

    // Static world-space vertices: zoom/pan must NOT rebuild scene geometry.
    Scene createScene();
    Camera makeCamera(int pixelWidth, int pixelHeight, double devicePixelRatio,
                      double zoom, double panX, double panY);
}
