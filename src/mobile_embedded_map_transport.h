#ifndef MOBILE_EMBEDDED_MAP_TRANSPORT_H
#define MOBILE_EMBEDDED_MAP_TRANSPORT_H

#include "services/interface_server_map.h"

#include <aowis/map/maptiles.h>
#include <aowis/map/terrain_data.h>
#include <QThreadPool>

class MobileEmbeddedMapBackend;

class MobileEmbeddedMapTransport : public InterfaceServerMap
{
    Q_OBJECT

public:
    explicit MobileEmbeddedMapTransport(MobileEmbeddedMapBackend &backend, QObject *parent = nullptr);
    ~MobileEmbeddedMapTransport() override;
    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;
    void requestTerrainTile(const QString &endpoint, const QString &key) override;
    void requestTerrainDatasets() override;
    void requestTerrainElevation(const QString &endpoint) override;
    void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) override;
    void deleteTerrainCache(quint64 request_id, MapTerrainCacheDeletionScope scope,
                            const QString &dataset, int zoom,
                            int tile_x_min, int tile_x_max,
                            int tile_y_min, int tile_y_max) override;
    void requestMapTileUpstreamActivity() override;
    void requestTerrainUpstreamActivity() override;
    void cancelMapTileUpstreamDownloads() override;
    void cancelTerrainUpstreamDownloads() override;
private:
    MapTiles *map_tiles = nullptr;
    Aowis::Map::TerrainData *terrain_data = nullptr;
    QThreadPool terrain_request_pool;
    bool terrain_data_initialized = false;
    bool terrain_elevation_pending = false;
    QString terrain_initialization_error;
};
#endif // MOBILE_EMBEDDED_MAP_TRANSPORT_H
