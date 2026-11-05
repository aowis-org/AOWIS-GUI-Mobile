#pragma once

#include <cstdint>

// Dependency-free verification of first-frame recovery after an application
// activation, scene-graph recreation or surface resize. The Qt GUI thread owns
// this state. The render thread supplies a monotonic last-frame timestamp.
namespace AowisMobileLifecycle
{
    enum class Outcome
    {
        None,
        Recovered,
        TimedOut,
        LateRecovered
    };

    class Watchdog final
    {
    public:
        void arm(int64_t eventUs);
        void cancel();
        Outcome poll(int64_t nowUs, int64_t lastFrameUs, bool active,
                     bool exposed, int64_t timeoutUs = INT64_C(5000000));
        bool pending() const;
        bool timedOut() const;
        int64_t lastDurationUs() const;
        int64_t ageUs(int64_t nowUs) const;

    private:
        int64_t eventUs = 0;
        int64_t exposedSinceUs = 0;
        int64_t durationUs = 0;
        bool waiting = false;
        bool failed = false;
    };
}
