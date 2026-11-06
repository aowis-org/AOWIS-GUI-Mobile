#include "flashlight_service.h"

#include <QCoreApplication>
#include <QPermissions>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace
{
#ifdef Q_OS_ANDROID
const char *kTorchBridge = "org/aowis/mobile/TorchBridge";

bool androidTorchAvailable()
{
    const QJniObject context(QNativeInterface::QAndroidApplication::context());
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(kTorchBridge, "isAvailable",
        "(Landroid/content/Context;)Z", context.object<jobject>()) == JNI_TRUE;
}

bool androidSetTorch(bool enabled)
{
    const QJniObject context(QNativeInterface::QAndroidApplication::context());
    if (!context.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(kTorchBridge, "setEnabled",
        "(Landroid/content/Context;Z)Z", context.object<jobject>(),
        static_cast<jboolean>(enabled ? JNI_TRUE : JNI_FALSE)) == JNI_TRUE;
}
#endif
}

FlashlightService::FlashlightService(QObject *parent)
    : QObject(parent)
{
    refreshAvailability();
}

FlashlightService::~FlashlightService()
{
    // Never leave the LED on when the application exits normally.
    if (this->m_enabled)
        setTorch(false);
}

bool FlashlightService::available() const
{
    return this->m_available;
}

bool FlashlightService::enabled() const
{
    return this->m_enabled;
}

bool FlashlightService::busy() const
{
    return this->m_busy;
}

QString FlashlightService::error() const
{
    return this->m_error;
}

void FlashlightService::setError(const QString &message)
{
    this->m_error = message;
    emit changed();
}

void FlashlightService::refreshAvailability()
{
#ifdef Q_OS_ANDROID
    const bool available = androidTorchAvailable();
#else
    const bool available = false;
#endif
    if (this->m_available != available)
    {
        this->m_available = available;
        emit changed();
    }
}

void FlashlightService::setApplicationActive(bool active)
{
    this->m_applicationActive = active;
    if (!active)
    {
        if (this->m_enabled)
            setTorch(false);
        this->m_busy = false;
        emit changed();
        return;
    }
    refreshAvailability();
}

void FlashlightService::setTorch(bool enabled)
{
#ifdef Q_OS_ANDROID
    const bool succeeded = androidSetTorch(enabled);
#else
    const bool succeeded = false;
#endif
    if (succeeded)
    {
        this->m_enabled = enabled;
        this->m_error.clear();
        emit changed();
    }
    else if (enabled)
        setError(tr("Could not switch on the flashlight. It may be in use by another app."));
    else
    {
        // On failure to turn off, never display a misleading confirmed OFF state.
        setError(tr("Could not switch off the flashlight."));
    }
}

void FlashlightService::toggle()
{
    if (this->m_busy)
        return;
    if (this->m_enabled)
    {
        setTorch(false);
        return;
    }
    if (!this->m_applicationActive)
        return;
    refreshAvailability();
    if (!this->m_available)
    {
        setError(tr("No flashlight is available on this device."));
        return;
    }

#ifdef Q_OS_ANDROID
    const QCameraPermission permission;
    if (qApp->checkPermission(permission) != Qt::PermissionStatus::Granted)
    {
        this->m_busy = true;
        this->m_error.clear();
        emit changed();
        qApp->requestPermission(permission, this, [this](const QPermission &result) {
            this->m_busy = false;
            emit changed();
            if (result.status() != Qt::PermissionStatus::Granted)
            {
                setError(tr("Camera permission is required to use the flashlight."));
                return;
            }
            // The permission prompt may have backgrounded the app.
            if (this->m_applicationActive)
                setTorch(true);
        });
        return;
    }
#endif
    setTorch(true);
}
