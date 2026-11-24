#include "embedded_map_check.h"

#include <aowis/map/maptiles.h>
#include <aowis/map/settings_repository.h>
#include <aowis/map/terrain_data.h>

#include <QDebug>
#include <QDir>
#include <QTemporaryDir>
#include <memory>

int checkEmbeddedMap()
{
    QTemporaryDir temporary;
    if (!temporary.isValid())
    {
        qCritical() << "Cannot create temporary MAP test directory";
        return 1;
    }
    const QDir root(temporary.path());
    const QString settings_root = root.filePath(QStringLiteral("settings"));
    const QString owner = QStringLiteral("mobile-map");
    QString error;
    std::unique_ptr<Aowis::Map::SettingsRepository> repository =
        Aowis::Map::SettingsRepository::openAtOwnerRoot(owner, settings_root, &error);
    QVector<Aowis::Map::MapProvider> providers;
    QString provider_revision;
    Aowis::Map::MapCascadeConfig cascade;
    QString cascade_revision;
    if (!repository || !repository->readProviders(&providers, &provider_revision, &error) ||
        providers.isEmpty() || !repository->readCascade(&cascade, &cascade_revision, &error))
    {
        qCritical() << "Cannot load MAP defaults:" << error;
        return 1;
    }
    cascade.upstreams.clear();
    cascade.allow_direct_fetch = false;
    cascade.poor_internet_mode = true;
    cascade.jpeg_quality = 65;
    providers[0].enabled = false;
    if (!repository->saveCascade(cascade, cascade_revision, nullptr, &error) ||
        !repository->saveProviders(providers, provider_revision, nullptr, &error))
    {
        qCritical() << "Cannot save MAP settings:" << error;
        return 1;
    }
    repository.reset();
    repository = Aowis::Map::SettingsRepository::openAtOwnerRoot(owner, settings_root, &error);
    Aowis::Map::MapCascadeConfig loaded;
    QVector<Aowis::Map::MapProvider> loaded_providers;
    if (!repository || !repository->readCascade(&loaded, nullptr, &error) ||
        !repository->readProviders(&loaded_providers, nullptr, &error) ||
        loaded.allow_direct_fetch || !loaded.upstreams.isEmpty() || !loaded.poor_internet_mode ||
        loaded.jpeg_quality != 65 || loaded_providers.size() != providers.size() ||
        loaded_providers[0].enabled)
    {
        qCritical() << "MAP settings did not survive reopening:" << error;
        return 1;
    }
    MapTiles tiles(root.filePath(QStringLiteral("raster")), 2, 32);
    tiles.setRemoteFetchEnabled(false);
    tiles.setCascadeConfig(loaded);
    tiles.configureProviders(loaded_providers, {}, {});
    Aowis::Map::TerrainData::Config terrain_config;
    terrain_config.cache_base_directory = root.filePath(QStringLiteral("terrain"));
    terrain_config.remote_fetch_enabled = false;
    terrain_config.cascade = loaded;
    Aowis::Map::TerrainData terrain(terrain_config);
    terrain.setCascadeInstanceId(tiles.cascadeInstanceId());
    if (!terrain.initialize(&error))
    {
        qCritical() << "Cannot initialize offline terrain storage:" << error;
        return 1;
    }
    qInfo() << "Embedded MAP settings round trip and offline raster/terrain initialization passed.";
    return 0;
}
