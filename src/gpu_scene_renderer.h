#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

#include "gpu_scene_geometry.h"

class GpuSceneRenderer final
{
public:
    bool initialize(); // Render thread; bgfx must be initialized first.
    void render(int pixelWidth, int pixelHeight, double devicePixelRatio,
                double zoom, double panX, double panY);
    void release();    // Render thread; must precede bgfx::shutdown().

private:
    static bgfx::ShaderHandle loadShader(const char *name, const char *backend);
    static bgfx::VertexBufferHandle createBuffer(const std::vector<AowisMobileGpu::Vertex> &vertices,
                                                  const bgfx::VertexLayout &layout);
    void submit(bgfx::VertexBufferHandle handle, uint64_t state);

    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle minorGrid = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle majorGrid = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle pipes = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle nodes = BGFX_INVALID_HANDLE;
};
