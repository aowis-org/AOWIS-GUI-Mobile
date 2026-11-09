#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <initializer_list>
#include <mutex>
#include <utility>
#include <vector>

// Raw sensor recording for Look around (diagnostics). Qt-free.
//
// While Look around runs, every raw sensor event and every camera tracking
// result is kept with its sensor time and its arrival time (CLOCK_BOOTTIME,
// the clock of sensor timestamps on Android), and written next to the frame
// recording (AppDataLocation/lookaround/sensors-<utc>.csv). The host script
// tools/pull_look_around.sh fetches both; tools/replay_look_around.sh feeds
// them through the current engine (tools/look_around_replay.cpp), so tuning
// runs on the phone's real data.
//
// Thread-safe: the sensor thread and the GUI thread add records.
namespace AowisMobileMap
{
class LookAroundRecorder final
{
public:
    // Kinds: A accelerometer (x, y, z m/s^2), M magnetometer (x, y, z uT),
    // Y gyroscope (x, y, z rad/s), R OS rotation vector (x, y, z, w),
    // G game rotation vector (x, y, z, w), U uncalibrated magnetometer (x, y,
    // z, bias x, y, z uT), C camera result (valid, inliers, x, y, z, w of the
    // tracker rotation).
    struct Record
    {
        char kind = 0;
        double seconds = 0.0;
        double arrival = 0.0;
        std::array<double, 6> values{};
    };

    // About two minutes of all sensors at their usual rates.
    static constexpr std::size_t kMaxRecords = 400000;

    static LookAroundRecorder &instance()
    {
        static LookAroundRecorder recorder;
        return recorder;
    }

    void start()
    {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        this->m_records.clear();
        this->m_enabled.store(true);
    }

    void stop()
    {
        this->m_enabled.store(false);
    }

    bool enabled() const
    {
        return this->m_enabled.load(std::memory_order_relaxed);
    }

    void add(char kind, double seconds, std::initializer_list<double> values)
    {
        if (!enabled())
            return;
        Record record;
        record.kind = kind;
        record.seconds = seconds;
        record.arrival = arrivalSeconds();
        std::size_t index = 0;
        for (double value : values)
        {
            if (index < record.values.size())
                record.values[index++] = value;
        }
        std::lock_guard<std::mutex> lock(this->m_mutex);
        if (this->m_records.size() < kMaxRecords)
            this->m_records.push_back(record);
    }

    // The records so far; recording continues if enabled.
    std::vector<Record> take()
    {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        std::vector<Record> records;
        records.swap(this->m_records);
        return records;
    }

    // Arrival clock: CLOCK_BOOTTIME where available (Android, Linux), as the
    // map's sensorClockSeconds().
    static double arrivalSeconds()
    {
#if defined(CLOCK_BOOTTIME)
        timespec now{};
        if (clock_gettime(CLOCK_BOOTTIME, &now) == 0)
            return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
#endif
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    LookAroundRecorder() = default;

    std::mutex m_mutex;
    std::vector<Record> m_records;
    std::atomic<bool> m_enabled{false};
};
}
