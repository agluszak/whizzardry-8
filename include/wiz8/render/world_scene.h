#pragma once

#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "wiz8/render/camera.h"
#include "wiz8/render/draw_inputs.h"

class GpuRenderer;
class MeshInstance;
class Light;
class ParticleSystem;
class GroundShadow;
class FogParams;
class ClipPlane;

// Geometry has explicit owners; entities lend their concrete drawables for as
// long as they are attached. No traversal tree or polymorphic discovery exists.
class WorldScene {
public:
    WorldScene();
    ~WorldScene();
    WorldScene(const WorldScene&) = delete;
    WorldScene& operator=(const WorldScene&) = delete;

    Camera3D camera;
    glm::vec3 ambient_light{0.2f};
    glm::vec3 fog_color{0.1f, 0.2f, 0.4f};
    std::vector<std::unique_ptr<MeshInstance>> static_meshes;
    std::vector<MeshInstance*> dynamic_meshes;
    std::vector<Light*> lights;
    std::vector<ParticleSystem*> particles;
    std::vector<GroundShadow*> shadows;
    std::vector<FogParams*> fogs;
    std::vector<std::unique_ptr<ClipPlane>> clip_planes;
    unsigned static_exclusion_mask = 1;
    unsigned submitted_polygons = 0;

    void attach(MeshInstance&);
    void attach(Light&);
    void attach(ParticleSystem&);
    void attach(GroundShadow&);
    void attach(FogParams&);
    void detach(MeshInstance&);
    void detach(Light&);
    void detach(ParticleSystem&);
    void detach(GroundShadow&);
    void detach(FogParams&);
    MeshInstance* addStaticMesh(std::unique_ptr<MeshInstance> mesh);
    // Draws the scene and tests `picks` against its pickable triangles.
    void render(GpuRenderer& renderer, Camera3D* view, std::span<Pick> picks = {});
    std::size_t renderableCount() const;

    glm::vec3 getAmbientLight() const { return ambient_light; }
    glm::vec3 getFogColor() const { return fog_color; }
    void setAmbientLight(const glm::vec3& value) { ambient_light = value; }
    void setAmbientLight(float r, float g, float b) { ambient_light = {r, g, b}; }
    void setFogColor(const glm::vec3& value) { fog_color = value; }
    void setFogColor(float r, float g, float b) { fog_color = {r, g, b}; }

private:
    void drawStaticMeshes(GpuRenderer& renderer, const DrawTransform& view, const ScenePass& pass);
};
