#include "wiz8/render/ground_shadow.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "wiz8/asset_paths.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_submission.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/world.h"

/* The SR.DLL registry string is the original runtime class identity. */

static std::shared_ptr<Texture> g_ground_shadow_texture;
static std::shared_ptr<Material> g_ground_shadow_material;
/* A default-constructed DrawShader: retail's dynamic initializer (0x004D61A0)
   stores the DrawShader default 0x0100241b before the constructor masks in
   the shadow bits, so the 0x13 bits survive into the shadow shader. */
static DrawShader g_ground_shadow_shader;

GroundShadow::GroundShadow(WorldScene* scene)
{
    angle = 0;
    depth = 500.0f;
    width = 500.0f;
    setScene(scene);

    if (g_ground_shadow_texture == 0) {
        g_ground_shadow_texture = LoadTextureFromPath(
            wiz8::asset_path("data/monsters/bitmaps/Shadow.tga"), nullptr, true);
        g_ground_shadow_texture->setMipmap(Texture::MIPMAP_NONE);
        g_ground_shadow_texture->setWrapS(Texture::WRAP_CLAMP);
        g_ground_shadow_texture->setWrapT(Texture::WRAP_CLAMP);

        g_ground_shadow_material = std::make_shared<Material>();
        g_ground_shadow_shader.value = (g_ground_shadow_shader.value & 0xfeff9277UL) | 0x00808260UL;
    }
}

GroundShadow::GroundShadow(const GroundShadow& other)
{
    transform = other.transform;
    visible = other.visible;
    setName(other.isNamed() ? other.getName() : std::string{});
    angle = other.angle;
    depth = other.depth;
    width = other.width;
}

/* Retail ICF folds this onto stSurface2D::traverse at 0x004D6540. No separate
   FUNCTION claim: decomplint rejects FOLDED-before-primary when engine_code
   sorts ahead of surface2d.cpp. */

void GroundShadow::draw(GpuRenderer& renderer)
{
    if (!g_settings.monster_shadows) return;
    const meshSubmission::PickScope picking(renderer, true);
    auto position = glm::vec3(this->transform.getLocation());
    position.y += kWorldScale;
    const float radius = std::max(width, depth);
    const auto polygons =
        GetWorldSpatialIndex()->polygonsNearPoint(position, radius, radius * 2 + kWorldScale);
    if (polygons.empty()) return;

    const struct OffsetScope {
        OffsetScope(GpuRenderer& target, w8_long saved) : renderer(target), previous(saved) {}
        GpuRenderer& renderer;
        w8_long previous;
        ~OffsetScope() { renderer.setPolygonOffset(previous); }
        OffsetScope(const OffsetScope&) = delete;
        OffsetScope& operator=(const OffsetScope&) = delete;
    } offset{renderer, renderer.getPolygonOffset()};
    renderer.setPolygonOffset(2);
    const GroundShadowProjection projection(position, width, depth, angle);
    std::vector<w8_ulong> selected;
    std::vector<glm::vec2> coordinates;

    for (std::size_t first = 0; first < polygons.size();) {
        const auto mesh_index = polygons[first] >> 16;
        auto& model = *static_cast<Mesh*>(GetWorld()->psrMeshes[mesh_index]->getModel());
        auto mesh = model.getTriMesh();
        selected.clear();
        do {
            selected.push_back(polygons[first++] & 0xffff);
        } while (first < polygons.size() && (polygons[first] >> 16) == mesh_index);
        mesh.active_polygons = std::span<const w8_ulong>(selected);
        mesh.materials[0][0] = g_ground_shadow_material;
        mesh.poly_textures[0][0] = nullptr;
        mesh.textures[0][0] = g_ground_shadow_texture;
        mesh.vertex_materials[0][0] = nullptr;
        mesh.poly_shaders[0] = nullptr;
        mesh.shaders[0] = g_ground_shadow_shader;
        mesh.poly_uv[0] = nullptr;
        coordinates.resize(mesh.vertex_count);
        for (std::size_t index = 0; index < coordinates.size(); ++index)
            coordinates[index] = projection.coordinates(mesh.positions[index]);
        mesh.texcoords[0][0] = coordinates.data();
        model.RenderTriMeshWithEquations(renderer, mesh, {});
    }
}

GroundShadow::~GroundShadow()
{
    setScene(nullptr);
}
