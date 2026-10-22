#include "wiz8/render/ground_shadow.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/Data.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/PipelineLayout.h>

#include "effect_geometry.h"
#include "wiz8/asset_paths.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/world.h"

struct GroundShadowAsset {
    std::shared_ptr<Texture> texture;
    std::shared_ptr<Material> material = std::make_shared<Material>();
    DrawShader shader;

    GroundShadowAsset()
    {
        texture = LoadTextureFromPath(wiz8::asset_path("data/monsters/bitmaps/Shadow.tga"), nullptr,
                                      true);
        texture->setMipmap(Texture::MIPMAP_NONE);
        texture->setWrapS(Texture::WRAP_CLAMP);
        texture->setWrapT(Texture::WRAP_CLAMP);
        // Preserve the default shader's low bits when applying the blob-shadow rules.
        shader.value = (shader.value & 0xfeff9277u) | 0x00808260u;
    }
};

// The first-pass colours a model's lighting bakes into, if it has any yet.
static vsg::ref_ptr<vsg::vec4Array> bakedColors(const render::MeshAsset& asset)
{
    for (const auto& pass : asset.passes)
        if (pass.pass == 0 && (pass.draw_flags & render::HasColors)) return pass.attributes[1];
    return {};
}

struct ShadowRenderData {
    struct Part {
        Mesh* mesh;
        std::shared_ptr<render::MeshAsset> source_asset;
        std::uint32_t control;
        vsg::ref_ptr<vsg::vec4Array> colors;
        // The asset vertex that carries each source vertex's baked colour.
        std::vector<std::uint32_t> color_vertices;
        render::EffectGeometry geometry;
        vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
        std::array<vsg::ref_ptr<render::DrawNode>, 2> draws;
        std::array<vsg::ref_ptr<vsg::vec4Array>, 2> instances;
        vsg::ref_ptr<vsg::BindDescriptorSet> batch;

        static std::uint32_t flags(const Mesh& source, bool colors)
        {
            std::uint32_t flags = render::HasTexcoord0;
            if (colors) flags |= render::HasColors;
            if (!source.dcg[0].empty()) flags |= render::HasDiffuseMultipliers;
            if (!source.scg[0].empty()) flags |= render::HasSpecularMultipliers;
            return flags;
        }
        Part(render::RenderContext& context, Mesh& mesh, const GroundShadowAsset& asset,
             std::uint32_t capacity, vsg::ref_ptr<vsg::BindDescriptorSet> batch)
            : mesh(&mesh), source_asset(mesh.renderAsset()), control(mesh.render_control),
              colors(bakedColors(*source_asset)),
              geometry(context, capacity, asset.material, flags(mesh, colors != nullptr),
                       mesh.vertex_location_count != 0, 0),
              batch(batch)
        {
            if (colors) {
                color_vertices.resize(static_cast<std::size_t>(mesh.vertex_location_count));
                const auto& pass =
                    *std::ranges::find_if(source_asset->passes, [&](const auto& pass) {
                        return pass.attributes[1] == colors;
                    });
                for (std::uint32_t vertex = 0; vertex < pass.source_vertices.size(); ++vertex)
                    color_vertices[pass.source_vertices[vertex]] = vertex;
            }
            geometry.normals->properties.dataVariance = vsg::DYNAMIC_DATA;
            auto indices = vsg::uintArray::create(capacity);
            for (std::uint32_t vertex = 0; vertex < capacity; ++vertex)
                indices->at(vertex) = vertex;
            for (unsigned side = 0; side < 2; ++side) {
                if (!(control & (1u << side))) continue;
                auto cull = FaceCull::Back;
                if (side)
                    cull = FaceCull::Front;
                else if (control & (1u << Mesh::CONTROL_NO_FRONT_CULL))
                    cull = FaceCull::None;
                instances[side] = side ? render::makeInstanceArray({}) : geometry.instance;
                auto draw = geometry.draw(indices, false);
                draw->cull = cull;
                draw->setPipeline(context.pipeline(asset.shader.value, cull, Winding::Standard, 2),
                                  context.pipeline(asset.shader.value, cull, Winding::Reversed, 2));
                draw->add(side ? context.descriptor(1, instances[side]) : geometry.instance_bind);
                draw->add(geometry.pass_bind);
                draw->add(batch);
                root->addChild(draw);
                draws[side] = draw;
            }
        }
    };

    vsg::ref_ptr<vsg::PipelineLayout> layout;
    std::vector<std::unique_ptr<Part>> parts;
    vsg::ref_ptr<vsg::ImageInfo> image;
    vsg::ref_ptr<vsg::BindDescriptorSet> batch;
    std::uint64_t frame_number = ~std::uint64_t{0};

    explicit ShadowRenderData(render::RenderContext& context) : layout(context.layout) {}
};

static std::shared_ptr<GroundShadowAsset> shadowAsset()
{
    // The last shadow releases its image and device references with the world.
    static std::weak_ptr<GroundShadowAsset> shared;
    if (auto asset = shared.lock()) return asset;
    auto asset = std::make_shared<GroundShadowAsset>();
    shared = asset;
    return asset;
}

GroundShadow::GroundShadow() : asset(shadowAsset()) {}

GroundShadow::GroundShadow(const GroundShadow& other) : asset(other.asset)
{
    transform = other.transform;
    visible = other.visible;
    setName(other.isNamed() ? other.getName() : std::string{});
    angle = other.angle;
    depth = other.depth;
    width = other.width;
}

vsg::ref_ptr<vsg::Node> GroundShadow::renderNode() const
{
    return root;
}

void GroundShadow::updateRenderNode(render::RenderContext& context, const SceneFrame& frame,
                                    std::uint64_t frame_number)
{
    // A new context rebuilds the resources under the same scene member.
    if (!render_data || render_data->layout != context.layout) {
        render_data = std::make_unique<ShadowRenderData>(context);
        root->children.clear();
    }
    auto& data = *render_data;
    if (data.frame_number == frame_number) return;
    data.frame_number = frame_number;
    const auto hide = [&] {
        for (auto& part : data.parts)
            for (auto& draw : part->draws)
                if (draw) draw->draw->indexCount = 0;
    };
    auto* spatial = world ? world->compiledIndex() : nullptr;
    if (!visible || !g_settings.monster_shadows || !spatial || width <= 0 || depth <= 0) {
        hide();
        return;
    }
    auto position = glm::vec3(transform.getLocation());
    position.y += kWorldScale;
    const float radius = std::max(width, depth);
    const auto polygons = spatial->polygonsNearPoint(position, radius, radius * 2 + kWorldScale);
    const GroundShadowProjection projection(position, width, depth, angle);
    render::InstanceBlock instance;
    if (scene) instance.ambient = glm::vec4(scene->ambient_light, 0);
    auto image = context.texture(asset->texture.get(), frame.texture_reduction);
    if (!data.batch || data.image != image) {
        data.image = image;
        data.batch = context.batchSet(asset->shader.value, image);
    }
    std::size_t part_index = 0;
    bool graph_changed = false;
    for (std::size_t first = 0; first < polygons.size(); ++part_index) {
        const auto mesh_index = polygons[first] >> 16;
        std::size_t end = first + 1;
        while (end < polygons.size() && (polygons[end] >> 16) == mesh_index) ++end;
        auto& model = *world->psrMeshes[mesh_index]->getModel();
        model.applyPendingLighting();
        const auto control = model.render_control;
        const auto count = static_cast<std::uint32_t>((end - first) * 3);
        if (part_index == data.parts.size()) {
            data.parts.push_back(std::make_unique<ShadowRenderData::Part>(context, model, *asset,
                                                                          count, data.batch));
            graph_changed = true;
        } else {
            auto& part = data.parts[part_index];
            if (part->mesh != &model || part->source_asset != model.renderAsset() ||
                part->colors != bakedColors(*part->source_asset) || part->control != control ||
                count > part->geometry.positions->size()) {
                const auto previous_capacity =
                    static_cast<std::uint32_t>(part->geometry.positions->size());
                const auto capacity = count > previous_capacity
                                          ? std::max(count, previous_capacity * 2)
                                          : previous_capacity;
                part = std::make_unique<ShadowRenderData::Part>(context, model, *asset, capacity,
                                                                data.batch);
                graph_changed = true;
            }
        }
        auto& part = *data.parts[part_index];
        auto& geometry = part.geometry;
        const auto* triangles = model.getPolyVertex();
        const auto* positions = model.getVertexLoc();
        const auto* normals = model.getVertexNormal();
        const auto* dcg = model.dcg[0].empty() ? nullptr : model.dcg[0].data();
        const auto* scg = model.scg[0].empty() ? nullptr : model.scg[0].data();
        glm::vec3 center;
        float shading_radius;
        if (control & (1u << Mesh::CONTROL_SKIP_AUTO_SPHERE)) {
            // An authored sphere is not a usable bound; fall back to the box, and
            // to the vertices themselves when the box is authored too.
            const auto bounds = (control & (1u << Mesh::CONTROL_SKIP_AUTO_BOX))
                                    ? boundingBox(std::span(positions, model.vertex_location_count))
                                    : model.getBounds().box;
            center = bounds.center();
            shading_radius = glm::length(bounds.minimum - center) * 1.001f;
        } else {
            const auto sphere = model.getBounds().sphere;
            center = sphere.center;
            shading_radius = sphere.radius;
        }
        // Native shading rejects lights and fog against the source terrain's
        // complete bound, even when the shadow selects only a few of its faces.
        instance.bounds = glm::vec4(center, shading_radius);
        bool positions_changed = false, normals_changed = false;
        std::array<bool, 4> attributes_changed{};
        const auto update = [](auto& destination, const auto& value, bool& changed) {
            if (destination != value) {
                destination = value;
                changed = true;
            }
        };
        std::uint32_t written = 0;
        for (; first < end; ++first) {
            const auto triangle = triangles[polygons[first] & 0xffff];
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto vertex = triangle[corner];
                const auto p = positions[vertex];
                const auto uv = projection.coordinates(p);
                update(geometry.positions->at(written), vsg::vec3(p.x, p.y, p.z),
                       positions_changed);
                if (normals) {
                    const auto normal = normals[vertex];
                    update(geometry.normals->at(written), vsg::vec3(normal.x, normal.y, normal.z),
                           normals_changed);
                }
                update(geometry.attributes[0]->at(written), vsg::vec4(uv.x, uv.y, 0, 0),
                       attributes_changed[0]);
                if (part.colors)
                    update(geometry.attributes[1]->at(written),
                           part.colors->at(part.color_vertices[vertex]), attributes_changed[1]);
                if (dcg) {
                    const auto color = dcg[vertex];
                    update(geometry.attributes[2]->at(written),
                           vsg::vec4(color.x, color.y, color.z, color.w), attributes_changed[2]);
                }
                if (scg) {
                    const auto color = scg[vertex];
                    update(geometry.attributes[3]->at(written),
                           vsg::vec4(color.x, color.y, color.z, color.w), attributes_changed[3]);
                }
                ++written;
            }
        }
        if (positions_changed) geometry.positions->dirty();
        if (normals_changed) geometry.normals->dirty();
        for (std::size_t attribute = 0; attribute < attributes_changed.size(); ++attribute)
            if (attributes_changed[attribute]) geometry.attributes[attribute]->dirty();
        for (unsigned side = 0; side < part.draws.size(); ++side) {
            auto& draw = part.draws[side];
            if (!draw) continue;
            draw->draw->indexCount = count;
            auto block = instance;
            block.masks.y = side;
            render::store(*part.instances[side], block);
            if (part.batch != data.batch) draw->stateCommands[3] = data.batch;
        }
        part.batch = data.batch;
    }
    if (data.parts.size() != part_index) {
        data.parts.resize(part_index);
        graph_changed = true;
    }
    if (graph_changed) {
        root->children.clear();
        for (auto& part : data.parts) root->addChild(part->root);
    }
}

void GroundShadow::setWorld(const World* value)
{
    auto* target = value ? value->scene.get() : nullptr;
    if (scene != target) {
        if (scene) scene->detach(*this);
        if (target) target->attach(*this);
    }
    world = value;
}

GroundShadow::~GroundShadow()
{
    setWorld(nullptr);
}
