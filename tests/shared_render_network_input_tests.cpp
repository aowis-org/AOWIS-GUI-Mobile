#include "map/bgfx/map_bgfx_2d_network_input.h"
#include "map/bgfx/map_bgfx_2d_scene_variants.h"
#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/bgfx/map_bgfx_2d_style_binding.h"

#include <QVector>

#include <cstdio>
#include <type_traits>

// Verify the public, non-GUI input ABI from Android and Linux consumers.
// No GPU context, hydraulic model or Qt Widgets required.
class TestBinding final : public MapBgfx2dStyleBinding
{
public:
    void bind(Mode, Table, bool) const override {}
};

int main()
{
    static_assert(std::is_abstract_v<MapBgfx2dStyleBinding>);
    static_assert(!std::is_abstract_v<TestBinding>);
    MapBgfx2dNetworkInput input;
    input.valid = true;
    input.geometry_revision = 42;
    input.nodes.append({11, 3, {179.99, 34.5}, false});
    MapBgfx2dInputLink pipe;
    pipe.render_id = 12;
    pipe.entity_type = 4;
    pipe.start_node_render_id = 11;
    pipe.vertices.append({179.99, 34.5});
    pipe.vertices.append({-179.99, 34.5});
    pipe.pipe = true;
    input.links.append(pipe);
    input.attachments.append({11, {179.99, 34.5}, {179.99, 34.6}});
    if (!input.valid || input.geometry_revision != 42
        || input.nodes.first().entity_type != 3 || input.links.first().vertices.size() != 2
        || !input.links.first().pipe || input.attachments.first().demand_point_render_id != 11)
    {
        std::fprintf(stderr, "shared 2D network input contract failure\n");
        return 1;
    }
    MapBgfx2dSceneVariants variants;
    if (!variants.prepare(input) || variants.active().linkPolylines().size() != 1
        || variants.active().networkNodes().size() != 1
        || !variants.active().isPrepared())
        return 2;
    MapBgfx2dMesh mesh;
    if (!mesh.prepare(variants.active(), {}) || mesh.linkIndexCount() != 6
        || mesh.nodeVertexCount() != 6 || mesh.attachmentVertexCount() != 4)
        return 3;
    if (variants.prepare(input) || mesh.prepare(variants.active(), {}))
        return 4;
    return 0;
}
