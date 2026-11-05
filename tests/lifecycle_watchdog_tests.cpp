#include "lifecycle_watchdog.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    using AowisMobileLifecycle::Outcome;
    using AowisMobileLifecycle::Watchdog;
    Watchdog watchdog;
    assert(!watchdog.pending());
    assert(watchdog.poll(110, 0, true, true) == Outcome::None);

    watchdog.arm(1000000);
    assert(watchdog.pending());
    assert(watchdog.poll(2000000, 900000, true, true) == Outcome::None);
    assert(watchdog.poll(2500000, 2100000, true, true) == Outcome::Recovered);
    assert(watchdog.lastDurationUs() == 1100000);
    assert(!watchdog.pending());

    // A paused/unexposed window must not be declared stalled.
    watchdog.arm(5000000);
    assert(watchdog.poll(15000000, 0, false, false) == Outcome::None);
    assert(watchdog.poll(25000000, 0, true, false) == Outcome::None);
    assert(watchdog.poll(26000000, 0, true, true) == Outcome::None);
    assert(watchdog.poll(31999999, 0, true, true) == Outcome::TimedOut);
    assert(watchdog.poll(33000000, 0, true, true) == Outcome::None);
    assert(watchdog.timedOut());
    assert(watchdog.poll(34000000, 33500000, true, true) == Outcome::LateRecovered);
    assert(!watchdog.pending());

    // Re-arming after a failed attempt resets the failure state.
    watchdog.arm(40000000);
    assert(!watchdog.timedOut());
    assert(watchdog.poll(42000000, 41000000, true, true) == Outcome::Recovered);

    watchdog.arm(50000000);
    watchdog.cancel();
    assert(watchdog.poll(62000000, 61000000, true, true) == Outcome::None);
    std::cout << "Lifecycle watchdog tests passed\n";
}
