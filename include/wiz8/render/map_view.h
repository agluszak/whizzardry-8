#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include "wiz8/render/camera.h"
#include "wiz8/render/draw_inputs.h"

class Mesh;
class MeshInstance;
namespace render {
class RenderContext;
}

/* The automap's view of a world scene's static geometry. It shares the static
   meshes' topology, materials and textures with the gameplay view and owns what
   only the map draws: its orthographic camera, the polygons MapFilters.txt leaves
   visible, and the explored vertices that appear lit. Actors, effects and lights
   never join its graph, and nothing here changes the shared meshes. */
class MapView {
public:
    MapView();
    ~MapView();
    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    // Looks straight down; the automap places it and sets its layer clip.
    Camera3D camera;

    // Hide static polygons whose texture name matches one of these, ignoring case.
    void excludeTextures(std::vector<std::string> names);
    // Light every vertex of a static instance within `range` of `center`.
    void reveal(const MeshInstance& instance, const glm::vec3& center, float range);
    void forgetExplored();

    // The map graph over these static instances, updated for this pass.
    vsg::ref_ptr<vsg::Group> prepare(render::RenderContext& context,
                                     std::span<const std::unique_ptr<MeshInstance>> instances,
                                     const ScenePass& pass);

private:
    struct Surface;
    Surface& surface(const Mesh& mesh);
    // Derived from the static meshes it is keyed by, which the scene owns.
    std::unordered_map<const Mesh*, std::unique_ptr<Surface>> surfaces;
    std::vector<std::string> excluded;
    std::uint64_t exclusion_revision = 0;
    vsg::ref_ptr<vsg::Group> graph;
};
