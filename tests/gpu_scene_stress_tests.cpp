#include "../src/gpu_scene_geometry.h"
#include "../src/performance_samples.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

int main()
{
    const uint64_t targets[] = {0, 1000, 10000, 100000, 1000000};
    for (int preset = 1; preset <= 4; ++preset)
    {
        const AowisMobileGpu::StressProfile profile = AowisMobileGpu::stressProfile(preset);
        assert(profile.nodes == uint64_t(profile.side) * uint64_t(profile.side));
        assert(profile.pipes == 2 * uint64_t(profile.side) * uint64_t(profile.side - 1));
        assert(profile.features == profile.nodes + profile.pipes);
        assert(std::abs(double(profile.features) / double(targets[preset]) - 1.0) < 0.06);
        assert(profile.nodes < profile.features);
    }
    assert(AowisMobileGpu::stressProfile(0).features == 0);
    assert(AowisMobileGpu::stressProfile(-1).features == 0);
    assert(AowisMobileGpu::stressProfile(5).features == 0);

    const AowisMobileGpu::StressProfile large = AowisMobileGpu::stressProfile(4);
    const AowisMobileGpu::Camera center = AowisMobileGpu::makeCamera(1440, 2400, 1.0, 0.3, 0, 0);
    const AowisMobileGpu::TileSelection selection = AowisMobileGpu::visibleStressTiles(center, large);
    assert(!selection.tiles.empty());
    assert(selection.tiles.size() <= size_t(AowisMobileGpu::kMaximumVisibleTiles));
    assert(selection.stride >= 1);
    for (size_t index = 0; index < selection.tiles.size(); ++index)
    {
        const AowisMobileGpu::StressTile tile = selection.tiles[index];
        assert(tile.stride == selection.stride);
        for (size_t other = index + 1; other < selection.tiles.size(); ++other)
            assert(tile.column != selection.tiles[other].column || tile.row != selection.tiles[other].row);
    }

    const AowisMobileGpu::StressMesh tile = AowisMobileGpu::createStressTile(large, selection.tiles.front());
    const AowisMobileGpu::StressMesh duplicate = AowisMobileGpu::createStressTile(large, selection.tiles.front());
    assert(tile.nodeCount > 0);
    assert(tile.nodeCount <= 1024);
    assert(tile.pipeCount <= 2048);
    assert(tile.nodes.size() == size_t(tile.nodeCount) * 6);
    assert(tile.pipes.size() == size_t(tile.pipeCount) * 6);
    assert(tile.nodes.size() == duplicate.nodes.size());
    assert(tile.nodes.front().x == duplicate.nodes.front().x);
    assert(tile.nodes.front().abgr == duplicate.nodes.front().abgr);
    assert(AowisMobileGpu::createStressTile(large, {-1, 0, 1}).nodes.empty());
    assert(AowisMobileGpu::createStressTile(large, {500, 500, 1}).nodes.empty());

    const AowisMobileGpu::Camera farAway = {100000.0f, 101000.0f, 100000.0f, 101000.0f};
    assert(AowisMobileGpu::visibleStressTiles(farAway, large).tiles.empty());
    const AowisMobileGpu::Camera hugeViewport = {-50000.0f, 50000.0f, -50000.0f, 50000.0f};
    const AowisMobileGpu::TileSelection huge = AowisMobileGpu::visibleStressTiles(hugeViewport, large);
    assert(huge.tiles.size() <= size_t(AowisMobileGpu::kMaximumVisibleTiles));
    assert(huge.stride > 1);

    const std::vector<AowisMobileBench::FrameSample> frames = {
        {0, 1000, 100, -1}, {1000, 2000, 300, 200},
        {3000, 4000, 500, 400}, {7000, 6000, 1000, -1}
    };
    assert(AowisMobileBench::percentile({}, 0.5) == -1);
    assert(AowisMobileBench::percentile({10, 20, 30, 40}, 0.5) == 20);
    assert(AowisMobileBench::percentile({10, 20, 30, 40}, 0.95) == 40);
    assert(AowisMobileBench::percentile({10, 20, 30, 40}, 0.0) == 10);
    assert(AowisMobileBench::percentile(AowisMobileBench::intervals(frames), 0.5) == 2000);
    assert(AowisMobileBench::gpuTimes(frames).size() == 2);
    assert(AowisMobileBench::submissions(frames).size() == 4);
    return 0;
}
