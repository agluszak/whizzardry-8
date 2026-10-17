#pragma once

#include <memory>
#include <string_view>

#include "compat/compiler.h"
#include "surrender/srMaterial.h"
#include "surrender/srShader.h"
#include "surrender/srVertexProcessor.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/texture_animation.h"

class srMaterial;
class srMeshModel;
class srModelInstance;
class srTexture;
class srTexture;
class stTextureAnim;
template<class T> class ResourceCache;

/* Combinable flags in level materials; distinct from srShader's packed word. */
enum { W8_MATERIAL_TWO_SIDED = 1u, W8_MATERIAL_NORMAL_TEXCOORD_MASK = 0x1feu };

// Applies the game's per-draw material overrides before CPU lighting.
class stMaterial : public srMaterial {
public:
    stMaterial();
    virtual srVertexProcessor::MaterialInfo getMaterialInfo() override;

public:
    stMaterial(const stMaterial&) = default;
    stMaterial& operator=(const stMaterial&) = default;
    int m_surface_flags; /* 0x78 */
};

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
    w8_ulong surface_flags;           /* 0x116 */
    float texture_modes[4];                /* 0x11a */
};

#pragma pack(pop)

static_assert(sizeof(W8MaterialRecord) == 0x12a, "W8MaterialRecord is a serialized record");

/* Per-draw material override switches consumed by stMaterial::getMaterialInfo;
   stModelInstance's mesh submit arms them around each chained model. */
extern bool g_material_diffuse_scale_enabled;
extern float g_material_diffuse_scale;
extern bool g_material_emissive_override_enabled;
extern float g_material_emissive_override;

class stTextureFile;

/* Loaded resources by name; each lives while something renders with it. */
extern ResourceCache<stTextureFile> g_texture_files;
extern ResourceCache<stTextureAnim> g_texture_animations;

/* A level material's shared render state. */
struct W8MeshMaterial {
    std::shared_ptr<srMaterial> material;
    std::shared_ptr<srTexture> texture;
    srShader render_flags;
};

W8MeshMaterial LoadMaterial(const char* bitmap_folder, const W8MaterialRecord& source);
std::shared_ptr<srTexture> LoadTextureFromFolder(const char* folder, const char* name,
                                                 bool required);
std::shared_ptr<stTextureAnim> LoadAnimatedTexture(const char* folder, const char* name,
                                                   const W8MaterialRecord* source, bool required);
bool MeshHasAnimatedTexture(srMeshModel* model);
void SetModelAnimatedTextureFrame(srModelInstance* instance, int frame);
stTextureAnim* GetModelAnimatedTexture(srModelInstance* instance);

W8MeshMaterial CreateDefaultMaterial();
std::shared_ptr<srTexture> LoadTextureFromPath(const char* path, const W8MaterialRecord* source,
                                               bool required);

struct W8OctRegionPolygon;

