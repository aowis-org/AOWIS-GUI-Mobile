#include "wireguard_vpn_settings.h"
#include "wireguard_key_sealing.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QNetworkProxy>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <functional>

static bool check(bool condition, const char *message)
{
    if (!condition)
        qCritical() << "FAILED:" << message;
    return condition;
}

// Keys from the wg(8) manual: syntactically valid, never a real peer.
static const QString valid_conf = QStringLiteral(
    "[Interface]\n"
    "PrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\n"
    "Address = 10.50.0.2/32\n"
    "\n"
    "[Peer]\n"
    "PublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\n"
    "Endpoint = 127.0.0.1:9\n"
    "AllowedIPs = 10.50.0.0/24\n");

static QString waitWhileStarting(WireGuardVpnSettings &settings, const QString &id)
{
    QDeadlineTimer deadline(15000);
    QString state;
    do
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        state = settings.status(id, settings.statusRevision()).value(QStringLiteral("state")).toString();
        if (state != QStringLiteral("starting"))
            return state;
        QThread::msleep(20);
    } while (!deadline.hasExpired());
    return state;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary settings directory"))
        return 1;
    // AOWIS-CONFIG namespace root ("aowis"); the owner "gui-mobile" lives below.
    const QString root = directory.filePath(QStringLiteral("aowis"));
    QString office;
    {
        WireGuardVpnSettings settings(nullptr, root);
        if (!check(settings.engineAvailable() && settings.profiles().isEmpty() && settings.storageProblem().isEmpty(),
                   "new installation has no profiles and usable storage")) return 1;

        // Diagnostics are translated texts with line numbers and never echo keys.
        // Hooks are ignored (warning) only in [Interface]; in [Peer] they are unknown.
        const QString broken = QStringLiteral("[Interface]\nPrivateKey = nope\nAddress = 10.50.0.2/32\nPostUp = rm -rf /\n[Peer]\nEndpoint = 127.0.0.1:9\nAllowedIPs = 10.50.0.0/24\n");
        const QVariantMap diagnosis = settings.check(broken);
        const QVariantList messages = diagnosis.value(QStringLiteral("messages")).toList();
        bool line_two_key = false;
        bool hook_warning = false;
        for (const QVariant &entry : messages)
        {
            const QVariantMap message = entry.toMap();
            const QString text = message.value(QStringLiteral("text")).toString();
            if (!check(!text.contains(QStringLiteral("nope")) && !text.contains(QStringLiteral("rm -rf")), "diagnostics never quote the text")) return 1;
            line_two_key = line_two_key || (text == QStringLiteral("Line 2: Invalid WireGuard key.") && message.value(QStringLiteral("error")).toBool());
            hook_warning = hook_warning || (text.startsWith(QStringLiteral("Warning: Line 4:")) && !message.value(QStringLiteral("error")).toBool());
        }
        if (!check(!diagnosis.value(QStringLiteral("ok")).toBool() && line_two_key && hook_warning,
                   "invalid key and ignored hook are reported with line numbers")) return 1;
        QString keyless = valid_conf;
        keyless.remove(QStringLiteral("PublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\n"));
        const QVariantList keyless_messages = settings.check(keyless).value(QStringLiteral("messages")).toList();
        if (!check(keyless_messages.size() == 1
                       && keyless_messages.at(0).toMap().value(QStringLiteral("text")) == QStringLiteral("The peer's PublicKey is missing."),
                   "missing peer public key reported without a line number")) return 1;
        if (!check(settings.check(valid_conf).value(QStringLiteral("ok")).toBool(), "valid configuration accepted")) return 1;

        if (!check(settings.addProfile(QStringLiteral("Broken"), QString(), broken).isEmpty() && !settings.error().isEmpty(), "invalid configuration is not saved")) return 1;
        if (!check(settings.addProfile(QStringLiteral("  "), QString(), valid_conf).isEmpty(), "empty name rejected")) return 1;
        office = settings.addProfile(QStringLiteral(" Office "), QStringLiteral(" Main site "), valid_conf);
        if (!check(!office.isEmpty() && settings.error().isEmpty(), "valid profile saved")) return 1;
        if (!check(QDir(root).exists(QStringLiteral("gui-mobile")), "stored in the gui-mobile owner root")) return 1;
        if (!check(settings.addProfile(QStringLiteral("office"), QString(), valid_conf).isEmpty(), "duplicate name rejected (case-insensitive)")) return 1;
        const QString field = settings.addProfile(QStringLiteral("Field"), QString(), valid_conf);
        if (!check(!field.isEmpty(), "second profile saved")) return 1;
        const QVariantList listed = settings.profiles();
        if (!check(listed.size() == 2 && listed.at(0).toMap().value(QStringLiteral("name")) == QStringLiteral("Field")
                       && listed.at(1).toMap().value(QStringLiteral("endpoint")) == QStringLiteral("127.0.0.1:9"),
                   "profiles sorted by name with endpoint")) return 1;
        if (!check(!settings.updateProfile(field, QStringLiteral("Office"), QString(), valid_conf), "rename onto an existing name rejected")) return 1;
        if (!check(settings.updateProfile(field, QStringLiteral("Field 2"), QString(), valid_conf), "rename accepted")) return 1;
        if (!check(settings.removeProfile(field) && settings.profiles().size() == 1, "profile removed")) return 1;
        if (!check(settings.suggestedName(QUrl(QStringLiteral("content://com.android.providers.downloads.documents/document/primary%3ADownload%2Fwg-office.conf"))) == QStringLiteral("wg-office"),
                   "name suggested from an Android document URI")) return 1;
        if (!check(settings.suggestedName(QUrl::fromLocalFile(QStringLiteral("/tmp/Office.conf"))) == QStringLiteral("Office (2)"),
                   "suggested name avoids existing names")) return 1;

        QFile conf(directory.filePath(QStringLiteral("office.conf")));
        if (!check(conf.open(QIODevice::WriteOnly) && conf.write(valid_conf.toUtf8()) > 0, "write .conf")) return 1;
        conf.close();
        if (!check(settings.readConfFile(QUrl::fromLocalFile(conf.fileName())) == valid_conf, "picked .conf file read")) return 1;
        if (!check(settings.readConfFile(QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.conf")))).isEmpty()
                       && !settings.error().isEmpty(), "unreadable file reported")) return 1;

        // The embedded engine starts (no peer answers 127.0.0.1:9, so no
        // handshake), reports status and stops again.
        if (!check(settings.status(office, 0).value(QStringLiteral("state")) == QStringLiteral("stopped"), "initially stopped")) return 1;
        if (!check(settings.startTunnel(office), "start accepted")) return 1;
        const QString started = waitWhileStarting(settings, office);
        const QVariantMap running = settings.status(office, settings.statusRevision());
        if (!check(started == QStringLiteral("waiting") && running.value(QStringLiteral("running")).toBool()
                       && running.value(QStringLiteral("handshakeText")) == QStringLiteral("Never")
                       && running.value(QStringLiteral("trafficText")).toString().contains(QStringLiteral("received")),
                   "running tunnel waits for its first handshake")) return 1;
        settings.stopTunnel(office);
        if (!check(settings.status(office, 0).value(QStringLiteral("state")) == QStringLiteral("stopped"), "stopped")) return 1;

        // Editing the configuration of a running profile stops its tunnel.
        settings.startTunnel(office);
        waitWhileStarting(settings, office);
        QString edited = valid_conf;
        edited.replace(QStringLiteral("10.50.0.2/32"), QStringLiteral("10.50.0.3/32"));
        if (!check(settings.updateProfile(office, QStringLiteral("Office"), QStringLiteral("Main site"), edited)
                       && settings.status(office, 0).value(QStringLiteral("state")) == QStringLiteral("stopped"),
                   "configuration change stops the tunnel")) return 1;
        // Stop while starting: the tunnel never stays up.
        settings.startTunnel(office);
        settings.stopTunnel(office);
        if (!check(waitWhileStarting(settings, office) == QStringLiteral("stopped"), "stop during start wins")) return 1;

        // A connection (the map server) requires the profile: the tunnel
        // starts on demand, its proxy is handed out, a user Stop blocks it
        // with a reason instead of a direct fallback, and the profile cannot
        // be deleted while it is used.
        int availability = 0;
        QObject::connect(&settings, &WireGuardVpnSettings::tunnelAvailabilityChanged, &settings,
                         [&availability, office](const QString &profile_id) { if (profile_id == office) ++availability; });
        settings.setProfileUsers([office](const QString &profile_id) {
            return profile_id == office ? QStringList{QStringLiteral("Map server")} : QStringList();
        });
        QNetworkProxy proxy;
        QString reason;
        settings.startTunnel(office);
        settings.stopTunnel(office); // A user Stop is remembered...
        waitWhileStarting(settings, office);
        settings.require(QStringLiteral("map-server"), office);
        if (!check(!settings.proxyFor(office, &proxy, &reason) && reason.contains(QStringLiteral("is stopped")),
                   "a user-stopped tunnel is not started for a connection")) return 1;
        settings.startTunnel(office); // ...until the user starts it again.
        waitWhileStarting(settings, office);
        if (!check(settings.proxyFor(office, &proxy, &reason) && proxy.type() == QNetworkProxy::Socks5Proxy
                       && proxy.port() > 0 && !proxy.user().isEmpty()
                       && (proxy.capabilities() & QNetworkProxy::HostNameLookupCapability),
                   "running tunnel hands out its authenticated SOCKS5 proxy")) return 1;
        if (!check(availability >= 2, "consumers are told about tunnel changes")) return 1;
        if (!check(!settings.removeProfile(office) && settings.error().contains(QStringLiteral("Map server")),
                   "a used profile cannot be deleted")) return 1;
        if (!check(settings.profileUsers(office, 0) == QStringList{QStringLiteral("Map server")}, "users listed")) return 1;
        settings.stopTunnel(office);
        if (!check(!settings.proxyFor(office, &proxy, &reason) && reason.contains(QStringLiteral("Settings")),
                   "stopped tunnel blocks the connection with a pointer to Settings")) return 1;
        // A user-started tunnel keeps running when the connection releases it.
        settings.startTunnel(office);
        waitWhileStarting(settings, office);
        settings.require(QStringLiteral("map-server"), QString());
        if (!check(settings.status(office, 0).value(QStringLiteral("running")).toBool(),
                   "user-started tunnel survives the release")) return 1;
        settings.stopTunnel(office);
        settings.setProfileUsers(std::function<QStringList(const QString &)>());

        // Demand alone starts a tunnel, and releasing it stops it again.
        const QString field_site = settings.addProfile(QStringLiteral("Field site"), QString(), valid_conf);
        settings.require(QStringLiteral("map-server"), field_site);
        if (!check(waitWhileStarting(settings, field_site) == QStringLiteral("waiting")
                       && settings.proxyFor(field_site, &proxy, &reason), "demand starts the tunnel")) return 1;
        settings.require(QStringLiteral("map-server"), QString());
        if (!check(settings.status(field_site, 0).value(QStringLiteral("state")) == QStringLiteral("stopped"),
                   "released demand stops the tunnel")) return 1;
        if (!check(settings.removeProfile(field_site), "unused profile deleted")) return 1;
    }
    {
        WireGuardVpnSettings reloaded(nullptr, root);
        const QVariantList listed = reloaded.profiles();
        if (!check(listed.size() == 1 && reloaded.profileName(office) == QStringLiteral("Office")
                       && reloaded.profileDescription(office) == QStringLiteral("Main site")
                       && reloaded.profileText(office).contains(QStringLiteral("10.50.0.3/32")),
                   "profiles restored after restart")) return 1;
    }

    // Secrets sealed in storage (Android: the Keystore). A reversible fake
    // sealer stands in for it here.
    {
        const QString prefix = WireGuardKeySealing::sealedPrefix();
        const WireGuardKeySealing::Seal seal = [prefix](const QString &plain) {
            QString reversed = plain;
            std::reverse(reversed.begin(), reversed.end());
            return prefix + QString::fromLatin1(reversed.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        };
        const WireGuardKeySealing::Unseal unseal = [prefix](const QString &token, QString *plain) {
            if (!token.startsWith(prefix))
                return false;
            QString value = QString::fromUtf8(QByteArray::fromBase64(token.mid(prefix.size()).toLatin1(), QByteArray::Base64UrlEncoding));
            std::reverse(value.begin(), value.end());
            *plain = value;
            return true;
        };
        const WireGuardKeySealing::Unseal foreign = [](const QString &, QString *) { return false; };

        // Only the secret values change; comments, other lines and CRLF stay.
        const QString text = QStringLiteral("[Interface]\r\n  privatekey=yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk= # mine\r\nAddress = 10.50.0.2/32\r\n"
                                            "[Peer]\r\nPresharedKey = FpCyhws9cxwWoV4xELtfJvjJN+zQVRPISllRWgeopVE=\r\n");
        QString sealed;
        if (!check(WireGuardKeySealing::hasPlainSecrets(text) && WireGuardKeySealing::sealSecrets(text, seal, &sealed)
                       && !sealed.contains(QStringLiteral("yAnz5TF")) && !sealed.contains(QStringLiteral("FpCyhws9"))
                       && sealed.contains(QStringLiteral("  privatekey=") + prefix) && sealed.contains(QStringLiteral(" # mine\r\n"))
                       && sealed.contains(QStringLiteral("Address = 10.50.0.2/32\r\n"))
                       && WireGuardKeySealing::hasSealedSecrets(sealed) && !WireGuardKeySealing::hasPlainSecrets(sealed),
                   "only PrivateKey/PresharedKey values are sealed")) return 1;
        QString opened;
        if (!check(WireGuardKeySealing::unsealSecrets(sealed, unseal, &opened) && opened == text, "sealed text opens to the original")) return 1;
        if (!check(!WireGuardKeySealing::unsealSecrets(sealed, foreign, &opened) && opened == sealed, "foreign key leaves the tokens")) return 1;
        const WireGuardKeySealing::Seal broken = [](const QString &) { return QString(); };
        if (!check(!WireGuardKeySealing::sealSecrets(text, broken, &sealed), "a failed seal is reported")) return 1;

        QTemporaryDir sealing_directory;
        const QString sealing_root = sealing_directory.filePath(QStringLiteral("aowis"));
        QString plain_id;
        {
            // Stored before sealing existed (or on desktop): plain.
            WireGuardVpnSettings plain(nullptr, sealing_root);
            plain.setKeySealing(WireGuardKeySealing::Seal(), WireGuardKeySealing::Unseal());
            plain_id = plain.addProfile(QStringLiteral("Legacy"), QString(), valid_conf);
            if (!check(!plain_id.isEmpty(), "plain profile stored")) return 1;
        }
        {
            // The first load with a sealer seals it in place.
            WireGuardVpnSettings migrating(nullptr, sealing_root);
            migrating.setKeySealing(seal, unseal);
            if (!check(migrating.profileText(plain_id) == valid_conf && migrating.storageProblem().isEmpty(),
                       "migrated profile still opens")) return 1;
            const QString sealed_id = migrating.addProfile(QStringLiteral("Sealed"), QString(), valid_conf);
            if (!check(!sealed_id.isEmpty() && migrating.profileText(sealed_id) == valid_conf, "new profile sealed and opened")) return 1;
            migrating.setKeySealing(broken, unseal);
            if (!check(migrating.addProfile(QStringLiteral("Unprotected"), QString(), valid_conf).isEmpty()
                           && migrating.error().contains(QStringLiteral("Keystore")),
                       "never stored plain when sealing fails")) return 1;
        }
        {
            // Without the sealing key (other phone, reinstall): locked, not
            // started, and the reason is shown, never a plain fallback.
            WireGuardVpnSettings stranger(nullptr, sealing_root);
            stranger.setKeySealing(seal, foreign);
            QNetworkProxy proxy;
            QString reason;
            bool all_locked = !stranger.profiles().isEmpty();
            for (const QVariant &entry : stranger.profiles())
                all_locked = all_locked && entry.toMap().value(QStringLiteral("locked")).toBool();
            if (!check(all_locked && !stranger.profileText(plain_id).contains(QStringLiteral("yAnz5TF"))
                           && stranger.storageProblem().contains(QStringLiteral("cannot be decrypted")),
                       "profiles sealed with another key are locked")) return 1;
            if (!check(!stranger.startTunnel(plain_id)
                           && stranger.status(plain_id, 1).value(QStringLiteral("stateText")).toString().contains(QStringLiteral("cannot be decrypted"))
                           && !stranger.proxyFor(plain_id, &proxy, &reason) && reason.contains(QStringLiteral("cannot be decrypted")),
                       "a locked profile never starts")) return 1;
            // Pasting the key again unlocks it.
            if (!check(stranger.updateProfile(plain_id, QStringLiteral("Legacy"), QString(), valid_conf), "re-entered key saved")) return 1;
            stranger.setKeySealing(seal, unseal);
            if (!check(stranger.profileText(plain_id) == valid_conf, "re-entered key opens with the new key")) return 1;
        }
    }

    const QLocale c(QLocale::C);
    const QLocale german(QLocale::German, QLocale::Germany);
    if (!check(WireGuardVpnSettings::dataSizeText(1023, c) == QStringLiteral("1023 B")
                   && WireGuardVpnSettings::dataSizeText(320ull * 1024, c) == QStringLiteral("320 KiB")
                   && WireGuardVpnSettings::dataSizeText(1023ull * 1024 + 900, c) == QStringLiteral("1.00 MiB")
                   && WireGuardVpnSettings::dataSizeText(300000ull * 1024, german) == QStringLiteral("293 MiB")
                   && WireGuardVpnSettings::dataSizeText(5ull * 1024 * 1024 * 1024 / 2, german) == QStringLiteral("2,50 GiB"),
               "traffic sizes scale units without group separators")) return 1;
    {
        // A second instance changed the profile: a stale revision is a
        // conflict, never a silent overwrite; the view reloads.
        WireGuardVpnSettings first(nullptr, root);
        WireGuardVpnSettings second(nullptr, root);
        if (!check(second.updateProfile(office, QStringLiteral("Office"), QStringLiteral("Changed"), second.profileText(office)),
                   "second instance updates")) return 1;
        if (!check(!first.updateProfile(office, QStringLiteral("Office"), QStringLiteral("Stale"), first.profileText(office))
                       && first.profileDescription(office) == QStringLiteral("Changed"),
                   "stale revision rejected and reloaded")) return 1;
        if (!check(first.removeProfile(office) && first.profiles().isEmpty(), "profile deleted")) return 1;
    }
    qInfo() << "WireGuard VPN settings: profiles, diagnostics, AOWIS-CONFIG persistence and tunnel start/stop passed.";
    return 0;
}
