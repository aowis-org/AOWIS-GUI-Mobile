#include "performance_samples.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

int64_t AowisMobileBench::percentile(std::vector<int64_t> samples, double fraction)
{
    if (samples.empty() || !std::isfinite(fraction))
        return -1;
    std::sort(samples.begin(), samples.end());
    const double p = std::clamp(fraction, 0.0, 1.0);
    const size_t index = p <= 0.0 ? 0 :
        std::min(samples.size() - 1, size_t(std::ceil(p * double(samples.size()))) - 1);
    return samples[index];
}

std::vector<int64_t> AowisMobileBench::intervals(const std::vector<FrameSample> &samples)
{
    std::vector<int64_t> result;
    for (const FrameSample &sample : samples)
        if (sample.frameIntervalUs > 0)
            result.push_back(sample.frameIntervalUs);
    return result;
}

std::vector<int64_t> AowisMobileBench::submissions(const std::vector<FrameSample> &samples)
{
    std::vector<int64_t> result;
    for (const FrameSample &sample : samples)
        if (sample.submitUs >= 0)
            result.push_back(sample.submitUs);
    return result;
}

std::vector<int64_t> AowisMobileBench::gpuTimes(const std::vector<FrameSample> &samples)
{
    std::vector<int64_t> result;
    for (const FrameSample &sample : samples)
        if (sample.gpuUs >= 0)
            result.push_back(sample.gpuUs);
    return result;
}
