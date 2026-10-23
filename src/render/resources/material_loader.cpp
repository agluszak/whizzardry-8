#include "wiz8/render/material_loader.h"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_filesystem.h>
#include <fmt/format.h>
#include <wiz8/filesystem.h>

#include "asset_paths.h"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/packed_bits.h"
#include "wiz8/render/compiled_mesh.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/texture.h"
#include "wiz8/resource_cache.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/update.h"

namespace {
// Exact render-state identity; serialized padding and texture filenames do not
// belong to the material state shared by separate mesh texture assignments.
using MaterialKey = std::array<std::uint32_t, 13>;
std::map<MaterialKey, std::weak_ptr<Material>> level_materials;
std::weak_ptr<Material> default_material;
}
static std::map<std::filesystem::path, std::weak_ptr<Texture>> g_texture_files;
ResourceCache<Texture> g_texture_animations;

/* Load and share the material state used by the runtime renderer. */
W8MeshMaterial LoadMaterial(const std::filesystem::path& texture_directory,
                            const W8MaterialRecord& record)
{
    const W8MaterialRecord* source = &record;
    W8MeshMaterial result;
    result.two_sided = (record.surface_flags & W8_MATERIAL_TWO_SIDED) != 0;
    std::shared_ptr<Texture>& texture = result.texture;
    DrawShader* render_flags = &result.render_flags;
    std::filesystem::path texture_path;
    int texture_index = -1;
    bool has_alpha = false;
    int index;

    render_flags->value = 0x0100a51b; /* packed DrawShader; TEXTURING set until no texture */
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
        render_flags->value &= ~DrawShader::MASK_TEXTURING;
    } else {
        texture = LoadTextureFromPath(texture_path, source, true);
        if (texture == 0) {
            return result;
        }

        if (auto* file =
                ((texture.get() && !texture.get()->source_path.empty()) ? texture.get() : nullptr);
            file && file->hasAlpha()) {
            has_alpha = true;
        }
        if (auto* animation =
                ((texture.get() && texture.get()->playback) ? texture.get() : nullptr)) {
            if (animation->Prepare()) {
                has_alpha = true;
            }
            if (source->version > 3 && source->texture_modes[texture_index] > 0.0f) {
                float mode = source->texture_modes[texture_index];
                if (mode <= 1.0f) {
                    animation->playback->trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_FRAME;
                } else {
                    animation->playback->trigger_mode = W8_TEXTURE_TRIGGER_RANDOM_START;
                    mode -= 1.0f;
                }
                animation->playback->probability = mode;
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
                          source->surface_flags & W8_MATERIAL_NORMAL_TEXCOORD_MASK,
                          textured};
    auto& cached = level_materials[key];
    auto concrete = cached.lock();
    if (!concrete) {
        concrete = std::make_shared<Material>();
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
        if ((source->surface_flags & W8_MATERIAL_NORMAL_TEXCOORD_MASK) != 0) {
            concrete->uv_mapping = Material::UvMapping::EyeNormals;
        }
    }
    result.material = std::move(concrete);
    return result;
}

#define DEFAULT_MATERIAL_NAME "Default Material"

W8MeshMaterial CreateDefaultMaterial()
{
    W8MeshMaterial result;
    result.render_flags.value = 0x0100251b;

    auto concrete = default_material.lock();
    if (concrete == 0) {
        concrete = std::make_shared<Material>();
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
    }
    result.material = std::move(concrete);
    return result;
}

namespace {
std::shared_ptr<Texture> LoadAnimatedTexture(const std::filesystem::path& path,
                                             const W8MaterialRecord* source, bool required)
{
    auto handle = wiz8::open_file(path);
    std::istringstream frames(handle->read_text());
    auto animation = std::make_shared<Texture>(TexturePlayback{});
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
        animation->playback->animation_mode = source->animation_mode;
        animation->playback->initial_frame = source->animation_frame;
        animation->playback->frame = source->animation_frame;
        animation->playback->frame_rate = source->animation_rate;
    }
    return animation;
}
} // namespace

// Resolve the actual file before sharing it. The texture retains this path for
// lazy decoding; neither cache lookup nor decoding consults the current asset root.
std::shared_ptr<Texture> LoadTextureFromPath(const std::filesystem::path& path,
                                             const W8MaterialRecord* source, bool required)
{
    if (CurrentScreenState().id == W8_SCREEN_PLEASE_WAIT) UpdatePleaseWaitLoadFrame();
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
        auto texture = std::make_shared<Texture>(candidate, g_texture_cache_enabled);
        texture->setName(wiz8::path_to_utf8(path.filename()).c_str());
        if (required) {
            if (!texture->resolve()) continue;
        }
        g_texture_files.insert_or_assign(path, texture);
        return texture;
    }
    throw std::runtime_error(fmt::format("Missing texture file: {}", wiz8::path_to_utf8(path)));
}

/* getPolyTexture selects the layer and table up front, then returns one smart
   pointer per polygon. Report whether any selected texture is animated. */
bool MeshHasAnimatedTexture(Mesh* model)
{
    if (model != 0) {
        auto* textures = model->getPolygonTextureTable(0, 0, false);

        if (textures != 0) {
            int polygon;

            for (polygon = 0; polygon < model->polygon_count; ++polygon) {
                if ((*textures)[polygon] && (*textures)[polygon]->playback) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* The model instance supplies its mesh model. The first
   polygon texture is the shared animation object whose frame is restarted. */
void SetModelAnimatedTextureFrame(MeshInstance* instance, int frame)
{
    if (instance != 0) {
        Mesh* model = static_cast<Mesh*>(instance->getModel());

        if (model != 0) {
            auto* textures = model->getPolygonTextureTable(0, 0, false);

            if (textures != 0) {
                if (auto* animation = (((*textures)[0].get() && (*textures)[0].get()->playback)
                                           ? (*textures)[0].get()
                                           : nullptr)) {
                    animation->SetFrame(frame);
                }
            }
        }
    }
}

Texture* GetModelAnimatedTexture(MeshInstance* instance)
{
    if (instance != 0) {
        Mesh* model = static_cast<Mesh*>(instance->getModel());

        if (model != 0) {
            auto* textures = model->getPolygonTextureTable(0, 0, false);

            if (textures != 0) {
                return (((*textures)[0].get() && (*textures)[0].get()->playback)
                            ? (*textures)[0].get()
                            : nullptr);
            }
        }
    }
    return 0;
}
