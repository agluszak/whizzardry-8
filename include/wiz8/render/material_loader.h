#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include "wiz8/render/draw_shader.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/texture_animation.h"

class Material;
class Mesh;
class MeshInstance;
class Texture;
class Texture;
template<class T> class ResourceCache;

/* Combinable flags in level materials; distinct from DrawShader's packed word. */
enum { W8_MATERIAL_TWO_SIDED = 1u, W8_MATERIAL_NORMAL_TEXCOORD_MASK = 0x1feu };

#pragma pack(push, 1)

/* Engine Code\materials.cpp consumes this serialized material record from
   level particles and animated-texture descriptors. The four 40-byte texture
   names and the unaligned tail fields are fixed by 0x004B8A70/0x004B98F0. */
struct W8MaterialRecord {
    unsigned char version;                 /* 0x000 */
    char texture_name[0x28];               /* 0x001 */
    char texture_names[4][0x28];           /* 0x029 */
    wiz8::retail::Vec3f ambient;             /* 0x0c9 */
    wiz8::retail::Vec3f diffuse;             /* 0x0d5 */
    wiz8::retail::Vec3f emissive_colour;     /* 0x0e1 */
    wiz8::retail::Vec3f specular;            /* 0x0ed */
    float shininess;                       /* 0x0f9 */
    float opacity;                         /* 0x0fd */
    float emission;                        /* 0x101 */
    unsigned char padding_105[8];          /* 0x105 */
    W8TextureAnimationMode animation_mode; /* 0x10d */
    int animation_frame;                   /* 0x10e */
    float animation_rate;                  /* 0x112 */
    std::uint32_t surface_flags;           /* 0x116 */
    float texture_modes[4];                /* 0x11a */
};

#pragma pack(pop)

static_assert(sizeof(W8MaterialRecord) == 0x12a, "W8MaterialRecord is a serialized record");

class Texture;

/* Loaded resources by name; each lives while something renders with it. */
extern ResourceCache<Texture> g_texture_animations;

/* A level material's shared render state. */
struct W8MeshMaterial {
    std::shared_ptr<Material> material;
    std::shared_ptr<Texture> texture;
    DrawShader render_flags;
    bool two_sided = false; // Geometry construction, independent of shared render state.
};

W8MeshMaterial LoadMaterial(const std::filesystem::path& texture_directory,
                            const W8MaterialRecord& source);
bool MeshHasAnimatedTexture(Mesh* model);
void SetModelAnimatedTextureFrame(MeshInstance* instance, int frame);
Texture* GetModelAnimatedTexture(MeshInstance* instance);

W8MeshMaterial CreateDefaultMaterial();
std::shared_ptr<Texture> LoadTextureFromPath(const std::filesystem::path& path,
                                             const W8MaterialRecord* source, bool required);

struct W8OctRegionPolygon;
