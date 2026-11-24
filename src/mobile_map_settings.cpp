#include "mobile_map_settings.h"
#include <QDir>
#include <QStandardPaths>
#include <QUuid>

using Aowis::Map::MapCascadeConfig;
using Aowis::Map::MapCascadeUpstream;
using Aowis::Map::MapProvider;
using Aowis::Map::AvailableMapProvider;
using Aowis::Map::MapProviderAcquisitionMode;
using Aowis::Map::SettingsRepository;

std::unique_ptr<SettingsRepository> MobileMapSettings::openRepository(const QString &root, QString *error)
{
    QString path = root;
#if defined(Q_OS_ANDROID)
    if (path.isEmpty())
    {
        const QString location = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (location.isEmpty())
        {
            if (error) *error = QStringLiteral("No private MAP configuration directory available");
            return {};
        }
        path = QDir(location).filePath(QStringLiteral("aowis/mobile-map"));
    }
#endif
    return path.isEmpty() ? SettingsRepository::openForCurrentUser(QStringLiteral("mobile-map"), error)
        : SettingsRepository::openAtOwnerRoot(QStringLiteral("mobile-map"), path, error);
}

MobileMapSettings::MobileMapSettings(const QString &legacy_url, const QString &legacy_profile,
                                     QObject *parent, const QString &root) : QObject(parent)
{
    this->repository = openRepository(root, &this->last_error);
    if (!this->repository || !this->load()) return;
    // The first persisted cascade is the migration marker. Deleting the last
    // upstream must not resurrect the old QSettings endpoint on restart.
    if (this->cascade_revision.isEmpty() && !legacy_url.isEmpty())
    {
        MapCascadeConfig migrated = this->cascade;
        MapCascadeUpstream upstream;
        upstream.name = QStringLiteral("mobile-upstream");
        upstream.url = QUrl(legacy_url);
        upstream.wireguard_profile_id = legacy_profile;
        migrated.upstreams.append(upstream);
        migrated.allow_direct_fetch = false;
        if (!this->saveCascade(migrated)) this->valid = false;
    }
}

bool MobileMapSettings::load()
{
    this->valid = this->repository &&
        this->repository->readCascade(&this->cascade, &this->cascade_revision, &this->last_error) &&
        this->repository->readProviders(&this->local, &this->provider_revision, &this->last_error) &&
        this->repository->readDisabledInheritedProviders(&this->disabled, &this->disabled_revision, &this->last_error) &&
        this->repository->readProviderAcquisitionModes(&this->modes, &this->mode_revision, &this->last_error) &&
        this->repository->readProviderUpstreamRoutes(&this->routes, &this->route_revision, &this->last_error);
    return this->valid;
}

bool MobileMapSettings::fail(const QString &error)
{
    this->last_error = error;
    emit errorChanged();
    return false;
}

bool MobileMapSettings::finish(bool ok, const QString &error)
{
    // Refresh revisions also on conflicts, without overwriting the other writer.
    const bool loaded = this->load();
    if (!ok) this->last_error = error;
    emit changed();
    emit catalogChanged();
    emit errorChanged();
    return ok && loaded;
}

bool MobileMapSettings::saveProviders(const QVector<MapProvider> &value)
{
    if (!this->valid) return false;
    QString error;
    const bool ok = this->repository->saveProviders(value, this->provider_revision, nullptr, &error);
    return this->finish(ok, error);
}

bool MobileMapSettings::saveCascade(const MapCascadeConfig &value)
{
    if (!this->valid) return false;
    QString error;
    const bool ok = this->repository->saveCascade(value, this->cascade_revision, nullptr, &error);
    return this->finish(ok, error);
}

QVariantList MobileMapSettings::providers() const
{
    QVariantList result;
    QVector<AvailableMapProvider> entries;
    for (const MapProvider &provider : this->local)
        entries.append({provider, false, {}});
    for (const AvailableMapProvider &entry : this->available)
        if (entry.inherited)
        {
            bool local_exists = false;
            for (const MapProvider &provider : this->local)
                local_exists = local_exists || provider.id == entry.provider.id;
            if (!local_exists) entries.append(entry);
        }
    for (const AvailableMapProvider &entry : entries)
    {
        const MapProvider &p = entry.provider;
        MapProviderAcquisitionMode mode = this->modes.value(p.id,
            entry.inherited ? MapProviderAcquisitionMode::Cascade : MapProviderAcquisitionMode::Automatic);
        if (mode == MapProviderAcquisitionMode::Direct && (entry.inherited || !this->cascade.allow_direct_fetch))
            mode = MapProviderAcquisitionMode::Cascade;
        result.append(QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("url"), p.url_template},
            {QStringLiteral("enabled"), entry.inherited ? !this->disabled.contains(p.id) : p.enabled},
            {QStringLiteral("inherited"), entry.inherited}, {QStringLiteral("remote"), p.remote_fetch_enabled},
            {QStringLiteral("lossy"), p.allow_lossy_transfer}, {QStringLiteral("minZoom"), p.minimum_zoom}, {QStringLiteral("maxZoom"), p.maximum_zoom},
            {QStringLiteral("mode"), Aowis::Map::providerAcquisitionModeName(mode)}, {QStringLiteral("routes"), this->routes.value(p.id)}});
    }
    return result;
}

QVariantList MobileMapSettings::upstreams() const
{
    QVariantList result;
    for (const MapCascadeUpstream &u : this->cascade.upstreams)
        result.append(QVariantMap{{QStringLiteral("name"), u.name}, {QStringLiteral("url"), u.url.toString()},
                                  {QStringLiteral("profile"), u.wireguard_profile_id}, {QStringLiteral("key"), QString::fromUtf8(u.api_key)}});
    return result;
}

void MobileMapSettings::setAvailable(const QVector<AvailableMapProvider> &entries)
{
    this->available = entries;
    emit catalogChanged();
}

QStringList MobileMapSettings::profileUsers(const QString &id) const
{
    QStringList result;
    if (id.isEmpty()) return result;
    for (const MapCascadeUpstream &u : this->cascade.upstreams)
        if (u.wireguard_profile_id == id) result.append(tr("Map server") + QStringLiteral(": ") + u.name);
    return result;
}

bool MobileMapSettings::knownProvider(const QString &id) const
{
    for (const MapProvider &p : this->local) if (p.id == id) return true;
    for (const AvailableMapProvider &entry : this->available) if (entry.provider.id == id) return true;
    return false;
}

QString MobileMapSettings::addProvider(const QString &name, const QString &url)
{
    MapProvider p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name.trimmed();
    p.url_template = url.trimmed();
    QVector<MapProvider> proposed = this->local;
    proposed.append(p);
    return this->saveProviders(proposed) ? p.id : QString();
}

bool MobileMapSettings::setProviderField(const QString &id, const QString &field, const QVariant &value)
{
    if (!this->valid) return false;
    QVector<MapProvider> proposed = this->local;
    for (MapProvider &p : proposed)
        if (p.id == id)
        {
            if (field == QStringLiteral("name")) p.name = value.toString().trimmed();
            else if (field == QStringLiteral("url")) p.url_template = value.toString().trimmed();
            else if (field == QStringLiteral("enabled")) p.enabled = value.toBool();
            else if (field == QStringLiteral("remote")) p.remote_fetch_enabled = value.toBool();
            else if (field == QStringLiteral("lossy")) p.allow_lossy_transfer = value.toBool();
            else if (field == QStringLiteral("minZoom")) p.minimum_zoom = value.toInt();
            else if (field == QStringLiteral("maxZoom")) p.maximum_zoom = value.toInt();
            else return false;
            return this->saveProviders(proposed);
        }
    if (field != QStringLiteral("enabled") || !this->knownProvider(id)) return false;
    QSet<QString> updated = this->disabled;
    if (value.toBool()) updated.remove(id); else updated.insert(id);
    QString error;
    const bool ok = this->repository->saveDisabledInheritedProviders(updated, this->disabled_revision, nullptr, &error);
    return this->finish(ok, error);
}

bool MobileMapSettings::removeProvider(const QString &id)
{
    QVector<MapProvider> proposed = this->local;
    for (qsizetype i = 0; i < proposed.size(); ++i)
        if (proposed.at(i).id == id)
        {
            proposed.removeAt(i);
            return this->saveProviders(proposed);
        }
    return false;
}

bool MobileMapSettings::setAcquisition(const QString &id, const QString &mode)
{
    MapProviderAcquisitionMode parsed;
    if (!this->valid || !this->knownProvider(id) || !Aowis::Map::parseProviderAcquisitionMode(mode, &parsed)) return false;
    if (parsed == MapProviderAcquisitionMode::Direct)
    {
        bool local_exists = false;
        for (const MapProvider &p : this->local) local_exists = local_exists || p.id == id;
        if (!local_exists || !this->cascade.allow_direct_fetch) return false;
    }
    QHash<QString, MapProviderAcquisitionMode> updated = this->modes;
    updated.insert(id, parsed);
    QString error;
    const bool ok = this->repository->saveProviderAcquisitionModes(updated, this->mode_revision, nullptr, &error);
    return this->finish(ok, error);
}

bool MobileMapSettings::setRoutes(const QString &id, const QStringList &names)
{
    if (!this->valid || !this->knownProvider(id)) return false;
    for (const QString &name : names)
    {
        bool exists = false;
        for (const MapCascadeUpstream &u : this->cascade.upstreams) exists = exists || u.name == name;
        if (!exists && !this->routes.value(id).contains(name)) return this->fail(tr("Unknown upstream name"));
    }
    QHash<QString, QStringList> updated = this->routes;
    if (names.isEmpty()) updated.remove(id); else updated.insert(id, names);
    QString error;
    const bool ok = this->repository->saveProviderUpstreamRoutes(updated, this->route_revision, nullptr, &error);
    return this->finish(ok, error);
}

bool MobileMapSettings::setAllowDirectFetch(bool enabled)
{
    MapCascadeConfig proposed = this->cascade;
    proposed.allow_direct_fetch = enabled;
    return this->saveCascade(proposed);
}

bool MobileMapSettings::setPoorInternet(bool enabled)
{
    MapCascadeConfig proposed = this->cascade;
    proposed.poor_internet_mode = enabled;
    return this->saveCascade(proposed);
}

bool MobileMapSettings::setJpegQuality(int quality)
{
    MapCascadeConfig proposed = this->cascade;
    proposed.jpeg_quality = quality;
    return this->saveCascade(proposed);
}

static QUrl upstreamUrl(const QString &text)
{
    QUrl url(text.trimmed(), QUrl::StrictMode);
    if (url.isValid() && url.port(-1) == -1) url.setPort(8122);
    return url;
}

bool MobileMapSettings::addUpstream(const QString &name, const QString &url, const QString &profile, const QString &key)
{
    MapCascadeConfig proposed = this->cascade;
    MapCascadeUpstream u;
    u.name = name.trimmed();
    u.url = upstreamUrl(url);
    u.wireguard_profile_id = profile;
    u.api_key = key.toUtf8();
    // Do not silently bypass a new first upstream. Users can opt in afterward.
    if (proposed.upstreams.isEmpty()) proposed.allow_direct_fetch = false;
    proposed.upstreams.append(u);
    return this->saveCascade(proposed);
}

bool MobileMapSettings::setUpstreamField(const QString &name, const QString &field, const QString &value)
{
    MapCascadeConfig proposed = this->cascade;
    for (MapCascadeUpstream &u : proposed.upstreams)
        if (u.name == name)
        {
            if (field == QStringLiteral("url")) u.url = upstreamUrl(value);
            else if (field == QStringLiteral("profile")) u.wireguard_profile_id = value;
            else if (field == QStringLiteral("key")) u.api_key = value.toUtf8();
            else return false;
            return this->saveCascade(proposed);
        }
    return false;
}

bool MobileMapSettings::moveUpstream(const QString &name, int step)
{
    MapCascadeConfig proposed = this->cascade;
    if (step != -1 && step != 1) return false;
    for (qsizetype i = 0; i < proposed.upstreams.size(); ++i)
        if (proposed.upstreams.at(i).name == name && i + step >= 0 && i + step < proposed.upstreams.size())
        {
            proposed.upstreams.move(i, i + step);
            return this->saveCascade(proposed);
        }
    return false;
}

bool MobileMapSettings::removeUpstream(const QString &name)
{
    MapCascadeConfig proposed = this->cascade;
    for (qsizetype i = 0; i < proposed.upstreams.size(); ++i)
        if (proposed.upstreams.at(i).name == name)
        {
            proposed.upstreams.removeAt(i);
            // Keep direct fetch policy unchanged; removal is not permission to go online.
            return this->saveCascade(proposed);
        }
    return false;
}
