#include "wireguard_vpn_settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDeadlineTimer>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

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
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    QString office;
    {
        WireGuardVpnSettings settings(nullptr, file);
        if (!check(settings.engineAvailable() && settings.profiles().isEmpty(), "new installation has no profiles")) return 1;

        // Diagnostics are translated texts with line numbers and never echo keys.
        const QString broken = QStringLiteral("[Interface]\nPrivateKey = nope\nAddress = 10.50.0.2/32\n[Peer]\nEndpoint = 127.0.0.1:9\nAllowedIPs = 10.50.0.0/24\nPostUp = rm -rf /\n");
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
            hook_warning = hook_warning || (text.startsWith(QStringLiteral("Warning: Line 7:")) && !message.value(QStringLiteral("error")).toBool());
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

        if (!check(settings.addProfile(QStringLiteral("Broken"), broken).isEmpty() && !settings.error().isEmpty(), "invalid configuration is not saved")) return 1;
        if (!check(settings.addProfile(QStringLiteral("  "), valid_conf).isEmpty(), "empty name rejected")) return 1;
        office = settings.addProfile(QStringLiteral(" Office "), valid_conf);
        if (!check(!office.isEmpty() && settings.error().isEmpty(), "valid profile saved")) return 1;
        if (!check(settings.addProfile(QStringLiteral("office"), valid_conf).isEmpty(), "duplicate name rejected (case-insensitive)")) return 1;
        const QString field = settings.addProfile(QStringLiteral("Field"), valid_conf);
        if (!check(!field.isEmpty(), "second profile saved")) return 1;
        const QVariantList listed = settings.profiles();
        if (!check(listed.size() == 2 && listed.at(0).toMap().value(QStringLiteral("name")) == QStringLiteral("Field")
                       && listed.at(1).toMap().value(QStringLiteral("endpoint")) == QStringLiteral("127.0.0.1:9"),
                   "profiles sorted by name with endpoint")) return 1;
        if (!check(!settings.updateProfile(field, QStringLiteral("Office"), valid_conf), "rename onto an existing name rejected")) return 1;
        if (!check(settings.updateProfile(field, QStringLiteral("Field 2"), valid_conf), "rename accepted")) return 1;
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
        if (!check(settings.updateProfile(office, QStringLiteral("Office"), edited)
                       && settings.status(office, 0).value(QStringLiteral("state")) == QStringLiteral("stopped"),
                   "configuration change stops the tunnel")) return 1;
        // Stop while starting: the tunnel never stays up.
        settings.startTunnel(office);
        settings.stopTunnel(office);
        if (!check(waitWhileStarting(settings, office) == QStringLiteral("stopped"), "stop during start wins")) return 1;
    }
    {
        WireGuardVpnSettings reloaded(nullptr, file);
        const QVariantList listed = reloaded.profiles();
        if (!check(listed.size() == 1 && reloaded.profileName(office) == QStringLiteral("Office")
                       && reloaded.profileText(office).contains(QStringLiteral("10.50.0.3/32")),
                   "profiles restored after restart")) return 1;
    }

    const QLocale c(QLocale::C);
    const QLocale german(QLocale::German, QLocale::Germany);
    if (!check(WireGuardVpnSettings::dataSizeText(1023, c) == QStringLiteral("1023 B")
                   && WireGuardVpnSettings::dataSizeText(320ull * 1024, c) == QStringLiteral("320 KiB")
                   && WireGuardVpnSettings::dataSizeText(1023ull * 1024 + 900, c) == QStringLiteral("1.00 MiB")
                   && WireGuardVpnSettings::dataSizeText(300000ull * 1024, german) == QStringLiteral("293 MiB")
                   && WireGuardVpnSettings::dataSizeText(5ull * 1024 * 1024 * 1024 / 2, german) == QStringLiteral("2,50 GiB"),
               "traffic sizes scale units without group separators")) return 1;
    qInfo() << "WireGuard VPN settings: profiles, diagnostics, persistence and tunnel start/stop passed.";
    return 0;
}
