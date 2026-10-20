#pragma once

// A deterministic scene made of the game's own objects: MeshInstances sharing
// Mesh models, Lights, FogParams and a Camera3D. The scene owns them as the
// game's World does; a renderer only borrows them.
//
// It covers what a renderer replacement has to preserve:
//  - a floor with per-polygon textures and baked vertex light (DIG colours);
//  - a herd of creatures of any size sharing one Mesh, animated through its
//    compressed frame codec and interpolated between frames, with specular
//    highlights;
//  - a lantern attached to the lead creature's mapped "hand" vertex, carrying
//    the point light, so both follow the animation;
//  - a creature that fades out, which switches it to sorted, blended drawing;
//  - two crossing translucent panes whose triangles must interleave;
//  - a scrolling material whose UV-seam corners keep their raw coordinates;
//  - point (3ds Max ranges), spot and directional lights, a light whose group
//    the floor excludes, fog and depth cue.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "shading_block.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/mesh_instance.h"

class FogParams;
class Light;
class Texture;
struct SDL_Surface;

namespace render_poc {

class DemoScene {
public:
    static constexpr short kHandKey = 7;
    static constexpr std::uint32_t kWardGroup = 0x2;

    explicit DemoScene(int herd_size = 1);
    ~DemoScene();
    DemoScene(const DemoScene&) = delete;
    DemoScene& operator=(const DemoScene&) = delete;

    // Advances camera, the leader's walk, animation frames and fading to `seconds`.
    void update(double seconds);

    // A creature of a kind not loaded at start-up, for runtime attach and detach.
    MeshInstance& addVisitor();
    void removeVisitor(MeshInstance& visitor);

    std::vector<std::shared_ptr<Texture>> textures;
    std::vector<std::unique_ptr<MeshInstance>> instances;
    MeshInstance* floor = nullptr;
    MeshInstance* falls = nullptr;
    MeshInstance* lantern = nullptr;  // attached to herd.front() at kHandKey
    MeshInstance* fading = nullptr;   // a herd member that fades in and out
    std::vector<MeshInstance*> herd;  // herd.front() is the leader
    std::vector<std::unique_ptr<Light>> lights;
    Light* lamp = nullptr; // attached to the lantern
    std::vector<std::unique_ptr<FogParams>> fogs;
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

// Textures are SDL surfaces owned by game Texture objects; backends upload them
// as RGBA8 (R first in memory).
std::vector<std::uint8_t> rgba8Pixels(SDL_Surface& surface);

} // namespace render_poc
