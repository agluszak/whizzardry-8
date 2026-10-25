#include "wiz8/world/update.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/geometry.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/math.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/plane_math.h"
#include "wiz8/render/triangle_projection.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/world.h"

constexpr double kLightRangeAttenuationDenominator = 0.0019569471624266144;
constexpr double kLightRangeAttenuationNumerator = 0.9980430528375734;

void World::updateMonsters()
{
    if (monster_retirements_) throw std::logic_error("recursive monster update");
    const auto updating = active_monsters;
    std::vector<std::unique_ptr<W8MonsterInfo>> retired;
    monster_retirements_ = &retired;
    try {
        const auto position = party.eye();
        updateNearestMonsterGroupMembers();
        for (Monster* monster : updating) {
            if (!monster || !std::ranges::contains(active_monsters, monster)) continue;
            monster->visual.DetachRepresentation();
            monster->visual.SelectLOD(&position);
            monster->Update();
            if (std::ranges::contains(active_monsters, monster)) monster->UpdateRepresentation();
        }
    } catch (...) {
        monster_retirements_ = nullptr;
        throw;
    }
    monster_retirements_ = nullptr;
    // The batch is reclaimed after all update/callback stack frames have unwound.
}

bool World::isRetiring(const Monster& monster) const
{
    return monster_retirements_ &&
           std::ranges::any_of(*monster_retirements_,
                               [&](const auto& info) { return info->entity.get() == &monster; });
}

/* Animation changes the instances already attached to this world's scene. */
void AnimateWorldItems(World& world)
{
    for (const auto& item : world.population->world_items) {
        if (!item->entity) continue;
        item->entity->UpdateAnimation();
        item->entity->ApplyRepTransform();
    }
}

/* Five-byte tail-jump thunk to GameData.cpp's GetCameraYawRadians. */

/* Put static-scene illuminators in group one and the world's camera light in
   group two. */
void FinalizeStaticScene(World& world)
{
    for (const auto& light : world.transient_lights)
        if (light->scene == world.scene.get()) light->setGroupMask(1);
    if (world.camera_light) world.camera_light->setGroupMask(2);
}

/* Bake the dynamic scene's non-"Sun" lights into every mesh of the instance's
   model chain. Each mesh gets a world-space vertex set (translated or fully
   transformed, or aliased when the transform is identity) and a direction
   array of vertex-minus-light vectors per overlapping light; a vertex that
   faces the light accumulates diffuse attenuated by range into its vertex
   light. "Sun"-prefixed lights bake into the sunlight array instead and are
   skipped here. `walk_chain` limits the light walk to the first sibling. */
bool BakeInstanceVertexLighting(MeshInstance* instance, std::span<Light* const> lights)
{
    auto world = glm::mat4(instance->worldMatrix());
    const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(world)));
    std::vector<glm::vec3> world_vertices, world_normals, material_colors;

    for (auto* mesh : meshParts(static_cast<Mesh*>(instance->getModel()))) {
        const auto count = mesh->vertex_location_count;
        if (count <= 0) continue;
        const auto vertex_lights = mesh->GetVertexLights(true);
        mesh->GetVertexSunlight(true);
        if (vertex_lights.empty()) return false;
        if (lights.empty()) continue;
        auto* vertex_materials = mesh->getVertexMaterialTable(0, Mesh::SIDE_FRONT, false);
        auto* base_material = mesh->getMaterial(0, Mesh::SIDE_FRONT);
        const glm::vec3 base_color =
            base_material ? glm::vec3(base_material->parms.diffuse) : glm::vec3(1);
        const bool animated = (mesh->flags & W8_MESH_HAS_FRAME_STORAGE) != 0;
        const auto frame_vertices = mesh->decodeFrame(0, W8_MESH_FRAME_LOCATIONS);
        const auto frame_normals = mesh->decodeFrame(0, W8_MESH_FRAME_VERTEX_NORMALS);
        const auto* vertices = animated ? frame_vertices.data() : mesh->getVertexLoc();
        const auto* normals = animated ? frame_normals.data() : mesh->getVertexNormal();
        world_vertices.resize(count);
        world_normals.resize(count);
        material_colors.resize(count);
        for (int i = 0; i < count; ++i) {
            world_vertices[i] = glm::vec3(world * glm::vec4(vertices[i], 1));
            world_normals[i] = normalizedOrZero(normal_matrix * normals[i]);
            auto* material = vertex_materials ? (*vertex_materials)[i].get() : nullptr;
            material_colors[i] = material ? glm::vec3(material->parms.diffuse) : base_color;
        }
        const auto box = mesh->getBounds().box.transformed(world);
        for (const auto* light : lights) {
            if (!light || SDL_strncasecmp(light->getName().c_str(), "Sun", 3) == 0) continue;
            const float range = static_cast<float>(
                kLightRangeAttenuationNumerator /
                (light->opengl_attenuation.y * kLightRangeAttenuationDenominator));
            const auto position = glm::vec3(light->worldMatrix()[3]);
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
    return true;
}

/* Bake the dynamic scene's light children into every not-yet-lit model
   instance under one static-scene subtree. render_flags bit 1 is the instance's
   own lit marker; the child-chain exclusion update uses the shared helper. */
void FinalizeWorldScenes(WorldScene& scene)
{
    for (const auto& instance : scene.static_meshes)
        BakeInstanceVertexLightingIfNeeded(instance.get(), &scene);
    for (auto* instance : scene.dynamic_meshes)
        BakeInstanceVertexLightingIfNeeded(instance, &scene);
}

/* Bake dynamic-scene lights into the model an instance shows, the first time it
   shows it. */
bool BakeInstanceVertexLightingIfNeeded(MeshInstance* instance, WorldScene* scene)
{
    const Mesh* model = instance->getModel();
    if (!std::ranges::contains(instance->baked_models, model)) {
        instance->baked_models.push_back(model);
        instance->setExclusionMask(1);
        std::vector<Light*> lights;
        for (auto* light : scene->lights)
            if (light->bake_into_mesh) lights.push_back(light);
        BakeInstanceVertexLighting(instance, lights);
    }
    return true;
}

/* Visibility queries share midpoint/corner generation and retain early-exit order. */
bool World::targetMarkerVisible(const BoundingBox& bounds) const
{
    const auto from = party.eye();
    return std::ranges::any_of(bounds.visibilitySamples(), [&](const auto& sample) {
        return projectsToCamera(sample) && compiledIndex()->hasLineOfSight(from, glm::vec3(sample));
    });
}

bool World::hasLineOfSightTo(const BoundingBox& bounds) const
{
    const auto from = party.eye();
    return std::ranges::any_of(bounds.visibilitySamples(), [&](const auto& sample) {
        return !compiledIndex()->traceSight(from, glm::vec3(sample));
    });
}

/* Finish the retail render representation before publishing its first instance.
   All frames, material partitions, owned parts and compound bounds are ready
   here; building ordinary VSG arrays does not require a graphics device. */
std::unique_ptr<MeshInstance> CreateModelInstance(std::shared_ptr<Mesh> model)
{
    assert(model != nullptr);
    for (auto* part : meshParts(model.get())) part->renderAsset();
    auto instance = std::make_unique<MeshInstance>(nullptr);
    instance->setName("ST_CreateInstance");
    instance->setModel(model);
    return instance;
}

static void ConfigureLightDefaults(Light& light, std::string_view name)
{
    light.setName(std::string(name));
    light.attenuation_model = Light::ATTENUATION_3DSTUDIO_MAX;
    light.enable_flags |= (1UL << Light::ENABLE_RANGE_FAR);
}

std::unique_ptr<Light> CreateLight(WorldScene* scene, std::string_view name)
{
    auto light = std::make_unique<Light>(scene);
    ConfigureLightDefaults(*light, name);
    return light;
}

Light* AddWorldLight(World* world, std::unique_ptr<Light> light)
{
    Light* borrowed = light.get();
    world->transient_lights.push_back(std::move(light));
    return borrowed;
}

Light* CreateWorldLight(World* world, std::string_view name)
{
    assert(!name.empty());

    auto light = std::make_unique<Light>(world->scene.get());
    ConfigureLightDefaults(*light, name);
    return AddWorldLight(world, std::move(light));
}

Light* CreateRangedWorldLight(World* world, std::string_view name)
{
    assert(!name.empty());
    assert(world != 0);

    auto light = std::make_unique<Light>(world->scene.get());
    ConfigureLightDefaults(*light, name);
    light->near_start = 0.0;
    light->near_end = 0.0;
    light->far_start = 0.0;
    light->far_end = 1500.0;
    light->setLinearAttenuation(1500.0f, 0.0019569471f);
    return AddWorldLight(world, std::move(light));
}

void ConfigureWorldLight(Light* light, float range)
{
    light->far_end = static_cast<double>(range);
    light->near_start = 0.0;
    light->near_end = 0.0;
    light->far_start = 0.0;
    light->setLinearAttenuation(range, 0.0019569471f);
}

// Source unit is Engine Code\3d.cpp; the assertion at line 344 is what names
// and types World::plsProps.
void WorldUpdateProps(World* world, std::chrono::milliseconds elapsed)
{
    int count;
    int index;
    Prop* prop;

    assert(world);
    count = static_cast<int>(world->plsProps.size());
    for (index = 0; index < count; index++) {
        prop = GetWorldProp(world, index);
        if (prop) {
            prop->DetachAnimationInstances(world);
            prop->UpdatePropAnimation(elapsed, world->clocks.held());
            prop->AttachAnimationInstances(world);
        }
    }
}

void WorldUpdateLights(World* world)
{
    // Authored lights are owned by the world. Cycle lights remain owned by
    // their emitter and participate while attached to this world's scene.
    for (const auto& light : world->transient_lights)
        if (!std::holds_alternative<std::monostate>(light->animator))
            light->Update(std::chrono::duration<float>(CurrentGame().visual_time).count());
    const auto update_cycle = [&](const auto& cycle) {
        if (!cycle.visual.m_plsLights) return;
        for (const auto& light : *cycle.visual.m_plsLights)
            if (!std::holds_alternative<std::monostate>(light->animator) &&
                light->scene == world->scene.get())
                light->Update(std::chrono::duration<float>(CurrentGame().visual_time).count());
    };
    for (const auto* navigator : world->navigators)
        if (navigator->monster_owner) update_cycle(*navigator->monster_owner);
    for (const auto& missile : world->missiles) update_cycle(*missile);
    for (const auto& visual : world->spell_visuals) update_cycle(*visual);
}

void SetWorldSceneMeshShaderLowBits(World* owner, int argument)
{
    SetSceneMeshShaderLowBits(owner->scene.get(), argument);
}

void SetWorldSceneMeshShaderBit3(World* owner, int argument)
{
    SetSceneMeshShaderBit3(owner->scene.get(), argument);
}

/* Walk one scene subtree and toggle shader DEPTH_WRITE on every mesh model of
   every model instance; the instance's flags bit zero selects the off
   state. */
void SetSceneMeshShaderBit3(WorldScene* scene, int argument)
{
    DrawShader shader;
    std::vector<MeshInstance*> instances = scene->dynamic_meshes;
    for (const auto& instance : scene->static_meshes) instances.push_back(instance.get());
    for (auto* instance : instances) {
        {
            for (Mesh* mesh : meshParts(static_cast<Mesh*>(instance->getModel()))) {
                const bool clear = argument == 0 || (mesh->flags & W8_MESH_SORTED_RENDERING) != 0;
                shader = mesh->getShader(0);
                if (clear) {
                    shader.value &= ~DrawShader::MASK_DEPTH_WRITE;
                } else {
                    shader.value |= DrawShader::MASK_DEPTH_WRITE;
                }
                mesh->setShader(shader, 0);
            }
        }
    }
}

/* The sibling walker that leaves PASS bit 2 clear and writes the low three
   bits: PASS_ALWAYS when the argument is zero, otherwise PASS_LEQUAL. */
void SetSceneMeshShaderLowBits(WorldScene* scene, int argument)
{
    DrawShader shader;
    std::vector<MeshInstance*> instances = scene->dynamic_meshes;
    for (const auto& instance : scene->static_meshes) instances.push_back(instance.get());
    for (auto* instance : instances) {
        {
            for (Mesh* mesh : meshParts(static_cast<Mesh*>(instance->getModel()))) {
                if ((mesh->flags & W8_MESH_SORTED_RENDERING) != 0) {
                    continue;
                }
                shader = mesh->getShader(0);
                if (argument == 0) {
                    shader.value |= DrawShader::PASS_ALWAYS;
                } else {
                    shader.value = (shader.value & 0xfffffffb) | DrawShader::PASS_LEQUAL;
                }
                mesh->setShader(shader, 0);
            }
        }
    }
}

void SetModelInstanceExclusionMask(MeshInstance* instance, int value)
{
    if (instance) instance->setExclusionMask(value);
}

/* The world's own float pair at 0x74 and 0x78. Both are assigned from the one
   argument, and only zero is rejected, so the guard is a "leave it alone"
   rather than a range check. The assertion at 3d.cpp:651 is what names the
   receiver pWorld. */
void WorldSetRenderRange(World* world, float value)
{
    assert(world);
    if (value != 0.0f) {
        world->render_range = value;
    }
}

/* Pushes the world's view distance into the camera as the far clip plane; the
   near plane is the fixed 62.5 the original materialises inline. The guard is
   against a denormal-scale epsilon rather than zero, so a distance that has
   collapsed leaves the camera as it was. */
void WorldSetFarClip(World* world, float distance)
{
    assert(world);
    if (distance > 5.9604644775390625e-008) {
        world->camera->setClipRange(62.5, distance);
        RefreshFogRanges(*world);
    }
}

/* The matching reader. Both planes come back, and only the far one is
   returned, which is the same asymmetry the setter has. A world without a
   camera answers zero rather than reading through it. */
double WorldGetFarClip(World* world)
{
    if (world != nullptr && world->camera != nullptr) return world->camera->getClipRange().second;
    return 0;
}

void SetSceneAmbientLightWhite(WorldScene* scene)
{
    assert(scene != 0);
    scene->setAmbientLight(1.0f, 1.0f, 1.0f);
}

bool PointInsideFrustum(const glm::vec3& point, std::span<const W8Plane, 6> planes)
{
    return SphereInsideFrustum(point, 0.0f, planes);
}

/* Point-in-triangle test by even-odd crossing on the plane perpendicular to
   `axis`: the two remaining components (axis+1, axis+2 mod 3) project the
   triangle and query point, and each edge straddling the point's second
   coordinate toggles the inside flag when its interpolation crosses the
   first. */
bool PointInsideTriangle(std::span<const glm::vec3, 3> vertices, unsigned axis,
                         const glm::vec3& point)
{
    return triangleProjection::contains(vertices, axis, point,
                                        triangleProjection::Boundary::visibility);
}

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

bool SphereInsideFrustum(const glm::vec3& point, float radius, std::span<const W8Plane, 6> planes)
{
    const auto coefficients = planes | std::views::transform([](const W8Plane& plane) {
                                  return glm::vec4(plane.normal, plane.w);
                              });
    return planeMath::containsSphere(coefficients, point, radius);
}

bool BoundsInsideFrustum(const W8OctRegionVolume& volume, const BoundingBox& bounds)
{
    for (unsigned corner = 0; corner < 8; ++corner)
        if (PointInsideFrustum(bounds.corner(corner), volume.m_planes)) return true;
    for (unsigned corner = 1; corner < 9; ++corner)
        if (bounds.containsHalfOpen(volume.m_points[corner])) return true;
    return false;
}

/* glm::mat4::Set emitted for this TU (BakeInstanceVertexLighting's
   transform builds); the primary template lives in srMath.h. */
