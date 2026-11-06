#pragma once

#include <QObject>

#include <atomic>
#include <functional>
#include <thread>

// Android's fused TYPE_ROTATION_VECTOR sensor (gyroscope + accelerometer +
// magnetometer, calibrated by the OS) read directly through the NDK.
//
// Qt's Android QCompass backend derives azimuth from the raw accelerometer
// and magnetometer only. Without a gyroscope, every hand tremor tilts the
// measured gravity vector and swings the heading; this is the main reason
// dedicated compass apps appear much steadier and more precise.
//
// Samples are produced on a private ALooper thread and delivered on the
// thread owning this object (the GUI thread). On non-Android platforms,
// start() returns false and the caller falls back to QCompass.
class CompassRotationVectorSource final : public QObject
{
public:
    struct Sample
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double w = 1.0;
        double headingAccuracyDegrees = -1.0; // -1: not reported by the sensor
        int magneticStatus = -1;              // Android SENSOR_STATUS_*, -1 unknown
        double seconds = 0.0;                 // sensor timestamp (monotonic)
    };

    explicit CompassRotationVectorSource(QObject *parent = nullptr);
    ~CompassRotationVectorSource() override;

    void setSampleHandler(std::function<void(const Sample &)> handler);
    bool start();
    void stop();
    bool active() const;

private:
    void run(void *manager, const void *rotationSensor, const void *magneticSensor);

    std::function<void(const Sample &)> m_handler;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_running{false};
    std::atomic<void *> m_looper{nullptr};
    std::atomic<int> m_magneticStatus{-1};
    std::atomic<unsigned int> m_generation{0};
};
