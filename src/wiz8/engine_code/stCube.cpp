#include "wiz8/engine_code/stCube.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "Font.h"
#include "compat/compiler.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "layouts/world.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srColorSurface.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srFilter.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srNode.h"
#include "surrender/srPixelConvert.h"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srTextureMap.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/sr_api.h"
#include "wiz8/surface_storage.h"
#include "wiz8/unicode.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

#define ST_CUBE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\stCube.cpp"

/* Copy the node's world location out; the searchable position resolver treats
   a missing node chain as unresolvable. */
// FUNCTION: WIZ8 0x0048d050
unsigned char W8WorldCursorNode::GetLocation(glm::vec3* position)
{
    if (node != 0) {
        *position = glm::vec3(node->getLocation());
        return 1;
    }
    return 0;
}

/* Cursor nodes are allocated separately so their addresses survive
   changes to the node table. */
// GLOBAL: WIZ8 0x0065ba58
std::vector<W8WorldCursorNode*> g_world_cursor_nodes;

/* The double selection range at 0x005ECAC8: node distances below it select the
   node. Read as 75000.0, not the zero a float view would give. */
// GLOBAL: WIZ8 0x005ecac8
const double g_cube_effect_max_distance = 75000.0;

// GLOBAL: WIZ8 0x0060a9b0
static int g_cursor_node_index = -1;

// GLOBAL: WIZ8 0x005ebf50
const double g_world_cursor_scale = 0.002;

/* 0x0048D070 is a bare JMP to CreateWorldCursorCube: a tail-jump thunk
   with no distinct source entity. */

/* Build the numbered cube the world cursor table holds: a 500-unit
   cube, a translucent white material, and a 32x32 texture the label painter
   later fills. */

// FUNCTION: WIZ8 0x0048d080
W8WorldCursorNode* CreateWorldCursorCube(void)
{
    auto model = std::make_shared<stMeshModel>(12, 24);
    stModelInstance* instance = new stModelInstance(0);
    W8WorldCursorNode* entry = new W8WorldCursorNode;
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

    auto surface =
        std::make_shared<srColorSurface>(srPixelConvert::SURFACE_ARGB1555, 0x20UL, 0x20UL);
    if (surface == 0) {
        srAssertFail("psrSurface", ST_CUBE_CPP, 0xac, 0);
    }

    w8_ulong pixel;
    unsigned char* bytes =
        reinterpret_cast<unsigned char*>(&pixel); // reinterpret-ok: packed colour storage
    bytes[3] = static_cast<unsigned char>(srFloatToInt(255.0));
    bytes[2] = static_cast<unsigned char>(srFloatToInt(0.0));
    bytes[1] = static_cast<unsigned char>(srFloatToInt(0.0));
    bytes[0] = static_cast<unsigned char>(srFloatToInt(127.5));
    surface->fill(pixel);
    surface->setFilter(&srBoxFilter);

    auto texture = std::make_shared<srTextureMap>();
    texture->setSurfacePtr(surface);
    texture->setMipmap(srTextureIFace::MIPMAP_NONE);

    model->setTexture(std::move(texture), 0, 0);

    model->setDirty(srMeshModel::DIRTY_TRI_MESH);
    model->setDirty(srMeshModel::DIRTY_BOUNDS);

    model->setName("stCube");
    instance->setModel(model);
    entry->node = instance;

    for (int index = 0; index < 3; ++index) {
        SetWorldCursorNodeParameter(entry, index, 0);
    }

    w8_ulong packed;
    PackColourToLong(&packed, 1.0, 0.0, 0.0, 0.5);
    SetWorldCursorNodeColor(entry, packed);
    DrawWorldCursorNodeLabel(entry);
    entry->pUserdata = 0;
    entry->userdata_size = 0;
    entry->name[0] = 0;

    g_world_cursor_nodes.push_back(entry);
    return entry;
}

// FUNCTION: WIZ8 0x0048da80
void DestroyWorldCursorCube(W8WorldCursorNode* entry)
{
    if (entry != 0) {
        if (entry->pUserdata != 0) {
            free(entry->pUserdata);
            entry->pUserdata = 0;
        }
        entry->userdata_size = 0;
        entry->node->setParent(0, 1);
        delete entry->node;
        wiz8::erase_first(g_world_cursor_nodes, entry);
        delete entry;
    }
}

// FUNCTION: WIZ8 0x0048dbf0
void MoveWorldCursorNode(W8WorldCursorNode* entry, glm::vec3* position)
{
    if (position == 0) {
        srAssertFail("vPos", ST_CUBE_CPP, 0x101, 0);
    }
    if (entry == 0) {
        srAssertFail("pCube", ST_CUBE_CPP, 0x102, 0);
    }
    glm::vec3 location = *position;
    if (entry == 0) {
        srAssertFail("pCube", ST_CUBE_CPP, 0x10b, 0);
    }
    if (entry->node != 0) {
        entry->node->setLocation(glm::dvec3(static_cast<double>(location.x),
                                            static_cast<double>(location.y),
                                            static_cast<double>(location.z)));
    }
}

// FUNCTION: WIZ8 0x0048dca0
void RefreshWorldCursorNodeLabel(W8WorldCursorNode* entry)
{
    DrawWorldCursorNodeLabel(entry);
}

/* Paint the three cube parameters onto the model's first texture using the menu
   small font. */
// FUNCTION: WIZ8 0x0048dcb0
void DrawWorldCursorNodeLabel(W8WorldCursorNode* entry)
{
    if (entry != 0) {
        stModelInstance* instance = static_cast<stModelInstance*>(entry->node);
        if (instance == 0) {
            srAssertFail("pstModelInstance", ST_CUBE_CPP, 0x124, 0);
        }
        stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());
        if (mesh == 0) {
            srAssertFail("pstMeshModel", ST_CUBE_CPP, 0x127, 0);
        }
        srTextureMap* texture = static_cast<srTextureMap*>(mesh->getTexture(0, 0));
        if (texture == 0) {
            srAssertFail("psrTexture", ST_CUBE_CPP, 0x12a, 0);
        }
        srColorSurfaceIFace* surface = texture->getSurfacePtr();
        surface->fill(entry->color);
        unsigned char* data = static_cast<unsigned char*>(surface->getDataPtr());
        if (data == 0) {
            srAssertFail("pBuffer", ST_CUBE_CPP, 0x130, 0);
        }
        {
            g_smfnt_font.resetPalette();
            SDLSurfaceOwner text_surface{
                SDL_CreateSurfaceFrom(surface->getWidth(), surface->getHeight(),
                                      SDL_PIXELFORMAT_RGB565, data, surface->getPitch()),
                SDL_DestroySurface};
            if (text_surface) {
                for (int index = 0; index < 3; ++index) {
                    std::string text;
                    text = fmt::format("{:d}", entry->parameters[index]);
                    DrawText(*text_surface, g_smfnt_font, {0, g_smfnt_font.height() * index}, text);
                }
            }
            texture->invalidate();
        }
    }
}

// Scales the cursor's mesh by per-axis factors; a missing node or model is a no-op.
static void scaleWorldCursorNode(W8WorldCursorNode* entry, const glm::vec3& factors)
{
    if (entry != 0) {
        stMeshModel* model =
            static_cast<stMeshModel*>(static_cast<stModelInstance*>(entry->node)->getModel());
        if (model != 0) {
            model->scale(factors);
        }
    }
}

// FUNCTION: WIZ8 0x0048de40
void ScaleWorldCursorNodeX(W8WorldCursorNode* entry, double scale)
{
    scaleWorldCursorNode(entry, glm::vec3(static_cast<float>(scale), 1.0f, 1.0f));
}

// FUNCTION: WIZ8 0x0048de90
void ScaleWorldCursorNodeY(W8WorldCursorNode* entry, double scale)
{
    scaleWorldCursorNode(entry, glm::vec3(1.0f, static_cast<float>(scale), 1.0f));
}

// FUNCTION: WIZ8 0x0048dee0
void ScaleWorldCursorNodeZ(W8WorldCursorNode* entry, double scale)
{
    scaleWorldCursorNode(entry, glm::vec3(1.0f, 1.0f, static_cast<float>(scale)));
}

/* Debug wireframe box: project the eight corners of a world-aligned bounding
   box through the world camera, bail if any corner is off-screen, then paint
   the twelve edges into the primary GERD's back buffer. */
// FUNCTION: WIZ8 0x0048DF30
void DrawWorldBox(W8World* world, glm::vec3 minimum, glm::vec3 maximum, w8_ulong color)
{
    if (world == 0) {
        return;
    }

    glm::vec3 corners[8];
    corners[0] = glm::vec3(minimum.x, minimum.y, minimum.z);
    corners[1] = glm::vec3(minimum.x, minimum.y, maximum.z);
    corners[2] = glm::vec3(maximum.x, minimum.y, maximum.z);
    corners[3] = glm::vec3(maximum.x, minimum.y, minimum.z);
    corners[4] = glm::vec3(minimum.x, maximum.y, minimum.z);
    corners[5] = glm::vec3(minimum.x, maximum.y, maximum.z);
    corners[6] = glm::vec3(maximum.x, maximum.y, maximum.z);
    corners[7] = glm::vec3(maximum.x, maximum.y, minimum.z);

    const auto viewport = GetScaledViewportBounds();
    float viewport_width = viewport.maximum.x - viewport.minimum.x;
    float viewport_height = viewport.maximum.y - viewport.minimum.y;

    w8_long screen[8][2];
    for (int index = 0; index < 8; ++index) {
        const auto projection = world->camera->project(glm::dvec3(corners[index]));
        if (projection.result == srCamera::PROJECTION_RESULT_OUTSIDE_CLIP_RANGE) return;
        const auto projected = projection.position;
        screen[index][0] =
            static_cast<w8_long>((projected.x * viewport_width + viewport.minimum.x) * 640.0f);
        screen[index][1] =
            static_cast<w8_long>((projected.y * viewport_height + viewport.minimum.y) * 480.0f);
        if (screen[index][0] < 0 || screen[index][0] > 640 || screen[index][1] < 0 ||
            screen[index][1] > 480) {
            return;
        }
    }

    DrawBufferLine(screen[0][0], screen[0][1], screen[1][0], screen[1][1], &color);
    DrawBufferLine(screen[1][0], screen[1][1], screen[2][0], screen[2][1], &color);
    DrawBufferLine(screen[2][0], screen[2][1], screen[3][0], screen[3][1], &color);
    DrawBufferLine(screen[3][0], screen[3][1], screen[0][0], screen[0][1], &color);
    DrawBufferLine(screen[4][0], screen[4][1], screen[5][0], screen[5][1], &color);
    DrawBufferLine(screen[5][0], screen[5][1], screen[6][0], screen[6][1], &color);
    DrawBufferLine(screen[6][0], screen[6][1], screen[7][0], screen[7][1], &color);
    DrawBufferLine(screen[7][0], screen[7][1], screen[4][0], screen[4][1], &color);
    DrawBufferLine(screen[0][0], screen[0][1], screen[4][0], screen[4][1], &color);
    DrawBufferLine(screen[1][0], screen[1][1], screen[5][0], screen[5][1], &color);
    DrawBufferLine(screen[2][0], screen[2][1], screen[6][0], screen[6][1], &color);
    DrawBufferLine(screen[3][0], screen[3][1], screen[7][0], screen[7][1], &color);
}

/* The three label parameters double as generic per-node parameters; the world
   cursor and the master-function table index into them by slot. */
// FUNCTION: WIZ8 0x0048E2B0
int GetWorldCursorNodeParameter(W8WorldCursorNode* entry, int index)
{
    if (entry != 0) {
        return entry->parameters[index];
    }
    return -1;
}

/* Set a parameter and retire the userdata scratch - the handlers lazily
   allocate it again on the next visit. */
// FUNCTION: WIZ8 0x0048E2D0
void SetWorldCursorNodeParameter(W8WorldCursorNode* entry, int index, int value)
{
    if (entry != 0) {
        entry->parameters[index] = value;
        if (entry->pUserdata != 0) {
            free(entry->pUserdata);
            entry->pUserdata = 0;
        }
        entry->userdata_size = 0;
    }
}

/* Nearest cursor node to the world camera that is bound to the current pick
   model instance. The screen-point parameters are carried but unused - the
   pick is purely camera-distance based. */
// FUNCTION: WIZ8 0x0048e310
W8WorldCursorNode* FindNearestWorldCursorNode(int x, int y)
{
    float nearest = 999999.0f;
    W8WorldCursorNode* result = 0;
    int count = static_cast<int>(g_world_cursor_nodes.size());

    for (int index = 0; index < count; ++index) {
        W8WorldCursorNode* entry = g_world_cursor_nodes[index];
        if (entry != 0 && entry->node != 0 && entry->node == GetPickedModelInstance()) {
            const auto camera = g_world->camera->getLocation();
            const auto node = entry->node->getLocation();
            float distance = static_cast<float>(
                std::hypot(node.x - camera.x, node.y - camera.y, node.z - camera.z));
            if (distance < nearest) {
                nearest = distance;
                result = entry;
            }
        }
    }
    return result;
}

// FUNCTION: WIZ8 0x0048e3e0
W8WorldCursorNode* PickWorldCursorNodeAtScreenPoint(int x, int y)
{
    return FindNearestWorldCursorNode(x, y);
}

/* Store the packed fill colour and repaint the cube parameters. */
// FUNCTION: WIZ8 0x0048e400
void SetWorldCursorNodeColor(W8WorldCursorNode* entry, w8_ulong color)
{
    entry->color = color;
    DrawWorldCursorNodeLabel(entry);
}

/* Pack the RGB components at full alpha and repaint. */
// FUNCTION: WIZ8 0x0048E420
void SetWorldCursorNodeColorComponents(W8WorldCursorNode* entry, float red, float green, float blue)
{
    w8_ulong packed;
    SetWorldCursorNodeColor(entry, *PackColourToLong(&packed, 1.0, red, green, blue));
}

// FUNCTION: WIZ8 0x0048e470
unsigned int LoadWorldCursorNodeStates(wiz8::File& handle)
{
    int version;
    unsigned int count;
    unsigned int index;
    bool success = true;

    if (!(handle.read(&version, 4).bytes == static_cast<std::size_t>(4))) {
        return 0;
    }
    if (version == 0x21122112) {
        version = 1;
    }
    if (!(handle.read(&count, 4).bytes == static_cast<std::size_t>(4))) {
        return 0;
    }

    for (index = 0; index < count; ++index) {
        W8WorldCursorNode* cube = 0;
        bool temporary = false;

        if (version < 2) {
            cube = GetWorldCursorNode(index);
        } else {
            char name[0x20];
            int cube_index;

            handle.read_exact(name, sizeof(name));
            for (cube_index = 0; cube_index < static_cast<int>(g_world_cursor_nodes.size());
                 ++cube_index) {
                W8WorldCursorNode* candidate = g_world_cursor_nodes[cube_index];
                if (strcmp(candidate->name, name) == 0) {
                    cube = candidate;
                    break;
                }
            }
            if (cube == 0) {
                cube = new W8WorldCursorNode;
                temporary = true;
            }
        }

        if (success &&
            (handle.read(&cube->userdata_size, 4).bytes == static_cast<std::size_t>(4))) {
            success = true;
        } else {
            success = false;
        }
        if (cube->userdata_size != 0) {
            cube->pUserdata = malloc(cube->userdata_size);
            if (cube->pUserdata == 0) {
                srAssertFail("pCube->pUserdata", ST_CUBE_CPP, 0x3c8, 0);
            }
            memset(cube->pUserdata, 0, cube->userdata_size);
            if (success && (handle.read(cube->pUserdata, cube->userdata_size).bytes ==
                            static_cast<std::size_t>(cube->userdata_size))) {
                success = true;
            } else {
                success = false;
            }
        }
        if (temporary && cube != 0) {
            if (cube->pUserdata != 0) {
                free(cube->pUserdata);
                cube->pUserdata = 0;
            }
            cube->userdata_size = 0;
            delete cube;
        }
    }
    return count;
}

// FUNCTION: WIZ8 0x0048e6d0
unsigned char SaveWorldCursorNodeStates(wiz8::File& handle)
{
    bool ok = true;
    int version = 2;
    unsigned int count;
    unsigned int index;
    W8WorldCursorNode* node;

    if (!(handle.write(&version, 4), true)) {
        return 0;
    }
    count = static_cast<int>(g_world_cursor_nodes.size());
    if (!(handle.write(&count, 4), true)) {
        return 0;
    }
    for (index = 0; index < count && ok; ++index) {
        node = g_world_cursor_nodes[index];
        ok = (handle.write(node->name, sizeof(node->name)), true) &&
             (handle.write(&node->userdata_size, 4), true);
        if (node->userdata_size != 0) {
            ok = ok && (handle.write(node->pUserdata, node->userdata_size), true);
        }
    }
    return ok;
}

// FUNCTION: WIZ8 0x0048e7b0
unsigned int LoadWorldCursorNodes(wiz8::File& handle)
{
    int version;
    unsigned int count;
    unsigned int index;
    bool success = true;

    if (!(handle.read(&version, 4).bytes == static_cast<std::size_t>(4))) {
        return 0;
    }
    if (static_cast<unsigned int>(version) == 0xdeadd00d) {
        version = 1;
    }
    if (version > 2) {
        handle.read_exact(&gXStatus.mipe_cube_serial, 4);
    } else {
        gXStatus.mipe_cube_serial = 100;
    }
    if (!(handle.read(&count, 4).bytes == static_cast<std::size_t>(4))) {
        return 0;
    }

    for (index = 0; index < count; ++index) {
        W8WorldCursorNode* cube = CreateWorldCursorCube();
        glm::vec3 minimum;
        glm::vec3 maximum;
        glm::vec3 location;
        int component;

        if (version >= 2) {
            handle.read_exact(cube->name, 0x20);
            if (cube->name[0] == 0) {
                wiz8::text::write_utf8_field(fmt::format("Cube{:03d}", gXStatus.mipe_cube_serial++),
                                             cube->name);
            }
        } else {
            wiz8::text::write_utf8_field(fmt::format("Cube{:d}", index), cube->name);
        }
        for (component = 0; component < 3; ++component) {
            if (success && (handle.read(&cube->parameters[component], 4).bytes ==
                            static_cast<std::size_t>(4))) {
                success = true;
            } else {
                success = false;
            }
        }
        for (component = 0; component < 3; ++component) {
            if (success &&
                (handle.read(&minimum[component], 4).bytes == static_cast<std::size_t>(4))) {
                success = true;
            } else {
                success = false;
            }
        }
        for (component = 0; component < 3; ++component) {
            if (success &&
                (handle.read(&maximum[component], 4).bytes == static_cast<std::size_t>(4))) {
                success = true;
            } else {
                success = false;
            }
        }

        glm::vec3 scale((maximum.x - minimum.x) * static_cast<float>(g_world_cursor_scale),
                        (maximum.y - minimum.y) * static_cast<float>(g_world_cursor_scale),
                        (maximum.z - minimum.z) * static_cast<float>(g_world_cursor_scale));
        if (cube != 0) {
            stModelInstance* instance = static_cast<stModelInstance*>(cube->node);
            if (instance != 0) {
                stMeshModel* model = static_cast<stMeshModel*>(instance->getModel());
                if (model != 0) {
                    model->scale(scale);
                }
            }
        }

        for (component = 0; component < 3; ++component) {
            if (success &&
                (handle.read(&location[component], 4).bytes == static_cast<std::size_t>(4))) {
                success = true;
            } else {
                success = false;
            }
        }
        if (cube == 0) {
            srAssertFail("pCube", ST_CUBE_CPP, 0x10b, 0);
        }
        if (cube->node != 0) {
            glm::dvec3 node_location(static_cast<double>(location.x),
                                     static_cast<double>(location.y),
                                     static_cast<double>(location.z));
            cube->node->setLocation(node_location);
        }
        if (success && (handle.read(&cube->value_08, 4).bytes == static_cast<std::size_t>(4))) {
            success = true;
        } else {
            success = false;
        }
        DrawWorldCursorNodeLabel(cube);
    }
    return count;
}

// FUNCTION: WIZ8 0x0048ead0
unsigned char SaveWorldCursorNodes(wiz8::File& handle)
{
    bool ok = true;
    int version = 3;
    unsigned int count;
    unsigned int index;
    int component;
    glm::vec3 location;
    W8WorldCursorNode* node;

    if (!(handle.write(&version, 4), true)) {
        return 0;
    }
    handle.write(&gXStatus.mipe_cube_serial, 4);
    count = static_cast<int>(g_world_cursor_nodes.size());
    if (!(handle.write(&count, 4), true)) {
        return 0;
    }
    for (index = 0; index < count && ok; ++index) {
        node = g_world_cursor_nodes[index];
        handle.write(node->name, sizeof(node->name));
        for (component = 0; component < 3 && ok; ++component) {
            ok = (handle.write(&node->parameters[component], 4), true);
        }
        const auto bounds = node->node->getLocalBounds();
        location = glm::vec3(node->node->getLocation());
        ok = ok && (handle.write(&bounds.minimum.x, 4), true) &&
             (handle.write(&bounds.minimum.y, 4), true) &&
             (handle.write(&bounds.minimum.z, 4), true) &&
             (handle.write(&bounds.maximum.x, 4), true) &&
             (handle.write(&bounds.maximum.y, 4), true) &&
             (handle.write(&bounds.maximum.z, 4), true) && (handle.write(&location.x, 4), true) &&
             (handle.write(&location.y, 4), true) && (handle.write(&location.z, 4), true) &&
             (handle.write(&node->value_08, 4), true);
    }
    return ok;
}

// FUNCTION: WIZ8 0x0048ED00
int GetWorldCursorNodeCount(void)
{
    return static_cast<int>(g_world_cursor_nodes.size());
}

// FUNCTION: WIZ8 0x0048ED10
W8WorldCursorNode* GetWorldCursorNode(int index)
{
    return g_world_cursor_nodes[index];
}

/* Attach or detach the node's scene node under the world's dynamic scene. */
// FUNCTION: WIZ8 0x0048ED30
void AttachWorldCursorNode(W8WorldCursorNode* entry, bool attached)
{
    if (entry != 0) {
        if (attached) {
            entry->node->setParent(g_world->dynamic_scene, 1);
            return;
        }
        entry->node->setParent(0, 1);
    }
}

/* Copy `name` into the node's fixed 0x20-byte label field, always leaving a
   terminator. */
// FUNCTION: WIZ8 0x0048F110
void SetWorldCursorNodeName(W8WorldCursorNode* entry, const char* name)
{
    strncpy(entry->name, name, 0x20);
    entry->name[0x1f] = 0;
}

/* Reparent the world's cursor-attached nodes onto the dynamic scene, or
   detach them when hidden. Levels.cpp drives this from the world-cursor
   flag. */
// FUNCTION: WIZ8 0x0048ED70
void SetWorldCursorNodesVisible(bool visible)
{
    unsigned int count = static_cast<int>(g_world_cursor_nodes.size());

    for (unsigned int index = 0; index < count; ++index) {
        W8WorldCursorNode* entry = g_world_cursor_nodes[index];

        if (entry != 0) {
            srNode* parent = 0;
            if (visible) {
                parent = g_world->dynamic_scene;
            }
            entry->node->setParent(parent, 1);
        }
    }
}

/* Answer the next table node after `after` whose world-space bounds contain
   `point`; a null `after` starts the walk at the head of the table. When
   `after` is the last entry the walk answers null immediately. */
// FUNCTION: WIZ8 0x0048EDD0
W8WorldCursorNode* FindWorldCursorNodeAtPoint(W8WorldCursorNode* after, glm::vec3* point)
{
    unsigned int index = 0;
    unsigned int count = static_cast<int>(g_world_cursor_nodes.size());

    if (after != 0) {
        for (unsigned int i = 0; i < count; ++i) {
            if (g_world_cursor_nodes[i] == after) {
                index = i + 1;
                if (i == count - 1) {
                    return 0;
                }
            }
        }
    }
    while (index < count) {
        W8WorldCursorNode* entry = g_world_cursor_nodes[index];
        if (entry->node != 0) {
            const auto bounds = entry->node->getLocalBounds();
            const auto location = glm::vec3(entry->node->getLocation());
            const srBoundingBox box{bounds.minimum + location, bounds.maximum + location};
            if (box.contains((*point))) {
                return entry;
            }
        }
        ++index;
    }
    return 0;
}

/* Allocate the node's userdata scratch, or release it when `size` is zero.
   Retail overwrites an existing allocation without freeing it first. */
// FUNCTION: WIZ8 0x0048EF40
void SetWorldCursorNodeUserdataSize(W8WorldCursorNode* entry, int size)
{
    if (entry != 0) {
        if (size != 0) {
            entry->pUserdata = malloc(size);
            if (entry->pUserdata == 0) {
                srAssertFail("pCube->pUserdata", ST_CUBE_CPP, 0x3c8, 0);
            }
            memset(entry->pUserdata, 0, size);
        } else {
            if (entry->pUserdata != 0) {
                free(entry->pUserdata);
                entry->pUserdata = 0;
            }
        }
        entry->userdata_size = size;
    }
}

/* Select the cursor node nearest the camera within the selection distance,
   remembering it for the next call. Answers whether one was close enough.
   Retail loads entry->node->getLocation() before TEST ESI,ESI on both the
   cached-index path and the table scan; there is no separate node null
   check. Keep that load order. */
// FUNCTION: WIZ8 0x0048EFC0
bool SelectWorldCursorNode(void)
{
    if (g_world != 0 && g_world->camera != 0) {
        const glm::dvec3 camera_location(cameraLocation());
        int selected = g_cursor_node_index;
        if (static_cast<std::size_t>(selected) < g_world_cursor_nodes.size()) {
            W8WorldCursorNode* entry = g_world_cursor_nodes[selected];
            glm::dvec3 target = entry->node->getLocation();

            if (entry != 0) {
                glm::dvec3 delta = target;

                delta -= camera_location;
                if (glm::length(delta) < g_cube_effect_max_distance) {
                    return true;
                }
            }
        }
        int count = static_cast<int>(g_world_cursor_nodes.size());
        for (int index = 0; index < count; ++index) {
            W8WorldCursorNode* entry = g_world_cursor_nodes[index];
            glm::dvec3 target = entry->node->getLocation();

            if (entry != 0) {
                glm::dvec3 delta = target;

                delta -= camera_location;
                if (glm::length(delta) < g_cube_effect_max_distance) {
                    g_cursor_node_index = index;
                    return true;
                }
            }
        }
    }
    return false;
}

/* Release every node the cursor table still holds: free its scratch buffer,
   detach and release its scene node, drop it from the table and run its own
   destructor. A null head with a nonzero count spins, as in retail. */
// FUNCTION: WIZ8 0x0048DB30
void ReleaseWorldCursorNodes(void)
{
    while (!g_world_cursor_nodes.empty()) {
        W8WorldCursorNode* entry = g_world_cursor_nodes[0];

        DestroyWorldCursorCube(entry);
    }
}
