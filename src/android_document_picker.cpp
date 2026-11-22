#include "android_document_picker.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QThreadPool>
#include <QtCore/private/qandroidextras_p.h>
#endif

namespace AndroidDocumentPicker
{

namespace
{
#ifdef Q_OS_ANDROID
const char *kDocumentPicker = "org/aowis/mobile/DocumentPicker";
constexpr int kRequestCode = 0x4157; // "AW"
constexpr int kResultOk = -1;        // Activity.RESULT_OK
#endif

void deliver(QPointer<QObject> guard, std::function<void(Outcome, const QString &, const QString &)> done,
             Outcome outcome, const QString &name, const QString &text)
{
    // Posted to the application object (the GUI thread): context may be
    // destroyed meanwhile, the guard then drops the result.
    QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, done, outcome, name, text]() {
        if (guard)
            done(outcome, name, text);
    }, Qt::QueuedConnection);
}
} // namespace

bool available()
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

void pickText(QObject *context, int max_bytes,
              std::function<void(Outcome, const QString &name, const QString &text)> done)
{
#ifdef Q_OS_ANDROID
    const QJniObject intent = QJniObject::callStaticObjectMethod(
        kDocumentPicker, "openDocumentIntent", "()Landroid/content/Intent;");
    if (!intent.isValid())
    {
        deliver(context, done, Outcome::Unavailable, QString(), QString());
        return;
    }
    const QPointer<QObject> guard(context);
    // Retain the activity context while the system picker is open.
    const QJniObject app_context(QNativeInterface::QAndroidApplication::context());
    QtAndroidPrivate::startActivity(intent, kRequestCode,
        [guard, done, max_bytes, app_context](int request, int result, const QJniObject &data) {
            if (request != kRequestCode || !guard)
                return;
            if (result != kResultOk)
            {
                deliver(guard, done, Outcome::Cancelled, QString(), QString());
                return;
            }
            const QJniObject uri = QJniObject::callStaticObjectMethod(
                kDocumentPicker, "resultUri", "(Landroid/content/Intent;)Landroid/net/Uri;",
                data.object<jobject>());
            if (!uri.isValid())
            {
                deliver(guard, done, Outcome::Unavailable, QString(), QString());
                return;
            }
            QThreadPool::globalInstance()->start([guard, done, app_context, uri, max_bytes]() {
                const QJniObject read = QJniObject::callStaticObjectMethod(
                    kDocumentPicker, "readText", "(Landroid/content/Context;Landroid/net/Uri;I)Lorg/aowis/mobile/DocumentPicker$ReadResult;",
                    app_context.object<jobject>(), uri.object<jobject>(), static_cast<jint>(max_bytes));
                const QString status = read.isValid() ? read.getObjectField<jstring>("status").toString() : QString();
                const QString detail = status.isEmpty() ? QStringLiteral("JNI reader returned no status") : status;
                if (status != QStringLiteral("ok"))
                {
                    Outcome outcome = Outcome::Unreadable;
                    if (status == QStringLiteral("denied"))
                        outcome = Outcome::Denied;
                    else if (status == QStringLiteral("notFound"))
                        outcome = Outcome::NotFound;
                    else if (status == QStringLiteral("tooLarge"))
                        outcome = Outcome::TooLarge;
                    deliver(guard, done, outcome, QString(), detail);
                    return;
                }
                const QJniObject name = QJniObject::callStaticObjectMethod(
                    kDocumentPicker, "displayName", "(Landroid/content/Context;Landroid/net/Uri;)Ljava/lang/String;",
                    app_context.object<jobject>(), uri.object<jobject>());
                deliver(guard, done, Outcome::Picked, name.isValid() ? name.toString() : QString(),
                        read.getObjectField<jstring>("text").toString());
            });
        });
#else
    Q_UNUSED(max_bytes)
    deliver(context, done, Outcome::Unavailable, QString(), QString());
#endif
}

} // namespace AndroidDocumentPicker
