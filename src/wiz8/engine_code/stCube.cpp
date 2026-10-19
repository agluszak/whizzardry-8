#include "wiz8/engine_code/stCube.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/world.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/surface_storage.h"
#include "wiz8/unicode.h"
#include "wiz8/world_cursor.h"

// The scene node unlinks itself from the world when it is destroyed.
W8WorldCursorNode::~W8WorldCursorNode() = default;

/* The double selection range at 0x005ECAC8: node distances below it select the
   node. Read as 75000.0, not the zero a float view would give. */
const double g_cube_effect_max_distance = 75000.0;

static int g_cursor_node_index = -1;

const double g_world_cursor_scale = 0.002;

/* 0x0048D070 is a bare JMP to CreateWorldCursorCube: a tail-jump thunk
   with no distinct source entity. */

/* Build the numbered cube the world cursor table holds: a 500-unit
   cube, a translucent white material, and a 32x32 texture the label painter
   later fills. */

static void DrawWorldCursorNodeLabel(W8WorldCursorNode& entry);

std::unique_ptr<W8WorldCursorNode> CreateWorldCursorCube()
{
    auto model = std::make_shared<stMeshModel>(12, 24);
    auto entry = std::make_unique<W8WorldCursorNode>();
    entry->node = std::make_unique<stModelInstance>(nullptr);
    const std::array<glm::vec3, 8> corners{{{-0.5f, 1, 0.5f},
                                            {0.5f, 1, 0.5f},
                                            {0.5f, 1, -0.5f},
                                            {-0.5f, 1, -0.5f},
                                            {-0.5f, 0, -0.5f},
                                            {0.5f, 0, 0.5f},
                                            {-0.5f, 0, 0.5f},
                                            {0.5f, 0, -0.5f}}};
    const std::array<std::array<int, 4>, 6> faces{
        {{0, 1, 2, 3}, {4, 6, 0, 3}, {6, 5, 1, 0}, {5, 7, 2, 1}, {7, 4, 3, 2}, {7, 5, 6, 4}}};
    const std::array<glm::vec2, 4> uv{{{1, 1}, {0, 1}, {0, 0}, {1, 0}}};
    // Keep a UV seam on every face, but share normal accumulation at each corner.
    model->vertex_locations.resize(24);
    model->texcoords[0][0].resize(24);
    model->vertex_shade_indices.resize(24);
    for (int face = 0; face < 6; ++face) {
        const int base = face * 4;
        for (int corner = 0; corner < 4; ++corner) {
            model->vertex_locations[base + corner] = corners[faces[face][corner]] * 500.0f;
            model->texcoords[0][0][base + corner] = uv[corner];
            model->vertex_shade_indices[base + corner] = faces[face][corner];
        }
        model->poly_vertices.emplace_back(base + 3, base, base + 1);
        model->poly_vertices.emplace_back(base + 3, base + 1, base + 2);
    }

    auto material = std::make_shared<srMaterial>();
    material->setName("Cube Number Material");

    glm::vec4 colour;
    colour = glm::vec4(1.0f);
    material->setAmbient(colour);
    colour = glm::vec4(1.0f);
    material->setEmissive(colour);
    colour = glm::vec4(1.0f);
    material->setDiffuse(colour);
    colour = glm::vec4(0.0f);
    material->setSpecular(colour);
    material->parms.diffuse.w = 0.35f;
    material->parms.shininess = 1.0f;
    material->dirty = 1;
    model->setMaterial(std::move(material), 0, srMeshModel::SIDE_FRONT);

    srShader shader;
    shader.value = 0x0100c5b3; /* packed srShader: LEQUAL, color write, dst 1-srcA, fog, modulate */
    model->setShader(shader, 0);

    auto surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(0x20UL, 0x20UL, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
    if (surface == 0) {
        Fail("psrSurface");
    }

    w8_ulong pixel;
    unsigned char* bytes =
        reinterpret_cast<unsigned char*>(&pixel); // reinterpret-ok: packed colour storage
    bytes[3] = static_cast<unsigned char>(srFloatToInt(255.0));
    bytes[2] = static_cast<unsigned char>(srFloatToInt(0.0));
    bytes[1] = static_cast<unsigned char>(srFloatToInt(0.0));
    bytes[0] = static_cast<unsigned char>(srFloatToInt(127.5));
    SDL_FillSurfaceRect(surface.get(), nullptr,
                        SDL_MapSurfaceRGBA(surface.get(), (pixel >> 16) & 255, (pixel >> 8) & 255,
                                           pixel & 255, pixel >> 24));

    auto texture = std::make_shared<srTexture>();
    texture->setSurfacePtr(surface);
    texture->setMipmap(srTexture::MIPMAP_NONE);

    model->setTexture(std::move(texture), 0, 0);

    model->setDirty(srMeshModel::DIRTY_TRI_MESH);
    model->setDirty(srMeshModel::DIRTY_BOUNDS);

    model->setName("stCube");
    entry->node->setModel(model);

    w8_ulong packed;
    PackColourToLong(&packed, 1.0, 0.0, 0.0, 0.5);
    entry->color = packed;
    DrawWorldCursorNodeLabel(*entry);
    return entry;
}

/* Paint the three cube parameters onto the model's first texture using the menu
   small font. */
static void DrawWorldCursorNodeLabel(W8WorldCursorNode& entry)
{
    stModelInstance* instance = entry.node.get();
    if (instance == 0) {
        Fail("pstModelInstance");
    }
    stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());
    if (mesh == 0) {
        Fail("pstMeshModel");
    }
    srTexture* texture = static_cast<srTexture*>(mesh->getTexture(0, 0));
    if (texture == 0) {
        Fail("psrTexture");
    }
    SDL_Surface* surface = texture->getSurfacePtr();
    SDL_FillSurfaceRect(surface, nullptr,
                        SDL_MapSurfaceRGBA(surface, (entry.color >> 16) & 255,
                                           (entry.color >> 8) & 255, entry.color & 255,
                                           entry.color >> 24));
    unsigned char* data = static_cast<unsigned char*>(surface->pixels);
    if (data == 0) {
        Fail("pBuffer");
    }
    {
        g_smfnt_font.resetPalette();
        SDLSurfaceOwner text_surface{SDL_CreateSurfaceFrom(surface->w, surface->h,
                                                           SDL_PIXELFORMAT_RGB565, data,
                                                           surface->pitch),
                                     SDL_DestroySurface};
        if (text_surface) {
            for (int index = 0; index < 3; ++index) {
                std::string text;
                text = fmt::format("{:d}", entry.parameters[index]);
                DrawText(*text_surface, g_smfnt_font, {0, g_smfnt_font.height() * index}, text);
            }
        }
        texture->invalidate();
    }
}

void ReadWorldCursorNodeStates(W8Chunk& file, W8SavedCursorNodes& table)
{
    std::int32_t version;
    std::uint32_t count;
    file.ReadBytes(&version, sizeof(version));
    if (version == 0x21122112) version = 1;
    if (version != 1 && version != 2) throw std::runtime_error("invalid cursor state version");
    file.ReadBytes(&count, sizeof(count));
    if (count > file.RemainingBytes() / (version == 1 ? 4 : 36))
        throw std::runtime_error("invalid cursor state count");
    std::vector<std::pair<std::size_t, std::vector<std::byte>>> pending;
    pending.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        auto destination = static_cast<std::size_t>(index);
        if (version == 2) {
            std::array<char, 32> name;
            file.ReadBytes(name.data(), name.size());
            const auto end = std::find(name.begin(), name.end(), '\0');
            if (end == name.end()) throw std::runtime_error("unterminated cursor state name");
            const auto found =
                std::find_if(table.nodes.begin(), table.nodes.end(), [&](const auto& node) {
                    return std::string_view(node.name.data()) ==
                           std::string_view(name.data(), end - name.begin());
                });
            destination = found - table.nodes.begin();
        }
        std::int32_t size;
        file.ReadBytes(&size, sizeof(size));
        if (size < 0 || size > file.RemainingBytes())
            throw std::runtime_error("invalid cursor userdata size");
        std::vector<std::byte> bytes(size);
        file.ReadBytes(bytes.data(), bytes.size());
        pending.emplace_back(destination, std::move(bytes));
    }
    for (auto& [index, bytes] : pending) {
        // Version 1 matched by position; an index past the table names the first node.
        if (version == 1 && index >= table.nodes.size()) index = 0;
        if (index < table.nodes.size()) table.nodes[index].userdata = std::move(bytes);
    }
}

void SaveWorldCursorNodeStates(wiz8::File& handle)
{
    const std::int32_t version = 2;
    const auto count = static_cast<std::uint32_t>(GetWorld()->cursor_nodes.size());
    handle.write(&version, sizeof(version));
    handle.write(&count, sizeof(count));
    for (const auto& node : GetWorld()->cursor_nodes) {
        if (node->userdata.size() > std::numeric_limits<std::int32_t>::max())
            throw std::length_error("cursor userdata exceeds save format");
        const auto size = static_cast<std::int32_t>(node->userdata.size());
        handle.write(node->name, sizeof(node->name));
        handle.write(&size, sizeof(size));
        handle.write(node->userdata.data(), node->userdata.size());
    }
}

/* Read every saved cube before publishing any, so a truncated section leaves
   the existing table untouched. Each record is the label parameters, local
   bounds (stored as the scale applied to the 500-unit cube), location and
   one opaque word; version 2 adds the name and version 3 the name serial. */
W8SavedCursorNodes ReadWorldCursorNodes(W8Chunk& file)
{
    W8SavedCursorNodes result;
    int version;
    unsigned int count;
    file.ReadBytes(&version, sizeof(version));
    if (static_cast<unsigned int>(version) == 0xdeadd00d) version = 1;
    if (version > 2) file.ReadBytes(&result.next_serial, sizeof(result.next_serial));
    file.ReadBytes(&count, sizeof(count));
    if (count > file.RemainingBytes() / (version >= 2 ? 84 : 52))
        throw std::runtime_error("invalid cursor node count");
    result.nodes.resize(count);
    for (unsigned int index = 0; index < count; ++index) {
        auto& node = result.nodes[index];
        if (version >= 2) {
            file.ReadBytes(node.name.data(), node.name.size());
            if (std::find(node.name.begin(), node.name.end(), '\0') == node.name.end())
                throw std::runtime_error("unterminated cursor node name");
        }
        if (node.name.front() == '\0') {
            const auto name = version >= 2 ? fmt::format("Cube{:03d}", result.next_serial++)
                                           : fmt::format("Cube{:d}", index);
            wiz8::text::write_utf8_field(name, node.name);
        }
        for (auto& value : node.parameters) file.ReadBytes(&value, sizeof(value));
        for (auto* vector : {&node.minimum, &node.maximum, &node.location}) {
            file.ReadBytes(&vector->x, sizeof(float));
            file.ReadBytes(&vector->y, sizeof(float));
            file.ReadBytes(&vector->z, sizeof(float));
        }
        file.ReadBytes(&node.value, sizeof(node.value));
    }
    return result;
}

void BuildWorldCursorNodes(W8World& world, W8SavedCursorNodes records)
{
    world.cursor_nodes.clear();
    world.cursor_serial = records.next_serial;
    for (auto& record : records.nodes) {
        auto cube = CreateWorldCursorCube();
        std::copy(record.name.begin(), record.name.end(), cube->name);
        std::copy(record.parameters.begin(), record.parameters.end(), cube->parameters);
        const auto scale =
            (record.maximum - record.minimum) * static_cast<float>(g_world_cursor_scale);
        static_cast<stMeshModel*>(cube->node->getModel())->scale(scale);
        cube->node->setLocation(glm::dvec3(record.location));
        cube->value_08 = record.value;
        cube->userdata = std::move(record.userdata);
        DrawWorldCursorNodeLabel(*cube);
        world.cursor_nodes.push_back(std::move(cube));
    }
}

void SaveWorldCursorNodes(wiz8::File& handle)
{
    const std::int32_t version = 3;
    const auto count = static_cast<std::uint32_t>(GetWorld()->cursor_nodes.size());

    handle.write(&version, sizeof(version));
    handle.write(&GetWorld()->cursor_serial, sizeof(GetWorld()->cursor_serial));
    handle.write(&count, sizeof(count));
    for (const auto& node : GetWorld()->cursor_nodes) {
        handle.write(node->name, sizeof(node->name));
        handle.write(node->parameters, sizeof(node->parameters));
        const auto bounds = node->node->getLocalBounds();
        const auto location = glm::vec3(node->node->getLocation());
        handle.write(&bounds.minimum, sizeof(bounds.minimum));
        handle.write(&bounds.maximum, sizeof(bounds.maximum));
        handle.write(&location, sizeof(location));
        handle.write(&node->value_08, sizeof(node->value_08));
    }
}

/* Reparent the world's cursor-attached nodes onto the dynamic scene, or
   detach them when hidden. Levels.cpp drives this from the world-cursor
   flag. */
void SetWorldCursorNodesVisible(bool visible)
{
    if (!GetWorld()) return;
    srNode* parent = visible ? GetWorld()->dynamic_scene : nullptr;
    for (const auto& entry : GetWorld()->cursor_nodes) {
        entry->node->setParent(parent, 1);
    }
}

/* Answer the next table node after `after` whose world-space bounds contain
   `point`; a null `after` starts the walk at the head of the table. When
   `after` is the last entry the walk answers null immediately. */
W8WorldCursorNode* FindWorldCursorNodeAtPoint(W8WorldCursorNode* after, glm::vec3* point)
{
    auto next = GetWorld()->cursor_nodes.begin();
    if (after != 0) {
        next = std::ranges::find(GetWorld()->cursor_nodes, after,
                                 &std::unique_ptr<W8WorldCursorNode>::get);
        if (next != GetWorld()->cursor_nodes.end()) ++next;
    }
    for (; next != GetWorld()->cursor_nodes.end(); ++next) {
        const auto& entry = *next;
        const auto bounds = entry->node->getLocalBounds();
        const auto location = glm::vec3(entry->node->getLocation());
        const srBoundingBox box{bounds.minimum + location, bounds.maximum + location};
        if (box.contains(*point)) {
            return entry.get();
        }
    }
    return 0;
}

/* Select a cursor node within the selection distance of the camera,
   remembering it for the next call. Answers whether one was close enough. */
bool SelectWorldCursorNode(void)
{
    if (GetWorld() != 0 && GetWorld()->camera != 0) {
        const glm::dvec3 camera_location(cameraLocation());
        const auto in_range = [&](const W8WorldCursorNode& entry) {
            return glm::length(entry.node->getLocation() - camera_location) <
                   g_cube_effect_max_distance;
        };
        const auto selected = static_cast<std::size_t>(g_cursor_node_index);
        if (selected < GetWorld()->cursor_nodes.size() &&
            in_range(*GetWorld()->cursor_nodes[selected])) {
            return true;
        }
        for (std::size_t index = 0; index < GetWorld()->cursor_nodes.size(); ++index) {
            if (in_range(*GetWorld()->cursor_nodes[index])) {
                g_cursor_node_index = static_cast<int>(index);
                return true;
            }
        }
    }
    return false;
}
