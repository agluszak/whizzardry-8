#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "surrender/mesh_submission.h"
#include "surrender/srCore.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srStatisticsManager.h"
#include "surrender/srTexture.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/float_constants.h"
#include "wiz8/ground_shadow.h"
#include "wiz8/layouts/world.h"
#include "wiz8/local_code/Configuration.h"

/* The SR.DLL registry string is the original runtime class identity. */

// GLOBAL: WIZ8 0x006834cc
static std::shared_ptr<srTexture> g_ground_shadow_texture;
// GLOBAL: WIZ8 0x006834d0
static std::shared_ptr<srMaterial> g_ground_shadow_material;
/* A default-constructed srShader: retail's dynamic initializer (0x004D61A0)
   stores the srShader default 0x0100241b before the constructor masks in
   the shadow bits, so the 0x13 bits survive into the shadow shader. */
// GLOBAL: WIZ8 0x006834c8
static srShader g_ground_shadow_shader;

/* The material mapper installed for ground shadows: process projects each
   indexed vertex's world x/z through a rotated scale matrix into the first
   texture-coordinate set. The native projection combines the rotated scale
   and world z/x center; owned polygons carries every selected triangle
   index for the active mesh. No source name survives, so the
   mapper keeps a descriptive name anchored at its constructor. */
// VTABLE: WIZ8 0x005ED3B8
// class W8GroundShadowMapper
class W8GroundShadowMapper : public srVertexProcessor {
public:
    /* Retail ICF folds this onto W8NormalTexcoordMapper::isActive at
       0x004D6190. */
    virtual int isActive(srVertexPipe&) override { return 1; }
    virtual void process(srVertexPipe& pipe) override;

    std::optional<GroundShadowProjection> projection;
    std::span<const glm::vec3> vertices;
    std::vector<w8_ulong> polygons;
};

// GLOBAL: WIZ8 0x00683430
static W8GroundShadowMapper g_ground_shadow_material_parameters;

/* Retail ICF folds this class's scalar deleting destructor onto
   W8NormalTexcoordMapper's at 0x004B8A50. */

// FUNCTION: WIZ8 0x004D6090
void W8GroundShadowMapper::process(srVertexPipe& pipe)
{
    srCore.getStatisticsManager()->statistics.texture_coordinate_operations += pipe.vertex_count;
    pipe.lazy_setup_mask |= 1 << CHANNEL_ST0;

    if (pipe.vertex_count == 0) return;
    const std::span<const w8_ulong> indices(pipe.avt + pipe.sub_batch_offset, pipe.vertex_count);
    const std::span<glm::vec2> output(
        pipe.vertex_array->st0 + pipe.batch_base + pipe.sub_batch_offset, pipe.vertex_count);
    for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = projection->coordinates(vertices[indices[i]]);
}

// FUNCTION: WIZ8 0x004D61B0
stGroundShadow::stGroundShadow(srNode* parent) : srNode(static_cast<srNode*>(0))
{
    angle = 0;
    depth = 500.0f;
    width = 500.0f;
    setParent(parent, 1);

    if (g_ground_shadow_texture == 0) {
        g_ground_shadow_texture =
            LoadTextureFromFolder("Data\\Monsters\\Bitmaps\\", "Shadow.tga", true);
        g_ground_shadow_texture->setMipmap(srTexture::MIPMAP_NONE);
        g_ground_shadow_texture->setWrapS(srTexture::WRAP_CLAMP);
        g_ground_shadow_texture->setWrapT(srTexture::WRAP_CLAMP);

        g_ground_shadow_material = std::make_shared<srMaterial>();
        g_ground_shadow_material->setMapper(&g_ground_shadow_material_parameters);
        g_ground_shadow_shader.value = (g_ground_shadow_shader.value & 0xfeff9277UL) | 0x00808260UL;
    }
}

// FUNCTION: WIZ8 0x004d6430
stGroundShadow::stGroundShadow(const stGroundShadow& other) : srNode(static_cast<srNode*>(0))
{
    setParent(other.parent_, 1);
    setName(other.isNamed() ? other.getName() : std::string{});
    angle = other.angle;
    depth = other.depth;
    width = other.width;
}

/* Retail ICF folds this onto stSurface2D::traverse at 0x004D6540. No separate
   FUNCTION claim: decomplint rejects FOLDED-before-primary when engine_code
   sorts ahead of surface2d.cpp. */
void stGroundShadow::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }

    if (!testFlag(FLAG_DISABLE)) {
        info.entries.push_back({this, 0});
    }

    if (!testFlag(FLAG_TERMINATE) && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

// FUNCTION: WIZ8 0x004d6640
void stGroundShadow::process(const ProcessInfo& info, e_processType)
{
    if (g_settings.monster_shadows != 0) {
        const srMeshSubmission::PickScope picking(*info.renderer, true);
        renderGroundShadow(*info.renderer);
    }
}

// FUNCTION: WIZ8 0x004D66A0
void stGroundShadow::renderGroundShadow(srGERD& renderer)
{
    auto position = glm::vec3(getLocation());
    position.y += g_world_scale;
    const float radius = std::max(width, depth);
    const auto polygons = g_octree->polygonsNearPoint(position, radius, radius * 2 + g_world_scale);
    if (polygons.empty()) return;

    const struct OffsetScope {
        srGERD& renderer;
        w8_long previous;
        ~OffsetScope() { renderer.setPolygonOffset(previous); }
    } offset{renderer, renderer.getPolygonOffset()};
    renderer.setPolygonOffset(2);
    g_ground_shadow_material_parameters.projection.emplace(position, width, depth, angle);

    for (std::size_t first = 0; first < polygons.size();) {
        const auto mesh_index = polygons[first] >> 16;
        auto& model = *static_cast<stMeshModel*>(g_world->psrMeshes[mesh_index]->getModel());
        auto mesh = model.getTriMesh();
        auto& selected = g_ground_shadow_material_parameters.polygons;
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
        g_ground_shadow_material_parameters.vertices = {
            model.getVertexLoc(), static_cast<std::size_t>(mesh.vertex_count)};
        model.RenderTriMeshWithEquations(renderer, mesh, {});
    }
}
