#include "wiz8/engine_code/materials.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <wiz8/filesystem.h>

#include "OctMeshModel.h"
#include "asset_paths.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/mesh_resources.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "surrender/srVertexPipe.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/packed_bits.h"
#include "wiz8/resource_cache.h"

#define MATERIALS_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\materials.cpp"

/* The global at 0x0065BEA8 is a real zero-storage srVertexProcessor subclass:
   its non-template process body establishes the boundary independently of its
   constructor and vtable, and that body maps eye-space normals into the first
   texture-coordinate set. No source or export name survives, so the class
   keeps a descriptive name anchored at its constructor address. */
// VTABLE: WIZ8 0x005ED0D0
// class W8NormalTexcoordMapper
class W8NormalTexcoordMapper : public srVertexProcessor {
public:
    // FUNCTION: WIZ8 0x004D6190
    virtual int isActive(srVertexPipe&) override { return 1; }
    virtual void process(srVertexPipe& pipe) override;
};

// GLOBAL: WIZ8 0x0065BEA8
static W8NormalTexcoordMapper g_normal_texcoord_mapper;

// GLOBAL: WIZ8 0x0065BA9E
bool g_material_diffuse_scale_enabled;
// GLOBAL: WIZ8 0x0065BAA0
float g_material_diffuse_scale;
// GLOBAL: WIZ8 0x0065BAA4
bool g_material_emissive_override_enabled;
// GLOBAL: WIZ8 0x0065BAA8
float g_material_emissive_override;

/* Convert eye-space normals to the material's first texture-coordinate set.
   The exported srVertexPipe queries preserve the closed renderer's ownership
   of its internal workspace while expressing every operation in this body. */
// FUNCTION: WIZ8 0x004B89B0
void W8NormalTexcoordMapper::process(srVertexPipe& pipe)
{
    glm::vec2* coordinates;
    w8_ulong count;
    w8_ulong index;

    if (!pipe.isChannelAvailable(srVertexProcessor::CHANNEL_ST0)) {
        return;
    }
    const auto normals = pipe.getEyeSpaceNormal();
    coordinates = pipe.getST(0, 1);
    count = pipe.getVertexCount();

    for (index = 0; index < count; ++index) {
        coordinates[index] =
            glm::vec2((normals[index].x + 1.0f) * 0.5f, (normals[index].y + 1.0f) * 0.5f);
    }
}

// FUNCTION: WIZ8 0x004925B0
stMaterial::stMaterial()
{
    m_surface_flags = 0;
}

// FUNCTION: WIZ8 0x004928F0
srVertexProcessor::MaterialInfo stMaterial::getMaterialInfo()
{
    auto info = srMaterial::getMaterialInfo();
    if (g_material_diffuse_scale_enabled) {
        info.diffuse.w *= g_material_diffuse_scale;
    }
    if (g_material_emissive_override_enabled) {
        info.emissive = glm::vec4(g_material_emissive_override, g_material_emissive_override,
                                  g_material_emissive_override, 1.0f);
    }
    return info;
}

namespace {
// Exact render-state identity; serialized padding and texture filenames do not
// belong to the material state shared by separate mesh texture assignments.
using MaterialKey = std::array<std::uint32_t, 13>;
std::map<MaterialKey, std::weak_ptr<stMaterial>> level_materials;
std::weak_ptr<stMaterial> default_material;
}
ResourceCache<stTextureFile> g_texture_files;
ResourceCache<stTextureAnim> g_texture_animations;

/* Load and share the material state used by the runtime renderer. */
W8MeshMaterial LoadMaterial(const char* bitmap_folder, const W8MaterialRecord& record)
{
    const W8MaterialRecord* source = &record;
    W8MeshMaterial result;
    std::shared_ptr<srTexture>& texture = result.texture;
    srShader* render_flags = &result.render_flags;
    std::string texture_path;
    int texture_index = -1;
    bool has_alpha = false;
    int index;

    render_flags->value = 0x0100a51b; /* packed srShader; TEXTURING set until no texture */
    for (index = 0; index < 4; ++index) {
        if (source->texture_names[index][0] != '\0') {
            const std::string_view field(source->texture_names[index],
                                         sizeof(source->texture_names[index]));
            const auto terminator = field.find('\0');
            if (terminator == field.npos)
                throw std::runtime_error("Unterminated material texture name");
            const auto name = field.substr(0, terminator);
            texture_path = bitmap_folder[0] == '\0' ? std::string(name)
                                                    : fmt::format("{}\\{}", bitmap_folder, name);
            texture_index = index;
            /* Retail 0x004b8c93: a texture found only in the fourth slot marks
               the material as alpha-carrying before any texture is loaded. */
            if (index == 3) {
                has_alpha = true;
            }
            break;
        }
    }

    if (texture_path.empty()) {
        render_flags->value &= ~srShader::MASK_TEXTURING;
    } else {
        const std::string_view path = texture_path;
        const auto separator = path.find_last_of("\\/");
        const std::size_t file_start = separator == std::string_view::npos ? 0 : separator + 1;
        const std::string texture_folder(path.substr(0, file_start));
        const std::string texture_file(path.substr(file_start));
        const auto dot = texture_file.rfind('.');
        const char* extension =
            texture_file.c_str() + (dot == std::string::npos ? texture_file.size() : dot);

        if (path.size() > 3 && SDL_strncasecmp(extension, ".IFL", 4) == 0) {
            texture =
                LoadAnimatedTexture(texture_folder.c_str(), texture_file.c_str(), source, true);
        } else {
            texture = LoadTextureFromFolder(texture_folder.c_str(), texture_file.c_str(), true);
        }
        if (texture == 0) {
            return result;
        }

        if (auto* file = dynamic_cast<stTextureFile*>(texture.get()); file && file->hasAlpha()) {
            has_alpha = true;
        }
        if (auto* animation = dynamic_cast<stTextureAnim*>(texture.get())) {
            if (animation->Prepare()) {
                has_alpha = true;
            }
            if (source->version > 3 && source->texture_modes[texture_index] > 0.0f) {
                float mode = source->texture_modes[texture_index];
                if (mode <= 1.0f) {
                    animation->trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_FRAME;
                } else {
                    animation->trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_START;
                    mode -= 1.0f;
                }
                animation->probability = mode;
            }
        }
    }

    if (source->opacity < 1.0f || has_alpha) {
        if (texture_index == 0 || texture_index == 1) {
            render_flags->value = (render_flags->value & 0xffffdfbfUL) | 0x40a0;
        } else if (texture_index == 2 || texture_index == 3) {
            render_flags->value = (render_flags->value & 0xffffdc3fUL) | 0x4020;
        }
        render_flags->value &= ~8UL;
    }

    const bool textured = !texture_path.empty();
    // Take scalar values before bit_cast: packed disk fields must not escape
    // through references that require native float alignment.
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    const MaterialKey key{bits(source->specular.x),
                          bits(source->specular.y),
                          bits(source->specular.z),
                          bits(source->diffuse.x),
                          bits(source->diffuse.y),
                          bits(source->diffuse.z),
                          bits(source->opacity == 0.0f ? 0.7f : source->opacity),
                          bits(textured ? source->ambient.x : source->diffuse.x),
                          bits(textured ? source->ambient.y : source->diffuse.y),
                          bits(textured ? source->ambient.z : source->diffuse.z),
                          bits(textured ? source->emission : 0.f),
                          source->surface_flags,
                          textured};
    auto& cached = level_materials[key];
    auto concrete = cached.lock();
    if (!concrete) {
        concrete = std::make_shared<stMaterial>();
        concrete->setName("Level material");
        cached = concrete;
        concrete->parms.specular =
            glm::vec4(source->specular.x, source->specular.y, source->specular.z, 0.0f);
        concrete->dirty = 1;
        concrete->parms.shininess = 1.0f;
        concrete->dirty = 1;

        concrete->parms.diffuse.x = source->diffuse.x;
        concrete->parms.diffuse.y = source->diffuse.y;
        concrete->parms.diffuse.z = source->diffuse.z;
        concrete->parms.diffuse.w = source->opacity == 0.0f ? 0.7f : source->opacity;
        concrete->dirty = 1;
        concrete->setOpacity(source->opacity == 0.0f ? 0.7f : source->opacity);

        if (texture_path.empty()) {
            concrete->parms.ambient =
                glm::vec4(source->diffuse.x, source->diffuse.y, source->diffuse.z, 1.0f);
            concrete->dirty = 1;
            concrete->parms.emissive = glm::vec4(0.0f);
        } else {
            concrete->parms.ambient =
                glm::vec4(source->ambient.x, source->ambient.y, source->ambient.z, 0.0f);
            concrete->dirty = 1;
            concrete->parms.emissive =
                glm::vec4(source->emission, source->emission, source->emission, 1.0f);
        }
        concrete->dirty = 1;
        concrete->m_surface_flags = source->surface_flags;
        if ((source->surface_flags & W8_MATERIAL_NORMAL_TEXCOORD_MASK) != 0) {
            concrete->setMapper(&g_normal_texcoord_mapper);
        }
    }
    result.material = std::move(concrete);
    return result;
}

// STRING: WIZ8 0x0060e0f4
#define DEFAULT_MATERIAL_NAME "Default Material"

// FUNCTION: WIZ8 0x004B9280
W8MeshMaterial CreateDefaultMaterial()
{
    W8MeshMaterial result;
    result.render_flags.value = 0x0100251b;

    auto concrete = default_material.lock();
    if (concrete == 0) {
        concrete = std::make_shared<stMaterial>();
        concrete->setName(DEFAULT_MATERIAL_NAME);
        default_material = concrete;

        glm::vec4 color;
        color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        concrete->setAmbient(color);
        concrete->setDiffuse(color);
        color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        concrete->setSpecular(color);
        concrete->parms.shininess = 1.0f;
        concrete->parms.diffuse.w = 1.0f;
        concrete->parms.emissive = glm::vec4(0.0f);
        concrete->dirty = 1;
        concrete->m_surface_flags = 0;
    }
    result.material = std::move(concrete);
    return result;
}

/* Load a texture by full path: split it into folder and file, then take the
   animated loader for .IFL names and the plain one for everything else. */
// FUNCTION: WIZ8 0x004B9460
std::shared_ptr<srTexture> LoadTextureFromPath(const char* path, const W8MaterialRecord* source,
                                               bool required)
{
    const std::string_view full_path(path);
    const auto split = full_path.find_last_of("/\\:");
    const std::string folder(full_path.substr(0, split == full_path.npos ? 0 : split + 1));
    const std::string name(full_path.substr(split == full_path.npos ? 0 : split + 1));
    if (name.size() >= 4 && SDL_strcasecmp(name.c_str() + name.size() - 4, ".IFL") == 0)
        return LoadAnimatedTexture(folder.c_str(), name.c_str(), source, required);
    return LoadTextureFromFolder(folder.c_str(), name.c_str(), required);
}

// FUNCTION: WIZ8 0x004B95D0
std::shared_ptr<srTexture> LoadTextureFromFolder(const char* folder, const char* name,
                                                 bool required)
{
    std::string path;

    if (g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT) {
        UpdatePleaseWaitLoadFrame();
    }
    path = folder;
    path += name;
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) path += '.';
    const auto extension = dot == std::string::npos ? path.size() : dot + 1;
    path.resize(extension);

    std::shared_ptr<stTextureFile> texture = g_texture_files.find(name);
    if (texture == 0) {
        path += "tga";
        if (required) {
            texture = std::make_shared<stTextureFile>(path.c_str(), g_texture_cache_enabled);
            if (g_gerd != 0) {
                g_gerd->setTexture(texture.get(), 0);
                g_gerd->setTexture(0, 0);
            }
            if (texture->resolve() == 0) {
                path.replace(extension, std::string::npos, "jpg");
                texture->setFileName(path.c_str());
                if (g_gerd != 0) {
                    g_gerd->setTexture(texture.get(), 0);
                    g_gerd->setTexture(0, 0);
                }
                if (texture->resolve() == 0) {
                    ShutdownWithErrorBox(fmt::format("Missing texture file: {}", path));
                }
            }
        } else {
            if (![&]() {
                    const auto status = wiz8::file_status(wiz8::asset_path(path));
                    return status && status->info.type == SDL_PATHTYPE_FILE;
                }()) {
                path.replace(extension, std::string::npos, "jpg");
                if (![&]() {
                        const auto status = wiz8::file_status(wiz8::asset_path(path));
                        return status && status->info.type == SDL_PATHTYPE_FILE;
                    }()) {
                    ShutdownWithErrorBox(fmt::format("Missing texture file: {}", path));
                }
            }
            texture = std::make_shared<stTextureFile>(path.c_str(), g_texture_cache_enabled);
        }
        texture->setName(name);
        g_texture_files.insert(name, texture);
    }
    return texture;
}

// FUNCTION: WIZ8 0x004B98F0
std::shared_ptr<stTextureAnim> LoadAnimatedTexture(const char* folder, const char* name,
                                                   const W8MaterialRecord* source, bool required)
{
    const auto path = std::string(folder) + name;
    std::unique_ptr<wiz8::File> handle;
    try {
        handle = wiz8::open_file(wiz8::asset_path(path));
    } catch (const std::exception&) {
        ShutdownWithErrorBox(fmt::format("Cannot load/find material: {}", path));
        return {};
    }
    std::istringstream frames(handle->read_text());
    auto animation = std::make_shared<stTextureAnim>();
    animation->setName(name);
    g_texture_animations.insert(name, animation);
    for (std::string frame; std::getline(frames, frame);) {
        if (frame.ends_with('\r')) frame.pop_back();
        if (frame.size() <= 2) break;
        if (auto texture = LoadTextureFromFolder(folder, frame.c_str(), required))
            animation->AddTexture(std::move(texture));
    }
    animation->setupDefaultValues();
    if (source != 0) {
        animation->animation_mode = source->animation_mode;
        int frame = source->animation_frame;
        animation->initial_frame = frame;
        animation->frame = frame;
        animation->frame_rate = source->animation_rate;
    }
    return animation;
}

/* getPolyTexture selects the layer and table up front, then returns one smart
   pointer per polygon. Report whether any selected texture is animated. */
// FUNCTION: WIZ8 0x004b9aa0
bool MeshHasAnimatedTexture(srMeshModel* model)
{
    if (model != 0) {
        MeshResources<srTexture>* textures = model->getPolygonTextureTable(0, 0, false);

        if (textures != 0) {
            int polygon;

            for (polygon = 0; polygon < model->polygon_count; ++polygon) {
                if (dynamic_cast<stTextureAnim*>((*textures)[polygon].get())) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* The model instance supplies its mesh model. The first
   polygon texture is the shared animation object whose frame is restarted. */
// FUNCTION: WIZ8 0x004b9b00
void SetModelAnimatedTextureFrame(srModelInstance* instance, int frame)
{
    if (instance != 0) {
        srMeshModel* model = static_cast<srMeshModel*>(instance->getModel());

        if (model != 0) {
            MeshResources<srTexture>* textures = model->getPolygonTextureTable(0, 0, false);

            if (textures != 0) {
                if (auto* animation = dynamic_cast<stTextureAnim*>((*textures)[0].get())) {
                    animation->SetFrame(frame);
                }
            }
        }
    }
}

// FUNCTION: WIZ8 0x004B9B50
stTextureAnim* GetModelAnimatedTexture(srModelInstance* instance)
{
    if (instance != 0) {
        srMeshModel* model = static_cast<srMeshModel*>(instance->getModel());

        if (model != 0) {
            MeshResources<srTexture>* textures = model->getPolygonTextureTable(0, 0, false);

            if (textures != 0) {
                return dynamic_cast<stTextureAnim*>((*textures)[0].get());
            }
        }
    }
    return 0;
}
