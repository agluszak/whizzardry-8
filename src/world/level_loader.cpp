#include "wiz8/world/level_loader.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtc/quaternion.hpp>

#include "application.h"
#include "asset_paths.h"
#include "geometry.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "local_code/MonsterGroup.h"
#include "named_position.h"
#include "texture_animation.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/binary_reader.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/game_data.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterGenerator.h" // IWYU pragma: keep
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/math.h"
#include "wiz8/monster_generators.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h" // IWYU pragma: keep - completes world-owned fog nodes.
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/spatial/surface_index.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

class Texture;

namespace {

#pragma pack(push, 1)
struct W8LevelLightRecord {
    short version;
    unsigned char create;
    bool visible;
    unsigned int flags; /* 2: a parametric light definition follows the name */
    wiz8::retail::Vec3f location;
    wiz8::retail::Vec3f colour;
    float intensity;
    float range;
};
#pragma pack(pop)
static_assert(sizeof(W8LevelLightRecord) == 40);

} // namespace

glm::vec3 CurrentEnvironmentOffset()
{
    return GetWorld() ? GetWorld()->environment.offset : glm::vec3(0.0f);
}

void AssociateWorldLights(World* world)
{
    for (const auto& owned : world->transient_lights) {
        Light* light = owned.get();
        auto* definition = std::get_if<ParametricLightAnim>(&light->animator);

        if (definition && (definition->flags & W8_PARAM_LIGHT_FLICKER) != 0) {
            const auto prop_count = std::ssize(world->plsProps);
            int prop_index;

            for (prop_index = 0; prop_index < prop_count; ++prop_index) {
                Prop* prop = GetWorldProp(world, prop_index);

                if (!prop->name.empty() &&
                    SDL_strcasecmp(prop->name.c_str(), light->getName().c_str()) == 0) {
                    MeshInstance* instance = prop->FrameModel(prop->subcycle);
                    light->m_prop = prop;
                    if (auto* texture = GetModelAnimatedTexture(instance))
                        texture->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                }
            }
        }
    }
}

static unsigned char ReadWorldLights(World* world, wiz8::File* hFile)
try {
    short light_count;
    int index;
    unsigned char success;
    success = (hFile->read(&light_count, sizeof(light_count)).bytes ==
               static_cast<std::size_t>(sizeof(light_count)));
    if (!success) {
        return 0;
    }

    for (index = 0; index < light_count; ++index) {
        W8LevelLightRecord record;
        std::optional<ParametricLightAnim> definition;
        std::optional<KeyframeTrack> path;
        Light* light = 0;
        char name[20];

        hFile->read_exact(&record, sizeof(record));
        if (record.version >= 2) {
            hFile->read_exact(name, sizeof(name));
            SDL_strupr(name);

            if ((record.flags & 2) != 0) {
                definition.emplace();
                record.create = 1;

                hFile->read_exact(&definition->flags, 4);
                hFile->read_exact(&definition->flicker_chance, 4);
                hFile->read_exact(&definition->color.x, 4);
                hFile->read_exact(&definition->color.y, 4);
                hFile->read_exact(&definition->color.z, 4);
                hFile->read_exact(&definition->color_to.x, 4);
                hFile->read_exact(&definition->color_to.y, 4);
                hFile->read_exact(&definition->color_to.z, 4);
                hFile->read_exact(&definition->intensity, 4);
                hFile->read_exact(&definition->intensity_to, 4);
                hFile->read_exact(&definition->period, 4);
                hFile->read_exact(&definition->rate, 4);
                hFile->read_exact(&definition->path_speed, 4);
                hFile->read_exact(&definition->subcycle_min, 4);
                hFile->read_exact(&definition->subcycle_max, 4);

                if ((definition->flags & W8_PARAM_LIGHT_HAS_PATH) != 0) {
                    path = LoadPathAI(*hFile);
                    if (!path) return false;
                    path->discrete_mode = 1;
                    path->animated = false;
                    path->speed = definition->path_speed;
                }
            }
        }

        if (record.version >= 2 && record.create != 0) {
            if (!definition) {
                light = CreateRangedWorldLight(world, name);
            } else {
                light = CreateWorldLight(world, name);
                if (path) {
                    light->path_ai = std::move(path);
                }
                record.intensity = definition->intensity;
                if (definition->intensity_to < definition->intensity) {
                    float swap = definition->intensity;
                    definition->intensity = definition->intensity_to;
                    definition->intensity_to = swap;
                }
                light->animator = *definition;
            }
            if (!record.visible) {
                light->visible = false;
            }
            light->setGroupMask(2);
        } else if (record.version < 2 || record.visible) {
            record.visible = true;
            if (record.version < 2) {
                strcpy(name, "Static Point Light");
            }
            light = AddWorldLight(world, CreateLight(world->scene.get(), name));
            light->bake_into_mesh = true;
            assert(light != 0);
            light->setGroupMask(1);
        }

        if (light != 0) {
            if (SDL_strncasecmp(light->getName().c_str(), "Sun", 3) == 0) {
                light->diffuse = glm::vec3(0);
                light->ambient = {record.colour.x, record.colour.y, record.colour.z};
                light->setGroupMask(light->getGroupMask() | 4);
                world->environment.lights.push_back(light);
            } else {
                light->diffuse = {record.colour.x, record.colour.y, record.colour.z};
                light->ambient = glm::vec3(0);
            }

            light->specular = glm::vec3(0);
            ConfigureWorldLight(light, record.range * kWorldScale);
            light->intensity = record.intensity;
            light->transform.setLocation(record.location.x * kWorldScale,
                                         record.location.y * kWorldScale,
                                         record.location.z * kWorldScale);
        }
    }

    return success;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldEnvironment(LevelLoadContext* pInfo, World* pWorld)
try {
    /* Retail read these uninitialised when a FileRead chain short-circuited;
       natively the scalars start at zero. */
    glm::vec3 environment_range;
    glm::vec3 white{1.0f};
    glm::vec3 position{0.0f};
    wiz8::retail::Vec3f stored_position{};
    glm::vec3 axis;
    glm::mat3 rotation;
    float intensity = 0.0f;
    float view_distance = 0.0f;
    float angle = 0.0f;
    float distance_scale;
    unsigned char camera_mode = 0;
    unsigned char has_light_colours = 0;
    unsigned char has_environment_colours = 0;
    unsigned char fog_enabled = 0;
    bool success;

    success = (pInfo->hFile->read(&fog_enabled, sizeof(fog_enabled)).bytes ==
               static_cast<std::size_t>(sizeof(fog_enabled))) &&
              (pInfo->hFile->read(&environment_range.x, sizeof(environment_range.x)).bytes ==
               static_cast<std::size_t>(sizeof(environment_range.x))) &&
              (pInfo->hFile->read(&environment_range.y, sizeof(environment_range.y)).bytes ==
               static_cast<std::size_t>(sizeof(environment_range.y))) &&
              (pInfo->hFile->read(&environment_range.z, sizeof(environment_range.z)).bytes ==
               static_cast<std::size_t>(sizeof(environment_range.z))) &&
              (pInfo->hFile->read(&intensity, sizeof(intensity)).bytes ==
               static_cast<std::size_t>(sizeof(intensity))) &&
              (pInfo->hFile->read(&view_distance, sizeof(view_distance)).bytes ==
               static_cast<std::size_t>(sizeof(view_distance))) &&
              (pInfo->hFile->read(&camera_mode, sizeof(camera_mode)).bytes ==
               static_cast<std::size_t>(sizeof(camera_mode)));

    if (camera_mode == 1) {
        success = success && (pInfo->hFile->read(&stored_position, sizeof(stored_position)).bytes ==
                              static_cast<std::size_t>(sizeof(stored_position)));
        position = glm::vec3(stored_position.x, stored_position.y, stored_position.z) * kWorldScale;
        pWorld->party.camera.m_position = position;
    } else if (camera_mode == 2) {
        success = success &&
                  (pInfo->hFile->read(&stored_position, sizeof(stored_position)).bytes ==
                   static_cast<std::size_t>(sizeof(stored_position))) &&
                  (pInfo->hFile->read(&angle, sizeof(angle)).bytes ==
                   static_cast<std::size_t>(sizeof(angle))) &&
                  (pInfo->hFile->read(&axis.x, sizeof(axis.x)).bytes ==
                   static_cast<std::size_t>(sizeof(axis.x))) &&
                  (pInfo->hFile->read(&axis.y, sizeof(axis.y)).bytes ==
                   static_cast<std::size_t>(sizeof(axis.y))) &&
                  (pInfo->hFile->read(&axis.z, sizeof(axis.z)).bytes ==
                   static_cast<std::size_t>(sizeof(axis.z)));
        position = glm::vec3(stored_position.x, stored_position.y, stored_position.z) * kWorldScale;
        pWorld->party.camera.m_position = position;

        rotation = glm::mat3(1);
        if (angle != 0.0f) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                      normalizedOrZero(glm::vec3(axis))));
        }
        pWorld->initial_camera_rotation = rotation;
    }

    // Absent tables keep the default ramps the world was constructed with.
    const auto read_flag = [&](unsigned char& flag) {
        return pInfo->hFile->read(&flag, sizeof(flag)).bytes == sizeof(flag);
    };
    success = success && read_flag(has_light_colours);
    if (has_light_colours != 0)
        success =
            success && ReadEnvironmentColours(pInfo->hFile, pWorld->environment.light_colours);
    success = success && read_flag(has_environment_colours);
    if (has_environment_colours != 0)
        success = success && ReadEnvironmentColours(pInfo->hFile, pWorld->environment.colours);

    pWorld->environment.view_distance = view_distance * kWorldScale;
    pWorld->environment.intensity = intensity;
    pWorld->environment.colour = white;
    pWorld->scene->setAmbientLight(glm::clamp(white * intensity, 0.0f, 1.0f));
    WorldSetFarClip(pWorld, pWorld->environment.view_distance);
    distance_scale = view_distance < g_octree_cell_scale ? kEnvironmentNearScale : 1.5f;
    WorldSetRenderRange(pWorld, distance_scale * pWorld->environment.view_distance);
    pWorld->environment.range_start = environment_range.x;
    pWorld->environment.range_end = environment_range.y;

    pWorld->environment.fog_enabled = fog_enabled != 0;
    return success;
} catch (const std::exception&) {
    return false;
}

/* Validate a signed count before publishing records from the section. */
static bool ReadRecordCount(wiz8::File& file, int& count)
{
    const bool read =
        file.read(&count, sizeof(count)).bytes == static_cast<std::size_t>(sizeof(count));
    return read && count >= 0 && count < 100000;
}

static unsigned char ReadWorldClipPlanes(LevelLoadContext* info, World* world)
try {
    assert(info != nullptr && info->hFile != nullptr && world != nullptr);
    auto& file = *info->hFile;
    int count;
    if (!ReadRecordCount(file, count)) return false;
    if (count == 0) return true;

    unsigned char version;
    file.read_exact(&version, sizeof(version));
    // Attach nothing to the scene until every plane has decoded.
    std::vector<std::unique_ptr<ClipPlane>> pending;
    for (int index = 0; index < count; ++index) {
        std::array<char, 64> name;
        glm::vec3 position;
        float unknown_w;
        file.read_exact(name.data(), name.size());
        wiz8::retail::read_exact(file, position);
        file.read_exact(&unknown_w, sizeof(unknown_w));
        if (std::find(name.begin(), name.end(), '\0') == name.end()) return false;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return false;
        SDL_strupr(name.data());

        auto& clip_plane = *pending.emplace_back(std::make_unique<ClipPlane>());
        clip_plane.setName(name.data());
        clip_plane.setClipPlane(glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        clip_plane.transform.setLocation(glm::dvec3(position) * double{kWorldScale});
        clip_plane.setClipType(ClipPlane::CLIP_POSITIONAL_0);
        clip_plane.visible = false;
    }
    std::ranges::move(pending, std::back_inserter(world->scene->clip_planes));
    return true;
} catch (const std::exception&) {
    return false;
}

static unsigned char ReadWorldProps(LevelLoadContext* pInfo, World* pWorld,
                                    bool mark_model_instances)
try {
    Prop* prop;
    int count;
    int index;
    int collidable_index;
    unsigned char success = 1;

    collidable_index = 0;
    if (pInfo == 0 || pInfo->hFile == 0 || pWorld == 0) {
        return 0;
    }
    if (!ReadRecordCount(*pInfo->hFile, count)) {
        return 0;
    }
    if (count == 0) {
        return 1;
    }

    for (index = 0; index < count; ++index) {
        auto owner = success ? CreateAndLoadProp(pInfo) : nullptr;
        prop = owner.get();
        if (!prop) {
            success = 0;
        } else {
            success = 1;
            if (pWorld->compiledIndex() != 0) {
                if (!pWorld->compiledIndex()->TestPropSunBit(index)) {
                    prop->flags |= W8_PROP_NO_DIRECT_SUN;
                }
                pWorld->compiledIndex()->AddLoadedProp(prop);
            }
            pWorld->plsProps.push_back(std::move(owner));
            if ((prop->flags & W8_PROP_COLLIDABLE) != 0) {
                const auto prop_bounds = prop->GetBounds();
                if (!prop_bounds) return false;
                pWorld->collidable_props.push_back(prop);
                if (pWorld->compiledIndex() != 0) {
                    pWorld->compiledIndex()->AddCollidablePropBounds(collidable_index,
                                                                     *prop_bounds);
                    ++collidable_index;
                }
            }
        }

        if (mark_model_instances && prop != 0) prop->pickable = false;
    }
    if (pWorld->compiledIndex() != 0) {
        pWorld->compiledIndex()->TestPropSunBit(-1);
    }
    return success;
} catch (const std::exception&) {
    return false;
}

/* Resolve a level item name: digits name a record index, anything else its internal name.
   Returns -1 for a name no record carries, which retail skips; nullopt rejects the record. */
static std::optional<int> ResolveLevelItemName(const std::array<char, 20>& name)
{
    const auto terminator = std::find(name.begin(), name.end(), '\0');
    if (terminator == name.end()) return std::nullopt;
    if (name[0] < '0' || name[0] > '9') return FindItemRecordByName(name.data());
    int item_id;
    const auto parsed = std::from_chars(name.data(), terminator, item_id);
    if (parsed.ec != std::errc{} || parsed.ptr != terminator ||
        item_id >= static_cast<int>(Data().items.size()))
        return std::nullopt;
    return item_id;
}

unsigned char ReadWorldItems(LevelLoadContext* info, World* world)
try {
    if (info == nullptr || info->hFile == nullptr || world == nullptr) return false;
    auto& file = *info->hFile;
    int count;
    if (!ReadRecordCount(file, count)) return false;

    for (int index = 0; index < count; ++index) {
        std::array<char, 20> name;
        glm::vec3 position;
        std::array<std::int32_t, 4> unknown_fields;
        unsigned char has_trigger;
        file.read_exact(name.data(), name.size());
        // Reject the name before its trigger is created.
        const auto item_id = ResolveLevelItemName(name);
        if (!item_id) return false;
        wiz8::retail::read_exact(file, position);
        file.read_exact(unknown_fields.data(), sizeof(unknown_fields));
        file.read_exact(&has_trigger, sizeof(has_trigger));
        Trigger* trigger =
            has_trigger != 0 ? Trigger::CreateAndLoadLevelTrigger(&file, world) : nullptr;
        std::array<unsigned char, 3> unknown_bytes;
        std::array<std::int32_t, 4> unknown_tail;
        file.read_exact(unknown_bytes.data(), unknown_bytes.size());
        file.read_exact(unknown_tail.data(), sizeof(unknown_tail));
        if (*item_id < 0) continue;
        position *= kWorldScale;
        auto& item = world->items.add(
            world->items.spawn(*item_id, position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
        world->items.activate(item);
        if (trigger != nullptr) {
            trigger->m_bRepType = W8_TRIGGER_REP_ITEM;
            item.trigger = trigger;
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadMonsterPaths(LevelLoadContext* info, World* world)
try {
    if (!info || !info->hFile || !world) return false;
    auto& file = *info->hFile;
    int count;
    if (!ReadRecordCount(file, count)) return false;
    // Retail options persist when the next record omits the colon.
    std::array<char, 12> options{};
    bool has_options = false;
    for (int index = 0; index < count; ++index) {
        std::array<char, 20> name;
        file.read_exact(name.data(), name.size());
        if (std::find(name.begin(), name.end(), '\0') == name.end()) return false;
        if (auto* separator = std::strchr(name.data(), ':')) {
            has_options = true;
            SDL_strlcpy(options.data(), separator + 1, options.size());
            *separator = '\0';
        }
        int monster_id;
        const auto end = name.data() + std::strlen(name.data());
        const auto parsed = std::from_chars(name.data(), end, monster_id);
        if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
        auto path = LoadPathAI(file);
        if (has_options && (options[2] == '1' || options[3] == '1')) {
            if (!path) return false;
            path->animated = options[2] == '1';
            path->looping = options[3] == '1';
        }
        const glm::vec3 origin(0);
        auto* group = CreateGroup(monster_id, 1, &origin, true, true);
        if (!group) return false;
        auto* monster_info = FindMonster(group->MemberLocationId(0));
        ActivateMonster(monster_info);
        Monster& monster = *monster_info->entity;
        const glm::vec3 camera_position = monster.owner_world.party.eye();
        monster.visual.SelectLOD(&camera_position);
        monster.nav.SetPathAI(std::move(path));
        if (has_options) {
            if (options[1] == '0' || options[1] == '\0') {
                group->members_active = false;
                monster.visual.rep_active = false;
            }
            if (options[0] == '0' || options[0] == '\0') continue;
        }
        monster.UpdateRepresentation();
    }
    return true;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldCameras(LevelLoadContext* info, World* world)
try {
    if (!info || !info->hFile || !world) {
        return false;
    }
    auto* file = info->hFile;
    int count;
    file->read_exact(&count, sizeof(count));
    if (count < 0 || count >= 100000) {
        return false;
    }
    std::vector<std::unique_ptr<W8CameraPath>> pending;
    for (int index = 0; index < count; ++index) {
        auto entry = std::make_unique<W8CameraPath>();
        int positional_0, positional_1;
        unsigned char has_scale;
        float scale = 15.0f;
        file->read_exact(&positional_0, sizeof(positional_0));
        file->read_exact(&positional_1, sizeof(positional_1));
        file->read_exact(&has_scale, sizeof(has_scale));
        file->read_exact(entry->name0, sizeof(entry->name0));
        if (memchr(entry->name0, '\0', sizeof(entry->name0)) == nullptr) {
            return false;
        }
        if (has_scale > 0) {
            file->read_exact(&scale, sizeof(scale));
        }

        entry->path = LoadPathAI(*file);
        if (!entry->path) {
            return false;
        }
        PathAIEnableTimedMode((entry->path ? &*entry->path : nullptr));
        entry->path->entry_index = index;
        entry->path->speed = scale;
        pending.push_back(std::move(entry));
    }
    world->plsCameras.insert(world->plsCameras.end(), std::make_move_iterator(pending.begin()),
                             std::make_move_iterator(pending.end()));
    return true;
} catch (const std::exception&) {
    return false;
}

unsigned char ReadWorldParticles(LevelLoadContext* pInfo, WorldScene* pScene,
                                 std::vector<std::unique_ptr<ParticleSystem>>* pParticles)
try {
    if (!pInfo || !pInfo->hFile || !pParticles) return false;
    LevelSpatialIndex* const octree = pInfo->world ? pInfo->world->compiledIndex() : nullptr;
    auto& file = *pInfo->hFile;
    std::int32_t count;
    file.read_exact(&count, sizeof(count));
    constexpr std::int64_t smallest_record_bytes = 1 + 0x216;
    if (count < 0 || count > (file.size() - file.tell()) / smallest_record_bytes) return false;
    std::vector<W8LevelParticleRecord> records;
    records.reserve(count);
    for (std::int32_t index = 0; index < count; ++index) {
        W8LevelParticleRecord record{};
        record.attachment_key = -1;
        record.start_frame = record.end_frame = -1;
        std::uint8_t version;
        file.read_exact(&version, sizeof(version));
        std::size_t record_bytes;
        switch (version) {
        case 1:
            record_bytes = 0x216;
            break;
        case 2:
            record_bytes = 0x218;
            break;
        case 3:
            record_bytes = 0x21d;
            break;
        case 4:
            record_bytes = sizeof(record);
            break;
        default:
            return false;
        }
        file.read_exact(&record, record_bytes);
        if (!std::memchr(record.name, '\0', sizeof(record.name))) return false;
        if (record.flutter_mode != 0 &&
            (!std::isfinite(record.flutter_period) || record.flutter_period < 0 ||
             static_cast<double>(record.flutter_period) > std::numeric_limits<unsigned int>::max()))
            return false;
        records.push_back(record);
    }
    std::vector<std::unique_ptr<ParticleSystem>> pending;
    std::vector<ParticleSystem*> octree_particles;
    pending.reserve(records.size());
    for (auto& record : records) {
        ParticleSystem* particle;
        glm::dvec3 axis;
        glm::dvec3 location;
        const auto particle_count = record.particle_count;
        auto owned = std::make_unique<ParticleSystem>(pScene, particle_count);
        particle = owned.get();

        SDL_strupr(record.name);
        particle->setName(record.name);
        axis = glm::dvec3(record.rotation_axis.x, record.rotation_axis.y, record.rotation_axis.z);
        particle->transform.rotate(record.rotation_angle, axis);
        location = glm::dvec3(record.location.x, record.location.y, record.location.z);
        location *= kWorldScale;
        particle->transform.setLocation(location);
        particle->transform.rotateX(1.5707963);

        if (record.bounds_origin.x != 0.0f) {
            particle->replace_when_full = true;
            record.bounds_origin.x = 0.0f;
        }
        if (record.bounds_mode == W8_PARTICLE_BOUNDS_BOX) {
            const auto center =
                glm::vec3(record.bounds_origin.x, record.bounds_origin.y, record.bounds_origin.z) *
                kWorldScale;
            const auto extent =
                glm::vec3(record.bounds_extent.x, record.bounds_extent.y, record.bounds_extent.z) *
                250.0f;
            particle->bounds_mode = W8_PARTICLE_BOUNDS_BOX;
            particle->lifetime_minimum = center - extent;
            particle->lifetime_maximum = center + extent;
        } else if (record.bounds_mode == W8_PARTICLE_BOUNDS_SPHERE && record.bounds_radius > 0.0f) {
            particle->bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
            particle->bounds_origin =
                glm::vec3(record.bounds_origin.x, record.bounds_origin.y, record.bounds_origin.z) *
                kWorldScale;
            particle->bounds_radius = record.bounds_radius * kWorldScale;
        } else {
            particle->bounds_mode = W8_PARTICLE_BOUNDS_NONE;
        }

        if (record.initially_active == 0) {
            particle->SetActive(false);
        }
        particle->particle_size = record.particle_size;
        particle->expiry_mode =
            record.expiry_mode != 0 ? W8_PARTICLE_EXPIRY_TEXTURE : W8_PARTICLE_EXPIRY_TIMED;
        particle->lifetime_ms = record.lifetime;
        particle->los_check_enabled = record.los_check != 0;
        particle->emission_interval = record.emission_interval < 2 ? 1 : record.emission_interval;
        particle->start_frame = record.start_frame;
        particle->end_frame = record.end_frame;

        if (record.has_acceleration != 0) {
            particle->has_acceleration = true;
            particle->acceleration =
                glm::vec3(record.acceleration.x, record.acceleration.y, record.acceleration.z) *
                kWorldScale;
        }

        if (record.emission_mode == W8_PARTICLE_EMISSION_NONE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_NONE;
        } else if (record.emission_mode == W8_PARTICLE_EMISSION_SINGLE) {
            particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
        } else {
            particle->emission_mode = W8_PARTICLE_EMISSION_CATCH_UP;
            particle->emission_minimum = {-record.spread.x * 250.0f, -record.spread.y * 250.0f, 0};
            particle->emission_maximum = {record.spread.x * 250.0f, record.spread.y * 250.0f,
                                          record.spread.z * kWorldScale};
        }

        if (record.direction_mode == W8_PARTICLE_DIRECTION_STATIONARY) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_STATIONARY;
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_FIXED) {
            glm::mat3 rotation;
            double angle = -1.5707963;

            rotation = glm::mat3(1);
            if (record.rotation_angle != 0.0f) {
                rotation *= glm::mat3_cast(glm::angleAxis(
                    float(std::atan2(sin(record.rotation_angle), cos(record.rotation_angle))),
                    normalizedOrZero(glm::vec3(record.rotation_axis.x, record.rotation_axis.y,
                                               record.rotation_axis.z))));
            }
            rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(1, 0, 0)));

            particle->direction_mode = W8_PARTICLE_DIRECTION_FIXED;
            particle->direction = normalizedOrZero(rotation * glm::vec3(0, 0, -1));
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_NODE_FORWARD) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_NODE_FORWARD;
        } else if (record.direction_mode == W8_PARTICLE_DIRECTION_CONE) {
            particle->direction_mode = W8_PARTICLE_DIRECTION_CONE;
            particle->cone_yaw = record.direction0 * 0.017453292519943295f;
            particle->cone_pitch = record.direction1 * 0.017453292519943295f;
        } else {
            particle->direction_mode = W8_PARTICLE_DIRECTION_RANDOM;
        }

        if (record.speed_mode == W8_PARTICLE_SPEED_ZERO) {
            particle->speed_mode = W8_PARTICLE_SPEED_ZERO;
        } else if (record.speed_mode == W8_PARTICLE_SPEED_FIXED) {
            particle->speed_mode = W8_PARTICLE_SPEED_FIXED;
            particle->initial_speed = record.initial_speed * kWorldScale;
        } else {
            particle->speed_mode = W8_PARTICLE_SPEED_RANDOM;
            particle->speed_min = record.speed_min * kWorldScale;
            particle->speed_max = record.speed_max * kWorldScale;
        }

        if (record.flutter_mode == 0) {
            particle->SetFlutter(W8_PARTICLE_FLUTTER_NONE);
        } else {
            particle->SetFlutter(W8_PARTICLE_FLUTTER_VELOCITY_SCALED);
            particle->flutter_amplitude = record.flutter_value;
            particle->flutter_period = static_cast<unsigned int>(record.flutter_period);
        }
        if (record.attachment_key >= 0) {
            particle->attachment_key = record.attachment_key;
        }
        particle->requires_sorted_renderer = record.requires_sorted_renderer;
        particle->emission_limit = record.emission_limit;
        particle->release_when_done = false;

        W8MeshMaterial material = LoadMaterial(pInfo->bitmap_folder, record.material);
        particle->SetMaterial(std::move(material.material));
        particle->SetRenderFlags(material.render_flags);
        particle->SetTexture(std::move(material.texture));

        if (strncmp(record.name, "CLOUD", 5) == 0) {
            particle->camera_offset = glm::vec3(particle->transform.getLocation());
            particle->camera_relative = true;
            particle->SetActive(true);
        } else if (octree) {
            octree_particles.push_back(particle);
        }
        pending.push_back(std::move(owned));
    }
    pParticles->reserve(pParticles->size() + pending.size());
    if (octree) octree->AddLoadedParticles(octree_particles);
    pParticles->insert(pParticles->end(), std::make_move_iterator(pending.begin()),
                       std::make_move_iterator(pending.end()));
    return 1;
} catch (const std::exception&) {
    return false;
}

bool ReadNamedPositions(wiz8::File* hFile, std::vector<W8NamedPosition>& named_positions)
try {
    int count;
    int index;
    unsigned char version;

    hFile->read_exact(&count, sizeof(count));
    if (count < 0) return false;
    std::vector<W8NamedPosition> pending;
    for (index = 0; index < count; ++index) {
        W8NamedPosition position;

        hFile->read_exact(&version, sizeof(version));
        if (version != 1) {
            return false;
        }

        hFile->read_exact(position.name, sizeof(position.name));
        if (memchr(position.name, '\0', sizeof(position.name)) == nullptr) {
            return false;
        }
        hFile->read_exact(&position.position.x, sizeof(position.position.x));
        hFile->read_exact(&position.position.y, sizeof(position.position.y));
        hFile->read_exact(&position.position.z, sizeof(position.position.z));
        position.position *= 500.0;
        hFile->read_exact(&position.angle, sizeof(position.angle));
        hFile->read_exact(&position.direction.x, sizeof(position.direction.x));
        hFile->read_exact(&position.direction.y, sizeof(position.direction.y));
        hFile->read_exact(&position.direction.z, sizeof(position.direction.z));
        pending.push_back(position);
    }
    named_positions.insert(named_positions.end(), pending.begin(), pending.end());
    return true;
} catch (const std::exception&) {
    return false;
}

#define CHECK_PVL_OFFSET(message)                                                                  \
    if (world->compiledIndex() != 0 && (!(handle->read(&section_end, sizeof(section_end)).bytes == \
                                          static_cast<std::size_t>(sizeof(section_end))) ||        \
                                        section_end != -1)) {                                      \
        sprintf(error_message, "%s\nTry deleting .PVL file and reloading.", message);              \
        ShutdownWithErrorBox(error_message);                                                       \
    }

unsigned char ReadLevel(World* world, wiz8::File* handle,
                        const std::filesystem::path& texture_directory)
try {
    LevelLoadContext info;
    std::unique_ptr<MeshInstance> level_mesh;
    glm::vec3 environment_offset;
    char error_message[512];
    int section_end;
    int section_count;
    int camera_mode;
    int index;
    std::size_t prop_count;
    unsigned char success;

    if (world == nullptr || world->scene == nullptr || handle == nullptr) {
        return false;
    }

    level_mesh = nullptr;
    world->update_mesh_source = nullptr;
    std::ranges::fill(world->psrMeshes, nullptr);
    world->scene->static_meshes.clear();
    if (world->compiledIndex() == nullptr) {
        world->psrMeshes.clear();
    }

    info.world = world;
    info.hFile = handle;
    info.bitmap_folder = texture_directory;

    if (world->psrMeshes.empty() || world->compiledIndex() == 0) {
        level_mesh = ReadSingleLevelMesh(*handle, texture_directory);
        if (!level_mesh) {
            return 0;
        }
        level_mesh->setName("ReadLevel");
        SetModelInstanceExclusionMask(level_mesh.get(), 1);
        world->update_mesh_source = world->scene->addStaticMesh(std::move(level_mesh));
    } else {
        auto meshes = ReadMultipleLevelMeshes(*handle, world->compiledIndex()->GetMeshCount(),
                                              texture_directory);
        if (meshes.empty()) {
            ShutdownWithErrorBox("ReadLevel: Error reading multi-meshes.");
        }
        for (unsigned int mesh_index = 0; mesh_index < world->compiledIndex()->m_meshCount;
             ++mesh_index) {
            if (meshes[mesh_index]) {
                SetModelInstanceExclusionMask(meshes[mesh_index].get(), 1);
                world->psrMeshes[mesh_index] =
                    world->scene->addStaticMesh(std::move(meshes[mesh_index]));
            }
        }
    }

    success = ReadWorldLights(world, handle);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after lights.");
    success = success && ReadMonsterPaths(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after monsters.");
    success = success && ReadWorldItems(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after items.");

    if (!success || info.hFile == 0 || !ReadRecordCount(*info.hFile, section_count)) {
        success = 0;
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after missiles.");
    success = success && ReadWorldProps(&info, world, false);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after props.");
    success = success && ReadWorldProps(&info, world, true);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after bitmaps.");
    success = success && ReadWorldCameras(&info, world);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after cameras.");
    AssociateWorldLights(world);
    if (!success) {
        return 0;
    }

    info.hFile->read(&section_count, sizeof(section_count));
    if (section_count == 0) {
        WorldSetFarClip(world, 42500.0f);
        WorldSetRenderRange(world, 37500.0f);
    } else {
        success = ReadWorldEnvironment(&info, world);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after fog options.");

    if (success && info.hFile != 0) {
        info.hFile->read(&section_count, sizeof(section_count));
        if (section_count != 0) {
            for (index = 0; index < section_count; ++index) {
                Trigger::CreateAndLoadLevelTrigger(info.hFile, world);
            }
            /* Retail 0x004bb565 integrates whenever the world has game data;
               IntegrateTriggers returns at once when the octree path took the
               triggers (no trigger vertices were banked). */
            if (world->collisionIndex() != 0) {
                world->collisionIndex()->IntegrateTriggers();
            }
        }
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after triggers.");

    UpdateWorldProps(world);
    info.hFile->read(&camera_mode, sizeof(camera_mode));
    world->initial_camera_sway = camera_mode != 0;
    if (world->compiledIndex() == 0 && world->level_id >= 0) {
        FinalizeWorldScenes(*world->scene);
    }

    success = ReadWorldClipPlanes(&info, world);
    if (!success) {
        return 0;
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after Clipping Planes.");

    FinalizeStaticScene(*world);

    if (!success || handle->read(&environment_offset, sizeof(environment_offset)).bytes !=
                        sizeof(environment_offset)) {
        success = 0;
    } else {
        success = ReadWorldParticles(&info, (world->scene.get()), &world->particles);
    }
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after particles.");
    success = success && ReadNamedPositions(info.hFile, world->named_positions);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after named position points.");
    success = success && ReadAutomapNodes(handle, world->automap);
    CHECK_PVL_OFFSET("Wrong offset in .pvl file after automap nodes.");

    world->environment.offset = environment_offset;

    prop_count = world->plsProps.size();
    for (index = 0; index < static_cast<int>(prop_count); ++index) {
        Prop* prop = GetWorldProp(world, index);
        Animation* animation = prop->currentAnimation();

        if ((prop->flags & W8_PROP_NO_DIRECT_SUN) != 0 && animation->positioned_parts) {
            const auto animation_count = animation->parts[2].size();
            for (unsigned int animation_index = 0; animation_index < animation_count;
                 ++animation_index) {
                MeshInstance* instance = prop->FrameModel(static_cast<int>(animation_index));
                Mesh* mesh = static_cast<Mesh*>(instance->getModel());

                for (auto* mesh_part : meshParts(mesh)) {
                    SetModelInstanceExclusionMask(instance, 4);

                    auto* material = mesh_part->getMaterial(0, Mesh::SIDE_FRONT);
                    if (material != 0) {
                        auto copy = std::make_shared<Material>(*material);
                        copy->setName("Unsunlit Prop Material");
                        copy->parms.ambient = glm::vec4(0.0f);
                        copy->dirty = 1;
                        copy->parms.emissive += glm::vec4(environment_offset, 0);
                        copy->dirty = 1;
                        mesh_part->setMaterial(std::move(copy), 0, Mesh::SIDE_FRONT);
                    }
                }
            }
        }
    }
    if (world->compiledIndex() != 0) {
        world->compiledIndex()->registry.collecting_links = false;
    }
    return success;
} catch (const std::exception&) {
    return false;
}

#undef CHECK_PVL_OFFSET

constexpr float kSurfaceFlatNormalThreshold = 0.9959999918937683f;

class Trigger;

float g_default_momentum_scale = 0.30000001192092896f;
float g_default_motion_limit = 112.5f;

const float g_path_endpoint_scale = 0.9900000095367432f;

/* Opens a game-data file, builds its record, and pulls the polygon and
   vertex banks through the record reader. */
static std::unique_ptr<LevelSpatialIndex> ReadGeometry(std::unique_ptr<wiz8::File> file,
                                                       World* owner)
{
    auto game_data = std::make_unique<LevelSpatialIndex>("", owner);
    game_data->compiled_available = false;
    game_data->ReadWGDList(*file, 0);
    game_data->ReadWGDList(*file, 1);
    game_data->collision_available = true;
    return game_data;
}

std::unique_ptr<LevelSpatialIndex> ReadCollisionGeometry(std::string_view path, World* owner)
{
    std::unique_ptr<wiz8::File> file;
    try {
        file = wiz8::open_file(wiz8::asset_path(path));
    } catch (const std::exception&) {
        return 0; // Retail: a level without game data (e.g. Test\DefaultSky) is valid.
    }
    try {
        return ReadGeometry(std::move(file), owner);
    } catch (const std::exception&) {
        return 0;
    }
}

/* The WGD face record's fixed head: three vertex indexes, the source plane,
   and a version tag checked before the rest of the record is read. */
struct W8GDFaceHeader {
    std::int32_t vertex_indices[3];
    float plane[3];
    std::int32_t version;
};
static_assert(sizeof(W8GDFaceHeader) == 0x1c);

/* The WGD face record's tail: classification flag, slope/value pair, footstep
   selectors, trace chance, and the trigger index the writer overrode. */
struct W8GDFaceData { /* 0x18 */
    int type;
    float slope;
    float contact_margin;
    unsigned char material;
    unsigned char surface;
    unsigned char pad_0e[2];
    int chance;
    int trigger_index;
};

/* The conditional-face record following a non-primary face: the group key the
   interface compiler buckets on and the interface's name. */
struct W8GDExtendedFace { /* 0x44 */
    int group;
    char name[0x40];
};

namespace {
struct ConditionalFace {
    int interface_id, surface, group;
};
struct InterfaceBank {
    std::vector<W8GDInterface> interfaces;
    std::vector<W8GDInterfaceState> states;
    std::vector<int> polygons{0};
};
InterfaceBank BuildInterfaces(std::size_t count, const std::vector<ConditionalFace>& faces)
{
    InterfaceBank bank;
    bank.interfaces.resize(count);
    if (!count) {
        bank.polygons.clear();
        return bank;
    }
    struct Group {
        int id;
        std::vector<int> polygons;
    };
    std::vector<std::vector<Group>> groups(count);
    for (std::size_t id = 1; id < count; ++id) groups[id].push_back({0, {}});
    for (const auto& face : faces) {
        auto& list = groups.at(face.interface_id);
        auto group = std::find_if(list.begin(), list.end(),
                                  [&](const auto& entry) { return entry.id == face.group; });
        if (group == list.end()) {
            list.push_back({face.group, {}});
            group = std::prev(list.end());
        }
        group->polygons.push_back(face.surface);
    }
    for (std::size_t id = 1; id < count; ++id) {
        bank.interfaces[id] = {int(id), int(groups[id].size()), int(bank.states.size())};
        for (const auto& [group, polygons] : groups[id]) {
            bank.states.push_back({group, int(polygons.size()), int(bank.polygons.size())});
            bank.polygons.insert(bank.polygons.end(), polygons.begin(), polygons.end());
            bank.polygons.push_back(0);
        }
    }
    return bank;
}
}

bool LevelSpatialIndex::ReadWGDList(wiz8::File& file, int poly_type)
{
    if (poly_type < 0 || poly_type > 2) throw std::runtime_error("invalid WGD poly type");
    wiz8::BinaryReader reader(file);
    const auto vertex_count = reader.record<int>();
    const auto face_count = reader.record<int>();
    if (vertex_count < 0 || face_count < 0 || vertex_count > 200000 || face_count > 200000 ||
        VerticesCount() > std::numeric_limits<int>::max() - vertex_count ||
        SurfacesCount() > std::numeric_limits<int>::max() - face_count)
        throw std::runtime_error("invalid WGD record count");
    if (!vertex_count && !face_count) return false;
    if (!vertex_count || !face_count) throw std::runtime_error("incomplete WGD bank counts");
    if (poly_type == 0 && (VerticesCount() || SurfacesCount()))
        throw std::runtime_error("primary WGD bank already loaded");
    reader.require(std::size_t(vertex_count) * 12 +
                   std::size_t(face_count) * (poly_type ? 0x78 : 0x34) + 24);

    std::vector<glm::vec3> vertices;
    std::vector<W8GDSurface> surfaces;
    if (VerticesCount()) vertices = m_pVertices;
    if (SurfacesCount()) surfaces = m_pSurfaces;
    auto names = interface_names;
    std::vector<ConditionalFace> conditional;
    // Preserve the earlier secondary banks when rebuilding the interface table.
    for (int id = 1; id < InterfacesCount(); ++id) {
        const auto& interface = m_pInterfaces[id];
        for (int state = 0; state < interface.state_count; ++state) {
            const auto& group = m_pStates[interface.iStates + state];
            for (int poly = 0; poly < group.poly_count; ++poly)
                conditional.push_back({id, m_piCondPolys[group.poly_first + poly], group.group});
        }
    }
    const auto input_vertices = reader.array<wiz8::retail::Vec3f>(vertex_count);
    if (!std::isfinite(kWorldScale) || kWorldScale <= 0)
        throw std::runtime_error("invalid world scale");
    auto bank_min =
        glm::vec3(input_vertices.front().x, input_vertices.front().y, input_vertices.front().z) *
        kWorldScale;
    auto bank_max = bank_min;
    for (const auto& vertex : input_vertices) {
        const auto transformed = glm::vec3(vertex.x, vertex.y, vertex.z) * kWorldScale;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(transformed[axis])) throw std::runtime_error("nonfinite WGD vertex");
            bank_min[axis] = std::min(bank_min[axis], transformed[axis]);
            bank_max[axis] = std::max(bank_max[axis], transformed[axis]);
        }
        vertices.push_back(transformed);
    }
    surfaces.reserve(surfaces.size() + face_count);
    for (int index = 0; index < face_count; ++index) {
        const auto header = reader.record<W8GDFaceHeader>();
        if (header.version != 2) throw std::runtime_error("wrong WGD version");
        for (int vertex : header.vertex_indices)
            if (vertex < 0 || vertex >= vertex_count)
                throw std::runtime_error("invalid WGD vertex index");
        const auto data = reader.record<W8GDFaceData>();
        W8GDSurface surface{};
        surface.contact_margin = data.contact_margin;
        surface.slope = data.slope;
        surface.chance = data.chance;
        surface.footstep_material = static_cast<W8FootstepMaterial>(data.material);
        surface.footstep_surface = static_cast<W8FootstepSurface>(data.surface);
        surface.flags = data.type == 1 ? W8_GD_SURFACE_WALKABLE | W8_GD_SURFACE_PATHFINDING : 0;
        unsigned axis = 0;
        for (unsigned candidate = 1; candidate < 3; ++candidate)
            if (fabs(header.plane[candidate]) > fabs(header.plane[axis])) axis = candidate;
        surface.flags |= axis;
        for (int corner = 0; corner < 3; ++corner) {
            surface.vertex_indices[corner] = header.vertex_indices[corner] + VerticesCount();
            surface.edge_link[corner] = -1;
        }
        surface.index = int(surfaces.size());
        ClassifySurfacePlane(vertices.data(), &surface);
        if (poly_type) {
            const auto extended = reader.record<W8GDExtendedFace>();
            if (!memchr(extended.name, '\0', sizeof(extended.name)))
                throw std::runtime_error("unterminated WGD interface name");
            auto name = std::find(names.begin(), names.end(), extended.name);
            if (name == names.end()) {
                names.emplace_back(extended.name);
                name = std::prev(names.end());
            }
            surface.trigger_index = int(name - names.begin()) + 1;
            conditional.push_back({surface.trigger_index, int(surface.index), extended.group});
            if (extended.group) surface.flags |= W8_GD_SURFACE_CONDITIONAL_DISABLED;
        }
        surfaces.push_back(surface);
    }
    const auto lower = reader.record<glm::vec3>();
    const auto upper = reader.record<glm::vec3>();
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(lower[axis] * kWorldScale) ||
            !std::isfinite(upper[axis] * kWorldScale) || lower[axis] > upper[axis])
            throw std::runtime_error("nonfinite WGD bounds");
        bank_min[axis] = std::min(bank_min[axis], lower[axis] * kWorldScale);
        bank_max[axis] = std::max(bank_max[axis], upper[axis] * kWorldScale);
        if (VerticesCount()) {
            bank_min[axis] = std::min(bank_min[axis], minimum[axis]);
            bank_max[axis] = std::max(bank_max[axis], maximum[axis]);
        }
    }
    auto interfaces = BuildInterfaces(names.empty() ? 0 : names.size() + 1, conditional);
    // Move the fully validated banks into their permanent owners.
    m_pVertices.swap(vertices);
    m_pSurfaces.swap(surfaces);

    m_pInterfaces.swap(interfaces.interfaces);
    m_pStates.swap(interfaces.states);
    m_piCondPolys.swap(interfaces.polygons);
    geometry_index.reset();
    interface_names.swap(names);
    minimum = bank_min;
    maximum = bank_max;
    return true;
}

/* Answers the 1-based ordinal of the name-table entry matching `name`,
   else -1. */
int LevelSpatialIndex::FindPointerByName(std::string_view name)
{
    const auto found = std::find(interface_names.begin(), interface_names.end(), name);
    return found == interface_names.end() ? -1 : int(found - interface_names.begin()) + 1;
}

void LevelSpatialIndex::AddTriggerTriangle(int trigger_index, int vertex_0, int vertex_1,
                                           int vertex_2)
{
    m_pTrigSurfaces.emplace_back();
    W8GDSurface* surface = &m_pTrigSurfaces.back();
    surface->flags = W8_GD_SURFACE_CROSSING;
    surface->index = SurfacesCount() + TrigSurfacesCount() - 1;
    surface->trigger_index = trigger_index;
    surface->contact_margin = 1.1f;
    surface->vertex_indices[0] = vertex_0;
    surface->vertex_indices[1] = vertex_1;
    surface->vertex_indices[2] = vertex_2;
    ClassifySurfacePlane(m_pTrigVertices.data(), surface);
    for (int& vertex_index : surface->vertex_indices) {
        vertex_index += VerticesCount();
    }
    surface->edge_link[0] = -1;
    surface->edge_link[1] = -1;
    surface->edge_link[2] = -1;
}

void LevelSpatialIndex::AddTriggerPlane(const glm::vec3* trigger_vertices, Trigger* trigger)
{
    int trigger_index = 0;
    int index;
    if (compiled_available) {
        if (m_ppTriggers.empty()) {
            state.integrated_triggers = 0;
            m_ppTriggers.resize(m_iNumTriggers);
        }
        if (state.integrated_triggers >= m_iNumTriggers)
            throw std::runtime_error("level has more trigger planes than its index");
        m_ppTriggers[state.integrated_triggers++] = trigger;
        return;
    }

    for (index = 0; index < m_iNumTriggers && trigger_index == 0; ++index) {
        if (m_ppTriggers[index] == trigger) {
            trigger_index = index;
        }
    }
    if (trigger_index == 0) {
        trigger_index = m_iNumTriggers++;
        m_ppTriggers.push_back(trigger);
    }
    for (index = 0; index < 4; ++index) {
        m_pTrigVertices.push_back(trigger_vertices[index]);
    }

    AddTriggerTriangle(trigger_index, TrigVerticesCount() - 4, TrigVerticesCount() - 3,
                       TrigVerticesCount() - 2);

    AddTriggerTriangle(trigger_index, TrigVerticesCount() - 2, TrigVerticesCount() - 1,
                       TrigVerticesCount() - 4);
}

void LevelSpatialIndex::IntegrateTriggers()
{
    if (TrigVerticesCount() == 0) {
        return;
    }

    m_pVertices.insert(m_pVertices.end(), m_pTrigVertices.begin(), m_pTrigVertices.end());

    m_pTrigVertices.clear();
    integrated_surface_count = TrigSurfacesCount();

    int end = SurfacesCount() + TrigSurfacesCount();
    for (int index = SurfacesCount(); index < end; ++index) {
        W8GDSurface* surface = index < SurfacesCount() ? &m_pSurfaces[index]
                                                       : &m_pTrigSurfaces[index - SurfacesCount()];
        geometry_index->InsertSurface(surface, m_pVertices);
    }
    state.pending_triggers.assign(m_iNumTriggers, false);
    state.active_triggers.assign(m_iNumTriggers, false);
}

/* Append an owned environment derived from the linked surface plane. */

struct W8ProcessedGameDataHeader {
    unsigned int version;
    glm::vec3 minimum;
    glm::vec3 maximum;
    int vertex_count;
    int surface_count;
    int trigger_surface_base;
    int trigger_surface_count;
    int integrated_surface_count;
    int interface_count;
    int state_count;
    int trigger_count;
    int cond_poly_count;
    int environ_count;
    unsigned char reserved[0x24];
};

bool LevelEnvironment::RescaleToReference(const LevelEnvironment* reference)
{
    if (reference == 0) {
        float difference = static_cast<float>(fabs(g_navigator_gravity + vector.y));
        if (g_navigator_gravity * kCameraSnapEpsilon < difference) {
            return true;
        }
        difference = static_cast<float>(fabs(forward_scale - g_camera_level_forward_scale));
        if (g_camera_level_forward_scale * kCameraSnapEpsilon < difference) {
            return true;
        }
        difference = static_cast<float>(fabs(motion_limit - g_default_motion_limit));
        if (g_default_motion_limit * kCameraSnapEpsilon < difference) {
            return true;
        }
        difference = static_cast<float>(fabs(momentum_scale - g_default_momentum_scale));
        if (g_default_momentum_scale * kCameraSnapEpsilon < difference) {
            return true;
        }
        return false;
    }

    float scale = g_navigator_gravity / -reference->gravity_y;
    gravity_x *= scale;
    gravity_y *= scale;
    gravity_z *= scale;
    forward_scale *= g_camera_level_forward_scale / reference->forward_scale;
    motion_limit *= g_default_motion_limit / reference->motion_limit;
    momentum_scale *= g_default_momentum_scale / reference->momentum_scale;
    return false;
}

/* Read the processed GameData header and all of its variable-size banks. */
void LevelSpatialIndex::ReadProcessedCollision(wiz8::File* handle)
{
    wiz8::BinaryReader reader(*handle);
    const auto header = reader.record<W8ProcessedGameDataHeader>();
    if (header.version != 1) {
        throw std::runtime_error("ReadProcessedCollision: Unsupported version.");
    }
    const auto remaining = handle->size() - handle->tell();
    std::uint64_t bytes = 0;
    const auto count_bytes = [&](int count, std::size_t stride) {
        if (count < 0 || count > std::numeric_limits<int>::max() - 2) {
            throw std::runtime_error("ReadProcessedCollision: Invalid record count.");
        }
        bytes += static_cast<std::uint64_t>(count) * stride;
    };
    count_bytes(header.vertex_count, sizeof(wiz8::retail::Vec3f));
    count_bytes(header.surface_count, sizeof(wiz8::retail::GDSurface));
    count_bytes(header.interface_count, sizeof(W8GDInterface));
    count_bytes(header.state_count, sizeof(W8GDInterfaceState));
    count_bytes(header.cond_poly_count, sizeof(int));
    count_bytes(header.environ_count, sizeof(wiz8::retail::Environ));
    // Trigger bits have no serialized bank; their IDs are carried by surfaces.
    if (remaining < 0 || bytes > static_cast<std::uint64_t>(remaining) ||
        header.trigger_count < 0 || header.trigger_count > header.surface_count ||
        header.trigger_surface_base < 0 || header.trigger_surface_count < 0 ||
        header.trigger_surface_base > header.surface_count ||
        header.trigger_surface_count > header.surface_count - header.trigger_surface_base ||
        header.integrated_surface_count < 0 ||
        header.integrated_surface_count > header.surface_count) {
        throw std::runtime_error("ReadProcessedCollision: Invalid geometry banks.");
    }

    std::vector<glm::vec3> vertices;
    vertices.reserve(header.vertex_count);
    for (const auto& image : reader.array<wiz8::retail::Vec3f>(header.vertex_count))
        vertices.emplace_back(image.x, image.y, image.z);
    std::vector<W8GDSurface> surfaces;
    surfaces.reserve(header.surface_count);
    for (const auto& image : reader.array<wiz8::retail::GDSurface>(header.surface_count))
        wiz8::retail::decode_image(image, surfaces.emplace_back());
    auto interfaces = reader.array<W8GDInterface>(header.interface_count);
    auto states = reader.array<W8GDInterfaceState>(header.state_count);
    auto cond_polys = reader.array<int>(header.cond_poly_count);
    std::vector<LevelEnvironment> environs(header.environ_count);
    for (auto& environ : environs) wiz8::retail::read_exact(*handle, environ);
    for (const auto& surface : surfaces) {
        for (int vertex : surface.vertex_indices)
            if (vertex < 0 || vertex >= header.vertex_count)
                throw std::runtime_error("invalid processed vertex index");
    }
    for (std::size_t id = 1; id < interfaces.size(); ++id) {
        const auto& interface = interfaces[id];
        if (interface.id != int(id) || interface.iStates < 0 || interface.state_count < 0 ||
            interface.iStates > header.state_count ||
            interface.state_count > header.state_count - interface.iStates)
            throw std::runtime_error("invalid processed interface states");
    }
    for (const auto& state : states) {
        if (state.poly_first < 0 || state.poly_count < 0 ||
            state.poly_first > header.cond_poly_count ||
            state.poly_count > header.cond_poly_count - state.poly_first)
            throw std::runtime_error("invalid processed polygon slice");
        for (int poly = 0; poly < state.poly_count; ++poly) {
            const auto index = cond_polys[state.poly_first + poly];
            if (index < 0 || index >= header.surface_count)
                throw std::runtime_error("invalid processed conditional surface");
        }
    }
    std::vector<bool> pending_bits(header.trigger_count);
    std::vector<bool> active_bits(header.trigger_count);
    if (!environs.empty() && environs[0].RescaleToReference(0) != 0) {
        for (std::size_t index = 1; index < environs.size(); ++index) {
            environs[index].RescaleToReference(&environs[0]);
        }
        environs[0].RescaleToReference(&environs[0]);
    }

    m_pVertices.swap(vertices);
    m_pSurfaces.swap(surfaces);

    m_pInterfaces.swap(interfaces);
    m_pStates.swap(states);
    m_piCondPolys.swap(cond_polys);
    environments.swap(environs);
    geometry_index.reset();

    minimum = header.minimum;
    maximum = header.maximum;
    trigger_surface_base = header.trigger_surface_base;
    trigger_surface_count = header.trigger_surface_count;
    integrated_surface_count = header.integrated_surface_count;
    m_iNumTriggers = header.trigger_count;
    m_ppTriggers.clear();
    m_pTrigVertices.clear();
    m_pTrigSurfaces.clear();
    state.crossing_margins.clear();
    state.conditional_disabled.clear();
    state.pending_triggers = std::move(pending_bits);
    state.active_triggers = std::move(active_bits);
    interface_names.clear();
    if (environments.empty()) environments.emplace_back();
    collision_available = true;
}

/* 0x0044902E is the constructor's shared body entry: the SEH wrapper at
   0x00449010 zeroes EBX and falls through into the code below. */
void LevelSpatialIndex::initializeCollision()
{
    geometry_index = 0;
    integrated_surface_count = 0;
    trigger_surface_count = 0;
    trigger_surface_base = 0;

    m_iNumTriggers = 0;

    minimum = glm::vec3(1.0e8f);
    maximum = glm::vec3(-1.0e8f);
    if (EnvironsCount() == 0) {
        environments.emplace_back();
    }
}

/* Build the processed level's spatial index once and publish every surface
   from its primary 0x4c-byte bank.  The constructor expands only local bounds,
   leaving the serialized GameData limits unchanged. */
bool BuildCollisionIndex(LevelSpatialIndex* game_data)
{
    if (game_data == 0) {
        return false;
    }

    glm::vec3 minimum = game_data->minimum;
    glm::vec3 maximum = game_data->maximum;
    if (game_data->geometry_index == 0) {
        game_data->geometry_index =
            std::make_unique<SurfaceIndex>(2000.0f, BoundingBox{minimum, maximum});
    }

    for (int index = 0; index < game_data->SurfacesCount(); ++index) {
        if (game_data->geometry_index->InsertSurface(&game_data->m_pSurfaces[index],
                                                     game_data->m_pVertices) == 0) {
            return false;
        }
    }
    return true;
}

/* Rebuild one indexed surface plane and derive the runtime classification
   carried by the level-geometry record. Bit 0x80 requests dominant-axis
   selection; bit 4 is the walkable slope classification. */
void ClassifySurfacePlane(const glm::vec3* vertices, W8GDSurface* surface)
{
    surface->plane =
        trianglePlane(vertices[surface->vertex_indices[0]], vertices[surface->vertex_indices[1]],
                      vertices[surface->vertex_indices[2]]);

    unsigned int flags = surface->flags;
    if ((flags & W8_GD_SURFACE_CROSSING) != 0) {
        const auto dominant_axis = dominantPlaneAxis(surface->plane.normal);
        flags |= dominant_axis;
        surface->flags = flags;
    }

    if ((surface->flags & W8_GD_SURFACE_WALKABLE) != 0) {
        surface->flags |= W8_GD_SURFACE_PATHFINDING;
    }

    float upper_value = 1.0f;
    if (0.5f < surface->plane.normal.y) {
        if ((surface->flags & W8_GD_SURFACE_WALKABLE) == 0 &&
            kSurfaceFlatNormalThreshold < surface->plane.normal.y) {
            surface->flags |= W8_GD_SURFACE_WALKABLE;
            surface->slope = 1.0f;
        }
        if (surface->slope < 0.0f) {
            surface->flags |= W8_GD_SURFACE_EXPLICIT_SLOPE;
            surface->slope = 0.0f;
        }
    } else if (surface->contact_margin < 1.01f && g_path_endpoint_scale < surface->contact_margin &&
               (surface->flags & W8_GD_SURFACE_WALKABLE) != 0) {
        surface->contact_margin = 0.1f;
    }

    flags = surface->flags;
    surface->contact_margin *= kWorldScale;
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        surface->slope = 0.0f;
    } else if (surface->slope < kVectorLengthSquaredEpsilon &&
               (flags & W8_GD_SURFACE_EXPLICIT_SLOPE) == 0) {
        if (surface->plane.normal.y <= 0.75f) {
            upper_value = surface->plane.normal.y;
        }
        surface->slope = upper_value;
    }
    surface->flags = flags & ~W8_GD_SURFACE_COLLISION_PROCESSED;
}

/* Release level globals; member owners release the runtime banks. */

void LevelSpatialIndex::adoptCollisionData(LevelSpatialIndex&& source)
{
    geometry_index = std::move(source.geometry_index);
    minimum = source.minimum;
    maximum = source.maximum;
    m_pVertices = std::move(source.m_pVertices);
    trigger_surface_base = source.trigger_surface_base;
    trigger_surface_count = source.trigger_surface_count;
    integrated_surface_count = source.integrated_surface_count;
    m_pSurfaces = std::move(source.m_pSurfaces);
    m_iNumTriggers = source.m_iNumTriggers;
    m_pTrigSurfaces = std::move(source.m_pTrigSurfaces);
    m_pTrigVertices = std::move(source.m_pTrigVertices);
    m_ppTriggers = std::move(source.m_ppTriggers);
    state = std::move(source.state);
    m_pInterfaces = std::move(source.m_pInterfaces);
    m_pStates = std::move(source.m_pStates);
    m_piCondPolys = std::move(source.m_piCondPolys);
    interface_names = std::move(source.interface_names);
    environments = std::move(source.environments);
    collision_available = source.collision_available;
}

std::unique_ptr<World> LoadWorld(int level_id, std::string source_path)
{
    auto owner = std::make_unique<World>(level_id, std::move(source_path));
    auto* world = owner.get();
    const auto& resource_path = world->source_path;
    ResetLegacyPropSaveIds();
    const auto path = wiz8::path_from_utf8(resource_path);
    std::string level_path(resource_path);
    const auto sibling = [&](const char* extension) {
        auto result = path;
        result.replace_extension(extension);
        return wiz8::path_to_utf8(result);
    };
    const auto pvl_path = sibling(".pvl");
    const auto oct_path = sibling(".oct");
    const auto game_data_path = sibling(".wgd");
    const auto material_folder = wiz8::path_to_utf8(path.parent_path() / "Bitmaps");

    if (CheckLevelAssetSet(oct_path.c_str()) >= 0) {
        world->spatial = std::make_unique<LevelSpatialIndex>(oct_path.c_str(), world);
        if (world->compiledIndex()->HasLoadError()) world->spatial.reset();
    }

    if (!world->collisionIndex()) {
        if (auto data = ReadCollisionGeometry(game_data_path, world)) {
            if (world->spatial)
                world->spatial->adoptCollisionData(std::move(*data));
            else
                world->spatial = std::move(data);
            if (BuildCollisionIndex(world->collisionIndex()) == 0) return nullptr;
        }
    }

    if (auto* spatial = world->collisionIndex(); spatial && !spatial->environments.empty()) {
        world->party.camera.environment.value = spatial->environments.front();
        world->party.camera.environment.index = 0;
    }
    world->camera = &world->scene->camera;
    world->camera->setName("Sirtech Camera");
    world->camera->transform.setLocation(glm::dvec3(0.0));
    world->camera->setClipRange(250.0, 75000.0);
    world->camera->setEnvironmentRange(0.0f, 1.0f);
    world->camera->setViewPlane(std::acos(-1.0) / 2.0, std::acos(-1.0) / 2.0);
    world->camera_light = CreateRangedWorldLight(world, "CameraLight");
    world->camera_light->ambient = glm::vec3(0);
    world->camera_light->diffuse = glm::vec3(1.0f, 0.85f, 0.39f);
    world->camera_light->specular = glm::vec3(0);
    world->camera_light->intensity = 1.0f;
    world->camera_light->setGroupMask(2);
    ConfigureWorldLight(world->camera_light, 4000.0f);

    if (world->compiledIndex() != nullptr && world->compiledIndex()->GetMeshCount() != 0) {
        world->psrMeshes.resize(
            static_cast<std::size_t>(world->compiledIndex()->GetMeshCount()) + 1, nullptr);
        level_path = pvl_path;
    }

    auto handle = wiz8::open_file(wiz8::asset_path(level_path), wiz8::OpenMode::read);
    if (!ReadLevel(world, handle.get(), wiz8::asset_path(material_folder)))
        throw std::runtime_error("cannot read level " + level_path);
    return owner;
}

// The sky world only retires particles and disables queued nodes.
