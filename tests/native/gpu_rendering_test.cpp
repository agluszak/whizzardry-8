#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <vsg/maths/vec4.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/Builder.h>

#include "surface_pixels.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/surface_storage.h"
#include "wiz8/texture_animation.h"

namespace {
struct Fixture {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<Renderer> renderer;
    Fixture()
    {
        SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        window.reset(
            SDL_CreateWindow("VSG rendering", 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN));
        REQUIRE(window);
        renderer = std::make_unique<Renderer>(window.get());
    }
    ~Fixture()
    {
        renderer.reset();
        window.reset();
        SDL_Quit();
    }
    unsigned pixel(int x, int y)
    {
        auto* surface = renderer->readback();
        REQUIRE(surface);
        return ReadARGB(*surface, x, y) & 0xffffff;
    }
};
std::shared_ptr<Texture> solidTexture(unsigned color)
{
    auto pixels = CreateCpuStorage(1, 1, SDL_PIXELFORMAT_BGRA32);
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), nullptr, color));
    return std::make_shared<Texture>(std::shared_ptr<SDL_Surface>(std::move(pixels)));
}
}

TEST_CASE("VSG frame owns submitted overlay resources through completion")
{
    Fixture fixture;
    auto texture = solidTexture(0xffff0000);
    OverlayQuad quad;
    quad.size = {320, 480};
    quad.texture = texture;
    fixture.renderer->beginFrame({0, 0, 1, 1});
    fixture.renderer->drawOverlayQuad(quad);
    texture = solidTexture(0xff00ff00);
    quad.texture = texture;
    quad.position.x = 320;
    fixture.renderer->drawOverlayQuad(quad);
    texture.reset();
    quad.texture.reset();
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0xff0000);
    REQUIRE(fixture.pixel(48, 32) == 0x00ff00);
    fixture.renderer->beginFrame({0, 0, 1, 1});
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0x0000ff);
}

TEST_CASE("Two VSG views share ordinary and Wizardry geometry in one world")
{
    Fixture fixture;
    WorldScene scene;
    auto builder = vsg::Builder::create();
    vsg::GeometryInfo geometry;
    geometry.position = {-1.2f, 0, 3};
    geometry.dx = {-1.5f, 0, 0};
    geometry.dy = {0, 2, 0};
    geometry.color = vsg::vec4(1, 0, 0, 1);
    vsg::StateInfo material;
    material.lighting = false;
    material.two_sided = true;
    auto ordinary = builder->createQuad(geometry, material);
    scene.root->addChild(ordinary);
    auto game_material = std::make_shared<Material>();
    game_material->setAmbient(glm::vec4(0));
    game_material->setDiffuse(glm::vec4(0, 0, 0, 1));
    game_material->setSpecular(glm::vec4(0));
    game_material->setEmissive(glm::vec4(0, 1, 0, 1));
    auto game_mesh = std::make_shared<Mesh>(2, 4);
    game_mesh->getPolyVertex()[0] = {0, 2, 1};
    game_mesh->getPolyVertex()[1] = {0, 3, 2};
    auto* positions = game_mesh->getVertexLoc();
    positions[0] = {.45f, -1, 3};
    positions[1] = {1.95f, -1, 3};
    positions[2] = {1.95f, 1, 3};
    positions[3] = {.45f, 1, 3};
    game_mesh->setMaterial(game_material, 0, Mesh::SIDE_FRONT);
    MeshInstance game_instance(&scene);
    game_instance.setModel(game_mesh);
    scene.camera.setClipRange(1, 10);
    scene.camera.setViewPlane(Camera3D::Rect{-1, -1, 1, 1}, 1);
    fixture.renderer->beginFrame({0, 0, 1, 1});
    const auto number = fixture.renderer->frameNumber();
    auto left = scene.createView(fixture.renderer->context(), &scene.camera, {},
                                 vsg::ViewportState::create(0, 0, 32, 64), number);
    auto right = scene.createView(fixture.renderer->context(), &scene.camera, {},
                                  vsg::ViewportState::create(32, 0, 32, 64), number);
    fixture.renderer->addView(left);
    fixture.renderer->addView(right);
    fixture.renderer->endFrame(false, true);
    // Both material layouts share each view and its depth attachment. VSG's
    // standard unlit shader multiplies RGB by .9; Wizardry's emissive material
    // supplies full green without changing the ordinary subgraph's state.
    const auto ordinary_pixel = fixture.pixel(10, 32);
    REQUIRE((ordinary_pixel >> 16) >= 200);
    REQUIRE((ordinary_pixel & 0xffff) == 0);
    REQUIRE(fixture.pixel(42, 32) == ordinary_pixel);
    REQUIRE(fixture.pixel(22, 32) == 0x00ff00);
    REQUIRE(fixture.pixel(54, 32) == 0x00ff00);
    REQUIRE(scene.root->children.back() == ordinary);
}

TEST_CASE("Wizardry shaders retain live lighting, fog and animated textures across views")
{
    Fixture fixture;
    WorldScene scene;
    scene.ambient_light = glm::vec3(.25f);
    scene.fog_color = {0, 0, 1};
    scene.camera.setProjectionType(Camera3D::PROJECT_ORTHOGRAPHIC);
    scene.camera.setClipRange(1, 10);
    scene.camera.setViewPlane(Camera3D::Rect{-1, -1, 1, 1}, 1);
    auto second_camera = scene.camera;
    second_camera.transform.setLocation(.25, 0, 0);

    auto material = std::make_shared<Material>();
    material->setAmbient(glm::vec4(1));
    material->setDiffuse(glm::vec4(1));
    material->setSpecular(glm::vec4(0));
    material->setEmissive(glm::vec4(0));
    TexturePlayback playback;
    playback.animation_mode = W8_TEXTURE_ANIM_MANUAL;
    playback.textures = {solidTexture(0xffff0000), solidTexture(0xff00ff00)};
    auto animated_texture = std::make_shared<Texture>(std::move(playback));

    auto mesh = std::make_shared<Mesh>(2, 4);
    mesh->getPolyVertex()[0] = {0, 2, 1};
    mesh->getPolyVertex()[1] = {0, 3, 2};
    mesh->InitializeVertexFrames(2);
    const std::array<short, 12> first{-1, -1, 3, 1, -1, 3, 1, 1, 3, -1, 1, 3};
    const std::array<short, 12> next{0, -1, 3, 2, -1, 3, 2, 1, 3, 0, 1, 3};
    std::ranges::copy(first, mesh->GetVertex(0));
    std::ranges::copy(next, mesh->GetVertex(1));
    mesh->FinalizeVertexFrame(0);
    mesh->FinalizeVertexFrame(1);
    mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
    mesh->setTexture(animated_texture, 0, 0);
    DrawShader shader;
    shader.value |= DrawShader::MASK_TEXTURING | (DrawShader::FOG_ENABLE << DrawShader::FOG_SHIFT);
    mesh->setShader(shader, 0);
    MeshInstance instance(&scene);
    instance.setModel(mesh);
    instance.frame_index = 0;
    instance.frame_interpolation = .25f;
    const auto sampled = instance.sampledPosition(*mesh, 0);
    REQUIRE(sampled == glm::vec3(-.75f, -1, 3));

    // Thirty-three active lights exercise the real shader beyond the former
    // prototype's limit. Their sum is exactly 33/64 in binary floating point.
    std::vector<std::unique_ptr<Light>> lights;
    for (int index = 0; index < 33; ++index) {
        auto light = std::make_unique<Light>(&scene, Light::PRESET_DIRECTIONAL);
        light->ambient = glm::vec3(0);
        light->diffuse = glm::vec3(1.f / 64);
        light->specular = glm::vec3(0);
        light->derived_flags = 0x1234;
        light->eye_location = {7, 8, 9};
        light->scaled_diffuse = glm::vec4(6);
        lights.push_back(std::move(light));
    }
    FogParams fog(&scene);
    fog.setRange(0, 1);
    fog.setDensity(.25f);

    const auto render = [&](const SceneFrame& frame) {
        fixture.renderer->beginFrame({0, 0, 0, 1});
        const auto number = fixture.renderer->frameNumber();
        fixture.renderer->addView(scene.createView(fixture.renderer->context(), &scene.camera,
                                                   frame, vsg::ViewportState::create(0, 0, 32, 64),
                                                   number));
        fixture.renderer->addView(scene.createView(fixture.renderer->context(), &second_camera,
                                                   frame, vsg::ViewportState::create(32, 0, 32, 64),
                                                   number));
        fixture.renderer->endFrame(false, true);
    };
    const auto expect = [&](unsigned expected) {
        for (int x : {16, 48}) {
            const auto actual = fixture.pixel(x, 32);
            INFO("pixel " << x << " RGB " << actual << ", expected " << expected);
            for (unsigned shift : {0u, 8u, 16u}) {
                const auto channel = static_cast<int>((actual >> shift) & 255u);
                const auto target = static_cast<int>((expected >> shift) & 255u);
                REQUIRE(channel >= target - 2);
                REQUIRE(channel <= target + 2);
            }
        }
    };
    render({});
    // (ambient 1/4 + diffuse 33/64) * red, then 1/4 blue fog.
    // Fog must survive even though the secondary RGB gradient is disabled.
    expect(0x920040);
    animated_texture->SetFrame(1);
    material->setDiffuse(glm::vec4(.5f, .5f, .5f, 1));
    render({.environment_offset = glm::vec3(.125f), .brightness = .5f});
    // Ambient = (1/4 + 1/8)/2; diffuse = (33/64)/2 * 1/2.
    // Green texture and 1/4 blue fog produce RGB (0, 61, 64).
    expect(0x003d40);
    REQUIRE(animated_texture->playback->frame == 1);
    REQUIRE(instance.frame_index == 0);
    REQUIRE(instance.frame_interpolation == .25f);
    REQUIRE(instance.sampledPosition(*mesh, 0) == sampled);
    for (const auto& light : lights) {
        REQUIRE(light->derived_flags == 0x1234);
        REQUIRE(light->eye_location == glm::vec3(7, 8, 9));
        REQUIRE(light->scaled_diffuse == glm::vec4(6));
    }
}
