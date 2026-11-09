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
        // x..w is a new compass reading at 'seconds'. False on samples
        // emitted for a game rotation vector event: x..w and 'seconds' then
        // repeat the latest compass reading.
        bool absoluteFresh = true;
        // Gyroscope + accelerometer only (Android game rotation vector).
        // Smooth and precise for relative motion, but its heading reference
        // is arbitrary and drifts: Look around takes its motion from here,
        // at this sensor's own rate, and only its heading from x..w.
        bool hasRelative = false;
        double relativeSeconds = 0.0;         // game rotation event time
        double relativeX = 0.0;
        double relativeY = 0.0;
        double relativeZ = 0.0;
        double relativeW = 1.0;
        // AOWIS' own orientation from the raw accelerometer and magnetometer,
        // time-matched and identically filtered (look_around_field.h), for
        // Look around on phones without a real gyroscope. Set on samples
        // emitted for it (absoluteFresh false); x..w repeat the compass.
        bool hasField = false;
        double fieldSeconds = 0.0;            // magnetometer event time
        double fieldX = 0.0;
        double fieldY = 0.0;
        double fieldZ = 0.0;
        double fieldW = 1.0;
        double fieldStrength = 0.0;           // |B|, microtesla
        double fieldDipDegrees = 0.0;         // field below the horizontal
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

    // Diagnostics: events since start (any thread may read).
    struct Counters
    {
        unsigned long long accelerometer = 0;
        unsigned long long magnetometer = 0;
        unsigned long long rotationVector = 0;
        unsigned long long fieldOutputs = 0;
        unsigned long long fieldSamples = 0;
    };
    Counters counters() const;

private:
    void run(Mode mode);

    std::function<void(const Sample &)> m_handler;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_running{false};
    std::atomic<void *> m_looper{nullptr};
    std::atomic<unsigned int> m_generation{0};
    std::atomic<unsigned long long> m_accelerometerEvents{0};
    std::atomic<unsigned long long> m_magnetometerEvents{0};
    std::atomic<unsigned long long> m_rotationVectorEvents{0};
    std::atomic<unsigned long long> m_fieldOutputs{0};
    std::atomic<unsigned long long> m_fieldSamples{0};
    Mode m_mode = Mode::None;
};
