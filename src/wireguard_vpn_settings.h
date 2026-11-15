#pragma once

#include <QHash>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <memory>

class QSettings;
class QThread;
class QTimer;

namespace aowis::network {
class WireGuardClient;
}

// Settings → WireGuard VPN. Stores WireGuard profiles (wg-quick .conf text,
// kept verbatim) in the app's private QSettings and runs one embedded tunnel
// per profile on request: AOWIS-NETWORK's userspace engine behind a loopback
// SOCKS5 proxy, so no Android VpnService and nothing else on the phone is
// routed. AOWIS-NETWORK's parser is the only one: whatever it accepts the
// engine can run. No app connection uses a tunnel yet; the map server will.
class WireGuardVpnSettings final : public QObject
{
    Q_OBJECT
    // [{id, name, endpoint, address}], sorted by name.
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    // Increments on every tunnel state change and once per second while a
    // tunnel runs; pass it to status() so QML bindings re-evaluate.
    Q_PROPERTY(int statusRevision READ statusRevision NOTIFY statusRevisionChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool engineAvailable READ engineAvailable CONSTANT)

public:
    explicit WireGuardVpnSettings(QObject *parent = nullptr,
                                  const QString &settingsFile = QString());
    ~WireGuardVpnSettings() override;

    QVariantList profiles() const;
    int statusRevision() const;
    QString error() const;
    bool engineAvailable() const;

    // {ok: bool, messages: [{error: bool, text: string}]}, translated, never
    // quoting keys or values from the text.
    Q_INVOKABLE QVariantMap check(const QString &text) const;
    // Returns the new profile's id, or an empty string with error() set.
    Q_INVOKABLE QString addProfile(const QString &name, const QString &text);
    // Saving a running profile stops its tunnel: it would run the old keys.
    Q_INVOKABLE bool updateProfile(const QString &id, const QString &name, const QString &text);
    Q_INVOKABLE bool removeProfile(const QString &id);
    Q_INVOKABLE QString profileName(const QString &id) const;
    Q_INVOKABLE QString profileText(const QString &id) const;
    // Reads a picked .conf (file:// or Android content:// URL); empty with
    // error() set when it cannot be read.
    Q_INVOKABLE QString readConfFile(const QUrl &url);
    Q_INVOKABLE QString suggestedName(const QUrl &url) const;

    Q_INVOKABLE bool startTunnel(const QString &id);
    Q_INVOKABLE void stopTunnel(const QString &id);
    // {state: stopped|starting|waiting|connected|stale|error, running: bool,
    //  stateText, handshakeText, trafficText, peerText}
    Q_INVOKABLE QVariantMap status(const QString &id, int revision) const;

    // Largest binary unit keeping the number below 1024, three significant
    // digits, no group separators ("312 KiB", "2.50 GiB").
    static QString dataSizeText(quint64 bytes, const QLocale &locale);

signals:
    void profilesChanged();
    void statusRevisionChanged();
    void errorChanged();

private:
    struct Profile
    {
        QString id;
        QString name;
        QString text;
    };
    struct Tunnel
    {
        std::shared_ptr<aowis::network::WireGuardClient> client;
        QThread *starting = nullptr; // Worker running WireGuardClient::start()
        bool stop_requested = false; // Stop pressed while starting
        bool running = false;
        QString error;               // Last start error (engine text)
    };

    int indexOf(const QString &id) const;
    bool nameTaken(const QString &name, const QString &except_id) const;
    bool validateProfile(const QString &name, const QString &text, const QString &except_id);
    bool persist();
    void setError(const QString &message);
    void bumpStatus();
    void updateTimer();
    void finishStart(const QString &id, QThread *thread, bool ok, const QString &message);
    void discardTunnel(const QString &id);

    std::unique_ptr<QSettings> settings;
    QVector<Profile> stored;
    QHash<QString, Tunnel> tunnels;
    QVector<QThread *> orphans; // Starts of discarded profiles, still running
    QTimer *poll = nullptr;
    int revision = 0;
    QString last_error;
};
