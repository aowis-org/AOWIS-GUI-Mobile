#pragma once

#include <aowis/map/maptiles.h>
#include <aowis/map/provider_catalog.h>
#include <aowis/map/terrain_data.h>
#include <QObject>
#include <memory>

// One MAP instance for both renderer repositories. Tile and terrain traffic
// and provider discovery use direct C++ calls.
class MobileEmbeddedMapBackend final : public QObject
{
    Q_OBJECT
public:
    explicit MobileEmbeddedMapBackend(QObject *parent = nullptr,
                                      const QString &data_root = {},
                                      const QString &settings_root = {});
    void configure(const Aowis::Map::MapCascadeUpstream &upstream);
    QVector<Aowis::Map::AvailableMapProvider> availableProviders() const;
    MapTiles *tiles() const { return this->map_tiles.get(); }
    Aowis::Map::TerrainData *terrain() const { return this->terrain_data.get(); }
    bool terrainReady() const { return this->terrain_ready; }
    QString terrainError() const { return this->terrain_error; }
    QString settingsError() const { return this->settings_error; }

signals:
    void providersChanged();

private:
    void applyProviders();
    QString root_directory;
    QString settings_root;
    std::unique_ptr<MapTiles> map_tiles;
    std::unique_ptr<Aowis::Map::TerrainData> terrain_data;
    std::unique_ptr<Aowis::Map::ProviderCatalog> provider_catalog;
    QHash<QString, QStringList> upstream_routes;
    QHash<QString, Aowis::Map::MapProviderAcquisitionMode> acquisition_modes;
    bool configuring = false;
    bool terrain_ready = false;
    QString terrain_error;
    QString settings_error;
};
