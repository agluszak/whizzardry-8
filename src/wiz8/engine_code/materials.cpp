#include "wiz8/engine_code/materials.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

namespace {
// Exact render-state identity; serialized padding and texture filenames do not
// belong to the material state shared by separate mesh texture assignments.
using MaterialKey = std::array<std::uint32_t, 13>;
std::map<MaterialKey, std::weak_ptr<stMaterial>> level_materials;
std::weak_ptr<stMaterial> default_material;
}
static std::map<std::filesystem::path, std::weak_ptr<stTextureFile>> g_texture_files;
ResourceCache<stTextureAnim> g_texture_animations;

/* Load and share the material state used by the runtime renderer. */
W8MeshMaterial LoadMaterial(const std::filesystem::path& texture_directory,
                            const W8MaterialRecord& record)
{
    const W8MaterialRecord* source = &record;
    W8MeshMaterial result;
    std::shared_ptr<srTexture>& texture = result.texture;
    srShader* render_flags = &result.render_flags;
    std::filesystem::path texture_path;
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
            texture_path = texture_directory / wiz8::normalize_asset_path(name);
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
        texture = LoadTextureFromPath(texture_path, source, true);
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

namespace {
std::shared_ptr<stTextureAnim> LoadAnimatedTexture(const std::filesystem::path& path,
                                                   const W8MaterialRecord* source, bool required)
{
    auto handle = wiz8::open_file(path);
    std::istringstream frames(handle->read_text());
    auto animation = std::make_shared<stTextureAnim>();
    const auto name = wiz8::path_to_utf8(path.filename());
    animation->setName(name.c_str());
    // Scripts refer to animations by their authored name. Playback is per load.
    g_texture_animations.insert(name, animation);
    for (std::string frame; std::getline(frames, frame);) {
        if (frame.ends_with('\r')) frame.pop_back();
        if (frame.size() <= 2) break;
        animation->AddTexture(LoadTextureFromPath(
            path.parent_path() / wiz8::normalize_asset_path(frame), nullptr, required));
    }
    animation->setupDefaultValues();
    if (source) {
        animation->animation_mode = source->animation_mode;
        animation->initial_frame = source->animation_frame;
        animation->frame = source->animation_frame;
        animation->frame_rate = source->animation_rate;
    }
    return animation;
}
} // namespace

// Resolve the actual file before sharing it. The texture retains this path for
// lazy decoding; neither cache lookup nor decoding consults the current asset root.
std::shared_ptr<srTexture> LoadTextureFromPath(const std::filesystem::path& path,
                                               const W8MaterialRecord* source, bool required)
{
    if (g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT) UpdatePleaseWaitLoadFrame();
    if (path.extension() == ".ifl") return LoadAnimatedTexture(path, source, required);
    // The authored basename is also used by skin-replacement scripts. Keep it
    // in the resource identity; two aliases may need different runtime names.
    if (const auto found = g_texture_files.find(path); found != g_texture_files.end()) {
        if (auto texture = found->second.lock()) return texture;
    }
    for (const auto* extension : {".tga", ".jpg"}) {
        auto candidate = path;
        candidate.replace_extension(extension);
        const auto status = wiz8::file_status(candidate);
        if (!status || status->info.type != SDL_PATHTYPE_FILE) continue;
        auto texture = std::make_shared<stTextureFile>(candidate, g_texture_cache_enabled);
        texture->setName(wiz8::path_to_utf8(path.filename()).c_str());
        if (required) {
            if (g_gerd) {
                g_gerd->setTexture(texture.get(), 0);
                g_gerd->setTexture(nullptr, 0);
            }
            if (!texture->resolve()) continue;
        }
        g_texture_files.insert_or_assign(path, texture);
        return texture;
    }
    ShutdownWithErrorBox(fmt::format("Missing texture file: {}", wiz8::path_to_utf8(path)));
    return {};
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
