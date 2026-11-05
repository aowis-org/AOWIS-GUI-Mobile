#include "lifecycle_watchdog.h"

#include <algorithm>

namespace AowisMobileLifecycle
{
    void Watchdog::arm(int64_t eventTimeUs)
    {
        this->eventUs = eventTimeUs;
        this->exposedSinceUs = 0;
        this->durationUs = 0;
        this->waiting = true;
        this->failed = false;
    }

    void Watchdog::cancel()
    {
        this->waiting = false;
        this->failed = false;
    }

    Outcome Watchdog::poll(int64_t nowUs, int64_t lastFrameUs, bool active,
                           bool exposed, int64_t timeoutUs)
    {
        if (!this->waiting)
            return Outcome::None;
        if (!active || !exposed)
        {
            // Background/screen-off time is not included in the deadline.
            this->exposedSinceUs = 0;
            return Outcome::None;
        }
        if (this->exposedSinceUs == 0)
            this->exposedSinceUs = nowUs;
        if (lastFrameUs >= this->eventUs && lastFrameUs > 0)
        {
            this->durationUs = std::max(INT64_C(0), lastFrameUs - this->eventUs);
            this->waiting = false;
            return this->failed ? Outcome::LateRecovered : Outcome::Recovered;
        }
        if (!this->failed && nowUs - this->exposedSinceUs >= timeoutUs)
        {
            this->failed = true;
            return Outcome::TimedOut;
        }
        return Outcome::None;
    }

    bool Watchdog::pending() const { return this->waiting; }
    bool Watchdog::timedOut() const { return this->failed; }
    int64_t Watchdog::lastDurationUs() const { return this->durationUs; }
    int64_t Watchdog::ageUs(int64_t nowUs) const
    {
        return this->waiting ? std::max(INT64_C(0), nowUs - this->eventUs) : 0;
    }
}
