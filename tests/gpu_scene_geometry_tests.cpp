#include "../src/gpu_scene_geometry.h"

#include <cassert>
#include <cmath>
#include <cstddef>

int main()
{
    const AowisMobileGpu::Scene scene = AowisMobileGpu::createScene();
    assert(scene.minorGridLines.size() > 100);
    assert(scene.majorGridLines.size() > 100);
    assert(scene.minorGridLines.size() % 2 == 0);
    assert(scene.majorGridLines.size() % 2 == 0);
    assert(scene.pipeTriangles.size() == 7 * 6);
    assert(scene.nodeTriangles.size() == 8 * 2 * 20 * 3);
    for (const AowisMobileGpu::Vertex &vertex : scene.nodeTriangles)
        assert(std::isfinite(vertex.x) && std::isfinite(vertex.y));

    const AowisMobileGpu::Camera base = AowisMobileGpu::makeCamera(480, 800, 1.0, 1.0, 0.0, 0.0);
    assert(base.left == -240.0f && base.right == 240.0f);
    assert(base.top == -400.0f && base.bottom == 400.0f);
    const AowisMobileGpu::Camera zoomed = AowisMobileGpu::makeCamera(480, 800, 1.0, 2.0, 0.0, 0.0);
    assert(zoomed.right == 120.0f && zoomed.bottom == 200.0f);
    const AowisMobileGpu::Camera panned = AowisMobileGpu::makeCamera(960, 1600, 2.0, 2.0, 40.0, -20.0);
    assert(panned.left == -280.0f && panned.right == 200.0f);
    assert(panned.top == -380.0f && panned.bottom == 420.0f);
    const AowisMobileGpu::Camera minimum = AowisMobileGpu::makeCamera(0, 0, 0, 0, 0, 0);
    assert(minimum.right > minimum.left && minimum.bottom > minimum.top);
    return 0;
}
