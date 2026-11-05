#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/bgfx/map_bgfx_three_view_camera.h"
#include "map/bgfx/map_bgfx_globe_icon_index.h"
#include "map/bgfx/map_bgfx_globe_height_cache.h"
#include "map/bgfx/map_bgfx_globe_imagery.h"

#include <cassert>
#include <cmath>
#include <functional>
#include <vector>

// Guard the shared header/API contract for Android and Linux. Mobile's lab
// stress scene stays intact until the production network/terrain scene API is
// separated from the desktop data and Qt Widgets application.
int main()
{
    MapBgfxThreeViewCamera view;
    view.reset(179.99, 0.0, 12.0);
    const MapBgfx2d::Camera camera = view.camera(800.0, 600.0);
    const MapBgfx2d::ScreenPoint opposite = MapBgfx2d::screenFromWorld(
        camera, MapBgfx2d::worldFromWgs84(-179.99, 0.0));
    assert(std::abs(opposite.x - 400.0) < 100.0);

    MapBgfxGlobeIconIndex index;
    const std::vector<MapBgfxGlobeIconIndex::Position> positions = {
        {0.0, 0.0, 6371000.0}, {400000.0, 0.0, 6371000.0}};
    index.rebuild(positions);
    MapBgfxGlobeIconIndex::Frustum frustum;
    frustum.valid = true;
    frustum.sides = {{{1.0, 0.0, 0.0, 1000.0}, {-1.0, 0.0, 0.0, 1000.0},
                      {0.0, 1.0, 0.0, 1000.0}, {0.0, -1.0, 0.0, 1000.0}}};
    std::vector<std::size_t> visible;
    index.query(frustum, &visible);
    assert(visible.size() == 1 && visible.front() == 0);

    MapBgfxGlobeHeightCache cache;
    cache.synchronize(5, 1.0);
    double height_m = 0.0;
    int queries = 0;
    const std::function<bool(double *)> sampler = [&queries](double *height) {
        ++queries;
        *height = 12.5;
        return true;
    };
    assert(cache.height(1.0, 1.0, &height_m, sampler));
    assert(cache.height(1.0, 1.0, &height_m, sampler));
    assert(queries == 1 && std::abs(height_m - 12.5) < 1e-8);

    const MapBgfx2dImagery::TileId tile{8, 139, 84};
    const MapBgfxGlobeImagery::Source fallback = MapBgfxGlobeImagery::ancestor(tile, 2);
    assert(fallback.tile.zoom == 6 && fallback.tile.x == 34);
    return 0;
}
