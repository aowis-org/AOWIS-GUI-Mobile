#include "compass_rotation_vector_source.h"

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
// 10 ms (100 Hz) is what SENSOR_DELAY_GAME typically delivers; the sensor
// clamps this to its own minimum delay.
constexpr int kRotationPeriodUs = 10000;
// The magnetometer is only read for its calibration status.
constexpr int kMagneticPeriodUs = 200000;
constexpr char kPackageName[] = "org.aowis.mobile";
#endif
}

CompassRotationVectorSource::CompassRotationVectorSource(QObject *parent)
    : QObject(parent)
{
}

CompassRotationVectorSource::~CompassRotationVectorSource()
{
    stop();
}

void CompassRotationVectorSource::setSampleHandler(std::function<void(const Sample &)> handler)
{
    this->m_handler = std::move(handler);
}

bool CompassRotationVectorSource::active() const
{
    return this->m_running.load();
}

bool CompassRotationVectorSource::start()
{
#ifdef Q_OS_ANDROID
    if (this->m_thread.joinable())
        return this->m_running.load();
    ASensorManager *manager = ASensorManager_getInstanceForPackage(kPackageName);
    if (manager == nullptr)
        return false;
    const ASensor *rotation = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_ROTATION_VECTOR);
    if (rotation == nullptr)
        return false; // No gyroscope fusion on this phone: use QCompass.
    const ASensor *magnetic = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_MAGNETIC_FIELD);
    this->m_stopRequested.store(false);
    this->m_generation.fetch_add(1);
    this->m_magneticStatus.store(-1);
    this->m_running.store(true);
    this->m_thread = std::thread([this, manager, rotation, magnetic]() {
        run(manager, rotation, magnetic);
    });
    return true;
#else
    return false;
#endif
}

void CompassRotationVectorSource::stop()
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
}

void CompassRotationVectorSource::run(void *managerPointer, const void *rotationPointer,
                                      const void *magneticPointer)
{
#ifdef Q_OS_ANDROID
    ASensorManager *manager = static_cast<ASensorManager *>(managerPointer);
    const ASensor *rotation = static_cast<const ASensor *>(rotationPointer);
    const ASensor *magnetic = static_cast<const ASensor *>(magneticPointer);
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ASensorEventQueue *queue = looper != nullptr
        ? ASensorManager_createEventQueue(manager, looper, kLooperIdent, nullptr, nullptr)
        : nullptr;
    if (queue == nullptr ||
        ASensorEventQueue_registerSensor(queue, rotation, kRotationPeriodUs, 0) < 0)
    {
        if (queue != nullptr)
            ASensorManager_destroyEventQueue(manager, queue);
        this->m_running.store(false);
        return;
    }
    if (magnetic != nullptr)
        ASensorEventQueue_registerSensor(queue, magnetic, kMagneticPeriodUs, 0);
    this->m_looper.store(looper);

    // Samples still queued from an earlier start/stop cycle must not be
    // presented as live after the page is reopened.
    const unsigned int generation = this->m_generation.load();
    ASensorEvent events[16];
    while (!this->m_stopRequested.load())
    {
        const int ident = ALooper_pollOnce(250, nullptr, nullptr, nullptr);
        if (ident != kLooperIdent)
            continue;
        ssize_t count = 0;
        while ((count = ASensorEventQueue_getEvents(queue, events, 16)) > 0)
        {
            for (ssize_t index = 0; index < count; ++index)
            {
                const ASensorEvent &event = events[index];
                if (event.type == ASENSOR_TYPE_MAGNETIC_FIELD)
                {
                    this->m_magneticStatus.store(event.magnetic.status);
                    continue;
                }
                if (event.type != ASENSOR_TYPE_ROTATION_VECTOR)
                    continue;
                Sample sample;
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
                    ? accuracy * 180.0 / 3.14159265358979323846 : -1.0;
                sample.magneticStatus = this->m_magneticStatus.load();
                sample.seconds = static_cast<double>(event.timestamp) / 1e9;
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
    ASensorEventQueue_disableSensor(queue, rotation);
    if (magnetic != nullptr)
        ASensorEventQueue_disableSensor(queue, magnetic);
    ASensorManager_destroyEventQueue(manager, queue);
#else
    Q_UNUSED(managerPointer)
    Q_UNUSED(rotationPointer)
    Q_UNUSED(magneticPointer)
#endif
}
