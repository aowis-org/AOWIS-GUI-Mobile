#include "android_keystore_sealer.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace
{
#ifdef Q_OS_ANDROID
const char *kKeystoreSealer = "org/aowis/mobile/KeystoreSealer";
#endif
}

namespace AndroidKeystoreSealer
{

WireGuardKeySealing::Seal seal()
{
#ifdef Q_OS_ANDROID
    return [](const QString &plain) -> QString {
        const QJniObject token = QJniObject::callStaticObjectMethod(
            kKeystoreSealer, "seal", "(Ljava/lang/String;)Ljava/lang/String;",
            QJniObject::fromString(plain).object<jstring>());
        return token.isValid() ? token.toString() : QString();
    };
#else
    return WireGuardKeySealing::Seal();
#endif
}

WireGuardKeySealing::Unseal unseal()
{
#ifdef Q_OS_ANDROID
    return [](const QString &token, QString *plain) -> bool {
        const QJniObject value = QJniObject::callStaticObjectMethod(
            kKeystoreSealer, "unseal", "(Ljava/lang/String;)Ljava/lang/String;",
            QJniObject::fromString(token).object<jstring>());
        if (!value.isValid())
            return false;
        *plain = value.toString();
        return true;
    };
#else
    return WireGuardKeySealing::Unseal();
#endif
}

} // namespace AndroidKeystoreSealer
