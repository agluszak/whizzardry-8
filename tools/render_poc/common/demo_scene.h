#pragma once

// A deterministic scene built through the game's own render types, and the
// library-neutral per-frame plan every backend consumes.
//
// The scene covers what the renderer evaluation has to preserve:
//  - a floor with per-polygon textures and baked vertex light (DIG colours);
//  - a creature animated through the mesh's compressed frame codec,
//    interpolated between frames, with specular highlights;
//  - two crossing translucent meshes whose triangles must interleave, which
//    only a global triangle sort can draw correctly;
//  - a scrolling material whose UV-seam corners keep their raw coordinates;
//  - point (3ds Max ranges), spot and directional lights, fog and depth cue.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "resident_mesh.h"
#include "shading_block.h"
#include "transparent_order.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/transform.h"

class FogParams;
class Light;
class Mesh;
class Texture;
struct SDL_Surface;

namespace render_poc {

struct DemoInstance {
    std::string name;
    std::shared_ptr<Mesh> mesh;
    ResidentMesh resident;
    Transform3D transform;
    unsigned frame = 0;
    float interpolation = 0;
    std::uint32_t exclusion_mask = 0;
};

class DemoScene {
public:
    DemoScene();
    ~DemoScene();
    DemoScene(const DemoScene&) = delete;
    DemoScene& operator=(const DemoScene&) = delete;

    // Advances camera, lights, scrolling and animation to `seconds`.
    void update(double seconds);

    std::vector<DemoInstance> instances;
    std::vector<std::unique_ptr<Light>> lights;
    std::vector<std::unique_ptr<FogParams>> fogs;
    std::vector<std::shared_ptr<Texture>> textures;
    Camera3D camera;
    ShadingEnvironment environment;
    glm::vec4 fog_color{0.25f, 0.3f, 0.4f, 1};
    glm::vec4 clear_color{0.1f, 0.12f, 0.16f, 1};
};

// The view and projection Camera3D::applyView loads into the native renderer.
struct CameraMatrices {
    glm::mat4 view;
    glm::mat4 projection; // OpenGL clip conventions, as glm::frustum
};
CameraMatrices cameraMatrices(const Camera3D& camera);

// Everything one draw of one batch needs.
struct PlannedBatch {
    std::uint32_t pass = 0;
    std::uint32_t batch = 0;
    DrawBlock draw{};
};

struct PlannedDraw {
    const DemoInstance* instance = nullptr;
    glm::mat4 model_view{1};
    glm::mat3 normal_matrix{1};
    // One shading block per pass (material tables differ per pass).
    std::vector<ShadingBlock> shading;
    std::vector<PlannedBatch> batches;
    // Draw block per (pass, batch) for the ordered transparent runs.
    std::vector<std::vector<DrawBlock>> draw_blocks;
};

struct FramePlan {
    CameraMatrices camera;
    std::vector<PlannedDraw> opaque;
    std::vector<PlannedDraw> sorted;
    TransparentOrder transparent; // runs index into `sorted`
    glm::vec4 clear_color{0};
};

// Per-object CPU work that remains in any library: light/fog selection,
// material packing and the global transparent order.
FramePlan planFrame(DemoScene& scene);

// Textures are SDL surfaces owned by game Texture objects; backends upload them
// as RGBA8 (R first in memory).
std::vector<std::uint8_t> rgba8Pixels(SDL_Surface& surface);

} // namespace render_poc
