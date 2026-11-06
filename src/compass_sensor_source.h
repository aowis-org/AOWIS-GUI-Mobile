#pragma once

#include <QObject>

#include <atomic>
#include <functional>
#include <thread>

// Compass orientation straight from Android's NDK sensor API; no Qt Sensors.
//
// Preferred: the OS-fused TYPE_ROTATION_VECTOR. If the phone does not list
// one, or it stays silent, AOWIS fuses the raw sensors itself
// (compass_fusion.h): gyroscope + accelerometer + magnetometer, or
// accelerometer + magnetometer when there is no gyroscope.
//
// Sensor events are read on a private ALooper thread; orientation samples
// are delivered on the thread owning this object (the GUI thread). On
// non-Android platforms no hardware is reported and start() fails.
class CompassSensorSource final : public QObject
{
public:
    enum class Mode { None, RotationVector, FusionWithGyroscope, FusionWithoutGyroscope };

    struct Hardware
    {
        bool rotationVector = false;
        bool gyroscope = false;
        bool accelerometer = false;
        bool magnetometer = false;
    };

    struct Sample
    {
        // Device-to-world (East, North, Up) unit quaternion.
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double w = 1.0;
        double headingAccuracyDegrees = -1.0; // -1: not reported
        int magneticStatus = -1;              // Android SENSOR_STATUS_*, -1 unknown
        double seconds = 0.0;                 // sensor timestamp (monotonic)
    };

    explicit CompassSensorSource(QObject *parent = nullptr);
    ~CompassSensorSource() override;

    static Hardware probeHardware();

    void setSampleHandler(std::function<void(const Sample &)> handler);
    // Starts the best mode available, never better than 'best'.
    // E.g. start(Mode::FusionWithGyroscope) skips the OS rotation vector.
    bool start(Mode best = Mode::RotationVector);
    void stop();
    bool active() const;
    Mode mode() const;

private:
    void run(Mode mode);

    std::function<void(const Sample &)> m_handler;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_running{false};
    std::atomic<void *> m_looper{nullptr};
    std::atomic<unsigned int> m_generation{0};
    Mode m_mode = Mode::None;
};
