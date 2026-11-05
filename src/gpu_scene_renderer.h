#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

#include "gpu_scene_geometry.h"

struct GpuSceneMetrics
{
    uint32_t visibleTiles = 0;
    uint32_t residentTiles = 0;
    uint32_t uploadsThisFrame = 0;
    uint32_t sampledNodes = 0;
    uint32_t sampledPipes = 0;
    uint32_t stride = 1;
    uint64_t gpuVertexBytes = 0; // Estimate: vertex buffer payloads, not driver allocations.
};

class GpuSceneRenderer final
{
public:
    bool initialize(); // Render thread; bgfx must be initialized first.
    GpuSceneMetrics render(int pixelWidth, int pixelHeight, double devicePixelRatio,
                           double zoom, double panX, double panY, int workload);
    void release();    // Render thread; must precede bgfx::shutdown().

private:
    struct CachedTile
    {
        AowisMobileGpu::StressTile key;
        bgfx::VertexBufferHandle pipes = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle nodes = BGFX_INVALID_HANDLE;
        uint32_t nodeCount = 0;
        uint32_t pipeCount = 0;
        uint64_t bytes = 0;
        uint64_t touched = 0;
    };

    static bgfx::ShaderHandle loadShader(const char *name, const char *backend);
    static bgfx::VertexBufferHandle createBuffer(const std::vector<AowisMobileGpu::Vertex> &vertices,
                                                  const bgfx::VertexLayout &layout);
    void submit(bgfx::VertexBufferHandle handle, uint64_t state);
    void destroyTile(CachedTile &tile);
    void clearTiles();
    CachedTile *findTile(const AowisMobileGpu::StressTile &key);
    CachedTile *loadTile(const AowisMobileGpu::StressProfile &profile,
                         const AowisMobileGpu::StressTile &key);

    bgfx::VertexLayout vertexLayout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle minorGrid = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle majorGrid = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle pipes = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle nodes = BGFX_INVALID_HANDLE;
    std::vector<CachedTile> cachedTiles;
    uint64_t sequence = 0;
    uint64_t baseVertexBytes = 0;
    uint64_t cachedVertexBytes = 0;
    int activeWorkload = 0;
};
