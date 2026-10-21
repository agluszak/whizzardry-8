#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <span>
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
#include <glm/ext/vector_int3.hpp>
#include <vsg/core/Array.h>
#include <vsg/io/write.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/Builder.h>

#include "surface_pixels.h"
#include "temporary_directory.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/native_content.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_scene.h"
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
    OverlayQuad second;
    second.size = quad.size;
    second.texture = texture;
    second.position.x = 320;
    fixture.renderer->drawOverlayQuad(second);
    texture.reset();
    quad.texture.reset();
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0xff0000);
    REQUIRE(fixture.pixel(48, 32) == 0x00ff00);
    fixture.renderer->beginFrame({0, 0, 1, 1});
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0x0000ff);
    const auto node = second.node->children.front().node;
    second.position = {0, 0};
    second.size = {640, 480};
    second.color = {0, 0, 1, 1};
    second.texture.reset();
    fixture.renderer->beginFrame();
    fixture.renderer->drawOverlayQuad(second);
    fixture.renderer->endFrame(false, true);
    REQUIRE(second.node->children.front().node == node);
    REQUIRE(fixture.pixel(16, 32) == 0x0000ff);
    second.visible = false;
    fixture.renderer->beginFrame({1, 0, 0, 1});
    fixture.renderer->drawOverlayQuad(second);
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0xff0000);
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

TEST_CASE("The map view draws explored static geometry without changing gameplay views")
{
    Fixture fixture;
    WorldScene scene;
    scene.ambient_light = glm::vec3(.25f);
    const auto look_down = [](Camera3D& camera) {
        camera.transform.setRotation(std::numbers::pi / 2, 0.0, 0.0);
        camera.transform.setLocation(0, 5, 0);
        camera.setClipRange(1, 10);
    };
    look_down(scene.camera);
    scene.camera.setViewPlane(Camera3D::Rect{-.5, -.5, .5, .5}, 1);
    look_down(scene.map.camera);
    scene.map.camera.setViewPlane(Camera3D::Rect{-2.5, -2.5, 2.5, 2.5}, 1);

    // Two static floor tiles; the second one's texture is excluded from the map.
    auto material = std::make_shared<Material>();
    material->setAmbient(glm::vec4(1));
    material->setDiffuse(glm::vec4(1));
    material->setSpecular(glm::vec4(0));
    material->setEmissive(glm::vec4(0));
    auto floor = std::make_shared<Mesh>(4, 8);
    const std::array<glm::vec3, 8> corners{{{-2, 0, -.5f},
                                            {-1, 0, -.5f},
                                            {-1, 0, .5f},
                                            {-2, 0, .5f},
                                            {1, 0, -.5f},
                                            {2, 0, -.5f},
                                            {2, 0, .5f},
                                            {1, 0, .5f}}};
    std::ranges::copy(corners, floor->getVertexLoc());
    const std::array<glm::ivec3, 4> triangles{{{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}}};
    std::ranges::copy(triangles, floor->getPolyVertex());
    floor->enable(Mesh::CONTROL_NO_FRONT_CULL);
    floor->setMaterial(material, 0, Mesh::SIDE_FRONT);
    auto hidden = solidTexture(0xffffffff);
    hidden->setName("Hidden");
    (*floor->getPolygonTextureTable(0, 0, true))[2] = hidden;
    (*floor->getPolygonTextureTable(0, 0, true))[3] = hidden;
    floor->GetVertexSunlight(true);
    std::ranges::fill(floor->GetVertexLights(true), glm::vec3(0, .25f, 0));
    auto owned_floor = std::make_unique<MeshInstance>();
    owned_floor->setModel(floor);
    const auto& floor_instance = *scene.addStaticMesh(std::move(owned_floor));

    // An actor between the tiles, above the floor.
    auto actor_material = std::make_shared<Material>();
    actor_material->setAmbient(glm::vec4(0));
    actor_material->setDiffuse(glm::vec4(0, 0, 0, 1));
    actor_material->setSpecular(glm::vec4(0));
    actor_material->setEmissive(glm::vec4(1, 0, 0, 1));
    auto actor_mesh = std::make_shared<Mesh>(2, 4);
    const std::array<glm::vec3, 4> actor_corners{
        {{-.5f, 1, -.5f}, {.5f, 1, -.5f}, {.5f, 1, .5f}, {-.5f, 1, .5f}}};
    std::ranges::copy(actor_corners, actor_mesh->getVertexLoc());
    std::ranges::copy(std::span(triangles).first(2), actor_mesh->getPolyVertex());
    actor_mesh->enable(Mesh::CONTROL_NO_FRONT_CULL);
    actor_mesh->setMaterial(actor_material, 0, Mesh::SIDE_FRONT);
    MeshInstance actor(&scene);
    actor.setModel(actor_mesh);

    const auto render = [&](bool map) {
        fixture.renderer->beginFrame({0, 0, 1, 1});
        const auto viewport = vsg::ViewportState::create(0, 0, 64, 64);
        const auto number = fixture.renderer->frameNumber();
        auto& context = fixture.renderer->context();
        fixture.renderer->addView(
            map ? scene.createMapView(context, {}, viewport, number)
                : scene.createView(context, &scene.camera, {}, viewport, number));
        fixture.renderer->endFrame(false, true);
        // The left tile, the actor and the right tile.
        return std::array{fixture.pixel(12, 32), fixture.pixel(32, 32), fixture.pixel(51, 32)};
    };
    const auto gameplay = render(false);
    // Ambient 1/4 plus the baked green 1/4 on both tiles; the actor is red.
    for (const auto tile : {gameplay[0], gameplay[2]}) {
        INFO("tile RGB " << tile);
        REQUIRE((tile >> 16) >= 62);
        REQUIRE((tile >> 16) <= 66);
        REQUIRE(((tile >> 8) & 255) >= 126);
        REQUIRE(((tile >> 8) & 255) <= 130);
    }
    REQUIRE(gameplay[1] == 0xff0000);

    // Only explored vertices are lit. The actor is not part of the map.
    scene.map.reveal(floor_instance, {-1.5f, 0, 0}, 1);
    REQUIRE(render(true) == std::array{0xffffffu, 0x0000ffu, 0x000000u});
    // Excluded textures leave the background, even when explored.
    scene.map.reveal(floor_instance, {1.5f, 0, 0}, 1);
    scene.map.excludeTextures({"HIDDEN"});
    REQUIRE(render(true) == std::array{0xffffffu, 0x0000ffu, 0x0000ffu});

    // Gameplay needs no restore step after the map.
    REQUIRE(render(false) == gameplay);
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

    // Grow the live light list after compiling both views: this exercises
    // descriptor-buffer replacement as well as counts beyond the old cap.
    std::vector<std::unique_ptr<Light>> lights;
    const auto add_lights = [&](int count) {
        for (int index = 0; index < count; ++index) {
            auto light = std::make_unique<Light>(&scene, Light::PRESET_DIRECTIONAL);
            light->ambient = glm::vec3(0);
            light->diffuse = glm::vec3(1.f / 64);
            light->specular = glm::vec3(0);
            lights.push_back(std::move(light));
        }
    };
    add_lights(1);
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
    // (ambient 1/4 + diffuse 1/64) * red, then 1/4 blue fog.
    expect(0x330040);
    add_lights(32);
    render({});
    // Thirty-three active lights sum to 33/64 exactly in binary arithmetic.
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
        REQUIRE(light->diffuse == glm::vec3(1.f / 64));
        REQUIRE(light->intensity == 1.f);
    }
}

TEST_CASE("Wizardry globally sorts transparent triangles around opaque VSG and depth-only draws")
{
    Fixture fixture;
    WorldScene scene;
    scene.camera.setProjectionType(Camera3D::PROJECT_ORTHOGRAPHIC);
    scene.camera.setClipRange(1, 10);
    scene.camera.setViewPlane(Camera3D::Rect{-2, -2, 2, 2}, 1);

    auto builder = vsg::Builder::create();
    vsg::GeometryInfo ordinary_geometry;
    ordinary_geometry.position = {-1, 0, 3};
    ordinary_geometry.dx = {-.5f, 0, 0};
    ordinary_geometry.dy = {0, 2, 0};
    ordinary_geometry.color = vsg::vec4(0, 1, 0, 1);
    vsg::StateInfo ordinary_state;
    ordinary_state.lighting = false;
    ordinary_state.two_sided = true;
    scene.root->addChild(builder->createQuad(ordinary_geometry, ordinary_state));

    using Quad = std::array<glm::vec3, 4>;
    const auto quad = [](float left, float right, float depth) -> Quad {
        return {{{left, -1, depth}, {right, -1, depth}, {right, 1, depth}, {left, 1, depth}}};
    };
    DrawShader transparent;
    transparent.value = DrawShader::PASS_LEQUAL | DrawShader::MASK_COLOR_WRITE |
                        DrawShader::MASK_GRADIENT_MODULATE |
                        (DrawShader::SRCBLEND_SRC_ALPHA << DrawShader::SRCBLEND_SHIFT) |
                        (DrawShader::DSTBLEND_ONE_MINUS_SRC_ALPHA << DrawShader::DSTBLEND_SHIFT);
    const auto visual = [&](std::initializer_list<Quad> quads, const glm::vec4& color,
                            DrawShader shader, bool sorted) {
        auto material = std::make_shared<Material>();
        material->setAmbient(glm::vec4(0));
        material->setDiffuse(glm::vec4(0, 0, 0, color.a));
        material->setSpecular(glm::vec4(0));
        material->setEmissive(color);
        auto mesh = std::make_shared<Mesh>(static_cast<int>(quads.size()) * 2,
                                           static_cast<int>(quads.size()) * 4);
        int vertex = 0;
        int polygon = 0;
        for (const auto& positions : quads) {
            for (const auto& position : positions) mesh->getVertexLoc()[vertex++] = position;
            mesh->getPolyVertex()[polygon++] = {vertex - 4, vertex - 2, vertex - 3};
            mesh->getPolyVertex()[polygon++] = {vertex - 4, vertex - 1, vertex - 2};
        }
        mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
        mesh->setShader(shader, 0);
        if (sorted) mesh->enable(Mesh::CONTROL_SORTED_RENDERING);
        auto instance = std::make_unique<MeshInstance>(&scene);
        instance->setModel(mesh);
        return instance;
    };
    // The red object has triangles both in front of and behind the blue
    // object. Sorting whole objects cannot produce both expected colors.
    auto red =
        visual({quad(-1.75f, -.25f, 2), quad(.25f, 1.75f, 6)}, {1, 0, 0, .5f}, transparent, true);
    auto blue = visual({quad(-1.75f, 1.75f, 4)}, {0, 0, 1, .5f}, transparent, true);
    DrawShader depth_only;
    depth_only.value &= ~DrawShader::MASK_COLOR_WRITE;
    auto occluder = visual({quad(.75f, 1.25f, 1.5f)}, {1, 1, 1, 1}, depth_only, false);
    DrawShader cutout;
    cutout.value |= DrawShader::MASK_TEXTURING | DrawShader::MASK_ALPHATEST;
    auto discarded = visual({quad(-1.75f, 1.75f, 1.25f)}, {1, 1, 1, 1}, cutout, false);
    discarded->getModel()->setTexture(solidTexture(0x00ffffff), 0, 0);

    const auto render = [&] {
        fixture.renderer->beginFrame({0, 0, 0, 1});
        fixture.renderer->addView(scene.createView(fixture.renderer->context(), &scene.camera, {},
                                                   vsg::ViewportState::create(0, 0, 64, 64),
                                                   fixture.renderer->frameNumber()));
        fixture.renderer->endFrame(false, true);
    };
    const auto expect = [&](int x, unsigned expected) {
        const auto actual = fixture.pixel(x, 32);
        INFO("pixel " << x << " RGB " << actual << ", expected " << expected);
        for (unsigned shift : {0u, 8u, 16u}) {
            const auto channel = static_cast<int>((actual >> shift) & 255u);
            const auto target = static_cast<int>((expected >> shift) & 255u);
            REQUIRE(channel >= target - 2);
            REQUIRE(channel <= target + 2);
        }
    };
    render();
    expect(16, 0x807300); // Near red over ordinary VSG green; blue fails depth.
    expect(24, 0x800040); // Blue, then near red.
    expect(40, 0x400080); // Far red, then blue.
    expect(48, 0x000000); // Depth-only geometry occludes without writing color.
    blue->visible = false;
    render();
    expect(16, 0x807300);
    expect(24, 0x800000);
    expect(40, 0x800000);
    expect(48, 0x000000);
    blue->visible = true;
    red->transform.setScale(glm::dvec3(-1, 1, 1));
    render();
    expect(16, 0x00e600);
    expect(24, 0x400080);
    expect(40, 0x800040);
    expect(48, 0x000000);
    scene.camera.flipHorizontal();
    render();
    expect(16, 0x000000);
    expect(24, 0x800040);
    expect(40, 0x400080);
    expect(48, 0x00e600);
}

TEST_CASE("Imported native VSG content shares game lighting, fog and picking")
{
    Fixture fixture;
    ScopedTemporaryDirectory directory("wiz8-native-content");
    auto positions = vsg::vec3Array::create({{-1, -1, 3}, {1, -1, 3}, {0, 1, 3}});
    auto normals = vsg::vec3Array::create(3, vsg::vec3(0, 0, -1));
    auto source = vsg::VertexIndexDraw::create();
    source->assignArrays({positions, normals});
    source->assignIndices(vsg::ushortArray::create({0, 1, 2}));
    source->indexCount = 3;
    source->instanceCount = 1;
    const auto path = directory.path / "triangle.vsgt";
    REQUIRE(vsg::write(source, path.string()));
    auto material = std::make_shared<Material>();
    material->setAmbient(glm::vec4(1));
    material->setDiffuse(glm::vec4(1, 0, 0, 1));
    material->setSpecular(glm::vec4(0));
    material->setEmissive(glm::vec4(0));
    auto object = render::loadGameContent(path, material);
    WorldScene scene;
    scene.root->addChild(object);
    scene.camera.setProjectionType(Camera3D::PROJECT_ORTHOGRAPHIC);
    scene.camera.setClipRange(1, 10);
    scene.camera.setViewPlane({-2, -2, 2, 2}, 1);
    scene.ambient_light = glm::vec3(0);
    scene.fog_color = {0, 0, 1};
    Light light(&scene, Light::PRESET_DIRECTIONAL);
    light.ambient = light.specular = glm::vec3(0);
    light.diffuse = glm::vec3(.5f);
    FogParams fog(&scene);
    fog.setRange(0, 1);
    fog.setDensity(.25f);
    const auto draw = [&] {
        fixture.renderer->beginFrame();
        auto view = scene.createView(fixture.renderer->context(), &scene.camera, {},
                                     vsg::ViewportState::create(0, 0, 64, 64),
                                     fixture.renderer->frameNumber());
        Pick pick;
        scene.pick(*view->camera, std::span{&pick, 1});
        REQUIRE(pick.hit);
        REQUIRE(std::ranges::find(pick.hit->nodePath, object.get()) != pick.hit->nodePath.end());
        REQUIRE(render::gameOwner(pick) == nullptr);
        fixture.renderer->addView(view);
        fixture.renderer->endFrame(false, true);
        return fixture.pixel(32, 32);
    };
    const auto lit = draw();
    REQUIRE(((lit >> 16) & 255) >= 94);
    REQUIRE(((lit >> 16) & 255) <= 97);
    REQUIRE((lit & 255) >= 62);
    light.diffuse = glm::vec3(0);
    const auto dark = draw();
    REQUIRE((dark & 0xff0000) == 0);
    REQUIRE((dark & 255) >= 62);
}

TEST_CASE("Rectangular world posters sample border texels without half-texel correction")
{
    Fixture fixture;
    WorldScene scene;
    scene.camera.setProjectionType(Camera3D::PROJECT_ORTHOGRAPHIC);
    scene.camera.setClipRange(1, 10);
    scene.camera.setViewPlane({-1, -.5, 1, .5}, 1);
    auto pixels = CreateCpuStorage(64, 32, SDL_PIXELFORMAT_BGRA32);
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), nullptr, 0xff00ff00));
    const SDL_Rect top{0, 0, 64, 1}, bottom{0, 31, 64, 1};
    const SDL_Rect left{0, 1, 1, 30}, right{63, 1, 1, 30};
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), &top, 0xffff0000));
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), &bottom, 0xff0000ff));
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), &left, 0xffffff00));
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), &right, 0xffffffff));
    auto texture = std::make_shared<Texture>(std::shared_ptr<SDL_Surface>(std::move(pixels)));
    texture->setDimensions({64, 32, true});
    texture->setWrapS(Texture::WRAP_CLAMP);
    texture->setWrapT(Texture::WRAP_CLAMP);
    texture->setMipmap(Texture::MIPMAP_NONE);
    auto poster = MakePosterQuad(texture, 2, 0, false);
    poster->transform.setLocation(0, 0, 3);
    scene.attach(*poster);
    for (auto filter : {Texture::FILTER_NONE, Texture::FILTER_GOOD}) {
        texture->setMinFilter(filter);
        texture->setMagFilter(filter);
        fixture.renderer->beginFrame();
        fixture.renderer->addView(scene.createView(fixture.renderer->context(), &scene.camera, {},
                                                   vsg::ViewportState::create(0, 0, 64, 32),
                                                   fixture.renderer->frameNumber()));
        fixture.renderer->endFrame(false, true);
        REQUIRE(fixture.pixel(32, 0) == 0xff0000);
        REQUIRE(fixture.pixel(32, 31) == 0x0000ff);
        REQUIRE(fixture.pixel(0, 16) == 0xffff00);
        REQUIRE(fixture.pixel(63, 16) == 0xffffff);
        REQUIRE(fixture.pixel(32, 16) == 0x00ff00);
    }
}
