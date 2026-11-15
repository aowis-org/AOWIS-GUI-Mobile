#include "wireguard_vpn_settings.h"

#include <aowis/config/config_root.h>
#include <aowis/config/object_id.h>
#include <aowis/config/owner_id.h>
#include <aowis/config/wireguard_profiles.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

#if AOWIS_MOBILE_WITH_WIREGUARD
#include "aowis/network/wireguard_client.h"
#include "aowis/network/wireguard_conf.h"
#endif

namespace {

// A handshake older than WireGuard's rekey/expiry window (180 s) means the
// tunnel carries no traffic, whatever the engine state says.
constexpr qint64 stale_handshake_ms = 180 * 1000;
constexpr qint64 max_conf_bytes = 2 * 1024 * 1024;

QString ageText(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    if (seconds < 60)
        return WireGuardVpnSettings::tr("%1 s ago").arg(seconds);
    if (seconds < 3600)
        return WireGuardVpnSettings::tr("%1 min ago").arg(seconds / 60);
    return WireGuardVpnSettings::tr("%1 h ago").arg(seconds / 3600);
}

#if AOWIS_MOBILE_WITH_WIREGUARD
QString issueText(aowis::network::WireGuardConfIssue issue)
{
    using Issue = aowis::network::WireGuardConfIssue;
    switch (issue)
    {
    case Issue::invalid_text: return WireGuardVpnSettings::tr("The text contains a NUL byte or is larger than 2 MiB.");
    case Issue::invalid_line: return WireGuardVpnSettings::tr("Not a [section] or key = value line.");
    case Issue::unexpected_section: return WireGuardVpnSettings::tr("Unknown section; only [Interface] and [Peer] are supported.");
    case Issue::duplicate_interface: return WireGuardVpnSettings::tr("More than one [Interface] section.");
    case Issue::multiple_peers: return WireGuardVpnSettings::tr("More than one [Peer] section; AOWIS supports exactly one peer.");
    case Issue::directive_outside_section: return WireGuardVpnSettings::tr("Setting outside of a section.");
    case Issue::empty_value: return WireGuardVpnSettings::tr("Setting without a value.");
    case Issue::duplicate_directive: return WireGuardVpnSettings::tr("Setting appears more than once.");
    case Issue::unknown_directive: return WireGuardVpnSettings::tr("Unknown setting.");
    case Issue::ignored_directive: return WireGuardVpnSettings::tr("Setting is ignored by AOWIS; hook commands are never run.");
    case Issue::invalid_key: return WireGuardVpnSettings::tr("Invalid WireGuard key.");
    case Issue::multiple_addresses: return WireGuardVpnSettings::tr("Only one interface address is supported.");
    case Issue::invalid_address: return WireGuardVpnSettings::tr("Invalid interface address.");
    case Issue::invalid_dns: return WireGuardVpnSettings::tr("Invalid DNS server address.");
    case Issue::ignored_dns_search_domain: return WireGuardVpnSettings::tr("DNS search domains are ignored.");
    case Issue::invalid_allowed_ip: return WireGuardVpnSettings::tr("Invalid AllowedIPs entry.");
    case Issue::invalid_endpoint: return WireGuardVpnSettings::tr("Invalid endpoint; use host:port or [IPv6]:port.");
    case Issue::invalid_number: return WireGuardVpnSettings::tr("Invalid number.");
    case Issue::missing_interface: return WireGuardVpnSettings::tr("The [Interface] section is missing.");
    case Issue::missing_peer: return WireGuardVpnSettings::tr("The [Peer] section is missing.");
    case Issue::missing_private_key: return WireGuardVpnSettings::tr("PrivateKey is missing.");
    case Issue::missing_address: return WireGuardVpnSettings::tr("Address is missing.");
    case Issue::missing_peer_public_key: return WireGuardVpnSettings::tr("The peer's PublicKey is missing.");
    case Issue::missing_endpoint: return WireGuardVpnSettings::tr("The peer's Endpoint is missing.");
    case Issue::missing_allowed_ips: return WireGuardVpnSettings::tr("The peer's AllowedIPs are missing.");
    }
    return WireGuardVpnSettings::tr("Invalid configuration.");
}

aowis::network::WireGuardConfParse parse(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    return aowis::network::parseWireGuardConf(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}
#endif

} // namespace

WireGuardVpnSettings::WireGuardVpnSettings(QObject *parent, const QString &namespaceRoot)
    : QObject(parent)
    , namespace_root(namespaceRoot)
{
    this->openStorage();
    this->reload();

    this->poll = new QTimer(this);
    this->poll->setInterval(1000);
    connect(this->poll, &QTimer::timeout, this, &WireGuardVpnSettings::bumpStatus);
}

WireGuardVpnSettings::~WireGuardVpnSettings()
{
    // Starts block at most for endpoint resolution; never leave a worker
    // running against a destroyed object. Clients stop as they are released.
    for (QHash<QString, Tunnel>::iterator it = this->tunnels.begin(); it != this->tunnels.end(); ++it)
    {
        if (it->starting != nullptr)
        {
            it->starting->disconnect(this);
            it->starting->wait();
            delete it->starting;
            it->starting = nullptr;
        }
    }
    for (QThread *thread : this->orphans)
    {
        thread->disconnect(this);
        thread->wait();
        delete thread;
    }
    this->orphans.clear();
    this->tunnels.clear();
}

QVariantList WireGuardVpnSettings::profiles() const
{
    QVector<Profile> sorted = this->stored;
    std::sort(sorted.begin(), sorted.end(), [](const Profile &a, const Profile &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    QVariantList list;
    for (const Profile &profile : sorted)
    {
        QVariantMap item;
        item.insert(QStringLiteral("id"), profile.id);
        item.insert(QStringLiteral("name"), profile.name);
        item.insert(QStringLiteral("description"), profile.description);
#if AOWIS_MOBILE_WITH_WIREGUARD
        const aowis::network::WireGuardConfParse parsed = parse(profile.text);
        item.insert(QStringLiteral("endpoint"), parsed.ok() ? QString::fromStdString(parsed.config.endpoint) : QString());
        item.insert(QStringLiteral("address"), parsed.ok() ? QString::fromStdString(parsed.config.address) : QString());
#endif
        list.append(item);
    }
    return list;
}

int WireGuardVpnSettings::statusRevision() const
{
    return this->revision;
}

QString WireGuardVpnSettings::error() const
{
    return this->last_error;
}

QString WireGuardVpnSettings::storageProblem() const
{
    return this->storage_problem;
}

bool WireGuardVpnSettings::openStorage()
{
    if (this->profile_store)
        return true;
    const aowis::config::Result<aowis::config::OwnerId> owner = aowis::config::OwnerId::parse("gui-mobile");
    if (!owner)
    {
        this->storage_problem = tr("WireGuard profiles cannot be stored: %1").arg(this->storageErrorText(owner.error()));
        return false;
    }
    // AOWIS-CONFIG: Android has no meaningful HOME/XDG, so the root is the
    // app's private configuration directory there; elsewhere the user's
    // configuration directory (~/.config/aowis/gui-mobile), as for the GUI.
    QString base = this->namespace_root;
    if (base.isEmpty())
    {
#if defined(Q_OS_ANDROID)
        const QString location = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#else
        const QString location = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#endif
        if (!location.isEmpty())
            base = QDir(location).filePath(QStringLiteral("aowis"));
    }
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
    {
        this->storage_problem = tr("WireGuard profiles cannot be stored: no private configuration directory is available.");
        return false;
    }
    const aowis::config::Result<aowis::config::ConfigRoot> root = aowis::config::ConfigRoot::fromNamespaceRoot(
        owner.value(), std::filesystem::path(QDir::cleanPath(base).toStdString()));
    if (!root)
    {
        this->storage_problem = tr("WireGuard profiles cannot be stored: %1").arg(this->storageErrorText(root.error()));
        return false;
    }
    aowis::config::Result<aowis::config::OwnerStore> opened = aowis::config::OwnerStore::open(root.value());
    if (!opened)
    {
        this->storage_problem = tr("WireGuard profiles cannot be stored: %1").arg(this->storageErrorText(opened.error()));
        return false;
    }
    aowis::config::Result<aowis::config::WireGuardProfiles> profiles = aowis::config::WireGuardProfiles::open(opened.value());
    if (!profiles)
    {
        this->storage_problem = tr("WireGuard profiles cannot be stored: %1").arg(this->storageErrorText(profiles.error()));
        return false;
    }
    this->store = std::make_unique<aowis::config::OwnerStore>(std::move(opened.value()));
    this->profile_store = std::make_unique<aowis::config::WireGuardProfiles>(std::move(profiles.value()));
    this->storage_problem.clear();
    return true;
}

void WireGuardVpnSettings::reload()
{
    this->stored.clear();
    if (!this->openStorage())
        return;
    const aowis::config::Result<aowis::config::WireGuardProfileListing> listing = this->profile_store->list();
    if (!listing)
    {
        this->storage_problem = tr("WireGuard profiles cannot be read: %1").arg(this->storageErrorText(listing.error()));
        return;
    }
    int unreadable = static_cast<int>(listing.value().unreadable.size());
    for (const aowis::config::WireGuardProfileSummary &summary : listing.value().profiles)
    {
        const aowis::config::Result<aowis::config::WireGuardProfile> read = this->profile_store->read(summary.info.id);
        if (!read)
        {
            ++unreadable;
            continue;
        }
        Profile profile;
        profile.id = QString::fromStdString(read.value().summary.info.id.value());
        profile.revision = QString::fromStdString(read.value().summary.info.revision);
        profile.name = QString::fromStdString(read.value().summary.name);
        profile.description = QString::fromStdString(read.value().summary.description);
        profile.text = QString::fromStdString(read.value().configuration);
        this->stored.append(profile);
    }
    this->storage_problem = unreadable > 0
        ? tr("%1 stored WireGuard profiles are damaged and cannot be read.").arg(unreadable)
        : QString();
}

QString WireGuardVpnSettings::storageErrorText(const aowis::config::Error &error) const
{
    using Code = aowis::config::ErrorCode;
    switch (error.code)
    {
    case Code::conflict: return tr("The profile was changed elsewhere; open it again and retry.");
    case Code::not_found: return tr("Profile not found.");
    case Code::invalid_wireguard_profile: return tr("The name or configuration cannot be stored (too long, line breaks in the name, or not UTF-8).");
    case Code::profile_in_use: return tr("An AOWIS connection uses this profile; choose another profile there first.");
    case Code::reference_unavailable: return tr("Could not check which connections use this profile.");
    case Code::lock_timeout: return tr("The configuration storage is busy; try again.");
    case Code::permission_denied:
    case Code::unsafe_path: return tr("The configuration directory is not private to AOWIS.");
    default: break;
    }
    // CONFIG messages are contextual labels without secrets.
    return QString::fromStdString(error.message);
}

bool WireGuardVpnSettings::engineAvailable() const
{
    return AOWIS_MOBILE_WITH_WIREGUARD != 0;
}

QVariantMap WireGuardVpnSettings::check(const QString &text) const
{
    QVariantMap result;
    QVariantList messages;
#if AOWIS_MOBILE_WITH_WIREGUARD
    const aowis::network::WireGuardConfParse parsed = parse(text);
    for (const aowis::network::WireGuardConfDiagnostic &diagnostic : parsed.diagnostics)
    {
        const bool is_error = diagnostic.severity == aowis::network::WireGuardConfSeverity::error;
        QString message = issueText(diagnostic.issue);
        if (diagnostic.line > 0)
            message = tr("Line %1: %2").arg(static_cast<qulonglong>(diagnostic.line)).arg(message);
        if (!is_error)
            message = tr("Warning: %1").arg(message);
        QVariantMap item;
        item.insert(QStringLiteral("error"), is_error);
        item.insert(QStringLiteral("text"), message);
        messages.append(item);
    }
    result.insert(QStringLiteral("ok"), parsed.ok());
#else
    Q_UNUSED(text)
    QVariantMap item;
    item.insert(QStringLiteral("error"), true);
    item.insert(QStringLiteral("text"), tr("WireGuard is not available in this build."));
    messages.append(item);
    result.insert(QStringLiteral("ok"), false);
#endif
    result.insert(QStringLiteral("messages"), messages);
    return result;
}

int WireGuardVpnSettings::indexOf(const QString &id) const
{
    for (int i = 0; i < this->stored.size(); ++i)
    {
        if (this->stored.at(i).id == id)
            return i;
    }
    return -1;
}

bool WireGuardVpnSettings::nameTaken(const QString &name, const QString &except_id) const
{
    for (const Profile &profile : this->stored)
    {
        if (profile.id != except_id && profile.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool WireGuardVpnSettings::validateProfile(const QString &name, const QString &text, const QString &except_id)
{
    if (name.trimmed().isEmpty())
    {
        this->setError(tr("Enter a profile name."));
        return false;
    }
    if (this->nameTaken(name.trimmed(), except_id))
    {
        this->setError(tr("A profile with this name already exists."));
        return false;
    }
    if (!this->check(text).value(QStringLiteral("ok")).toBool())
    {
        this->setError(tr("The configuration has errors; fix them before saving."));
        return false;
    }
    return true;
}


QString WireGuardVpnSettings::addProfile(const QString &name, const QString &description, const QString &text)
{
    if (!this->validateProfile(name, text, QString()))
        return QString();
    if (!this->openStorage())
    {
        this->setError(this->storage_problem);
        return QString();
    }
    const aowis::config::WireGuardProfileInput input{
        name.trimmed().toStdString(), description.trimmed().toStdString(), text.toStdString()};
    const aowis::config::Result<aowis::config::WireGuardProfile> imported = this->profile_store->importText(input);
    if (!imported)
    {
        this->setError(this->storageErrorText(imported.error()));
        return QString();
    }
    const QString id = QString::fromStdString(imported.value().summary.info.id.value());
    this->reload();
    this->setError(QString());
    emit profilesChanged();
    return id;
}

bool WireGuardVpnSettings::updateProfile(const QString &id, const QString &name,
                                         const QString &description, const QString &text)
{
    const int index = this->indexOf(id);
    const aowis::config::Result<aowis::config::ObjectId> object_id = aowis::config::ObjectId::parse(id.toStdString());
    if (index < 0 || !object_id || !this->profile_store)
    {
        this->setError(tr("Profile not found."));
        return false;
    }
    if (!this->validateProfile(name, text, id))
        return false;
    const bool text_changed = this->stored.at(index).text != text;
    const aowis::config::WireGuardProfileInput input{
        name.trimmed().toStdString(), description.trimmed().toStdString(), text.toStdString()};
    const aowis::config::Result<aowis::config::WireGuardProfile> updated = this->profile_store->update(
        object_id.value(), this->stored.at(index).revision.toStdString(), input);
    if (!updated)
    {
        this->setError(this->storageErrorText(updated.error()));
        this->reload();
        emit profilesChanged();
        return false;
    }
    if (text_changed)
        this->discardTunnel(id);
    this->reload();
    this->setError(QString());
    emit profilesChanged();
    return true;
}

bool WireGuardVpnSettings::removeProfile(const QString &id)
{
    const int index = this->indexOf(id);
    const aowis::config::Result<aowis::config::ObjectId> object_id = aowis::config::ObjectId::parse(id.toStdString());
    if (index < 0 || !object_id || !this->profile_store)
    {
        this->setError(tr("Profile not found."));
        return false;
    }
    const QStringList users = this->profileUsers(id, 0);
    if (!users.isEmpty())
    {
        this->setError(tr("Used by: %1. Change its connection in Settings first.").arg(users.join(QStringLiteral(", "))));
        return false;
    }
    // CONFIG's guard counts again inside its owner session.
    const aowis::config::WireGuardReferenceCount references =
        [this](const aowis::config::OwnerSession &, const aowis::config::ObjectId &object)
    {
        const QString referenced = QString::fromStdString(object.value());
        return aowis::config::Result<std::size_t>::success(
            static_cast<std::size_t>(this->profileUsers(referenced, 0).size()));
    };
    const aowis::config::Result<bool> erased = this->profile_store->erase(
        object_id.value(), this->stored.at(index).revision.toStdString(), references);
    if (!erased)
    {
        this->setError(this->storageErrorText(erased.error()));
        this->reload();
        emit profilesChanged();
        return false;
    }
    this->discardTunnel(id);
    this->reload();
    this->setError(QString());
    emit profilesChanged();
    return true;
}

QString WireGuardVpnSettings::profileName(const QString &id) const
{
    const int index = this->indexOf(id);
    return index < 0 ? QString() : this->stored.at(index).name;
}

QString WireGuardVpnSettings::profileDescription(const QString &id) const
{
    const int index = this->indexOf(id);
    return index < 0 ? QString() : this->stored.at(index).description;
}

QString WireGuardVpnSettings::profileText(const QString &id) const
{
    const int index = this->indexOf(id);
    return index < 0 ? QString() : this->stored.at(index).text;
}

QString WireGuardVpnSettings::readConfFile(const QUrl &url)
{
    // Qt for Android opens content:// URIs from the system file picker.
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly))
    {
        this->setError(tr("Could not read the selected file."));
        return QString();
    }
    const QByteArray data = file.read(max_conf_bytes + 1);
    if (data.size() > max_conf_bytes)
    {
        this->setError(tr("The selected file is larger than 2 MiB."));
        return QString();
    }
    this->setError(QString());
    return QString::fromUtf8(data);
}

QString WireGuardVpnSettings::suggestedName(const QUrl &url) const
{
    // content:// URIs carry the document name percent-encoded, e.g.
    // ".../document/primary%3ADownload%2Fwg0.conf".
    QString name = QUrl::fromPercentEncoding(url.toString(QUrl::FullyEncoded).toUtf8());
    const qsizetype cut = std::max(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char(':')));
    if (cut >= 0)
        name = name.mid(cut + 1);
    if (name.endsWith(QStringLiteral(".conf"), Qt::CaseInsensitive))
        name.chop(5);
    name = name.trimmed();
    if (name.isEmpty())
        name = tr("WireGuard profile");
    QString candidate = name;
    for (int n = 2; this->nameTaken(candidate, QString()); ++n)
        candidate = QStringLiteral("%1 (%2)").arg(name).arg(n);
    return candidate;
}

bool WireGuardVpnSettings::startTunnel(const QString &id)
{
    // The user's explicit Start also ends an earlier Stop.
    this->user_stopped.remove(id);
    this->user_started.insert(id);
    return this->startEngine(id);
}

bool WireGuardVpnSettings::startEngine(const QString &id)
{
#if AOWIS_MOBILE_WITH_WIREGUARD
    const int index = this->indexOf(id);
    if (index < 0)
    {
        this->setError(tr("Profile not found."));
        return false;
    }
    Tunnel &tunnel = this->tunnels[id];
    if (tunnel.running || tunnel.starting != nullptr)
    {
        tunnel.stop_requested = false;
        return true;
    }
    const aowis::network::WireGuardConfParse parsed = parse(this->stored.at(index).text);
    if (!parsed.ok())
    {
        tunnel.error = tr("The configuration has errors; fix them before saving.");
        this->bumpStatus();
        emit tunnelAvailabilityChanged(id);
        return false;
    }
    tunnel.client = std::make_shared<aowis::network::WireGuardClient>();
    tunnel.error.clear();
    tunnel.stop_requested = false;
    ++tunnel.generation; // Cancels a pending retry
    // start() may resolve the endpoint's host name: keep it off the GUI thread.
    std::shared_ptr<aowis::network::WireGuardClient> client = tunnel.client;
    const aowis::network::WireGuardConfig config = parsed.config;
    std::shared_ptr<bool> ok = std::make_shared<bool>(false);
    std::shared_ptr<std::string> message = std::make_shared<std::string>();
    QThread *thread = QThread::create([client, config, ok, message]() {
        *ok = client->start(config, message.get());
    });
    tunnel.starting = thread;
    connect(thread, &QThread::finished, this, [this, id, thread, ok, message]() {
        this->finishStart(id, thread, *ok, QString::fromStdString(*message));
    });
    thread->start();
    this->setError(QString());
    this->bumpStatus();
    this->updateTimer();
    emit tunnelAvailabilityChanged(id);
    return true;
#else
    Q_UNUSED(id)
    this->setError(tr("WireGuard is not available in this build."));
    return false;
#endif
}

bool WireGuardVpnSettings::wanted(const QString &id) const
{
    if (this->user_stopped.contains(id))
        return false;
    if (this->user_started.contains(id))
        return true;
    for (QHash<QString, QString>::const_iterator it = this->consumers.constBegin(); it != this->consumers.constEnd(); ++it)
    {
        if (it.value() == id)
            return true;
    }
    return false;
}

void WireGuardVpnSettings::finishStart(const QString &id, QThread *thread, bool ok, const QString &message)
{
    QHash<QString, Tunnel>::iterator it = this->tunnels.find(id);
    if (it == this->tunnels.end() || it->starting != thread)
    {
        // Profile removed or changed meanwhile: its client was released.
        thread->deleteLater();
        return;
    }
    it->starting = nullptr;
    thread->deleteLater();
    if (!ok)
    {
        it->client.reset();
        it->running = false;
        it->error = message;
        ++it->failures;
    }
    else if (it->stop_requested)
    {
        it->client->stop();
        it->client.reset();
        it->running = false;
    }
    else
    {
        it->running = true;
        it->failures = 0;
    }
    it->stop_requested = false;
    // A connection still needs the tunnel: retry after 5, 10, 20, 40, then
    // every 60 seconds, unless the user stopped it.
    if (!ok && this->wanted(id))
    {
        const int delay = std::min(60000, 5000 << std::min(it->failures - 1, 4));
        const quint64 generation = it->generation;
        QTimer::singleShot(delay, this, [this, id, generation]() {
            QHash<QString, Tunnel>::iterator again = this->tunnels.find(id);
            if (again == this->tunnels.end() || again->generation != generation || again->running
                || again->starting != nullptr || !this->wanted(id))
                return;
            this->startEngine(id);
        });
    }
    this->bumpStatus();
    this->updateTimer();
    emit tunnelAvailabilityChanged(id);
}

void WireGuardVpnSettings::stopTunnel(const QString &id)
{
    // Stays stopped, also for the connections that use the profile, until
    // the user starts it again (or AOWIS restarts).
    this->user_started.remove(id);
    this->user_stopped.insert(id);
    this->stopEngine(id);
}

void WireGuardVpnSettings::stopEngine(const QString &id)
{
    QHash<QString, Tunnel>::iterator it = this->tunnels.find(id);
    if (it == this->tunnels.end())
        return;
    ++it->generation;
    if (it->starting != nullptr)
    {
        it->stop_requested = true;
    }
    else
    {
        if (it->client)
            it->client->stop();
        it->client.reset();
        it->running = false;
        it->error.clear();
        it->failures = 0;
    }
    this->bumpStatus();
    this->updateTimer();
    emit tunnelAvailabilityChanged(id);
}

void WireGuardVpnSettings::discardTunnel(const QString &id)
{
    this->user_started.remove(id);
    QHash<QString, Tunnel>::iterator it = this->tunnels.find(id);
    if (it != this->tunnels.end())
    {
        if (it->starting != nullptr)
        {
            // The worker keeps its own client reference; the client stops
            // once the worker has finished.
            QThread *thread = it->starting;
            std::shared_ptr<aowis::network::WireGuardClient> client = it->client;
            disconnect(thread, nullptr, this, nullptr);
            this->orphans.append(thread);
            connect(thread, &QThread::finished, this, [this, thread, client]() {
                client->stop();
                this->orphans.removeAll(thread);
                thread->deleteLater();
            });
        }
        else if (it->client)
            it->client->stop();
        this->tunnels.erase(it);
    }
    // A connection using the profile gets the new configuration at once.
    if (this->indexOf(id) >= 0 && this->wanted(id))
        this->startEngine(id);
    this->bumpStatus();
    this->updateTimer();
    emit tunnelAvailabilityChanged(id);
}

void WireGuardVpnSettings::require(const QString &consumer, const QString &profile_id)
{
    const QString previous = this->consumers.value(consumer);
    if (profile_id.isEmpty())
        this->consumers.remove(consumer);
    else
        this->consumers.insert(consumer, profile_id);
    if (!previous.isEmpty() && previous != profile_id && !this->wanted(previous))
        this->stopEngine(previous);
    if (!profile_id.isEmpty() && this->wanted(profile_id) && this->indexOf(profile_id) >= 0)
    {
        QHash<QString, Tunnel>::const_iterator it = this->tunnels.constFind(profile_id);
        if (it == this->tunnels.constEnd() || (!it->running && it->starting == nullptr))
            this->startEngine(profile_id);
    }
}

bool WireGuardVpnSettings::proxyFor(const QString &profile_id, QNetworkProxy *proxy, QString *reason) const
{
#if AOWIS_MOBILE_WITH_WIREGUARD
    const QString name = this->profileName(profile_id);
    if (name.isEmpty())
    {
        *reason = tr("The WireGuard profile of this connection no longer exists. Choose another one in Settings.");
        return false;
    }
    if (this->user_stopped.contains(profile_id))
    {
        *reason = tr("The WireGuard tunnel “%1” is stopped. Start it in Settings → WireGuard VPN.").arg(name);
        return false;
    }
    const QHash<QString, Tunnel>::const_iterator it = this->tunnels.constFind(profile_id);
    if (it != this->tunnels.constEnd() && it->running && it->client)
    {
        // TCP through the tunnel, host names resolved inside it.
        const aowis::network::Socks5Proxy socks = it->client->proxy();
        *proxy = QNetworkProxy(QNetworkProxy::Socks5Proxy, QString::fromStdString(socks.host), socks.port,
                               QString::fromStdString(socks.username), QString::fromStdString(socks.password));
        proxy->setCapabilities(QNetworkProxy::TunnelingCapability | QNetworkProxy::HostNameLookupCapability);
        reason->clear();
        return true;
    }
    if (it != this->tunnels.constEnd() && it->starting == nullptr && !it->error.isEmpty())
        *reason = tr("The WireGuard tunnel “%1” could not start (retrying): %2").arg(name, it->error);
    else
        *reason = tr("Starting the WireGuard tunnel “%1”…").arg(name);
    return false;
#else
    Q_UNUSED(profile_id)
    Q_UNUSED(proxy)
    *reason = tr("WireGuard is not available in this build.");
    return false;
#endif
}

void WireGuardVpnSettings::setProfileUsers(std::function<QStringList(const QString &)> users)
{
    this->profile_users = std::move(users);
}

QStringList WireGuardVpnSettings::profileUsers(const QString &id, int revision) const
{
    Q_UNUSED(revision)
    return this->profile_users ? this->profile_users(id) : QStringList();
}

QVariantMap WireGuardVpnSettings::status(const QString &id, int revision) const
{
    Q_UNUSED(revision)
    QVariantMap result;
    QString state = QStringLiteral("stopped");
    QString state_text = tr("Stopped");
    QString handshake_text;
    QString traffic_text;
    QString peer_text;
    bool running = false;
    const QHash<QString, Tunnel>::const_iterator it = this->tunnels.constFind(id);
    if (it != this->tunnels.constEnd())
    {
        if (it->starting != nullptr)
        {
            state = QStringLiteral("starting");
            state_text = it->stop_requested ? tr("Stopping…") : tr("Starting…");
            running = !it->stop_requested;
        }
        else if (it->running && it->client)
        {
            running = true;
#if AOWIS_MOBILE_WITH_WIREGUARD
            aowis::network::WireGuardTelemetry telemetry;
            if (it->client->telemetry(&telemetry))
            {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const QLocale locale;
                if (telemetry.lastHandshakeUnixMs <= 0)
                {
                    state = QStringLiteral("waiting");
                    state_text = tr("Running · waiting for the first handshake");
                    handshake_text = tr("Never");
                }
                else
                {
                    const qint64 age = now - telemetry.lastHandshakeUnixMs;
                    handshake_text = ageText(age);
                    if (age > stale_handshake_ms)
                    {
                        state = QStringLiteral("stale");
                        state_text = tr("Last handshake %1 — check the endpoint, keys and network.")
                                         .arg(handshake_text);
                    }
                    else
                    {
                        state = QStringLiteral("connected");
                        state_text = tr("Connected");
                    }
                }
                traffic_text = tr("%1 received · %2 sent")
                                   .arg(dataSizeText(telemetry.receivedBytes, locale),
                                        dataSizeText(telemetry.sentBytes, locale));
                peer_text = QString::fromStdString(telemetry.peerEndpoint);
            }
            else
            {
                state = QStringLiteral("waiting");
                state_text = tr("Running");
            }
#endif
        }
        else if (!it->error.isEmpty())
        {
            state = QStringLiteral("error");
            state_text = this->wanted(id) ? tr("Could not start (retrying): %1").arg(it->error)
                                          : tr("Could not start: %1").arg(it->error);
        }
    }
    // Start/Stop: also stoppable while a connection keeps retrying it.
    const bool active = running || (state == QStringLiteral("error") && this->wanted(id));
    result.insert(QStringLiteral("state"), state);
    result.insert(QStringLiteral("running"), running);
    result.insert(QStringLiteral("active"), active);
    result.insert(QStringLiteral("stateText"), state_text);
    result.insert(QStringLiteral("handshakeText"), handshake_text);
    result.insert(QStringLiteral("trafficText"), traffic_text);
    result.insert(QStringLiteral("peerText"), peer_text);
    return result;
}

QString WireGuardVpnSettings::dataSizeText(quint64 bytes, const QLocale &locale)
{
    QLocale plain = locale;
    plain.setNumberOptions(plain.numberOptions() | QLocale::OmitGroupSeparator);
    if (bytes < 1024)
        return tr("%1 B").arg(plain.toString(static_cast<qulonglong>(bytes)));
    constexpr int largest_unit = 6;
    int unit = 0;
    double value = static_cast<double>(bytes);
    while (value >= 1024.0 && unit < largest_unit)
    {
        value /= 1024.0;
        ++unit;
    }
    int decimals = value < 10.0 ? 2 : (value < 100.0 ? 1 : 0);
    const double scale = decimals == 2 ? 100.0 : (decimals == 1 ? 10.0 : 1.0);
    if (unit < largest_unit && std::round(value * scale) / scale >= 1024.0)
    {
        // Rounding carried into the next unit (1023.7 KiB -> "1.00 MiB").
        value /= 1024.0;
        ++unit;
        decimals = 2;
    }
    const QString number = plain.toString(value, 'f', decimals);
    switch (unit)
    {
    case 1: return tr("%1 KiB").arg(number);
    case 2: return tr("%1 MiB").arg(number);
    case 3: return tr("%1 GiB").arg(number);
    case 4: return tr("%1 TiB").arg(number);
    case 5: return tr("%1 PiB").arg(number);
    default: return tr("%1 EiB").arg(number);
    }
}

void WireGuardVpnSettings::setError(const QString &message)
{
    if (this->last_error == message)
        return;
    this->last_error = message;
    emit errorChanged();
}

void WireGuardVpnSettings::bumpStatus()
{
    ++this->revision;
    emit statusRevisionChanged();
}

void WireGuardVpnSettings::updateTimer()
{
    bool active = false;
    for (QHash<QString, Tunnel>::const_iterator it = this->tunnels.constBegin(); it != this->tunnels.constEnd(); ++it)
        active = active || it->running || it->starting != nullptr;
    if (active && !this->poll->isActive())
        this->poll->start();
    else if (!active && this->poll->isActive())
        this->poll->stop();
}
