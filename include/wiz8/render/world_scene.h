#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <vsg/app/View.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/ViewportState.h>

#include "wiz8/render/camera.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/map_view.h"

class MeshInstance;
class Light;
class ParticleSystem;
class GroundShadow;
class FogParams;
class ClipPlane;
namespace render {
class RenderContext;
}
namespace vsg {
class Camera;
}

class WorldScene {
public:
    WorldScene();
    ~WorldScene();
    WorldScene(const WorldScene&) = delete;
    WorldScene& operator=(const WorldScene&) = delete;

    Camera3D camera;
    glm::vec3 ambient_light{0.2f};
    glm::vec3 fog_color{0.1f, 0.2f, 0.4f};
    // Ownership and named gameplay queries. Render parentage lives in root.
    std::vector<std::unique_ptr<MeshInstance>> static_meshes;
    std::vector<MeshInstance*> dynamic_meshes;
    std::vector<Light*> lights;
    std::vector<ParticleSystem*> particles;
    std::vector<GroundShadow*> shadows;
    std::vector<FogParams*> fogs;
    std::vector<std::unique_ptr<ClipPlane>> clip_planes;
    unsigned static_exclusion_mask = 1;
    unsigned submitted_polygons = 0;
    // The automap's independent view of static_meshes.
    MapView map;

    // Ordinary VSG subgraphs may join this group directly. Wizardry's material
    // descriptors are scoped to its own children, never imposed on this root.
    vsg::ref_ptr<vsg::Group> root;

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
    vsg::ref_ptr<vsg::View> createView(render::RenderContext&, Camera3D*, const SceneFrame&,
                                       vsg::ref_ptr<vsg::ViewportState>,
                                       std::uint64_t frame_number);
    // The map view: static geometry only, drawn with the map's own camera and appearance.
    vsg::ref_ptr<vsg::View> createMapView(render::RenderContext&, const SceneFrame&,
                                          vsg::ref_ptr<vsg::ViewportState>,
                                          std::uint64_t frame_number);
    void pick(const vsg::Camera&, std::span<Pick>) const;
    void advanceVisuals(std::chrono::milliseconds now);

    glm::vec3 getAmbientLight() const { return ambient_light; }
    glm::vec3 getFogColor() const { return fog_color; }
    void setAmbientLight(const glm::vec3& value) { ambient_light = value; }
    void setAmbientLight(float r, float g, float b) { ambient_light = {r, g, b}; }
    void setFogColor(const glm::vec3& value) { fog_color = value; }
    void setFogColor(float r, float g, float b) { fog_color = {r, g, b}; }

private:
    struct CachedView;
    std::vector<std::unique_ptr<CachedView>> views;
    vsg::ref_ptr<vsg::StateGroup> game_root;
    vsg::ref_ptr<vsg::Group> objects;
    vsg::ref_ptr<vsg::StateGroup> map_root;
    std::uint64_t prepared_frame = ~std::uint64_t{0};
    void prepare(render::RenderContext&, const SceneFrame&, std::uint64_t frame_number);
    vsg::ref_ptr<vsg::View> cachedView(render::RenderContext&, Camera3D* source,
                                       vsg::ref_ptr<vsg::Node> graph, const ScenePass&,
                                       vsg::ref_ptr<vsg::ViewportState>,
                                       std::uint64_t frame_number);
};
