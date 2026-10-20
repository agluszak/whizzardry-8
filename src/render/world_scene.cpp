#include "wiz8/render/world_scene.h"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/ground_shadow.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_system.h"

WorldScene::WorldScene() = default;

WorldScene::~WorldScene()
{
    for (auto* item : dynamic_meshes) item->scene = nullptr;
    for (auto* item : lights) item->scene = nullptr;
    for (auto* item : particles) item->scene = nullptr;
    for (auto* item : shadows) item->scene = nullptr;
    for (auto* item : fogs) item->scene = nullptr;
}

MeshInstance* WorldScene::addStaticMesh(std::unique_ptr<MeshInstance> mesh)
{
    mesh->setScene(nullptr);
    auto* result = mesh.get();
    static_meshes.push_back(std::move(mesh));
    return result;
}

std::size_t WorldScene::renderableCount() const
{
    return static_meshes.size() + dynamic_meshes.size() + lights.size() + particles.size() +
           shadows.size();
}

void WorldScene::drawStaticMeshes(GpuRenderer& renderer, const DrawTransform& view,
                                  const ScenePass& pass)
{
    submitted_polygons = 0;
    for (const auto& instance : static_meshes) {
        auto* model = instance->getModel();
        if (!instance->visible || !model || !model->isVisible(view)) continue;
        for (auto* mesh_part : meshParts(model, instance->draw_parts)) {
            submitted_polygons += mesh_part->getActivePolygonTable(0)
                                      ? mesh_part->getActivePolygonCount()
                                      : mesh_part->polygon_count;
            MeshState state{.transform = view, .scene = pass};
            state.scene.exclusion_mask |= static_exclusion_mask;
            // Sorted level geometry is never picked.
            if ((mesh_part->flags & W8_MESH_SORTED_RENDERING) != 0) state.scene.picks = {};
            if (mesh_part->vertex_lighting_ready) {
                state.scene.ambient_light = glm::vec4(0);
                mesh_part->SetAmbientColor(glm::vec3(pass.ambient_light),
                                           pass.frame.environment_offset, pass.frame.brightness);
            }
            auto mesh = mesh_part->getTriMesh();
            if (!mesh.active_polygons)
                if (const auto active = mesh_part->GetActivePolygons(-1, false))
                    mesh.active_polygons = active;
            mesh_part->renderTriMesh(renderer, mesh, state);
        }
    }
}

void WorldScene::render(GpuRenderer& renderer, Camera3D* view, const SceneFrame& frame,
                        std::span<Pick> picks)
{
    auto frame_lights = lights;
    std::erase_if(frame_lights,
                  [](const auto* light) { return !light->visible || light->intensity == 0; });
    auto frame_fogs = fogs;
    std::erase_if(frame_fogs, [](const auto* fog) { return !fog->visible; });
    const auto frame_meshes = dynamic_meshes;
    const auto frame_shadows = shadows;
    const auto frame_particles = particles;
    // Without a camera the scene draws in the identity view with no depth cue.
    auto scene_view = view ? view->drawTransform() : DrawTransform{};
    for (auto* light : frame_lights) light->beginPass(scene_view, frame.brightness);
    std::erase_if(frame_lights, [](const auto* light) {
        return !(light->derived_flags & Light::DERIVED_ACTIVE);
    });
    const ScenePass pass{.frame = frame,
                         .ambient_light = glm::vec4(ambient_light, 0),
                         .fog_color = glm::clamp(glm::vec4(fog_color, 0), 0.0f, 1.0f),
                         .lights = frame_lights,
                         .fogs = frame_fogs,
                         .depth_cue = view ? view->depthCue() : DepthCue{},
                         .picks = picks};
    std::vector<glm::vec4> eye_planes;
    for (const auto& plane : clip_planes)
        if (plane->visible)
            eye_planes.push_back(
                eyeSpaceClipPlane(scene_view.model_view * plane->transform.matrix<glm::mat4>(),
                                  plane->getClipPlane()));
    scene_view.user_clip_planes = eye_planes;
    drawStaticMeshes(renderer, scene_view, pass);
    for (auto it = frame_meshes.rbegin(); it != frame_meshes.rend(); ++it) {
        auto* mesh = *it;
        if (mesh->visible && mesh->getModel() &&
            !(mesh->getModel()->flags & W8_MESH_SORTED_RENDERING))
            mesh->draw(renderer, scene_view, pass);
    }
    for (auto* mesh : frame_meshes)
        if (mesh->visible && mesh->getModel() &&
            (mesh->getModel()->flags & W8_MESH_SORTED_RENDERING))
            mesh->draw(renderer, scene_view, pass);
    for (auto* shadow : frame_shadows)
        if (shadow->visible) shadow->draw(renderer, scene_view, pass);
    for (auto* particle : frame_particles)
        if (particle->visible && particle->traversal_enabled &&
            (particle->emitting || particle->active_particle_count))
            particle->SubmitToRenderer(&renderer, scene_view, pass);
    renderer.flushRenderers();
}

void WorldScene::attach(MeshInstance& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    dynamic_meshes.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(MeshInstance& item)
{
    std::erase(dynamic_meshes, &item);
    if (item.scene == this) item.scene = nullptr;
}
void MeshInstance::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}

void WorldScene::attach(Light& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    lights.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(Light& item)
{
    std::erase(lights, &item);
    if (item.scene == this) item.scene = nullptr;
}
void Light::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}

void WorldScene::attach(ParticleSystem& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    particles.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(ParticleSystem& item)
{
    std::erase(particles, &item);
    if (item.scene == this) item.scene = nullptr;
}
void ParticleSystem::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}

void WorldScene::attach(GroundShadow& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    shadows.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(GroundShadow& item)
{
    std::erase(shadows, &item);
    if (item.scene == this) item.scene = nullptr;
}
void GroundShadow::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}

void WorldScene::attach(FogParams& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    fogs.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(FogParams& item)
{
    std::erase(fogs, &item);
    if (item.scene == this) item.scene = nullptr;
}
void FogParams::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}
