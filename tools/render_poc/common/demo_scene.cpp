#include "demo_scene.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include <SDL3/SDL_surface.h>

#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_math.h"
#include "wiz8/render/texture.h"

namespace render_poc {
namespace {

constexpr float pi = std::numbers::pi_v<float>;

std::uint32_t shaderWord(bool translucent, bool secondary_gradient)
{
    std::uint32_t value = DrawShader::PASS_LEQUAL |
                          DrawShader::COLOR_WRITE_ENABLE << DrawShader::COLOR_WRITE_SHIFT |
                          DrawShader::FOG_ENABLE << DrawShader::FOG_SHIFT |
                          DrawShader::GRADIENT_MODULATE << DrawShader::GRADIENT_SHIFT |
                          DrawShader::TEXTURING_ENABLE << DrawShader::TEXTURING_SHIFT |
                          DrawShader::DITHER_ENABLE << DrawShader::DITHER_SHIFT;
    if (secondary_gradient) value |= DrawShader::MASK_SECONDARY_GRADIENT;
    if (translucent) {
        value |= DrawShader::SRCBLEND_SRC_ALPHA << DrawShader::SRCBLEND_SHIFT |
                 DrawShader::DSTBLEND_ONE_MINUS_SRC_ALPHA << DrawShader::DSTBLEND_SHIFT;
    } else {
        value |= DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT |
                 DrawShader::DSTBLEND_ZERO << DrawShader::DSTBLEND_SHIFT |
                 DrawShader::DEPTH_WRITE_ENABLE << DrawShader::DEPTH_WRITE_SHIFT;
    }
    return value;
}

DrawShader shaderOf(std::uint32_t value)
{
    DrawShader shader;
    shader.value = value;
    return shader;
}

template<class Pixel>
std::shared_ptr<Texture> makeTexture(int size, Pixel pixel)
{
    std::shared_ptr<SDL_Surface> surface(SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32),
                                         SDL_DestroySurface);
    if (!surface) throw std::runtime_error(SDL_GetError());
    for (int y = 0; y < size; ++y) {
        auto* row = static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch;
        for (int x = 0; x < size; ++x) {
            const glm::vec4 color = glm::clamp(pixel(x, y), 0.f, 1.f) * 255.f + .5f;
            for (int channel = 0; channel < 4; ++channel)
                row[x * 4 + channel] = static_cast<std::uint8_t>(color[channel]);
        }
    }
    return std::make_shared<Texture>(surface);
}

std::shared_ptr<Material> makeMaterial(glm::vec4 diffuse, glm::vec4 ambient,
                                       glm::vec4 specular = glm::vec4(0, 0, 0, 1),
                                       float shininess = 1)
{
    auto material = std::make_shared<Material>();
    material->setDiffuse(diffuse);
    material->setAmbient(ambient);
    material->setSpecular(specular);
    material->parms.shininess = shininess;
    return material;
}

// Outward winding, judged by the first triangle of a convex surface.
void orientOutward(Mesh& mesh, const glm::vec3& inside)
{
    auto* triangles = mesh.getPolyVertex();
    const std::span<const glm::vec3> positions(mesh.getVertexLoc(), mesh.vertex_location_count);
    const auto& first = triangles[0];
    const auto normal = meshMath::polygonNormal(positions, first);
    if (glm::dot(normal, positions[first.x] - inside) >= 0) return;
    auto* corners = mesh.getPolyUVIndex(0, 0);
    for (int polygon = 0; polygon < mesh.polygon_count; ++polygon) {
        std::swap(triangles[polygon].y, triangles[polygon].z);
        if (corners) std::swap(corners[polygon].y, corners[polygon].z);
    }
}

std::shared_ptr<Mesh> makeFloor(std::shared_ptr<Texture> light_tiles,
                                std::shared_ptr<Texture> dark_tiles)
{
    constexpr int cells = 12;
    constexpr float extent = 14;
    constexpr int side = cells + 1;
    auto mesh = std::make_shared<Mesh>(cells * cells * 2, side * side);
    mesh->setName("floor");
    auto* positions = mesh->getVertexLoc();
    auto* uv = mesh->getVertexTexCoords(0, 0, 1);
    auto* baked = mesh->getVertexDIG(0, 1);
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            const int vertex = z * side + x;
            const glm::vec2 at = (glm::vec2(x, z) / float(cells) - .5f) * 2.f * extent;
            positions[vertex] = {at.x, 0, at.y};
            uv[vertex] = glm::vec2(x, z) * .5f;
            // Baked light: a warm pool near the creature, fading outwards.
            const float pool =
                std::exp(-glm::dot(at - glm::vec2(-3, 2), at - glm::vec2(-3, 2)) / 30);
            baked[vertex] = glm::vec3(.35f, .2f, .05f) * pool;
        }
    }
    auto* triangles = mesh->getPolyVertex();
    auto* table = mesh->getPolygonTextureTable(0, 0, true);
    // Polygons grouped by texture, as the level loader's OptimizeMeshOrder leaves them.
    int polygon = 0;
    for (int parity = 0; parity < 2; ++parity) {
        const auto& tiles = parity ? light_tiles : dark_tiles;
        for (int z = 0; z < cells; ++z) {
            for (int x = 0; x < cells; ++x) {
                if ((x + z) % 2 != parity) continue;
                const int corner = z * side + x;
                triangles[polygon] = {corner, corner + side, corner + 1};
                table->set(polygon++, tiles);
                triangles[polygon] = {corner + 1, corner + side, corner + side + 1};
                table->set(polygon++, tiles);
            }
        }
    }
    orientOutward(*mesh, {0, -1, 0});
    auto material = makeMaterial({.75f, .75f, .7f, 1}, {.6f, .6f, .6f, 1});
    material->enable(Material::OPER_DEPTH_CUE);
    mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
    mesh->setShader(shaderOf(shaderWord(false, false)), 0);
    return mesh;
}

// A sphere whose frames go through the compressed codec exactly like
// mesh_loader.cpp: shorts scaled by vertex_compression_scale, normals rebuilt
// per frame by FinalizeVertexFrame.
std::shared_ptr<Mesh> makeCreature(std::shared_ptr<Texture> skin)
{
    constexpr int rings = 10, segments = 16, frames = 8;
    constexpr int vertices = (rings + 1) * (segments + 1);
    constexpr int polygons = rings * segments * 2;
    auto mesh = std::make_shared<Mesh>(polygons, vertices);
    mesh->setName("creature");
    const auto surface = [](int ring, int segment, int frame) {
        const float theta = pi * float(ring) / rings;
        const float phi = 2 * pi * float(segment % segments) / segments;
        const float wobble =
            1 + .18f * std::sin(2 * pi * float(frame) / frames + 3 * theta + 2 * phi);
        return glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta) * 1.3f,
                         std::sin(theta) * std::sin(phi)) *
               1.3f * wobble;
    };
    auto* positions = mesh->getVertexLoc();
    auto* uv = mesh->getVertexTexCoords(0, 0, 1);
    for (int ring = 0; ring <= rings; ++ring) {
        for (int segment = 0; segment <= segments; ++segment) {
            const int vertex = ring * (segments + 1) + segment;
            positions[vertex] = surface(ring, segment, 0);
            uv[vertex] = {float(segment) / segments * 2, float(ring) / rings};
        }
    }
    auto* triangles = mesh->getPolyVertex();
    int polygon = 0;
    for (int ring = 0; ring < rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const int a = ring * (segments + 1) + segment;
            const int b = a + segments + 1;
            triangles[polygon++] = {a, b, a + 1};
            triangles[polygon++] = {a + 1, b, b + 1};
        }
    }
    orientOutward(*mesh, glm::vec3(0));
    constexpr float compression = 1.f / 4096;
    mesh->InitializeVertexFrames(frames);
    mesh->vertex_compression_scale = compression;
    for (int frame = 0; frame < frames; ++frame) {
        auto* compressed = mesh->GetVertex(frame);
        for (int ring = 0; ring <= rings; ++ring) {
            for (int segment = 0; segment <= segments; ++segment) {
                const int vertex = ring * (segments + 1) + segment;
                const auto position = surface(ring, segment, frame) / compression;
                for (int axis = 0; axis < 3; ++axis)
                    compressed[vertex * 3 + axis] = static_cast<short>(std::lround(position[axis]));
            }
        }
        mesh->FinalizeVertexFrame(frame);
    }
    // The "hand": a mapped vertex on the equator that attachments follow.
    mesh->SetMappedVertex((rings / 2) * (segments + 1) + segments / 4, DemoScene::kHandKey);
    mesh->setMaterial(
        makeMaterial({.8f, .55f, .45f, 1}, {.5f, .4f, .35f, 1}, {.9f, .9f, .8f, 1}, 24), 0,
        Mesh::SIDE_FRONT);
    mesh->setTexture(std::move(skin), 0, 0);
    mesh->setShader(shaderOf(shaderWord(false, true)), 0);
    mesh->setDirtyAll();
    return mesh;
}

// A vertical wall of strips. Two of these cross, so their triangles alternate
// in depth along the crossing.
std::shared_ptr<Mesh> makePane(std::shared_ptr<Texture> glass, glm::vec4 tint)
{
    constexpr int strips = 8;
    constexpr int vertices = (strips + 1) * 2;
    auto mesh = std::make_shared<Mesh>(strips * 2, vertices);
    mesh->setName("pane");
    auto* positions = mesh->getVertexLoc();
    auto* uv = mesh->getVertexTexCoords(0, 0, 1);
    for (int column = 0; column <= strips; ++column) {
        const float x = (float(column) / strips - .5f) * 9;
        positions[column * 2] = {x, 0, 0};
        positions[column * 2 + 1] = {x, 4.5f, 0};
        uv[column * 2] = {float(column) / strips, 0};
        uv[column * 2 + 1] = {float(column) / strips, 1};
    }
    auto* triangles = mesh->getPolyVertex();
    for (int column = 0; column < strips; ++column) {
        const int a = column * 2;
        triangles[column * 2] = {a, a + 1, a + 2};
        triangles[column * 2 + 1] = {a + 2, a + 1, a + 3};
    }
    auto material = makeMaterial(tint, tint * .5f);
    mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
    mesh->setTexture(std::move(glass), 0, 0);
    mesh->setShader(shaderOf(shaderWord(true, false)), 0);
    mesh->flags |= W8_MESH_SORTED_RENDERING;
    mesh->enable(Mesh::CONTROL_SORTED_RENDERING);
    return mesh;
}

// A ring band whose seam column reuses the first vertices with distinct UV
// indices, so those corners keep raw coordinates while the rest scroll.
std::shared_ptr<Mesh> makeFalls(std::shared_ptr<Texture> water)
{
    constexpr int segments = 12;
    constexpr int vertices = segments * 2;
    auto mesh = std::make_shared<Mesh>(segments * 2, vertices);
    mesh->setName("falls");
    mesh->setUVCount(vertices + 2);
    auto* positions = mesh->getVertexLoc();
    auto* uv = mesh->getVertexTexCoords(0, 0, 1);
    for (int segment = 0; segment < segments; ++segment) {
        const float angle = 2 * pi * float(segment) / segments;
        const glm::vec3 around(std::cos(angle) * 1.4f, 0, std::sin(angle) * 1.4f);
        positions[segment * 2] = around;
        positions[segment * 2 + 1] = around + glm::vec3(0, 5, 0);
        uv[segment * 2] = {float(segment) / segments, 1};
        uv[segment * 2 + 1] = {float(segment) / segments, 0};
    }
    uv[vertices] = {1, 1};
    uv[vertices + 1] = {1, 0};
    auto* triangles = mesh->getPolyVertex();
    auto* corners = mesh->getPolyUVIndex(0, 1);
    for (int segment = 0; segment < segments; ++segment) {
        const int a = segment * 2;
        const int b = (a + 2) % vertices;
        const bool seam = segment == segments - 1;
        triangles[segment * 2] = {a, a + 1, b};
        triangles[segment * 2 + 1] = {b, a + 1, b + 1};
        corners[segment * 2] = {a, a + 1, seam ? vertices : b};
        corners[segment * 2 + 1] = {seam ? vertices : b, a + 1, seam ? vertices + 1 : b + 1};
    }
    orientOutward(*mesh, glm::vec3(0, 2.5f, 0));
    auto material = makeMaterial({.5f, .7f, .9f, 1}, {.4f, .5f, .6f, 1});
    material->uv_mapping = Material::UvMapping::Scroll;
    material->uv_velocity = {0, -.35f};
    mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
    mesh->setTexture(std::move(water), 0, 0);
    mesh->setShader(shaderOf(shaderWord(false, false)), 0);
    return mesh;
}

// A small glowing lamp body, emissive so it reads as the light's source.
std::shared_ptr<Mesh> makeLantern()
{
    constexpr int rings = 4, segments = 6;
    auto mesh = std::make_shared<Mesh>(rings * segments * 2, (rings + 1) * (segments + 1));
    mesh->setName("lantern");
    auto* positions = mesh->getVertexLoc();
    for (int ring = 0; ring <= rings; ++ring)
        for (int segment = 0; segment <= segments; ++segment) {
            const float theta = pi * float(ring) / rings;
            const float phi = 2 * pi * float(segment % segments) / segments;
            positions[ring * (segments + 1) + segment] =
                glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta),
                          std::sin(theta) * std::sin(phi)) *
                .25f;
        }
    auto* triangles = mesh->getPolyVertex();
    int polygon = 0;
    for (int ring = 0; ring < rings; ++ring)
        for (int segment = 0; segment < segments; ++segment) {
            const int a = ring * (segments + 1) + segment;
            const int b = a + segments + 1;
            triangles[polygon++] = {a, b, a + 1};
            triangles[polygon++] = {a + 1, b, b + 1};
        }
    orientOutward(*mesh, glm::vec3(0));
    auto material = makeMaterial({.2f, .2f, .1f, 1}, {.1f, .1f, .1f, 1});
    material->setEmissive({1, .85f, .5f, 1});
    mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
    auto shader = shaderWord(false, false) & ~DrawShader::MASK_TEXTURING;
    mesh->setShader(shaderOf(shader), 0);
    return mesh;
}

} // namespace

DemoScene::DemoScene(int herd_size)
{
    const auto checker = [](glm::vec4 a, glm::vec4 b) {
        return [=](int x, int y) { return ((x / 8 + y / 8) % 2) ? a : b; };
    };
    auto light_tiles = makeTexture(64, checker({.85f, .8f, .7f, 1}, {.7f, .65f, .55f, 1}));
    auto dark_tiles = makeTexture(64, checker({.45f, .5f, .55f, 1}, {.35f, .4f, .45f, 1}));
    auto skin = makeTexture(64, [](int x, int y) {
        const float stripe = .75f + .25f * std::sin(float(x) * .4f + float(y) * .2f);
        return glm::vec4(stripe, stripe * .9f, stripe * .8f, 1);
    });
    auto glass = makeTexture(64, [](int x, int y) {
        const bool frame = x < 4 || x > 59 || y < 4 || y > 59;
        return frame ? glm::vec4(1, 1, 1, 1) : glm::vec4(.9f, .95f, 1, .7f);
    });
    auto water = makeTexture(64, [](int x, int y) {
        const float wave = .5f + .5f * std::sin(float(y) * .3f + std::sin(float(x) * .2f) * 2);
        return glm::vec4(.3f + .5f * wave, .6f + .3f * wave, 1, 1);
    });
    textures = {light_tiles, dark_tiles, skin, glass, water};

    const auto add = [this](std::string name, std::shared_ptr<Mesh> mesh, glm::dvec3 at,
                            double yaw) {
        auto instance = std::make_unique<MeshInstance>();
        instance->setName(std::move(name));
        instance->setModel(std::move(mesh));
        instance->transform.setLocation(at);
        instance->transform.setRotation(0, yaw, 0);
        instances.push_back(std::move(instance));
        return instances.back().get();
    };
    floor = add("floor", makeFloor(light_tiles, dark_tiles), {0, 0, 0}, 0);
    floor->setExclusionMask(kWardGroup); // the ward light never reaches the floor
    // Every creature shares one Mesh, as monsters of one kind share a model.
    const auto creature = makeCreature(skin);
    for (int index = 0; index < herd_size; ++index) {
        const int row = (index - 1) / 8, column = (index - 1) % 8;
        const glm::dvec3 at = index == 0 ? glm::dvec3(-2.5, 1.8, 1)
                                         : glm::dvec3(-12 + column * 3.5, 1.8, -8 - row * 3.5);
        herd.push_back(add(index == 0 ? "creature" : "creature " + std::to_string(index), creature,
                           at, index * .7));
    }
    falls = add("falls", makeFalls(water), {5, 0, -4}, 0);
    add("red pane", makePane(glass, {1, .35f, .3f, .55f}), {1.5, 0, 3}, .35);
    add("blue pane", makePane(glass, {.3f, .5f, 1, .5f}), {1.5, 0, 3}, -.45);
    // Placed at the leader's hand; its pose relative to that vertex.
    lantern = add("lantern", makeLantern(), {0, 0, 0}, 0);
    lantern->transform.setLocation(.3, .1, 0);
    lantern->render_flags |= MeshInstance::RENDER_NO_PICK;
    if (herd.size() > 1) fading = herd[1];

    auto lamp = std::make_unique<Light>(nullptr, Light::PRESET_POINT);
    lamp->setName("lamp");
    lamp->attenuation_model = Light::ATTENUATION_3DSTUDIO_MAX;
    lamp->far_start = 4;
    lamp->far_end = 13;
    lamp->diffuse = {1, .8f, .55f};
    lamp->specular = {1, 1, .9f};
    lamp->ambient = {.05f, .04f, .03f};
    lamp->transform.setLocation(0, .3, 0); // above the lantern body
    this->lamp = lamp.get();
    lights.push_back(std::move(lamp));

    auto spot = std::make_unique<Light>(nullptr, Light::PRESET_SPOT);
    spot->setName("spot");
    spot->transform.setLocation(4, 9, 6);
    spot->setSpotDirection({-.4f, -1, -.35f});
    spot->spot_angle = .45f;
    spot->spot_exponent = 6;
    spot->opengl_attenuation = {1, .02f, .001f};
    spot->diffuse = {.35f, .55f, 1};
    spot->specular = {.5f, .6f, 1};
    lights.push_back(std::move(spot));

    auto sun = std::make_unique<Light>(nullptr, Light::PRESET_DIRECTIONAL);
    sun->setName("sun");
    sun->transform.setRotation(glm::dvec3(.3, -1, .5), 0);
    sun->diffuse = {.25f, .25f, .3f};
    sun->specular = glm::vec3(0);
    lights.push_back(std::move(sun));

    // A green ward light in a group the floor excludes.
    auto ward = std::make_unique<Light>(nullptr, Light::PRESET_POINT);
    ward->setName("ward");
    ward->setGroupMask(kWardGroup);
    ward->attenuation_model = Light::ATTENUATION_3DSTUDIO_MAX;
    ward->far_start = 2;
    ward->far_end = 9;
    ward->diffuse = {.2f, .9f, .3f};
    ward->specular = glm::vec3(0);
    ward->transform.setLocation(-6, 1, -3);
    lights.push_back(std::move(ward));

    auto fog = std::make_unique<FogParams>();
    fog->setName("mist");
    fog->setRange(12, 42);
    fog->setDensity(.75f);
    fogs.push_back(std::move(fog));

    camera.setFOV(60 * pi / 180, 4.0 / 3.0);
    camera.setClipRange(.5, 200);
    camera.setEnvironmentRange(15, 45);
    camera.setEnvironmentScale(0, .55f);
    environment.ambient_light = {.3f, .3f, .35f, 1};
    update(0);
}

DemoScene::~DemoScene() = default;

MeshInstance& DemoScene::addVisitor()
{
    // A new Mesh, not a shared one: its renderer resources do not exist yet.
    auto mesh = makeCreature(textures[2]);
    auto instance = std::make_unique<MeshInstance>();
    instance->setName("visitor");
    instance->setModel(std::move(mesh));
    instance->transform.setLocation(4, 1.8, 4);
    instance->emissive_override_enabled = true;
    instance->emissive_override = .35f;
    instances.push_back(std::move(instance));
    return *instances.back();
}

void DemoScene::removeVisitor(MeshInstance& visitor)
{
    std::erase_if(instances, [&](const auto& instance) { return instance.get() == &visitor; });
}

void DemoScene::update(double seconds)
{
    const double orbit = seconds * .25;
    const glm::dvec3 eye(std::sin(orbit) * 15, 6.5, std::cos(orbit) * 15);
    camera.transform.setLocation(eye);
    camera.transform.setRotation(glm::dvec3(0, 1.2, 0) - eye, 0);

    // The leader walks a small circle; the lantern and its light follow through
    // the attachment, not through code that recomputes their poses.
    herd.front()->transform.setLocation(-2.5 + std::cos(seconds * .6) * 1.5, 1.8,
                                        1 + std::sin(seconds * .6) * 1.5);
    herd.front()->transform.setRotation(0, -seconds * .6, 0);
    for (std::size_t index = 0; index < herd.size(); ++index) {
        auto& creature = *herd[index];
        const auto frames = static_cast<unsigned>(creature.getModel()->frames.size());
        const double frame = seconds * 5 + double(index) * 1.37;
        creature.frame_index = static_cast<unsigned>(frame) % frames;
        creature.frame_interpolation = static_cast<float>(frame - std::floor(frame));
    }
    if (fading) {
        // A creature fading out switches to sorted, blended rendering.
        const double phase = std::fmod(seconds * .4, 2.0);
        fading->diffuse_scale_enabled = phase > 1;
        fading->diffuse_scale = static_cast<float>(1 - std::abs(phase - 1.5) * 2);
    }

    const auto [near_range, far_range] = camera.getEnvironmentRange();
    const auto [near_scale, far_scale] = camera.getEnvironmentScale();
    environment.environment_minimum = near_range;
    environment.environment_maximum = far_range;
    environment.environment_scale = near_scale;
    environment.environment_inverse_scale = far_scale;
    environment.frame_time = seconds;
}

CameraMatrices cameraMatrices(const Camera3D& camera)
{
    // Camera3D::applyView: the inverse world matrix with its z row reversed.
    glm::dmat4 view = glm::inverse(camera.transform.matrix<glm::dmat4>());
    view = glm::row(view, 2, -glm::row(view, 2));
    const auto [near_plane, far_plane] = camera.getClipRange();
    auto [rectangle, distance] = camera.getViewPlane();
    const double scale = near_plane / distance;
    const auto projection =
        glm::frustum(float(rectangle.left * scale), float(rectangle.right * scale),
                     float(rectangle.bottom * scale), float(rectangle.top * scale),
                     float(near_plane), float(far_plane));
    return {glm::mat4(view), projection};
}

std::vector<std::uint8_t> rgba8Pixels(SDL_Surface& surface)
{
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
        SDL_ConvertSurface(&surface, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!converted) throw std::runtime_error(SDL_GetError());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(converted->w) * converted->h * 4);
    for (int y = 0; y < converted->h; ++y)
        std::copy_n(static_cast<const std::uint8_t*>(converted->pixels) + y * converted->pitch,
                    converted->w * 4,
                    pixels.data() + static_cast<std::size_t>(y) * converted->w * 4);
    return pixels;
}

} // namespace render_poc
