#include "wiz8/engine_code/3d.h"

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "layouts/world.h"
#include "surrender/plane_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "surrender/srShader.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/triangle_projection.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/float_constants.h"
#include "wiz8/geometry.h"
#include "wiz8/sr_api.h"

#define THREE_D_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\3d.cpp"

// GLOBAL: WIZ8 0x005EC428
const double g_light_range_attenuation_numerator = 0.9980430528375734;
// GLOBAL: WIZ8 0x005EC430
const double g_light_range_attenuation_denominator = 0.0019569471624266144;

// FUNCTION: WIZ8 0x0046DD70
void UpdateWorldMonsters(W8World* world)
{
    if (world == 0) {
        srAssertFail("pWorld", THREE_D_CPP, 0x10d, 0);
    }

    srVector3T<double> camera_location = world->camera->getLocation();
    srVector3T<float> position;
    position = camera_location;

    unsigned int count = world->plsMonsters.size();
    UpdateNearestMonsterGroupMembers();
    for (int index = 0; index < static_cast<int>(count); ++index) {
        W8Monster* monster = world->plsMonsters[index];
        if (monster != 0) {
            monster->DetachRepresentation(world);
            monster->SelectLOD(&position);
            monster->Update();
            monster->UpdateRepresentation(world);
        }
    }
}

/* Detach every item mesh in one world, refresh its animation, and reattach it
   to the world's dynamic scene before the renderer transition. */
// FUNCTION: WIZ8 0x0046de40
void DetachWorldItemMeshes(W8World* world)
{
    if (world == 0) {
        srAssertFail("pWorld", THREE_D_CPP, 0x135, 0);
    }
    unsigned int count = world->plsItems.size();
    for (int index = 0; index < static_cast<int>(count); ++index) {
        W8Item* item = world->plsItems[index];
        if (item != 0) {
            item->DetachMesh(world);
            item->UpdateAnimation();
            item->AttachMesh(world);
        }
    }
}

/* Five-byte tail-jump thunk to GameData.cpp's GetCameraYawRadians. */

/* Remove every light held in one world's list, drop it from the render update
   set, and release it. */
// FUNCTION: WIZ8 0x0046e4a0
void DestroyWorldLights(W8World* world)
{
    world->lights_to_update.clear();
    world->transient_lights.clear();
}

/* Put static-scene illuminators in group one and the world's camera light in
   group two.  The scene graph access is the ordinary srNode hierarchy API. */
// FUNCTION: WIZ8 0x0046F3A0
void FinalizeStaticScene(srScene* scene)
{
    srNode* node;

    for (node = scene->first_child_; node != 0; node = node->next_sibling_) {
        if (auto* light = sr_cast<srLight>(node)) {
            light->setGroupMask(1);
        }
    }
    if (g_world != 0 && g_world->camera_light != 0) {
        g_world->camera_light->setGroupMask(2);
    }
}

/* Bake the dynamic scene's non-"Sun" lights into every mesh of the instance's
   model chain. Each mesh gets a world-space vertex set (translated or fully
   transformed, or aliased when the transform is identity) and a direction
   array of vertex-minus-light vectors per overlapping light; a vertex that
   faces the light accumulates diffuse attenuated by range into its vertex
   light. "Sun"-prefixed lights bake into the sunlight array instead and are
   skipped here. `walk_chain` limits the light walk to the first sibling. */
// FUNCTION: WIZ8 0x0046E8A0
unsigned char BakeInstanceVertexLighting(stModelInstance* instance, srNode* lights, bool walk_chain)
{
    auto world = instance->getWorldSpaceMatrix<glm::mat4>();
    const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(world)));
    std::vector<glm::vec3> world_vertices, world_normals, material_colors;

    for (auto* mesh = static_cast<stMeshModel*>(instance->getModel()); mesh; mesh = mesh->Next()) {
        const auto count = mesh->vertex_location_count;
        if (count <= 0) continue;
        const auto vertex_lights = mesh->GetVertexLights(true, -1);
        mesh->GetVertexSunlight(true);
        if (vertex_lights.empty()) return 0;
        if (!lights) continue;
        auto* vertex_materials = mesh->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 0);
        auto* base_material =
            static_cast<srMaterial*>(mesh->getMaterial(0, srMeshModel::SIDE_FRONT));
        const glm::vec3 base_color =
            base_material ? glm::vec3(base_material->parms.diffuse) : glm::vec3(1);
        const auto* vertices = (mesh->flags & W8_MESH_HAS_FRAME_STORAGE)
                                   ? mesh->GetVertexLocations(0, true, 0.0f)
                                   : mesh->getVertexLoc();
        const auto* normals = (mesh->flags & W8_MESH_HAS_FRAME_STORAGE)
                                  ? mesh->GetVertexNormals(0, true)
                                  : mesh->getVertexNormal();
        world_vertices.resize(count);
        world_normals.resize(count);
        material_colors.resize(count);
        for (int i = 0; i < count; ++i) {
            world_vertices[i] = glm::vec3(world * glm::vec4(vertices[i], 1));
            world_normals[i] = normalizedOrZero(normal_matrix * normals[i]);
            auto* material =
                vertex_materials ? static_cast<srMaterial*>(vertex_materials[i].get()) : nullptr;
            material_colors[i] = material ? glm::vec3(material->parms.diffuse) : base_color;
        }
        const auto box = mesh->getBounds().box.transformed(world);
        for (auto* node = lights; node; node = walk_chain ? node->next_sibling_ : nullptr) {
            const auto* light = sr_cast<stLight>(node);
            if (!light || _strnicmp(node->getName().c_str(), "Sun", 3) == 0) continue;
            const float range = static_cast<float>(
                g_light_range_attenuation_numerator /
                (light->opengl_attenuation.y * g_light_range_attenuation_denominator));
            const auto position = glm::vec3(light->getWorldSpaceLocation());
            if (!box.overlaps({position - range, position + range})) continue;
            mesh->flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
            for (int i = 0; i < count; ++i) {
                const auto direction = world_vertices[i] - position;
                const float distance = glm::length(direction);
                if (distance > range) continue;
                const float facing = glm::dot(normalizedOrZero(direction), world_normals[i]);
                if (facing < 0) {
                    const float contribution = -facing * light->intensity * (1 - distance / range);
                    vertex_lights[i] =
                        vertex_lights[i] + material_colors[i] * light->diffuse * contribution;
                }
            }
        }
    }
    return 1;
}

/* Bake the dynamic scene's light children into every not-yet-lit model
   instance under one static-scene subtree. render_flags bit 1 is the instance's
   own lit marker; the child-chain exclusion update uses the shared helper. */
// FUNCTION: WIZ8 0x0046F410
unsigned char FinalizeWorldScenes(srNode* node, srNode* dynamic_scene)
{
    for (; node != 0; node = node->next_sibling_) {
        if (node->first_child_ != 0) {
            FinalizeWorldScenes(node->first_child_, dynamic_scene);
        }
        if (auto* instance = sr_cast<stModelInstance>(node)) {
            BakeInstanceVertexLightingIfNeeded(instance, dynamic_scene);
        }
    }
    return 1;
}

/* Bake dynamic-scene lights into one not-yet-lit model instance. render_flags
   bit 1 is the lit marker. */
// FUNCTION: WIZ8 0x0046F4A0
unsigned char BakeInstanceVertexLightingIfNeeded(stModelInstance* instance, srNode* dynamic_scene)
{
    srNode* lights = dynamic_scene->first_child_;

    if ((instance->render_flags & stModelInstance::RENDER_LIGHTING_BAKED) == 0) {
        instance->render_flags |= stModelInstance::RENDER_LIGHTING_BAKED;
        SetModelInstanceChainExclusionMask(instance, 1);
        BakeInstanceVertexLighting(instance, lights, true);
    }
    return 1;
}

/* Walk every live world mesh (octree mesh table, or the update-mesh chain when
   there is no octree) and point its vertex-light table index at `table`, then
   raise flags bit 1 so the next bake uses that table. */
// FUNCTION: WIZ8 0x0046F760
void SetWorldMeshVertexLightTable(W8World* world, int table)
{
    if (world->octree == 0) {
        for (stMeshModel* model : stMeshModel::Meshes(
                 static_cast<stMeshModel*>(world->update_mesh_source->getModel()))) {
            model->vertex_light_table = table;
            model->flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
        }
    } else {
        for (unsigned int mesh = 0; mesh < world->octree->m_meshCount; ++mesh) {
            srModelInstance* instance = world->psrMeshes[mesh];
            if (instance != 0) {
                for (stMeshModel* model :
                     stMeshModel::Meshes(static_cast<stMeshModel*>(instance->getModel()))) {
                    model->vertex_light_table = table;
                    model->flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
                }
            }
        }
    }
}

/* Visibility queries share midpoint/corner generation and retain early-exit order. */
// FUNCTION: WIZ8 0x0046F820
bool targetMarkerVisible(const glm::vec3& eye, const srBoundingBox& bounds)
{
    const srVector3 from(eye);
    for (const auto& sample : bounds.visibilitySamples()) {
        if (!projectsToCamera(sample)) continue;
        srVector3 point(sample);
        if (g_octree->HasLineOfSight(&from, &point, true)) return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x0046FAF0
bool traceLineOfSightToBounds(const glm::vec3& origin, const srBoundingBox& bounds)
{
    const srVector3 from(origin);
    for (const auto& sample : bounds.visibilitySamples()) {
        srVector3 point(sample);
        if (g_octree->TraceLineOfSight(&from, &point, true, -3, -3, true, 0) == 0) return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x0046FD70
bool hasLineOfSightToBounds(const glm::vec3& origin, const srBoundingBox& bounds)
{
    const srVector3 from(origin);
    for (const auto& sample : bounds.visibilitySamples()) {
        srVector3 point(sample);
        if (g_octree->HasLineOfSight(&from, &point, true)) return true;
    }
    return false;
}

/* Build the concrete first-party instance used for an already-created mesh. */
// FUNCTION: WIZ8 0x0046F5C0
std::unique_ptr<stModelInstance> CreateModelInstance(std::shared_ptr<stMeshModel> model)
{
    if (model == nullptr) {
        srAssertFail("pstMeshModel", THREE_D_CPP, 0x5bf, 0);
    }
    auto instance = std::make_unique<stModelInstance>(nullptr, srNode::Ownership::independent);
    instance->setName("ST_CreateInstance");
    instance->setModel(model);
    return instance;
}

// FUNCTION: WIZ8 0x0046F680
std::unique_ptr<stModelInstance> DuplicateModelInstance(const stModelInstance& instance)
{
    auto copy = std::make_unique<stModelInstance>(nullptr, srNode::Ownership::independent);
    *copy = instance;
    copy->setName("ST_CreateInstance");
    return copy;
}

static void ConfigureLightDefaults(stLight& light, const char* name)
{
    light.setName(name ? name : "");
    light.attenuation_model = srLight::ATTENUATION_3DSTUDIO_MAX;
    light.enable_flags |= (1UL << srLight::ENABLE_RANGE_FAR);
    light.enable_flags |= (1UL << srLight::ENABLE_BOUNDING_SPHERE);
}

// FUNCTION: WIZ8 0x0046DF90
stLight* CreateLight(srNode* parent, const char* name)
{
    stLight* light = new stLight(parent);
    ConfigureLightDefaults(*light, name);
    return light;
}

stLight* AddWorldLight(W8World* world, std::unique_ptr<stLight> light)
{
    stLight* borrowed = light.get();
    world->transient_lights.push_back(std::move(light));
    return borrowed;
}

// FUNCTION: WIZ8 0x0046E030
stLight* CreateWorldLight(W8World* world, const char* name)
{
    if (name == 0) {
        srAssertFail("name", THREE_D_CPP, 570, 0);
    }

    if (world == 0) {
        // Not tied to a world (animation template): scene-owned like any other node.
        auto* light = new stLight(static_cast<srNode*>(nullptr));
        ConfigureLightDefaults(*light, name);
        return light;
    }
    auto light = std::make_unique<stLight>(world->static_scene, srNode::Ownership::independent);
    ConfigureLightDefaults(*light, name);
    return AddWorldLight(world, std::move(light));
}

// FUNCTION: WIZ8 0x0046E140
stLight* CreateRangedWorldLight(W8World* world, const char* name)
{
    if (name == 0) {
        srAssertFail("name", THREE_D_CPP, 603, 0);
    }
    if (world == 0) {
        srAssertFail("pWorld", THREE_D_CPP, 604, 0);
    }

    auto light = std::make_unique<stLight>(world->static_scene, srNode::Ownership::independent);
    ConfigureLightDefaults(*light, name);
    light->near_start = 0.0;
    light->near_end = 0.0;
    light->far_start = 0.0;
    light->far_end = 1500.0;
    light->safe_range = 5000.0f;
    light->setLinearAttenuation(1500.0f, 0.0019569471f);
    return AddWorldLight(world, std::move(light));
}

/* Take a light out of both world collections and release it. */
// FUNCTION: WIZ8 0x0046e250
void WorldRemoveLight(W8World* world, stLight* light)
{
    if (world == 0) {
        srAssertFail("pWorld", THREE_D_CPP, 0x278, 0);
    }
    if (light == 0) {
        srAssertFail("pLight", THREE_D_CPP, 0x279, 0);
    }
    wiz8::erase_first(world->lights_to_update, light);
    // The world owns the light: erasing it from the list destroys it.
    std::erase_if(world->transient_lights,
                  [light](const std::unique_ptr<stLight>& owned) { return owned.get() == light; });
}

// FUNCTION: WIZ8 0x0046E300
void ConfigureWorldLight(srLight* light, float range)
{
    light->far_end = static_cast<double>(range);
    light->near_start = 0.0;
    light->near_end = 0.0;
    light->far_start = 0.0;
    light->safe_range = 5000.0f;
    light->setLinearAttenuation(range, 0.0019569471f);
}

// Source unit is Engine Code\3d.cpp; the assertion at line 344 is what names
// and types World::plsProps.
// FUNCTION: WIZ8 0x0046ded0
void WorldUpdateProps(W8World* world)
{
    int count;
    int index;
    W8Prop* prop;

    if (!world) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\3d.cpp", 0x158, 0);
    }
    count = static_cast<int>(world->plsProps.size());
    for (index = 0; index < count; index++) {
        prop = GetWorldProp(world, index);
        if (prop) {
            prop->DetachAnimationInstances(world);
            prop->UpdatePropAnimation();
            prop->AttachAnimationInstances(world);
        }
    }
}

// FUNCTION: WIZ8 0x0046df50
void WorldUpdateLights(W8World* world)
{
    int count = static_cast<int>(world->lights_to_update.size());

    for (int index = 0; index < count; ++index) {
        world->lights_to_update[index]->Update();
    }
}

/* 0x00659AB4: the world being rendered, which the list wrappers below reach
   through. Every one of them ignores the caller's own first argument and uses
   this global instead. */

// FUNCTION: WIZ8 0x0046e580
void AddMonsterToWorld(W8World*, W8Monster* monster)
{
    g_world->plsMonsters.push_back(monster);
}

// FUNCTION: WIZ8 0x0046e5c0
void AddItemToWorld(W8World*, W8Item* item)
{
    g_world->plsItems.push_back(item);
}

// FUNCTION: WIZ8 0x0046e5e0
void RemoveItemFromWorld(W8World*, W8Item* item)
{
    wiz8::erase_first(g_world->plsItems, item);
}

/* How many props the world holds, and the one at a position - both answers
   discarded by the wrapper itself, which is what makes these thin forwarders
   rather than accessors. */
// FUNCTION: WIZ8 0x0046e600
int WorldGetPropCount(W8World*)
{
    return g_world->plsProps.size();
}

// FUNCTION: WIZ8 0x0046e620
W8Prop* WorldGetPropAt(W8World*, int index)
{
    return GetWorldProp(g_world, index);
}

// FUNCTION: WIZ8 0x0046e860
void SetWorldSceneMeshShaderLowBits(W8World* owner, int argument)
{
    SetSceneMeshShaderLowBits(owner->static_scene, argument);
}

// FUNCTION: WIZ8 0x0046e880
void SetWorldSceneMeshShaderBit3(W8World* owner, int argument)
{
    SetSceneMeshShaderBit3(owner->static_scene, argument);
}

/* Walk one scene subtree and toggle shader DEPTH_WRITE on every mesh model of
   every model instance; the instance's flags bit zero selects the off
   state. */
// FUNCTION: WIZ8 0x0046e640
void SetSceneMeshShaderBit3(srNode* node, int argument)
{
    srShader shader;
    for (; node != 0; node = node->next_sibling_) {
        if (auto* instance = sr_cast<stModelInstance>(node)) {
            for (stMeshModel* mesh :
                 stMeshModel::Meshes(static_cast<stMeshModel*>(instance->getModel()))) {
                srShader* polygon_shader = mesh->getPolyShader(0, 0);
                bool clear = argument == 0 || (mesh->flags & W8_MESH_SORTED_RENDERING) != 0;

                if (polygon_shader == 0) {
                    shader = mesh->getShader(0);
                    if (clear) {
                        shader.value &= ~srShader::MASK_DEPTH_WRITE;
                    } else {
                        shader.value |= srShader::MASK_DEPTH_WRITE;
                    }
                    mesh->setShader(shader, 0);
                } else if (clear) {
                    for (w8_long index = 0; index < mesh->polygon_count; ++index) {
                        polygon_shader[index].value &= ~srShader::MASK_DEPTH_WRITE;
                    }
                } else {
                    for (w8_long index = 0; index < mesh->polygon_count; ++index) {
                        polygon_shader[index].value |= srShader::MASK_DEPTH_WRITE;
                    }
                }
            }
        }
        if (node->first_child_ != 0) {
            SetSceneMeshShaderBit3(node->first_child_, argument);
        }
    }
}

/* The sibling walker that leaves PASS bit 2 clear and writes the low three
   bits: PASS_ALWAYS when the argument is zero, otherwise PASS_LEQUAL. */
// FUNCTION: WIZ8 0x0046e750
void SetSceneMeshShaderLowBits(srNode* node, int argument)
{
    srShader shader;
    for (; node != 0; node = node->next_sibling_) {
        if (auto* instance = sr_cast<stModelInstance>(node)) {
            for (stMeshModel* mesh :
                 stMeshModel::Meshes(static_cast<stMeshModel*>(instance->getModel()))) {
                if ((mesh->flags & W8_MESH_SORTED_RENDERING) != 0) {
                    continue;
                }
                srShader* polygon_shader = mesh->getPolyShader(0, 0);
                if (polygon_shader == 0) {
                    shader = mesh->getShader(0);
                    if (argument == 0) {
                        shader.value |= srShader::PASS_ALWAYS;
                    } else {
                        shader.value = (shader.value & 0xfffffffb) | srShader::PASS_LEQUAL;
                    }
                    mesh->setShader(shader, 0);
                } else if (argument == 0) {
                    for (w8_long index = 0; index < mesh->polygon_count; ++index) {
                        polygon_shader[index].value |= srShader::PASS_ALWAYS;
                    }
                } else {
                    for (w8_long index = 0; index < mesh->polygon_count; ++index) {
                        polygon_shader[index].value =
                            (polygon_shader[index].value & 0xfffffffb) | srShader::PASS_LEQUAL;
                    }
                }
            }
        }
        if (node->first_child_ != 0) {
            SetSceneMeshShaderLowBits(node->first_child_, argument);
        }
    }
}

// FUNCTION: WIZ8 0x0046f4f0
void SetModelInstanceChainExclusionMask(srModelInstance* node, int value)
{
    for (; node != 0; node = static_cast<srModelInstance*>(node->first_child_)) {
        node->setExclusionMask(value);
    }
}

/* The world's own float pair at 0x74 and 0x78. Both are assigned from the one
   argument, and only zero is rejected, so the guard is a "leave it alone"
   rather than a range check. The assertion at 3d.cpp:651 is what names the
   receiver pWorld. */
// FUNCTION: WIZ8 0x0046e350
void WorldSetRenderRange(W8World* world, float value)
{
    if (!world) {
        srAssertFail("pWorld", THREE_D_CPP, 0x28b, 0);
    }
    if (value != 0.0f) {
        world->render_range0 = value;
        world->render_range = value;
        MarkRendererReady();
    }
}

/* Reads back only the second of the pair, which is what makes 0x78 the live
   copy and 0x74 the one nothing here consumes. */
// FUNCTION: WIZ8 0x0046e3a0
float WorldGetRenderRange(W8World* world)
{
    if (!world) {
        srAssertFail("pWorld", THREE_D_CPP, 0x297, 0);
    }
    return world->render_range;
}

/* Pushes the world's view distance into the camera as the far clip plane; the
   near plane is the fixed 62.5 the original materialises inline. The guard is
   against a denormal-scale epsilon rather than zero, so a distance that has
   collapsed leaves the camera as it was. */
// FUNCTION: WIZ8 0x0046e3d0
void WorldSetFarClip(W8World* world, float distance)
{
    if (!world) {
        srAssertFail("pWorld", THREE_D_CPP, 0x29e, 0);
    }
    if (distance > 5.9604644775390625e-008) {
        world->camera->setClipRange(62.5, distance);
        RefreshFogRanges();
        MarkRendererReady();
    }
}

/* The matching reader. Both planes come back, and only the far one is
   returned, which is the same asymmetry the setter has. A world without a
   camera answers zero rather than reading through it. */
// FUNCTION: WIZ8 0x0046e440
double WorldGetFarClip(W8World* world)
{
    if (world != nullptr && world->camera != nullptr) return world->camera->getClipRange().second;
    return 0;
}

// FUNCTION: WIZ8 0x0046E5A0
void RemoveMonsterFromWorldList(W8World*, W8Monster* monster)
{
    wiz8::erase_first(g_world->plsMonsters, monster);
}

// FUNCTION: WIZ8 0x0046DC90
void SetSceneAmbientLightWhite(srScene* scene)
{
    if (scene == 0) {
        srAssertFail("psrScene", THREE_D_CPP, 0xf1, 0);
    }
    scene->setAmbientLight(1.0f, 1.0f, 1.0f);
}

// GLOBAL: WIZ8 0x00652db0
W8GameData* g_octree_game_data;

/* The octree builds read the level data through this slot; the recovered
   caller passes the W8GameData object it just read. */
// FUNCTION: WIZ8 0x0046D7D0
void __stdcall SetOctreeGameData(W8GameData* value)
{
    g_octree_game_data = value;
}

// FUNCTION: WIZ8 0x0046d880
bool PointInsideFrustum(const glm::vec3& point, std::span<const W8Plane, 6> planes)
{
    return SphereInsideFrustum(point, 0.0f, planes);
}

/* Point-in-triangle test by even-odd crossing on the plane perpendicular to
   `axis`: the two remaining components (axis+1, axis+2 mod 3) project the
   triangle and query point, and each edge straddling the point's second
   coordinate toggles the inside flag when its interpolation crosses the
   first. */
// FUNCTION: WIZ8 0x0046d530
bool PointInsideTriangle(std::span<const glm::vec3, 3> vertices, unsigned axis,
                         const glm::vec3& point)
{
    return srTriangleProjection::contains(vertices, axis, point,
                                          srTriangleProjection::Boundary::visibility);
}

// FUNCTION: WIZ8 0x0046d7e0
std::array<W8Plane, 6> BuildFrustumPlanes(std::span<const glm::vec3, 8> points)
{
    constexpr std::array<std::array<unsigned, 3>, 6> corners{
        {{1, 5, 4}, {6, 7, 3}, {0, 2, 3}, {4, 5, 7}, {4, 6, 2}, {1, 3, 7}}};
    std::array<W8Plane, 6> planes;
    for (unsigned plane = 0; plane < planes.size(); ++plane) {
        const auto& indices = corners[plane];
        planes[plane] = trianglePlane(points[indices[0]], points[indices[1]], points[indices[2]]);
    }
    return planes;
}

/* Return a canonical y/z/x corner ordering, preserving ties within each run. */
// FUNCTION: WIZ8 0x0046da20
std::array<glm::vec3, 8> sortedFrustumCorners(std::span<const glm::vec3, 8> points)
{
    std::array<glm::vec3, 8> sorted;
    std::ranges::copy(points, sorted.begin());
    constexpr std::array<std::pair<unsigned, unsigned>, 3> passes{{{1, 8}, {2, 4}, {0, 2}}};
    for (const auto [axis, run] : passes)
        for (unsigned start = 0; start < sorted.size(); start += run)
            std::stable_sort(sorted.begin() + start, sorted.begin() + start + run,
                             [axis](const auto& a, const auto& b) { return a[axis] < b[axis]; });
    return sorted;
}

// FUNCTION: WIZ8 0x0046d8d0
bool SphereInsideFrustum(const glm::vec3& point, float radius, std::span<const W8Plane, 6> planes)
{
    const auto coefficients = planes | std::views::transform([](const W8Plane& plane) {
                                  return glm::vec4(plane.normal.glmValue(), plane.w);
                              });
    return srPlaneMath::containsSphere(coefficients, point, radius);
}

// FUNCTION: WIZ8 0x0046d920
bool BoundsInsideFrustum(const W8OctRegionVolume& volume, const srBoundingBox& bounds)
{
    for (unsigned corner = 0; corner < 8; ++corner)
        if (PointInsideFrustum(bounds.corner(corner), volume.m_planes)) return true;
    for (unsigned corner = 1; corner < 9; ++corner)
        if (bounds.containsHalfOpen(volume.m_points[corner].glmValue())) return true;
    return false;
}

/* glm::mat4::Set emitted for this TU (BakeInstanceVertexLighting's
   transform builds); the primary template lives in srMath.h. */
