#include "mobile_embedded_map_backend.h"

#include <aowis/map/settings_repository.h>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

MobileEmbeddedMapBackend::MobileEmbeddedMapBackend(QObject *parent,
                                                   const QString &data_root,
                                                   const QString &settings_root)
    : QObject(parent), settings_root(settings_root)
{
    // App-private persistent data, not the OS's evictable CacheLocation.
    const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    this->root_directory = data_root.isEmpty()
        ? QDir(app_data).filePath(QStringLiteral("map")) : data_root;
    this->map_tiles = std::make_unique<MapTiles>(QDir(this->root_directory).filePath(QStringLiteral("raster")), 4, 64);
    this->map_tiles->setRemoteFetchEnabled(false);
    Aowis::Map::TerrainData::Config terrain_config;
    terrain_config.cache_base_directory = QDir(this->root_directory).filePath(QStringLiteral("terrain"));
    terrain_config.cascade.allow_direct_fetch = false;
    this->terrain_data = std::make_unique<Aowis::Map::TerrainData>(terrain_config);
    this->terrain_data->setCascadeInstanceId(this->map_tiles->cascadeInstanceId());
    this->terrain_ready = this->terrain_data->initialize(&this->terrain_error);
    this->provider_catalog = std::make_unique<Aowis::Map::ProviderCatalog>(this->root_directory);
    this->provider_catalog->setInstanceId(this->map_tiles->cascadeInstanceId());
    connect(this->provider_catalog.get(), &Aowis::Map::ProviderCatalog::catalogChanged,
            this, &MobileEmbeddedMapBackend::applyProviders);
}

void MobileEmbeddedMapBackend::configure(const Aowis::Map::MapCascadeUpstream &upstream)
{
    this->configuring = true;
    this->settings_error.clear();
    QString settings_path = this->settings_root;
#if defined(Q_OS_ANDROID)
    // Android's HOME/XDG paths are not suitable for AOWIS-CONFIG.
    // Keep the MAP-owned records in the same private namespace as Mobile's
    // other CONFIG owners, without interpreting or writing their schema here.
    if (settings_path.isEmpty())
    {
        const QString location = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (!location.isEmpty())
            settings_path = QDir(location).filePath(QStringLiteral("aowis/mobile-map"));
    }
#endif
    std::unique_ptr<Aowis::Map::SettingsRepository> repository;
#if defined(Q_OS_ANDROID)
    if (settings_path.isEmpty())
        this->settings_error = QStringLiteral("No private MAP configuration directory available");
    else
        repository = Aowis::Map::SettingsRepository::openAtOwnerRoot(
            QStringLiteral("mobile-map"), settings_path, &this->settings_error);
#else
    repository = settings_path.isEmpty()
        ? Aowis::Map::SettingsRepository::openForCurrentUser(QStringLiteral("mobile-map"), &this->settings_error)
        : Aowis::Map::SettingsRepository::openAtOwnerRoot(QStringLiteral("mobile-map"),
                                                         settings_path, &this->settings_error);
#endif
    QVector<Aowis::Map::MapProvider> providers;
    Aowis::Map::MapCascadeConfig cascade;
    QSet<QString> disabled;
    this->upstream_routes.clear();
    this->acquisition_modes.clear();
    const bool valid = repository && repository->readProviders(&providers, nullptr, &this->settings_error) &&
        repository->readCascade(&cascade, nullptr, &this->settings_error) &&
        repository->readDisabledInheritedProviders(&disabled, nullptr, &this->settings_error) &&
        repository->readProviderUpstreamRoutes(&this->upstream_routes, nullptr, &this->settings_error) &&
        repository->readProviderAcquisitionModes(&this->acquisition_modes, nullptr, &this->settings_error);
    // Until the multiple-upstream settings UI is implemented, the existing
    // single endpoint remains authoritative. Never silently bypass it via
    // direct origin downloads, even with an unavailable WireGuard proxy.
    cascade.upstreams.clear();
    if (valid && !upstream.url.isEmpty())
    {
        cascade.upstreams.append(upstream);
        cascade.allow_direct_fetch = false;
    }
    if (!valid)
    {
        qWarning().noquote() << "Invalid MAP settings:" << this->settings_error;
        cascade.allow_direct_fetch = false;
        providers.clear();
        disabled.clear();
        this->upstream_routes.clear();
        this->acquisition_modes.clear();
    }
    this->map_tiles->setRemoteFetchEnabled(valid);
    this->map_tiles->setCascadeConfig(cascade);
    this->terrain_data->setCascadeConfig(cascade);
    this->provider_catalog->setDisabledInheritedProviders(disabled);
    this->provider_catalog->setUpstreamRoutes(this->upstream_routes);
    this->provider_catalog->setAcquisitionModes(this->acquisition_modes);
    this->provider_catalog->configure(providers, cascade);
    this->configuring = false;
    this->applyProviders();
    this->provider_catalog->refresh();
}

void MobileEmbeddedMapBackend::applyProviders()
{
    if (this->configuring) return;
    QVector<Aowis::Map::MapProvider> available;
    for (const Aowis::Map::AvailableMapProvider &entry : this->provider_catalog->available())
        available.append(entry.provider);
    this->map_tiles->configureProviders(available, {}, {}, this->provider_catalog->inheritedUpstreams(),
                                        this->upstream_routes, this->acquisition_modes);
    emit providersChanged();
}

QVector<Aowis::Map::AvailableMapProvider> MobileEmbeddedMapBackend::availableProviders() const
{
    return this->provider_catalog->available();
}
