#include "mobile_embedded_map_backend.h"
#include "mobile_map_settings.h"

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

void MobileEmbeddedMapBackend::configure(const Aowis::Map::MapCascadeConfig &runtime_cascade, bool settings_ready)
{
    this->configuring = true;
    this->settings_error.clear();
    std::unique_ptr<Aowis::Map::SettingsRepository> repository =
        MobileMapSettings::openRepository(this->settings_root, &this->settings_error);
    QVector<Aowis::Map::MapProvider> providers;
    Aowis::Map::MapCascadeConfig cascade = runtime_cascade;
    QSet<QString> disabled;
    this->upstream_routes.clear();
    this->acquisition_modes.clear();
    const bool valid = settings_ready && repository && repository->readProviders(&providers, nullptr, &this->settings_error) &&
        repository->readDisabledInheritedProviders(&disabled, nullptr, &this->settings_error) &&
        repository->readProviderUpstreamRoutes(&this->upstream_routes, nullptr, &this->settings_error) &&
        repository->readProviderAcquisitionModes(&this->acquisition_modes, nullptr, &this->settings_error);
    if (!valid)
    {
        qWarning().noquote() << "Invalid MAP settings:" << this->settings_error;
        cascade.upstreams.clear();
        cascade.allow_direct_fetch = false;
        if (this->settings_error.isEmpty()) this->settings_error = QStringLiteral("MAP settings unavailable");
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
