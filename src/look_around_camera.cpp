#include "look_around_camera.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#ifdef Q_OS_ANDROID
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <time.h>
#endif

#ifdef Q_OS_ANDROID
namespace
{
constexpr int kPreferredWidth = 640;
constexpr int kMinWidth = 320;
constexpr int kMaxWidth = 1280;
constexpr int kMaxImages = 3;
constexpr double kDefaultHorizontalFovDegrees = 66.0;

double clockSeconds(clockid_t clock)
{
    timespec now{};
    if (clock_gettime(clock, &now) != 0)
        return 0.0;
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}
}
#endif

struct LookAroundCamera::Impl
{
    LookAroundCamera *owner = nullptr;
    unsigned int generation = 0;
    std::mutex mutex;   // serializes frame processing against stop()
    bool stopping = false;
    AowisMobileMap::VisionTracker tracker;
    bool realtimeTimestamps = false;
#ifdef Q_OS_ANDROID
    ACameraManager *manager = nullptr;
    ACameraDevice *device = nullptr;
    AImageReader *reader = nullptr;
    ANativeWindow *window = nullptr;
    ACaptureSessionOutputContainer *outputs = nullptr;
    ACaptureSessionOutput *output = nullptr;
    ACameraOutputTarget *target = nullptr;
    ACaptureRequest *request = nullptr;
    ACameraCaptureSession *session = nullptr;
    ACameraDevice_StateCallbacks deviceCallbacks{};
    ACameraCaptureSession_stateCallbacks sessionCallbacks{};

    static void onDisconnected(void *, ACameraDevice *) {}
    static void onError(void *, ACameraDevice *, int error)
    {
        qWarning() << "AOWIS look around camera: device error" << error;
    }
    static void onSessionClosed(void *, ACameraCaptureSession *) {}
    static void onSessionReady(void *, ACameraCaptureSession *) {}
    static void onSessionActive(void *, ACameraCaptureSession *) {}

    static void onImageAvailable(void *context, AImageReader *reader)
    {
        static_cast<Impl *>(context)->processImage(reader);
    }

    void processImage(AImageReader *imageReader)
    {
        AImage *image = nullptr;
        if (AImageReader_acquireLatestImage(imageReader, &image) != AMEDIA_OK || image == nullptr)
            return;
        AowisMobileMap::VisionTracker::Result result;
        bool processed = false;
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (!this->stopping)
            {
                std::uint8_t *data = nullptr;
                int length = 0;
                int32_t width = 0;
                int32_t height = 0;
                int32_t rowStride = 0;
                int32_t pixelStride = 0;
                int64_t timestamp = 0;
                if (AImage_getPlaneData(image, 0, &data, &length) == AMEDIA_OK && data != nullptr &&
                    AImage_getWidth(image, &width) == AMEDIA_OK && AImage_getHeight(image, &height) == AMEDIA_OK &&
                    AImage_getPlaneRowStride(image, 0, &rowStride) == AMEDIA_OK &&
                    AImage_getPlanePixelStride(image, 0, &pixelStride) == AMEDIA_OK &&
                    AImage_getTimestamp(image, &timestamp) == AMEDIA_OK &&
                    length >= rowStride * (height - 1) + pixelStride * width)
                {
                    double seconds = static_cast<double>(timestamp) * 1e-9;
                    if (!this->realtimeTimestamps)
                        seconds += clockSeconds(CLOCK_BOOTTIME) - clockSeconds(CLOCK_MONOTONIC);
                    result = this->tracker.process(data, width, height, rowStride, pixelStride, seconds);
                    processed = true;
                }
            }
        }
        AImage_delete(image);
        if (!processed)
            return;
        QPointer<LookAroundCamera> target = this->owner;
        const unsigned int frameGeneration = this->generation;
        QMetaObject::invokeMethod(this->owner, [target, result, frameGeneration]() {
            if (target.isNull() || target->m_generation.load() != frameGeneration || !target->m_handler)
                return;
            target->m_handler(result);
        }, Qt::QueuedConnection);
    }

    // Opens the back camera; empty string on success, else the reason.
    QString open(int *reportedOrientation)
    {
        this->manager = ACameraManager_create();
        if (this->manager == nullptr)
            return QStringLiteral("no camera service");
        ACameraIdList *ids = nullptr;
        if (ACameraManager_getCameraIdList(this->manager, &ids) != ACAMERA_OK || ids == nullptr)
            return QStringLiteral("no camera list");
        std::string chosen;
        int orientation = -1;
        double focalMillimeters = 0.0;
        double sensorWidthMillimeters = 0.0;
        int32_t outputWidth = 0;
        int32_t outputHeight = 0;
        for (int index = 0; index < ids->numCameras && chosen.empty(); ++index)
        {
            ACameraMetadata *metadata = nullptr;
            if (ACameraManager_getCameraCharacteristics(this->manager, ids->cameraIds[index], &metadata) != ACAMERA_OK ||
                metadata == nullptr)
                continue;
            ACameraMetadata_const_entry entry{};
            const bool back = ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK &&
                              entry.count > 0 && entry.data.u8[0] == ACAMERA_LENS_FACING_BACK;
            if (back)
            {
                // Output size: YUV_420_888 near 640 wide, 4:3 preferred.
                if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK)
                {
                    double bestScore = 1e18;
                    for (uint32_t i = 0; i + 3 < entry.count; i += 4)
                    {
                        const int32_t format = entry.data.i32[i];
                        const int32_t w = entry.data.i32[i + 1];
                        const int32_t h = entry.data.i32[i + 2];
                        const int32_t input = entry.data.i32[i + 3];
                        if (format != AIMAGE_FORMAT_YUV_420_888 || input != 0 || w < kMinWidth || w > kMaxWidth || h <= 0)
                            continue;
                        const double aspect = std::fabs(double(w) / double(h) - 4.0 / 3.0);
                        const double score = std::fabs(double(w - kPreferredWidth)) + 400.0 * aspect;
                        if (score < bestScore)
                        {
                            bestScore = score;
                            outputWidth = w;
                            outputHeight = h;
                        }
                    }
                }
                if (outputWidth > 0)
                {
                    chosen = ids->cameraIds[index];
                    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_ORIENTATION, &entry) == ACAMERA_OK && entry.count > 0)
                        orientation = entry.data.i32[0];
                    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &entry) == ACAMERA_OK &&
                        entry.count > 0)
                        focalMillimeters = entry.data.f[0];
                    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_INFO_PHYSICAL_SIZE, &entry) == ACAMERA_OK &&
                        entry.count > 0)
                        sensorWidthMillimeters = entry.data.f[0];
                    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE, &entry) == ACAMERA_OK &&
                        entry.count > 0)
                        this->realtimeTimestamps = entry.data.u8[0] == ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
                }
            }
            ACameraMetadata_free(metadata);
        }
        ACameraManager_deleteCameraIdList(ids);
        if (chosen.empty())
            return QStringLiteral("no usable back camera");
        *reportedOrientation = orientation;
        // Focal length in output pixels (the output covers the sensor width).
        const double focal = focalMillimeters > 0.0 && sensorWidthMillimeters > 0.0
            ? focalMillimeters / sensorWidthMillimeters * outputWidth
            : 0.5 * outputWidth / std::tan(0.5 * kDefaultHorizontalFovDegrees * 3.14159265358979323846 / 180.0);
        this->tracker.setFocalLength(focal);
        qInfo().noquote() << "AOWIS look around camera:" << QString::fromStdString(chosen) << outputWidth << "x"
                          << outputHeight << "| orientation" << orientation << "| focal" << focal << "px"
                          << "| realtime timestamps" << this->realtimeTimestamps;

        if (AImageReader_new(outputWidth, outputHeight, AIMAGE_FORMAT_YUV_420_888, kMaxImages, &this->reader) != AMEDIA_OK ||
            this->reader == nullptr)
            return QStringLiteral("no image reader");
        AImageReader_ImageListener listener{this, &Impl::onImageAvailable};
        if (AImageReader_setImageListener(this->reader, &listener) != AMEDIA_OK)
            return QStringLiteral("no image listener");
        if (AImageReader_getWindow(this->reader, &this->window) != AMEDIA_OK || this->window == nullptr)
            return QStringLiteral("no image window");

        this->deviceCallbacks.context = this;
        this->deviceCallbacks.onDisconnected = &Impl::onDisconnected;
        this->deviceCallbacks.onError = &Impl::onError;
        if (ACameraManager_openCamera(this->manager, chosen.c_str(), &this->deviceCallbacks, &this->device) != ACAMERA_OK ||
            this->device == nullptr)
            return QStringLiteral("camera busy or not permitted");

        if (ACaptureSessionOutputContainer_create(&this->outputs) != ACAMERA_OK ||
            ACaptureSessionOutput_create(this->window, &this->output) != ACAMERA_OK ||
            ACaptureSessionOutputContainer_add(this->outputs, this->output) != ACAMERA_OK)
            return QStringLiteral("no capture output");
        this->sessionCallbacks.context = this;
        this->sessionCallbacks.onClosed = &Impl::onSessionClosed;
        this->sessionCallbacks.onReady = &Impl::onSessionReady;
        this->sessionCallbacks.onActive = &Impl::onSessionActive;
        if (ACameraDevice_createCaptureSession(this->device, this->outputs, &this->sessionCallbacks, &this->session) != ACAMERA_OK ||
            this->session == nullptr)
            return QStringLiteral("no capture session");
        if (ACameraDevice_createCaptureRequest(this->device, TEMPLATE_PREVIEW, &this->request) != ACAMERA_OK ||
            ACameraOutputTarget_create(this->window, &this->target) != ACAMERA_OK ||
            ACaptureRequest_addTarget(this->request, this->target) != ACAMERA_OK)
            return QStringLiteral("no capture request");
        // Up to 30 fps; a fixed upper rate also limits exposure (blur).
        const int32_t range[2] = {15, 30};
        ACaptureRequest_setEntry_i32(this->request, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, range);
        if (ACameraCaptureSession_setRepeatingRequest(this->session, nullptr, 1, &this->request, nullptr) != ACAMERA_OK)
            return QStringLiteral("capture did not start");
        return QString();
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->stopping = true;
        }
        if (this->session != nullptr)
        {
            ACameraCaptureSession_stopRepeating(this->session);
            ACameraCaptureSession_close(this->session);
            this->session = nullptr;
        }
        if (this->request != nullptr)
        {
            if (this->target != nullptr)
                ACaptureRequest_removeTarget(this->request, this->target);
            ACaptureRequest_free(this->request);
            this->request = nullptr;
        }
        if (this->target != nullptr)
        {
            ACameraOutputTarget_free(this->target);
            this->target = nullptr;
        }
        if (this->device != nullptr)
        {
            ACameraDevice_close(this->device);
            this->device = nullptr;
        }
        if (this->outputs != nullptr)
        {
            if (this->output != nullptr)
                ACaptureSessionOutputContainer_remove(this->outputs, this->output);
            ACaptureSessionOutputContainer_free(this->outputs);
            this->outputs = nullptr;
        }
        if (this->output != nullptr)
        {
            ACaptureSessionOutput_free(this->output);
            this->output = nullptr;
        }
        if (this->reader != nullptr)
        {
            AImageReader_delete(this->reader);
            this->reader = nullptr;
            this->window = nullptr;
        }
        if (this->manager != nullptr)
        {
            ACameraManager_delete(this->manager);
            this->manager = nullptr;
        }
    }
#endif
};

LookAroundCamera::LookAroundCamera(QObject *parent)
    : QObject(parent)
{
}

LookAroundCamera::~LookAroundCamera()
{
    stop();
}

void LookAroundCamera::setResultHandler(std::function<void(const AowisMobileMap::VisionTracker::Result &)> handler)
{
    this->m_handler = std::move(handler);
}

bool LookAroundCamera::start()
{
    if (this->m_impl)
        return true;
#ifdef Q_OS_ANDROID
    this->m_impl = std::make_unique<Impl>();
    this->m_impl->owner = this;
    this->m_impl->generation = this->m_generation.fetch_add(1) + 1;
    int orientation = -1;
    const QString problem = this->m_impl->open(&orientation);
    this->m_reportedOrientation = orientation;
    if (!problem.isEmpty())
    {
        qWarning().noquote() << "AOWIS look around camera:" << problem;
        this->m_impl->close();
        this->m_impl.reset();
        this->m_problem = problem;
        return false;
    }
    this->m_problem.clear();
    return true;
#else
    this->m_problem = QStringLiteral("camera only on Android");
    return false;
#endif
}

void LookAroundCamera::stop()
{
    // Results still queued from this run are ignored.
    this->m_generation.fetch_add(1);
    if (!this->m_impl)
        return;
#ifdef Q_OS_ANDROID
    this->m_impl->close();
#endif
    this->m_impl.reset();
}

bool LookAroundCamera::active() const
{
    return this->m_impl != nullptr;
}

QString LookAroundCamera::problem() const
{
    return this->m_problem;
}

int LookAroundCamera::reportedOrientation() const
{
    return this->m_reportedOrientation;
}
