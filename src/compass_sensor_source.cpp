#include "compass_sensor_source.h"
#include "compass_fusion.h"
#include "compass_heading.h"

#include <QDebug>
#include <QMetaObject>

#include <cmath>
#include <utility>
#include <vector>

#ifdef Q_OS_ANDROID
#include <android/looper.h>
#include <android/sensor.h>
#endif

namespace
{
#ifdef Q_OS_ANDROID
constexpr int kLooperIdent = 7;
constexpr int kGyroscopeType = 4;       // ASENSOR_TYPE_GYROSCOPE
constexpr int kRotationVectorType = 11; // ASENSOR_TYPE_ROTATION_VECTOR
constexpr int kGameRotationVectorType = 15; // ASENSOR_TYPE_GAME_ROTATION_VECTOR
// A game rotation vector older than this is not paired with a pose.
constexpr double kRelativeMaxAgeSeconds = 0.05;
// 100 Hz; each sensor clamps this to its own minimum delay.
constexpr int kFastPeriodUs = 10000;
// Status-only magnetometer while the OS rotation vector drives the heading.
constexpr int kStatusPeriodUs = 200000;
constexpr char kPackageName[] = "org.aowis.mobile";

ASensorManager *sensorManager()
{
    return ASensorManager_getInstanceForPackage(kPackageName);
}

const ASensor *defaultSensor(int type)
{
    ASensorManager *manager = sensorManager();
    return manager != nullptr ? ASensorManager_getDefaultSensor(manager, type) : nullptr;
}

// One logcat line per sensor, so a report can tell a real gyroscope from a
// vendor emulation (no gyroscope: the heading is magnetometer-only and the
// game rotation vector, if any, is emulated from the magnetometer too).
void logSensor(const char *role, const ASensor *sensor)
{
    if (sensor == nullptr)
    {
        qInfo().noquote() << "AOWIS compass sensor" << role << ": none";
        return;
    }
    qInfo().noquote() << "AOWIS compass sensor" << role << ":" << ASensor_getName(sensor)
                      << "| vendor" << ASensor_getVendor(sensor)
                      << "| min delay" << ASensor_getMinDelay(sensor) << "us";
}

// Unit quaternion of a (game) rotation vector event.
AowisCompass::Quaternion quaternionFromRotationEvent(const ASensorEvent &event)
{
    AowisCompass::Quaternion q;
    q.x = event.data[0];
    q.y = event.data[1];
    q.z = event.data[2];
    // data[3] (w) is provided since API 18; recompute if absent.
    const double xyz = q.x * q.x + q.y * q.y + q.z * q.z;
    const double w = event.data[3];
    q.w = (std::isfinite(w) && std::fabs(xyz + w * w - 1.0) < 0.1)
        ? w : std::sqrt(std::fmax(0.0, 1.0 - xyz));
    return q;
}
#endif
}

CompassSensorSource::CompassSensorSource(QObject *parent)
    : QObject(parent)
{
}

CompassSensorSource::~CompassSensorSource()
{
    stop();
}

CompassSensorSource::Hardware CompassSensorSource::probeHardware()
{
    Hardware hardware;
#ifdef Q_OS_ANDROID
    hardware.rotationVector = defaultSensor(kRotationVectorType) != nullptr;
    hardware.gyroscope = defaultSensor(kGyroscopeType) != nullptr;
    hardware.accelerometer = defaultSensor(ASENSOR_TYPE_ACCELEROMETER) != nullptr;
    hardware.magnetometer = defaultSensor(ASENSOR_TYPE_MAGNETIC_FIELD) != nullptr;
#endif
    return hardware;
}

void CompassSensorSource::setSampleHandler(std::function<void(const Sample &)> handler)
{
    this->m_handler = std::move(handler);
}

bool CompassSensorSource::active() const
{
    return this->m_running.load();
}

CompassSensorSource::Mode CompassSensorSource::mode() const
{
    return this->m_mode;
}

bool CompassSensorSource::start(Mode best)
{
    if (this->m_thread.joinable())
        return this->m_running.load();
    const Hardware hardware = probeHardware();
    Mode mode = Mode::None;
    if (best == Mode::RotationVector && hardware.rotationVector)
        mode = Mode::RotationVector;
    else if (best != Mode::FusionWithoutGyroscope && best != Mode::None &&
             hardware.gyroscope && hardware.accelerometer && hardware.magnetometer)
        mode = Mode::FusionWithGyroscope;
    else if (best != Mode::None && hardware.accelerometer && hardware.magnetometer)
        mode = Mode::FusionWithoutGyroscope;
    this->m_mode = mode;
    if (mode == Mode::None)
        return false;
    this->m_stopRequested.store(false);
    this->m_generation.fetch_add(1);
    this->m_running.store(true);
    this->m_thread = std::thread([this, mode]() { run(mode); });
    return true;
}

void CompassSensorSource::stop()
{
    this->m_stopRequested.store(true);
#ifdef Q_OS_ANDROID
    void *looper = this->m_looper.load();
    if (looper != nullptr)
        ALooper_wake(static_cast<ALooper *>(looper));
#endif
    if (this->m_thread.joinable())
        this->m_thread.join();
    this->m_running.store(false);
    this->m_mode = Mode::None;
}

void CompassSensorSource::run(Mode mode)
{
#ifdef Q_OS_ANDROID
    ASensorManager *manager = sensorManager();
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ASensorEventQueue *queue = (manager != nullptr && looper != nullptr)
        ? ASensorManager_createEventQueue(manager, looper, kLooperIdent, nullptr, nullptr)
        : nullptr;
    if (queue == nullptr)
    {
        this->m_running.store(false);
        return;
    }

    const ASensor *rotation = mode == Mode::RotationVector ? defaultSensor(kRotationVectorType) : nullptr;
    // Optional companion of the OS rotation vector for Look around.
    const ASensor *game = mode == Mode::RotationVector ? defaultSensor(kGameRotationVectorType) : nullptr;
    const ASensor *gyroscope = mode == Mode::FusionWithGyroscope ? defaultSensor(kGyroscopeType) : nullptr;
    const ASensor *accelerometer = mode != Mode::RotationVector
        ? defaultSensor(ASENSOR_TYPE_ACCELEROMETER) : nullptr;
    const ASensor *magnetometer = defaultSensor(ASENSOR_TYPE_MAGNETIC_FIELD);
    bool registered = true;
    if (rotation != nullptr)
        registered = registered && ASensorEventQueue_registerSensor(queue, rotation, kFastPeriodUs, 0) >= 0;
    if (gyroscope != nullptr)
        registered = registered && ASensorEventQueue_registerSensor(queue, gyroscope, kFastPeriodUs, 0) >= 0;
    if (accelerometer != nullptr)
        registered = registered && ASensorEventQueue_registerSensor(queue, accelerometer, kFastPeriodUs, 0) >= 0;
    if (magnetometer != nullptr)
    {
        const int period = mode == Mode::RotationVector ? kStatusPeriodUs : kFastPeriodUs;
        const bool ok = ASensorEventQueue_registerSensor(queue, magnetometer, period, 0) >= 0;
        if (mode != Mode::RotationVector)
            registered = registered && ok;
    }
    if (registered && game != nullptr &&
        ASensorEventQueue_registerSensor(queue, game, kFastPeriodUs, 0) < 0)
        game = nullptr;
    if (!registered)
    {
        ASensorManager_destroyEventQueue(manager, queue);
        this->m_running.store(false);
        return;
    }
    this->m_looper.store(looper);
    logSensor("rotation vector", rotation);
    logSensor("game rotation vector", game);
    logSensor("gyroscope", defaultSensor(kGyroscopeType));
    logSensor("magnetometer", magnetometer);

    AowisCompass::AttitudeFusion fusion;
    fusion.setGyroscopeAvailable(gyroscope != nullptr);
    int magneticStatus = -1;
    bool hasGame = false;
    AowisCompass::Quaternion gamePose;
    double gameSeconds = 0.0;
    // Latest OS rotation vector, repeated on game rotation vector samples.
    bool hasAbsolute = false;
    AowisCompass::Quaternion absolutePose;
    double absoluteSeconds = 0.0;
    double absoluteAccuracyDegrees = -1.0;
    // Samples still queued from an earlier start/stop cycle must not be
    // presented as live after the page is reopened.
    const unsigned int generation = this->m_generation.load();
    // Output cadence: the OS rotation vector, else the gyroscope, else the
    // magnetometer (the slowest input of the no-gyroscope fusion). Game
    // rotation vector events add samples of their own (absoluteFresh false).
    const int drivingType = mode == Mode::RotationVector ? kRotationVectorType
                          : mode == Mode::FusionWithGyroscope ? kGyroscopeType
                          : ASENSOR_TYPE_MAGNETIC_FIELD;

    ASensorEvent events[32];
    while (!this->m_stopRequested.load())
    {
        const int ident = ALooper_pollOnce(250, nullptr, nullptr, nullptr);
        if (ident != kLooperIdent)
            continue;
        ssize_t count = 0;
        while ((count = ASensorEventQueue_getEvents(queue, events, 32)) > 0)
        {
            std::vector<Sample> samples;
            samples.reserve(static_cast<std::size_t>(count));
            for (ssize_t index = 0; index < count; ++index)
            {
                const ASensorEvent &event = events[index];
                const double seconds = static_cast<double>(event.timestamp) / 1e9;
                Sample sample;
                if (event.type == ASENSOR_TYPE_MAGNETIC_FIELD)
                {
                    magneticStatus = event.magnetic.status;
                    fusion.addMagneticField(event.magnetic.x, event.magnetic.y, event.magnetic.z, seconds);
                }
                else if (event.type == ASENSOR_TYPE_ACCELEROMETER)
                    fusion.addAccelerometer(event.acceleration.x, event.acceleration.y,
                                            event.acceleration.z, seconds);
                else if (event.type == kGyroscopeType)
                    fusion.addGyroscope(event.data[0], event.data[1], event.data[2], seconds);
                else if (event.type == kGameRotationVectorType)
                {
                    gamePose = quaternionFromRotationEvent(event);
                    gameSeconds = seconds;
                    hasGame = true;
                }

                // Motion first: each game rotation vector event is a sample of
                // its own, so the compass never sets the pace of Look around.
                const bool gameSample = event.type == kGameRotationVectorType && hasAbsolute;
                if (event.type != drivingType && !gameSample)
                    continue;
                if (event.type == kRotationVectorType || gameSample)
                {
                    if (event.type == kRotationVectorType)
                    {
                        absolutePose = quaternionFromRotationEvent(event);
                        absoluteSeconds = seconds;
                        const double accuracy = event.data[4];
                        absoluteAccuracyDegrees = (std::isfinite(accuracy) && accuracy > 0.0)
                            ? accuracy * 180.0 / AowisCompass::kPi : -1.0;
                        hasAbsolute = true;
                    }
                    sample.absoluteFresh = !gameSample;
                    sample.x = absolutePose.x;
                    sample.y = absolutePose.y;
                    sample.z = absolutePose.z;
                    sample.w = absolutePose.w;
                    sample.headingAccuracyDegrees = absoluteAccuracyDegrees;
                    if (hasGame && std::fabs(seconds - gameSeconds) <= kRelativeMaxAgeSeconds)
                    {
                        sample.hasRelative = true;
                        sample.relativeSeconds = gameSeconds;
                        sample.relativeX = gamePose.x;
                        sample.relativeY = gamePose.y;
                        sample.relativeZ = gamePose.z;
                        sample.relativeW = gamePose.w;
                    }
                    sample.magneticStatus = magneticStatus;
                    sample.seconds = absoluteSeconds;
                    samples.push_back(sample);
                    continue;
                }
                else
                {
                    if (!fusion.hasOrientation())
                        continue;
                    const AowisCompass::Quaternion q = fusion.orientation();
                    sample.x = q.x;
                    sample.y = q.y;
                    sample.z = q.z;
                    sample.w = q.w;
                }
                sample.magneticStatus = magneticStatus;
                sample.seconds = seconds;
                samples.push_back(sample);
            }
            if (samples.empty())
                continue;
            // Keep every reading for fusion, with one GUI event per NDK batch.
            QMetaObject::invokeMethod(this, [this, samples = std::move(samples), generation]() {
                for (const Sample &sample : samples)
                {
                    if (!this->m_handler || this->m_stopRequested.load() ||
                        this->m_generation.load() != generation)
                        return;
                    this->m_handler(sample);
                }
            }, Qt::QueuedConnection);
        }
    }

    this->m_looper.store(nullptr);
    for (const ASensor *sensor : {rotation, game, gyroscope, accelerometer, magnetometer})
    {
        if (sensor != nullptr)
            ASensorEventQueue_disableSensor(queue, sensor);
    }
    ASensorManager_destroyEventQueue(manager, queue);
#else
    Q_UNUSED(mode)
#endif
}
