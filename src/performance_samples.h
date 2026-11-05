#pragma once

#include <cstdint>
#include <vector>

namespace AowisMobileBench
{
    struct FrameSample
    {
        int64_t elapsedUs = 0;
        int64_t frameIntervalUs = 0;
        int64_t submitUs = 0;
        int64_t gpuUs = -1;
        uint32_t draws = 0;
        uint32_t tiles = 0;
        uint32_t uploads = 0;
        uint32_t stride = 1;
        uint32_t nodesDrawn = 0;
        uint32_t pipesDrawn = 0;
        uint64_t vertexBytes = 0;
    };

    // Values are microseconds, including zero; empty/invalid series yields -1.
    int64_t percentile(std::vector<int64_t> samples, double fraction);
    std::vector<int64_t> intervals(const std::vector<FrameSample> &samples);
    std::vector<int64_t> submissions(const std::vector<FrameSample> &samples);
    std::vector<int64_t> gpuTimes(const std::vector<FrameSample> &samples);
}
