#pragma once

#include "wireguard_key_sealing.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <memory>

class QNetworkProxy;
class QThread;
class QTimer;

namespace aowis::network {
class WireGuardClient;
}
namespace aowis::config {
class OwnerStore;
class WireGuardProfiles;
struct Error;
}

// Settings → WireGuard VPN. Stores WireGuard profiles (wg-quick .conf text,
// kept verbatim, plus name and description) with AOWIS-CONFIG in the
// "gui-mobile" owner root (Android: inside the app's private configuration
// directory; desktop: ~/.config/aowis/gui-mobile) and runs one embedded tunnel
// per profile on request: AOWIS-NETWORK's userspace engine behind a loopback
// SOCKS5 proxy, so no Android VpnService and nothing else on the phone is
// routed. AOWIS-NETWORK's parser is the only one: whatever it accepts the
// engine can run. Connections (the map server) require() a profile's tunnel:
// it starts on demand, is retried while it fails, and is never bypassed.
class WireGuardVpnSettings final : public QObject
{
    Q_OBJECT
    // [{id, name, description, endpoint, address}], sorted by name.
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    // Increments on every tunnel state change and once per second while a
    // tunnel runs; pass it to status() so QML bindings re-evaluate.
    Q_PROPERTY(int statusRevision READ statusRevision NOTIFY statusRevisionChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // Why profiles cannot be stored or listed (storage unavailable, damaged
    // records); empty when everything is readable.
    Q_PROPERTY(QString storageProblem READ storageProblem NOTIFY profilesChanged)
    Q_PROPERTY(bool engineAvailable READ engineAvailable CONSTANT)

public:
    // namespaceRoot: absolute path of an "aowis" configuration directory
    // (tests); empty selects the platform location.
    explicit WireGuardVpnSettings(QObject *parent = nullptr,
                                  const QString &namespaceRoot = QString());
    ~WireGuardVpnSettings() override;

    QVariantList profiles() const;
    int statusRevision() const;
    QString error() const;
    QString storageProblem() const;
    bool engineAvailable() const;

    // {ok: bool, messages: [{error: bool, text: string}]}, translated, never
    // quoting keys or values from the text.
    Q_INVOKABLE QVariantMap check(const QString &text) const;
    // Returns the new profile's id, or an empty string with error() set.
    Q_INVOKABLE QString addProfile(const QString &name, const QString &description, const QString &text);
    // Saving a changed configuration stops the profile's tunnel: it would
    // run the old keys.
    Q_INVOKABLE bool updateProfile(const QString &id, const QString &name,
                                   const QString &description, const QString &text);
    // Refused while an AOWIS connection references the profile (CONFIG's
    // reference guard; none does yet).
    Q_INVOKABLE bool removeProfile(const QString &id);
    Q_INVOKABLE QString profileName(const QString &id) const;
    Q_INVOKABLE QString profileDescription(const QString &id) const;
    Q_INVOKABLE QString profileText(const QString &id) const;
    // Reads a picked .conf (file:// or Android content:// URL); empty with
    // error() set when it cannot be read.
    Q_INVOKABLE QString readConfFile(const QUrl &url);
    Q_INVOKABLE QString suggestedName(const QUrl &url) const;

    // The user's Start/Stop. A stopped tunnel stays stopped, also for the
    // connections that require it (they fail with a pointer to Settings),
    // until started again or AOWIS restarts.
    Q_INVOKABLE bool startTunnel(const QString &id);
    Q_INVOKABLE void stopTunnel(const QString &id);

    // consumer (e.g. "map-server") needs profile_id's tunnel; an empty
    // profile_id releases it. A tunnel only a released consumer wanted stops.
    void require(const QString &consumer, const QString &profile_id);
    // The running tunnel's SOCKS5 proxy (in-tunnel host name lookup), or
    // false with a translated reason (starting, failed and retrying, stopped
    // by the user, profile missing). Never a direct fallback.
    bool proxyFor(const QString &profile_id, QNetworkProxy *proxy, QString *reason) const;
    // Labels of the connections using a profile ("Map server"); deleting a
    // used profile is refused, also by CONFIG's reference guard.
    void setProfileUsers(std::function<QStringList(const QString &)> users);
    Q_INVOKABLE QStringList profileUsers(const QString &id, int revision) const;
    // Call when a connection's profile choice changed.
    Q_INVOKABLE void notifyUsersChanged() { this->bumpStatus(); }
    // {state: stopped|starting|waiting|connected|stale|error, running: bool,
    //  stateText, handshakeText, trafficText, peerText}
    Q_INVOKABLE QVariantMap status(const QString &id, int revision) const;

    // Largest binary unit keeping the number below 1024, three significant
    // digits, no group separators ("312 KiB", "2.50 GiB").
    static QString dataSizeText(quint64 bytes, const QLocale &locale);

    // How PrivateKey/PresharedKey values are stored: sealed (Android: the
    // Keystore, set by the constructor) or, with empty functions, plain.
    // Plain profiles are sealed on the next load. Tests inject their own.
    void setKeySealing(WireGuardKeySealing::Seal seal, WireGuardKeySealing::Unseal unseal);

signals:
    void profilesChanged();
    void statusRevisionChanged();
    void errorChanged();
    // The tunnel of profile_id started, failed, stopped or was replaced:
    // consumers ask proxyFor() again and rebuild their connections.
    void tunnelAvailabilityChanged(const QString &profile_id);

private:
    struct Profile
    {
        QString id;
        QString revision; // CONFIG's optimistic-concurrency token
        QString name;
        QString description;
        QString text;        // Plain text (secrets opened)
        bool locked = false; // Its sealed secrets cannot be opened here
    };
    struct Tunnel
    {
        std::shared_ptr<aowis::network::WireGuardClient> client;
        QThread *starting = nullptr; // Worker running WireGuardClient::start()
        bool stop_requested = false; // Stop pressed while starting
        bool running = false;
        QString error;               // Last start error (engine text)
        int failures = 0;            // Consecutive failed starts (retry backoff)
        quint64 generation = 0;      // Invalidates pending retries
    };

    int indexOf(const QString &id) const;
    bool nameTaken(const QString &name, const QString &except_id) const;
    bool validateProfile(const QString &name, const QString &text, const QString &except_id);
    // The text as stored: secrets sealed when a sealer is set. False (error
    // set) when sealing fails; a secret is never stored plain instead.
    bool storedText(const QString &text, std::string *out);
    QString lockedText(const QString &name) const;
    bool openStorage();
    void reload();
    QString storageErrorText(const aowis::config::Error &error) const;
    void setError(const QString &message);
    void bumpStatus();
    void updateTimer();
    bool startEngine(const QString &id);
    void stopEngine(const QString &id);
    bool wanted(const QString &id) const;
    void finishStart(const QString &id, QThread *thread, bool ok, const QString &message);
    void discardTunnel(const QString &id);

    QString namespace_root;
    std::unique_ptr<aowis::config::OwnerStore> store;
    std::unique_ptr<aowis::config::WireGuardProfiles> profile_store;
    QString storage_problem;
    QVector<Profile> stored;
    QHash<QString, Tunnel> tunnels;
    QVector<QThread *> orphans; // Starts of discarded profiles, still running
    QSet<QString> user_started;  // Started with the Start button
    QSet<QString> user_stopped;  // Stopped with the Stop button
    QHash<QString, QString> consumers; // consumer -> required profile id
    std::function<QStringList(const QString &)> profile_users;
    WireGuardKeySealing::Seal sealer;
    WireGuardKeySealing::Unseal unsealer;
    QTimer *poll = nullptr;
    int revision = 0;
    QString last_error;
};
