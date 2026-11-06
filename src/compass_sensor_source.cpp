#include "compass_sensor_source.h"
#include "compass_fusion.h"
#include "compass_heading.h"

#include <QMetaObject>

#include <cmath>
#include <utility>

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
    if (!registered)
    {
        ASensorManager_destroyEventQueue(manager, queue);
        this->m_running.store(false);
        return;
    }
    this->m_looper.store(looper);

    AowisCompass::AttitudeFusion fusion;
    fusion.setGyroscopeAvailable(gyroscope != nullptr);
    int magneticStatus = -1;
    // Samples still queued from an earlier start/stop cycle must not be
    // presented as live after the page is reopened.
    const unsigned int generation = this->m_generation.load();
    // Output cadence: the OS rotation vector, else the gyroscope, else the
    // magnetometer (the slowest input of the no-gyroscope fusion).
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

                if (event.type != drivingType)
                    continue;
                if (event.type == kRotationVectorType)
                {
                    sample.x = event.data[0];
                    sample.y = event.data[1];
                    sample.z = event.data[2];
                    // data[3] (w) is provided since API 18; recompute if absent.
                    const double xyz = sample.x * sample.x + sample.y * sample.y + sample.z * sample.z;
                    const double w = event.data[3];
                    sample.w = (std::isfinite(w) && std::fabs(xyz + w * w - 1.0) < 0.1)
                        ? w : std::sqrt(std::fmax(0.0, 1.0 - xyz));
                    const double accuracy = event.data[4];
                    sample.headingAccuracyDegrees = (std::isfinite(accuracy) && accuracy > 0.0)
                        ? accuracy * 180.0 / AowisCompass::kPi : -1.0;
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
                // Context object 'this': Qt drops the call if it is destroyed;
                // the destructor joins this thread before that can happen.
                QMetaObject::invokeMethod(this, [this, sample, generation]() {
                    if (this->m_handler && !this->m_stopRequested.load() &&
                        this->m_generation.load() == generation)
                        this->m_handler(sample);
                }, Qt::QueuedConnection);
            }
        }
    }

    this->m_looper.store(nullptr);
    for (const ASensor *sensor : {rotation, gyroscope, accelerometer, magnetometer})
    {
        if (sensor != nullptr)
            ASensorEventQueue_disableSensor(queue, sensor);
    }
    ASensorManager_destroyEventQueue(manager, queue);
#else
    Q_UNUSED(mode)
#endif
}
