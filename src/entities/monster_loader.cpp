#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include "application.h"
#include "asset_paths.h"
#include "glm/ext/vector_float3.hpp"
#include "monster_cycles.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/quality.h"
#include "wiz8/rules/random.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/unicode.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

static constexpr bool g_monster_gib_option = true;
static constexpr double g_monster_light_color_scale = 1.0 / 255.0;
/* Both skin directives ensure the base damage stage exists exactly once. */
static void EnsureMonsterBaseSkin(MonsterDefinition& monster, std::string_view monster_name,
                                  int& damage_stage, bool& skins_started)
{
    if (skins_started) return;
    monster.skin = monster_name;
    damage_stage = monster.AddDamageStage(0);
    monster.damage_stage = damage_stage;
    skins_started = true;
}

std::vector<Mesh*> MonsterDefinition::CollectModelMeshes()
{
    std::vector<Mesh*> meshes;
    const auto add = [&](const AnimationMesh* mesh) {
        if (!mesh) return;
        for (const auto& frame : mesh->frames)
            if (frame.mesh && std::ranges::find(meshes, frame.mesh.get()) == meshes.end())
                meshes.push_back(frame.mesh.get());
    };
    for (const auto& cycle : cycles) {
        for (const auto& slot : cycle) {
            const Animation* animation = slot.animation.get();
            if (!animation) continue;
            for (int lod = 0; lod < 3; ++lod) {
                for (auto& part : animation->parts[lod]) add(&part.mesh);
            }
        }
    }
    return meshes;
}

/* Replace one named texture in a damage stage. Model instances own the normal
   skin tables; shake particles are the fallback when no model uses the name. */
bool MonsterDefinition::ReplaceSkinTexture(int stage, std::string_view old_name,
                                           std::string_view new_name)
{
    std::string path;
    bool replaced = false;

    path = fmt::format("Data\\Monsters\\Bitmaps\\{}", new_name);
    auto texture = LoadTextureFromPath(wiz8::asset_path(path), nullptr, true);
    if (texture == 0) {
        ShutdownWithErrorBox(fmt::format("Missing skin texture: {}", new_name));
        return false;
    }

    if (texture->playback) texture->Prepare();
    const auto table_name = fmt::format("{}{}", skin, stage);
    for (Mesh* model : CollectModelMeshes()) {
        for (auto* mesh : meshParts(model)) {
            const int table = mesh->FindSkinTable(table_name);
            if (table != -1 && mesh->replaceSkinTexture(table, old_name, texture)) replaced = true;
        }
    }

    if (!replaced && !events.particles.empty()) {
        for (auto& attachment : events.particles) {
            auto* event = &attachment;
            if (event->m_pstParticles->ReplaceTexture(old_name, texture) != 0) {
                replaced = true;
            }
        }
    }
    return replaced;
}

/* A damage stage is a skin table on every mesh the species draws, named for the
   skin and the stage number, so meshes shared with another species keep that
   species' stages apart. Stage 0 starts from the authored textures and later
   stages from stage 0. Meshes that already have the stage keep it. */
int MonsterDefinition::AddDamageStage(int stage)
{
    const auto name = fmt::format("{}{}", skin, stage);
    const auto base = fmt::format("{}0", skin);
    const auto models = CollectModelMeshes();
    if (models.empty()) return -1;
    for (Mesh* model : models) {
        for (auto* mesh : meshParts(model)) {
            if (mesh->FindSkinTable(name) == -1)
                mesh->CreateSkinTable(name, stage > 0 ? mesh->FindSkinTable(base) : -1);
        }
    }
    ++damage_stage_count;
    return stage;
}

/* Read a species' text-side representation as its definition: the MLS file
   supplies scalar movement settings, visual flags, sound/shake events, skin
   stages, lights, and the list of binary .mon cycles. The definition is never
   randomized; each actor rolls its own appearance. */
static std::unique_ptr<MonsterDefinition>
LoadMonsterDefinition(World& world, std::string_view monster_name, int location_id)
{
    bool success = true;
    bool flies = false;
    bool swims = false;
    bool crawls = false;
    bool quadruped = false;
    bool full_transition = false;
    bool spice_monster = false;
    int left_handed = 45;
    bool has_light = false;
    bool light_pulsing = false;
    bool random_idle_range = false;
    bool has_lod_range = false;
    float movement_rate = 3.0f;
    float rotation_rate = 0.7f;
    float scale_factor = 1.0f;
    float walk_radius = 0.0f;
    float fight_radius = 0.0f;
    float target_height = 0.0f;
    float camera_height = 0.0f;
    float scale_range_start = -1.0f;
    float scale_range_end = -1.0f;
    float hover_range_start = 0.0f;
    float hover_range_end = 0.0f;
    float bob_range_start = 0.0f;
    float bob_range_end = 0.0f;
    float idle_fps_start = 0.0f;
    float idle_fps_end = 0.0f;
    float lod_range_start = 0.0f;
    float lod_range_end = 0.0f;
    float opacity = -1.0f;
    float glow = -1.0f;
    float death_scale = 1.0f;
    float sound_falloff = -1.0f;
    float shadow_width = -1.0f;
    float shadow_depth = 0.0f;
    int missile_start = -1;
    int spell_start = -1;
    int footstep_volume = 0;
    int footstep_combat_volume = 0;
    glm::vec3 light_first;
    glm::vec3 light_second;
    W8SoundEvent* last_sound = 0;
    int damage_stage = -1;
    int skin_stage = 0;
    bool skins_started = false;

    std::string path;
    path = fmt::format("data\\Monsters\\{}.mls", monster_name);
    auto handle = wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::read);
    auto monster = std::make_unique<MonsterDefinition>();
    world.takeLegacyPropSaveId();

    {
        std::istringstream input(handle->read_text());
        std::string line;
        while (std::getline(input, line)) {
            if (line.ends_with('\r')) line.pop_back();
            std::istringstream fields(line);
            std::string command, argument;
            if (!(fields >> command) || command.starts_with('#')) continue;
            if (SDL_strcasecmp(command.c_str(), "movementrate") == 0) {
                fields >> movement_rate;
            } else if (SDL_strcasecmp(command.c_str(), "RotationRate") == 0) {
                fields >> rotation_rate;
            } else if (SDL_strcasecmp(command.c_str(), "scalefactor") == 0) {
                fields >> scale_factor;
            } else if (SDL_strcasecmp(command.c_str(), "walkradius") == 0) {
                fields >> walk_radius;
            } else if (SDL_strcasecmp(command.c_str(), "fightradius") == 0) {
                fields >> fight_radius;
            } else if (SDL_strcasecmp(command.c_str(), "targetheight") == 0) {
                fields >> target_height;
            } else if (SDL_strcasecmp(command.c_str(), "cameraheight") == 0) {
                fields >> camera_height;
            } else if (SDL_strcasecmp(command.c_str(), "deathscale") == 0) {
                fields >> death_scale;
            } else if (SDL_strcasecmp(command.c_str(), "scalerangestart") == 0) {
                fields >> scale_range_start;
            } else if (SDL_strcasecmp(command.c_str(), "scalerangeend") == 0) {
                fields >> scale_range_end;
            } else if (SDL_strcasecmp(command.c_str(), "hoverrangestart") == 0) {
                fields >> hover_range_start;
            } else if (SDL_strcasecmp(command.c_str(), "hoverrangeend") == 0) {
                fields >> hover_range_end;
            } else if (SDL_strcasecmp(command.c_str(), "bobrangestart") == 0) {
                fields >> bob_range_start;
            } else if (SDL_strcasecmp(command.c_str(), "bobrangeend") == 0) {
                fields >> bob_range_end;
            } else if (SDL_strcasecmp(command.c_str(), "missilestart") == 0) {
                fields >> missile_start;
            } else if (SDL_strcasecmp(command.c_str(), "spellstart") == 0) {
                fields >> spell_start;
            } else if (SDL_strcasecmp(command.c_str(), "flies") == 0) {
                flies = true;
            } else if (SDL_strcasecmp(command.c_str(), "swims") == 0) {
                swims = true;
            } else if (SDL_strcasecmp(command.c_str(), "crawls") == 0) {
                crawls = true;
            } else if (SDL_strcasecmp(command.c_str(), "quadruped") == 0) {
                quadruped = true;
            } else if (SDL_strcasecmp(command.c_str(), "spicemonster") == 0) {
                spice_monster = true;
            } else if (SDL_strcasecmp(command.c_str(), "randomidlefps") == 0) {
                fields >> idle_fps_start >> idle_fps_end;
                random_idle_range = true;
            } else if (SDL_strcasecmp(command.c_str(), "loddistance") == 0) {
                fields >> lod_range_start >> lod_range_end;
                has_lod_range = true;
            } else if (SDL_strcasecmp(command.c_str(), "pitch") == 0) {
                int pitch = 0;
                if (last_sound == 0)
                    throw std::runtime_error(path + ": sound parameter before sound");
                fields >> pitch;
                last_sound->pitch = pitch;
            } else if (SDL_strcasecmp(command.c_str(), "volume") == 0) {
                if (last_sound == 0)
                    throw std::runtime_error(path + ": sound parameter before sound");
                fields >> last_sound->volume_min >> last_sound->volume_max;
            } else if (SDL_strcasecmp(command.c_str(), "sound_falloff") == 0) {
                fields >> sound_falloff;
            } else if (SDL_strcasecmp(command.c_str(), "footstep_vol") == 0) {
                fields >> footstep_volume >> footstep_combat_volume;
            } else if (SDL_strcasecmp(command.c_str(), "probability") == 0) {
                int probability = 0;
                if (last_sound == 0)
                    throw std::runtime_error(path + ": sound parameter before sound");
                fields >> probability;
                last_sound->probability = static_cast<unsigned char>(probability);
            } else if (SDL_strcasecmp(command.c_str(), "animscript") == 0) {
            } else if (SDL_strcasecmp(command.c_str(), "script") == 0) {
                fields >> argument;
                monster->script = LoadMonsterScript(argument);
            } else if (SDL_strcasecmp(command.c_str(), "opacity") == 0) {
                fields >> opacity;
            } else if (SDL_strcasecmp(command.c_str(), "glow") == 0) {
                fields >> glow;
            } else if (SDL_strcasecmp(command.c_str(), "shadow") == 0) {
                fields >> shadow_width >> shadow_depth;
            } else if (SDL_strcasecmp(command.c_str(), "lefthanded") == 0) {
                fields >> left_handed;
            } else if (SDL_strcasecmp(command.c_str(), "fulltransition") == 0) {
                full_transition = true;
            } else if (SDL_strcasecmp(command.c_str(), "addlight") == 0) {
                std::string light_mode;
                char open1{}, close1{}, open2{}, close2{};
                if (!(fields >> light_mode >> open1 >> light_first.x >> light_first.y >>
                      light_first.z >> close1 >> open2 >> light_second.x >> light_second.y >>
                      light_second.z >> close2) ||
                    open1 != '(' || open2 != '(' || close1 != ')' || close2 != ')')
                    throw std::runtime_error("Invalid monster light directive");
                light_first *= static_cast<float>(g_monster_light_color_scale);
                light_second *= static_cast<float>(g_monster_light_color_scale);
                light_pulsing = SDL_strcasecmp(light_mode.c_str(), "pulsing") == 0;
                has_light = true;
            } else if (SDL_strcasecmp(command.c_str(), "skin") == 0) {
                fields >> argument;
                EnsureMonsterBaseSkin(*monster, monster_name, damage_stage, skins_started);
                if (SDL_strcasecmp(argument.c_str(), "default") != 0) {
                    ++skin_stage;
                    damage_stage = monster->AddDamageStage(skin_stage);
                }
            } else if (SDL_strcasecmp(command.c_str(), "skinswap") == 0) {
                std::string old_name;
                std::string new_name;
                /* 0x004C0FA8..0x004C1070: a SKINSWAP with no SKIN before it
                   creates the base stage first, then swaps into that stage. */
                EnsureMonsterBaseSkin(*monster, monster_name, damage_stage, skins_started);
                fields >> old_name >> new_name;
                if (damage_stage != -1 && !monster->ReplaceSkinTexture(
                                              damage_stage, old_name.c_str(), new_name.c_str())) {
                    ShutdownWithErrorBox(fmt::format("The skin texture {} not found in monster {}!",
                                                     old_name, monster_name));
                }
            } else {
                /* 0x004C10FB: the length test is on the whole line, not the argument. */
                if (line.size() <= 2) {
                    continue;
                }
                fields >> argument;
                signed char subcycle;
                W8MonsterCycle cycle = ParseMonsterCycleName(command.c_str(), &subcycle);
                if (cycle != W8_MONSTER_CYCLE_NONE) {
                    if (SDL_strncasecmp(argument.c_str(), "gib", 3) != 0 || g_monster_gib_option) {
                        if (GetRenderOptionState(W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS) == 0) {
                            cycle = NormalizeMonsterCycle(cycle);
                        }
                        auto& slots = monster->cycles[cycle];
                        if (slots.empty() ||
                            GetRenderOptionState(W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS) != 0) {
                            float animation_scale = -1.0f;
                            fields >> animation_scale;

                            success = loadAnimationCycle(
                                argument, monster->events, cycle, "data\\monsters",
                                [&](LevelLoadContext& read_info, int read_cycle) {
                                    return monster->ReadCycleData(read_info, read_cycle);
                                });
                            if (!slots.empty()) {
                                const auto current = static_cast<signed char>(slots.size() - 1);
                                for (auto& event : monster->events.particles) {
                                    if (event.cycle == cycle && event.subcycle == -1)
                                        event.subcycle = current;
                                }
                                if (animation_scale > 0.0f) {
                                    slots.back().animation->playback_scale = animation_scale;
                                    slots.back().playback_scale = animation_scale;
                                }
                            } else if (animation_scale > 0.0f) {
                                /* 0x004C128A: a missing subcycle is fatal, as in
                                   every other retail cycle lookup. */
                                ShutdownWithErrorBox(
                                    fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                                monster_name, g_cycle_names[cycle].name, 0));
                            }
                        }
                    }
                } else {
                    W8SoundEventKind sound_type = W8_SOUND_EVENT_UNRECOGNIZED;
                    if (SDL_strcasecmp(command.c_str(), "SOUND_FRAME") == 0)
                        sound_type = W8_SOUND_EVENT_FRAME;
                    else if (SDL_strcasecmp(command.c_str(), "SOUND_CYCLE") == 0)
                        sound_type = W8_SOUND_EVENT_CYCLE;
                    else if (SDL_strcasecmp(command.c_str(), "SOUND_FOOTSTEP") == 0)
                        sound_type = W8_SOUND_EVENT_FOOTSTEP;

                    if (sound_type != W8_SOUND_EVENT_UNRECOGNIZED) {
                        const std::string& cycle_name = argument;
                        std::string wave_name;
                        std::string loop_name;
                        int frame = 0;
                        if (sound_type == W8_SOUND_EVENT_FOOTSTEP) {
                            fields >> frame;
                        } else {
                            fields >> frame >> wave_name >> loop_name;
                        }
                        W8MonsterCycle sound_cycle =
                            ParseMonsterCycleName(cycle_name.c_str(), &subcycle);
                        std::string wave_path;
                        if (sound_type != W8_SOUND_EVENT_FOOTSTEP) {
                            wave_path = fmt::format("data/sound/monsters/{}.WAV", wave_name);
                        }
                        auto sound = W8SoundEvent(sound_type, sound_cycle, frame, subcycle - 1,
                                                  wave_path.c_str(),
                                                  SDL_strcasecmp(loop_name.c_str(), "LOOP") == 0);
                        last_sound = &monster->events.sound_events.emplace_back(std::move(sound));
                        if (last_sound != nullptr) {
                            last_sound->location_id = location_id;
                            last_sound->volume_min =
                                sound_type == W8_SOUND_EVENT_FOOTSTEP ? 0x23 : 0x7f;
                            last_sound->volume_max = last_sound->volume_min;
                            if (sound_falloff > 0.0f) {
                                last_sound->falloff = sound_falloff * kWorldScale;
                            }
                            if (footstep_volume != 0 || footstep_combat_volume != 0) {
                                last_sound->footstep_volume = footstep_volume;
                                last_sound->footstep_combat_volume = footstep_combat_volume;
                            }
                        }
                    } else if (SDL_strcasecmp(command.c_str(), "SHAKE_FRAME") == 0) {
                        const std::string& cycle_name = argument;
                        int frame = 0;
                        float duration = 1.0f;
                        float intensity = 1.0f;
                        float value = 10.0f;
                        /* 0x004C1569..0x004C1621: the first float is the intensity and
                           the second the duration, as in the spell and missile tables. */
                        fields >> frame >> intensity >> duration >> value;
                        W8MonsterCycle shake_cycle =
                            ParseMonsterCycleName(cycle_name.c_str(), &subcycle);
                        ShakeDefinition effect(duration, true, intensity, value * kWorldScale, 0);
                        effect.frame = frame;
                        effect.cycle = shake_cycle;
                        effect.subcycle = subcycle - 1;
                        monster->events.shake_events.push_back(effect);
                    } else {
                        throw std::runtime_error(
                            fmt::format("{}: unrecognized command \"{}\"", path, command));
                    }
                }
            }
        }
        if (handle) handle->close();
        handle.reset();
    }

    monster->asset_name = monster_name;
    if (scale_range_start != -1.0f && scale_range_end != -1.0f) {
        monster->minimum_scale = scale_range_start;
        monster->maximum_scale = scale_range_end;
        scale_factor = (scale_range_end - scale_range_start) * 0.5f + scale_range_start;
    }
    monster->scale = scale_factor;
    monster->death_scale = death_scale;

    if (walk_radius != 0.0f) monster->collision_radius = walk_radius * kWorldScale;
    if (fight_radius != 0.0f) monster->alternate_radius = fight_radius * kWorldScale;
    if (target_height != 0.0f)
        monster->height_offset = std::max(target_height * kWorldScale, 250.0f);
    if (camera_height != 0.0f) monster->secondary_height_offset = camera_height * kWorldScale;

    if (!random_idle_range) {
        idle_fps_start = -3.0f;
        idle_fps_end = 3.0f;
    }
    if (monster->cycles[W8_MONSTER_CYCLE_IDLE].empty()) {
        ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                         monster_name, g_cycle_names[1].name, 0));
    }
    Animation& idle = *monster->cycles[W8_MONSTER_CYCLE_IDLE].front().animation;
    monster->random_idle = true;
    monster->idle_playback_scale = idle.playback_scale;
    monster->random_idle_fps_min = idle_fps_start;
    monster->random_idle_fps_max = idle_fps_end;
    if (missile_start > 0) monster->missile_frame = missile_start;
    if (spell_start > 0) monster->spell_frame = spell_start;
    if (has_lod_range)
        monster->lod_range = {lod_range_start * kWorldScale, lod_range_end * kWorldScale};
    if (opacity >= 0.0f && opacity < 1.0f) monster->opacity = opacity;
    if (glow > 0.0f) monster->emissive_override = glow;

    monster->hover_base_min = hover_range_start;
    monster->hover_base_max = hover_range_end;
    monster->bob_amplitude_min = bob_range_start;
    monster->bob_amplitude_max = bob_range_end;
    if (shadow_width != 0.0f) {
        if (shadow_width < 0.0f) shadow_width = monster->collision_radius * 0.002f;
        if (shadow_depth == 0.0f) shadow_depth = shadow_width;
        monster->ground_shadow = {shadow_width * kWorldScale, shadow_depth * kWorldScale};
    }
    monster->left_handed = left_handed;
    monster->special_movement = flies || swims || full_transition;

    if (has_light) {
        monster->light = std::make_unique<Light>(
            nullptr, light_pulsing, monster->collision_radius * 3.0f, &light_first, &light_second);
        std::get<MonsterLightAnim>(monster->light->animator).vertical_offset =
            monster->height_offset;
    }

    /* The navigator's bounds are the idle pose's, at the finest level of detail
       the animation has; the frame bounds .mon files record do not depend on it. */
    const signed char lod = idle.frame(2, 0) ? 2 : idle.frame(1, 0) ? 1 : 0;
    auto bounds = AnimObjGetBounds(&idle, lod, 0);
    if (!bounds) return nullptr;
    bounds->minimum *= monster->scale;
    bounds->maximum *= monster->scale;
    monster->bounds = *bounds;
    if (movement_rate < 0.1f) movement_rate = 2.0f;
    monster->movement_scale = movement_rate;
    monster->turn_rate = rotation_rate * static_cast<float>(kMotionFullTurnRadians);

    if (flies)
        monster->navigation_mode = W8_NAVIGATOR_MODE_FLYING;
    else if (swims)
        monster->navigation_mode = W8_NAVIGATOR_MODE_SWIMMING;
    else if (quadruped)
        monster->navigation_mode = W8_NAVIGATOR_MODE_QUADRUPED;
    else if (crawls)
        monster->navigation_mode = W8_NAVIGATOR_MODE_CRAWLING;
    else
        monster->navigation_mode = W8_NAVIGATOR_MODE_GROUNDED;
    /* 0x004C1CC5..0x004C1CD7: byte stores to +0x332, +0xa0 (navigator active)
       and +0x14e (movement boundary_enabled). */
    monster->hostility_preserved = spice_monster;

    return success ? std::move(monster) : nullptr;
}

/* Each new monster rolls its own handedness, size, idle pace and hover/bob
   motion from the species' ranges, in that order. */
static void RollAppearance(Monster& monster, const MonsterDefinition& species)
{
    auto& rng = monster.owner_world.rng();
    /* 0x004C1D3D: cmp/setl - a signed test against the LEFTHANDED percent. */
    monster.visual.mirror_x = static_cast<int>(rng.below(100)) < species.left_handed;

    if (species.minimum_scale != 0.0f && species.maximum_scale != 0.0f) {
        assert(species.minimum_scale <= species.maximum_scale);
        monster.scale = (species.maximum_scale - species.minimum_scale) * rng.below(1000) * 0.001f +
                        species.minimum_scale;
    }

    if (species.random_idle) {
        const float playback_scale =
            (species.random_idle_fps_max - species.random_idle_fps_min) * rng.below(1000) * 0.001f +
            species.random_idle_fps_min;
        const float scale = std::max(playback_scale + species.idle_playback_scale, 1.0f);
        for (auto& slot : monster.cycles[W8_MONSTER_CYCLE_IDLE]) {
            slot.animation->playback_scale = scale;
            slot.playback_scale = scale;
        }
    }

    auto& movement = monster.nav.movement;
    movement.vertical_base = ((species.hover_base_max - species.hover_base_min) *
                                  static_cast<float>(rng.below(1000)) * 0.001f +
                              species.hover_base_min) *
                             kWorldScale;
    movement.vertical_amplitude = ((species.bob_amplitude_max - species.bob_amplitude_min) *
                                       static_cast<float>(rng.below(1000)) * 0.001f +
                                   species.bob_amplitude_min) *
                                  kWorldScale;
    movement.vertical_phase = rng.below(1000) * 0.001f;
    movement.vertical_offset =
        static_cast<float>(sin(movement.vertical_phase * kMotionFullTurnRadians)) *
            movement.vertical_amplitude +
        movement.vertical_base;
    movement.height_offset += movement.vertical_base;
    movement.secondary_height_offset += movement.vertical_base;

    if (monster.visual.opacity < 1.0f) {
        monster.visual.instance_scale = monster.visual.opacity;
        monster.visual.apply_instance_scale = true;
    }
}

/* A new actor of a species, created from the species' definition and given its
   own appearance roll. The definition is loaded on first use and kept for the
   world's lifetime, so creating an actor never depends on another actor. */
std::unique_ptr<Monster> Monster::ReadAllCycles(World* world, std::string_view monster_name,
                                                int location_id)
{
    auto& definitions = world->monster_definitions;
    auto definition = std::ranges::find_if(definitions, [&](const auto& candidate) {
        return CompareTextIgnoreAsciiCase(candidate->asset_name, monster_name) == 0;
    });
    if (definition == definitions.end()) {
        auto loaded = LoadMonsterDefinition(*world, monster_name, location_id);
        if (!loaded) return nullptr;
        definitions.push_back(std::move(loaded));
        definition = definitions.end() - 1;
    }
    auto monster = std::make_unique<Monster>(**definition, *world);
    RollAppearance(*monster, **definition);
    // Retail gave the MLS script only to an actor created while none of its
    // species was alive; the others were copied without one.
    if ((*definition)->script && std::ranges::none_of(world->monsters, [&](const auto& live) {
            return CompareTextIgnoreAsciiCase(live->asset_name, monster_name) == 0;
        })) {
        monster->UseScript((*definition)->script, true);
    }
    return monster;
}

/* Select one of the four directional states without immediately repeating the
   caller's current state. Random values four and five fold back to zero in the
   original table. */
W8MonsterCycle ParseMonsterCycleName(std::string_view name, signed char* subcycle)
{
    int index = W8_MONSTER_CYCLE_COUNT;

    W8MonsterCycle cycle = W8_MONSTER_CYCLE_NONE;
    for (int candidate = 0; candidate < W8_MONSTER_CYCLE_COUNT; ++candidate) {
        if (name.starts_with(std::string_view(g_cycle_names[candidate].name,
                                              g_cycle_names[candidate].prefix_length))) {
            cycle = static_cast<W8MonsterCycle>(candidate);
            index = candidate;
            break;
        }
    }

    /* The model format retains these older names for the first two cycles. */
    if (cycle == W8_MONSTER_CYCLE_NONE) {
        if (name.starts_with("FLY")) {
            cycle = W8_MONSTER_CYCLE_BIRTH;
        } else if (name.starts_with("EXPLODE")) {
            cycle = W8_MONSTER_CYCLE_IDLE;
        }
    }

    if (subcycle != nullptr) {
        *subcycle = 1;
        if (index == W8_MONSTER_CYCLE_COUNT) {
            return cycle;
        }
        const int suffix = g_cycle_names[cycle].prefix_length;
        if (name.size() > static_cast<std::size_t>(suffix) && name[suffix] >= '0' &&
            name[suffix] <= '9') {
            *subcycle = static_cast<signed char>(
                atoi(std::string(name).c_str() + g_cycle_names[index].prefix_length));
        }
    }
    return cycle;
}

/* Append one animation to a cycle, keeping its lights in the slot that owns
   it. A path-list animation's tracks loop discretely at the animation's pace. */
bool MonsterDefinition::ReadCycleData(LevelLoadContext& info, int cycle_index)
{
    assert(info.hFile != 0);
    EmitterLights lights;
    auto animation = std::make_unique<Animation>();
    if (!AnimObjReadFromFile(&info, animation.get(), &lights.lights)) return false;
    if (cycle_index == W8_MONSTER_CYCLE_NONE) cycle_index = animation->cycle;
    assert(cycle_index >= W8_MONSTER_CYCLE_BIRTH && cycle_index < W8_MONSTER_CYCLE_COUNT);

    for (auto& group : animation->parts) {
        for (auto& part : group) {
            if (part.path) {
                part.path->looping = true;
                part.path->discrete_mode = true;
                part.path->speed = animation->playback_scale;
            }
        }
    }
    const float playback_scale = animation->playback_scale;
    cycles[cycle_index].emplace_back(std::move(lights), std::move(animation), playback_scale);
    return true;
}

/* A monster made from one .mon file, as the 3D cursor is: no MLS settings and
   no appearance roll. */
std::unique_ptr<Monster> LoadMonsterCycle(World* world, std::string_view mon_name, int cycle)
{
    MonsterDefinition definition;
    world->takeLegacyPropSaveId();
    if (!loadAnimationCycle(mon_name, definition.events, cycle, "data\\monsters",
                            [&](LevelLoadContext& read_info, int read_cycle) {
                                return definition.ReadCycleData(read_info, read_cycle);
                            }))
        return nullptr;
    for (auto& event : definition.events.particles) {
        if (event.cycle == cycle && event.subcycle == -1) event.subcycle = 0;
    }
    return std::make_unique<Monster>(definition, *world);
}

/* Pass a copied position through Navigator's collision adjustment and install
   the adjusted result on the Monster's ordinary Navigator base. */
