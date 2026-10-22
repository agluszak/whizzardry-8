#include "wiz8/render/map_view.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/Data.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/sphere.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/PipelineLayout.h>

#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/unicode.h"

/* One static mesh as the map sees it. The explored vertices are the map's
   state; the draws are rebuilt from the mesh's shared asset whenever that
   asset, the render context or the texture exclusions change. */
struct MapView::Surface {
    struct Batch {
        vsg::ref_ptr<render::DrawNode> leaf;
        vsg::ref_ptr<vsg::BindDescriptorSet> images;
        vsg::ref_ptr<vsg::ImageInfo> texture;
        vsg::ref_ptr<vsg::ImageInfo> detail;
        std::uint32_t shader = ~0u;
        std::size_t source = 0;
    };
    struct Pass {
        vsg::ref_ptr<vsg::vec4Array> colors;
        vsg::ref_ptr<vsg::vec4Array> data;
        std::vector<Batch> batches;
    };

    std::vector<bool> explored;
    bool explored_changed = true;

    std::shared_ptr<render::MeshAsset> asset;
    vsg::ref_ptr<vsg::PipelineLayout> layout;
    std::uint64_t revision = ~std::uint64_t{0};
    vsg::ref_ptr<vsg::MatrixTransform> node;
    vsg::ref_ptr<vsg::vec4Array> instance;
    std::vector<Pass> passes;

    void build(render::RenderContext& context, const MeshInstance& owner, Mesh& mesh,
               const std::vector<bool>& kept, const ScenePass& pass, std::uint64_t sort_order);
    void update(render::RenderContext& context, const MeshInstance& owner, Mesh& mesh,
                const ScenePass& pass);
};

namespace {

// Polygons the map keeps: every polygon whose texture is not excluded.
std::vector<bool> keptPolygons(Mesh& mesh, std::span<const std::string> excluded)
{
    std::vector<bool> kept(static_cast<std::size_t>(mesh.polygon_count), true);
    const auto* textures = mesh.getPolygonTextureTable(0, 0, false);
    if (!textures || excluded.empty()) return kept;
    for (std::size_t polygon = 0; polygon < kept.size(); ++polygon) {
        const auto& texture = (*textures)[polygon];
        kept[polygon] = !texture || std::ranges::none_of(excluded, [&](const std::string& name) {
            return CompareTextIgnoreAsciiCase(name, texture->getName()) == 0;
        });
    }
    return kept;
}

// A pass's kept triangles, batch by batch, as one index array and each
// batch's first index and count within it.
struct KeptIndices {
    vsg::ref_ptr<vsg::uintArray> indices;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
};
KeptIndices keptIndices(const render::MeshAsset::Pass& source, const std::vector<bool>& kept)
{
    std::vector<std::uint32_t> indices;
    KeptIndices result;
    for (const auto& batch : source.batches) {
        const auto first = static_cast<std::uint32_t>(indices.size());
        for (std::uint32_t corner = batch.first_index;
             corner < batch.first_index + batch.index_count; corner += 3) {
            if (!kept[source.polygons[corner / 3]]) continue;
            for (std::uint32_t offset = 0; offset < 3; ++offset)
                indices.push_back(source.indices->at(corner + offset));
        }
        result.ranges.emplace_back(first, static_cast<std::uint32_t>(indices.size()) - first);
    }
    result.indices = vsg::uintArray::create(indices.size());
    std::ranges::copy(indices, result.indices->begin());
    return result;
}

// Explored vertices take full light; everything else stays black.
void fillColors(vsg::vec4Array& colors, const render::MeshAsset::Pass& source,
                const std::vector<bool>& explored)
{
    for (std::size_t vertex = 0; vertex < source.source_vertices.size(); ++vertex) {
        const auto original = source.source_vertices[vertex];
        const bool lit = original < explored.size() && explored[original];
        colors.at(vertex) = lit ? vsg::vec4(1, 1, 1, 0) : vsg::vec4(0, 0, 0, 0);
    }
    colors.dirty();
}

} // namespace

// The shared asset's arrays with the map's colours, kept indices and its own
// instance and pass state; pipelines and textures follow the gameplay draws.
void MapView::Surface::build(render::RenderContext& context, const MeshInstance& owner, Mesh& mesh,
                             const std::vector<bool>& kept, const ScenePass& pass,
                             std::uint64_t sort_order)
{
    explored_changed = true;
    node = vsg::MatrixTransform::create();
    instance = render::makeInstanceArray(render::packInstance(owner, mesh, pass));
    const auto instance_bind = context.descriptor(1, instance);
    passes.clear();
    for (const auto& source : asset->passes) {
        auto& state = passes.emplace_back();
        state.colors = vsg::vec4Array::create(source.source_vertices.size());
        state.colors->properties.dataVariance = vsg::DYNAMIC_DATA;
        state.data = render::makeArray(
            render::packPass(source.materials, source.draw_flags, true, pass.frame.time));
        const auto pass_bind = context.descriptor(2, state.data);
        const auto [indices, ranges] = keptIndices(source, kept);
        const vsg::DataList arrays{source.positions,    source.normals,       source.attributes[0],
                                   state.colors,        source.attributes[2], source.attributes[3],
                                   source.attributes[4]};
        const auto cull =
            source.cull ? (source.side ? FaceCull::Front : FaceCull::Back) : FaceCull::None;
        auto body = vsg::Group::create();
        for (std::size_t batch = 0; batch < source.batches.size(); ++batch) {
            if (ranges[batch].second == 0) continue;
            auto draw = vsg::VertexIndexDraw::create();
            draw->assignArrays(arrays);
            draw->assignIndices(indices);
            draw->firstIndex = ranges[batch].first;
            draw->indexCount = ranges[batch].second;
            draw->instanceCount = 1;
            auto leaf = render::DrawNode::create(draw, source.positions, asset->sorted,
                                                 asset->offset_pass_keys ? source.pass : 0);
            leaf->cull = cull;
            leaf->pickable = false;
            leaf->sort_order = sort_order;
            leaf->add(instance_bind);
            leaf->add(pass_bind);
            body->addChild(leaf);
            state.batches.push_back({.leaf = leaf, .source = batch});
        }
        const auto bound = vsg::dsphere(asset->bounds_center.x, asset->bounds_center.y,
                                        asset->bounds_center.z, asset->bounds_radius);
        node->addChild(vsg::CullNode::create(bound, body));
    }
}

void MapView::Surface::update(render::RenderContext& context, const MeshInstance& owner, Mesh& mesh,
                              const ScenePass& pass)
{
    node->matrix = render::toVsg(owner.worldMatrix());
    render::store(*instance, render::packInstance(owner, mesh, pass));
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const auto& source = asset->passes[index];
        auto& state = passes[index];
        if (explored_changed) fillColors(*state.colors, source, explored);
        render::store(*state.data,
                      render::packPass(source.materials, source.draw_flags, true, pass.frame.time));
        const auto shader = source.shader.value;
        for (auto& batch : state.batches) {
            const auto& draw = source.batches[batch.source];
            auto texture = context.texture(draw.textures[0].get(), pass.frame.texture_reduction);
            auto detail = context.texture(draw.textures[1].get(), pass.frame.texture_reduction);
            if (batch.images && batch.shader == shader && batch.texture == texture &&
                batch.detail == detail)
                continue;
            if (batch.shader != shader) {
                const auto cull = batch.leaf->cull;
                batch.leaf->setPipeline(context.pipeline(shader, cull, Winding::Standard),
                                        context.pipeline(shader, cull, Winding::Reversed));
                batch.shader = shader;
            }
            if (batch.images) batch.leaf->remove(batch.images);
            batch.images = context.batchSet(shader, texture, detail);
            batch.leaf->add(batch.images);
            batch.texture = texture;
            batch.detail = detail;
        }
    }
    explored_changed = false;
}

MapView::MapView() : graph(vsg::Group::create())
{
    camera.setProjectionType(Camera3D::PROJECT_ORTHOGRAPHIC);
    camera.transform.setRotation(std::numbers::pi / 2, 0.0, 0.0);
}
MapView::~MapView() = default;

void MapView::excludeTextures(std::vector<std::string> names)
{
    if (names == excluded) return;
    excluded = std::move(names);
    ++exclusion_revision;
}

void MapView::reveal(const MeshInstance& instance, const glm::vec3& center, float range)
{
    if (!(range >= 0) || !instance.getModel() || !instance.renderNode()->static_geometry) return;
    const auto world = instance.transform.matrix<glm::mat4>();
    for (auto* mesh : meshParts(instance.getModel())) {
        if (!mesh->frames.empty() || mesh->vertex_location_count == 0) continue;
        auto& surface = this->surface(*mesh);
        surface.explored.resize(static_cast<std::size_t>(mesh->vertex_location_count), false);
        const auto* vertices = mesh->getVertexLoc();
        for (std::size_t vertex = 0; vertex < surface.explored.size(); ++vertex) {
            if (surface.explored[vertex]) continue;
            const auto point = glm::vec3(world * glm::vec4(vertices[vertex], 1));
            if (glm::distance(point, center) > range) continue;
            surface.explored[vertex] = true;
            surface.explored_changed = true;
        }
    }
}

MapView::Surface& MapView::surface(const Mesh& mesh)
{
    auto& owned = surfaces[&mesh];
    if (!owned) owned = std::make_unique<Surface>();
    return *owned;
}

vsg::ref_ptr<vsg::Group> MapView::prepare(render::RenderContext& context,
                                          std::span<const std::unique_ptr<MeshInstance>> instances,
                                          const ScenePass& pass)
{
    graph->children.clear();
    for (std::size_t index = 0; index < instances.size(); ++index) {
        const auto& instance = *instances[index];
        // Gameplay visibility culling never hides the map's static world.
        for (auto* mesh : meshParts(instance.getModel())) {
            // Level geometry has no animation frames, so its asset owns positions.
            if (!mesh->frames.empty()) continue;
            const auto asset = mesh->renderAsset();
            auto& surface = this->surface(*mesh);
            if (surface.asset != asset || surface.layout != context.layout ||
                surface.revision != exclusion_revision) {
                surface.asset = asset;
                surface.layout = context.layout;
                surface.revision = exclusion_revision;
                surface.build(context, instance, *mesh, keptPolygons(*mesh, excluded), pass, index);
            }
            surface.update(context, instance, *mesh, pass);
            graph->addChild(surface.node);
        }
    }
    return graph;
}
