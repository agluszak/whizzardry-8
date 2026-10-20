#include "wiz8/render/ground_shadow.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>

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
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/world.h"

struct ShadowRenderData {
    render::RenderContext* context;
    vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
    std::uint64_t frame_number = ~std::uint64_t{0};

    explicit ShadowRenderData(render::RenderContext& context) : context(&context) {}
};

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

static std::shared_ptr<GroundShadowAsset> shadowAsset()
{
    // The last shadow releases its image and device references with the world.
    static std::weak_ptr<GroundShadowAsset> shared;
    if (auto asset = shared.lock()) return asset;
    auto asset = std::make_shared<GroundShadowAsset>();
    shared = asset;
    return asset;
}

GroundShadow::GroundShadow(WorldScene* scene) : asset(shadowAsset())
{
    setScene(scene);
}

GroundShadow::GroundShadow(const GroundShadow& other) : asset(other.asset)
{
    transform = other.transform;
    visible = other.visible;
    setName(other.isNamed() ? other.getName() : std::string{});
    angle = other.angle;
    depth = other.depth;
    width = other.width;
}

vsg::ref_ptr<vsg::Node> GroundShadow::renderNode(render::RenderContext& context)
{
    if (!render_data || render_data->context != &context) {
        render_data = std::make_unique<ShadowRenderData>(context);
    }
    return render_data->root;
}

vsg::ref_ptr<vsg::Node> GroundShadow::renderNode() const
{
    if (!render_data) return {};
    return render_data->root;
}

void GroundShadow::updateRenderNode(render::RenderContext& context, const SceneFrame& frame,
                                    std::uint64_t frame_number)
{
    renderNode(context);
    auto& data = *render_data;
    if (data.frame_number == frame_number) return;
    data.frame_number = frame_number;
    data.root->children.clear();
    auto* world = GetWorld();
    auto* spatial = GetWorldSpatialIndex();
    if (!visible || !g_settings.monster_shadows || !world || !spatial || width <= 0 || depth <= 0)
        return;
    auto position = glm::vec3(transform.getLocation());
    position.y += kWorldScale;
    const float radius = std::max(width, depth);
    const auto polygons = spatial->polygonsNearPoint(position, radius, radius * 2 + kWorldScale);
    if (polygons.empty()) return;

    const GroundShadowProjection projection(position, width, depth, angle);
    render::InstanceBlock instance;
    if (scene) instance.ambient = glm::vec4(scene->ambient_light, 0);
    instance.bounds = glm::vec4(position, radius);
    for (std::size_t first = 0; first < polygons.size();) {
        const auto mesh_index = polygons[first] >> 16;
        std::size_t end = first + 1;
        while (end < polygons.size() && (polygons[end] >> 16) == mesh_index) ++end;
        auto& model = *world->psrMeshes[mesh_index]->getModel();
        const auto mesh = model.getTriMesh();
        const auto count = static_cast<std::uint32_t>((end - first) * 3);
        std::uint32_t flags = render::HasTexcoord0;
        if (mesh.dig[0]) flags |= render::HasColors;
        if (mesh.dcg[0]) flags |= render::HasDiffuseMultipliers;
        if (mesh.scg[0]) flags |= render::HasSpecularMultipliers;
        render::EffectGeometry geometry(context, count, asset->material, flags,
                                        mesh.normals != nullptr, frame.time);
        auto indices = vsg::uintArray::create(count);
        std::uint32_t written = 0;
        for (; first < end; ++first) {
            const auto triangle = mesh.poly_vertices[polygons[first] & 0xffff];
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto vertex = triangle[corner];
                const auto p = mesh.positions[vertex];
                const auto uv = projection.coordinates(p);
                geometry.positions->at(written) = vsg::vec3(p.x, p.y, p.z);
                if (mesh.normals) {
                    const auto normal = mesh.normals[vertex];
                    geometry.normals->at(written) = vsg::vec3(normal.x, normal.y, normal.z);
                }
                geometry.attributes[0]->at(written) = vsg::vec4(uv.x, uv.y, 0, 0);
                if (mesh.dig[0]) {
                    const auto color = mesh.dig[0][vertex];
                    geometry.attributes[1]->at(written) = vsg::vec4(color.x, color.y, color.z, 0);
                }
                if (mesh.dcg[0]) {
                    const auto color = mesh.dcg[0][vertex];
                    geometry.attributes[2]->at(written) =
                        vsg::vec4(color.x, color.y, color.z, color.w);
                }
                if (mesh.scg[0]) {
                    const auto color = mesh.scg[0][vertex];
                    geometry.attributes[3]->at(written) =
                        vsg::vec4(color.x, color.y, color.z, color.w);
                }
                indices->at(written) = written;
                ++written;
            }
        }
        render::store(*geometry.instance, instance);
        for (unsigned side = 0; side < 2; ++side) {
            if (!(mesh.control_flags & (1u << side))) continue;
            const auto cull = side ? FaceCull::Front
                              : mesh.control_flags & (1u << Mesh::CONTROL_NO_FRONT_CULL)
                                  ? FaceCull::None
                                  : FaceCull::Back;
            auto draw = geometry.draw(indices, false);
            draw->cull = cull;
            auto block = instance;
            block.masks.y = side;
            draw->stateCommands = {
                context.pipeline(asset->shader.value, cull, Winding::Standard, 2),
                context.descriptor(1, render::makeInstanceArray(block)), geometry.pass_bind,
                context.batchSet(asset->shader.value,
                                 context.texture(asset->texture.get(), frame.texture_reduction))};
            data.root->addChild(draw);
        }
    }
}

GroundShadow::~GroundShadow()
{
    setScene(nullptr);
}
