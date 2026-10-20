#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
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
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/entities/entity_assets.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/quality.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"

static constexpr bool g_monster_gib_option = true;
static constexpr double g_monster_light_color_scale = 1.0 / 255.0;
/* Both skin directives ensure the base damage stage exists exactly once. */
static void EnsureMonsterBaseSkin(Monster* monster, std::string_view monster_name,
                                  int& damage_stage, bool& skins_started)
{
    if (skins_started) return;
    damage_stage = monster->AddDamageStage(monster_name, 0);
    std::vector<MeshInstance*> instances;
    monster->CollectModelInstances(instances);
    for (MeshInstance* instance : instances) {
        instance->damage_stage = damage_stage;
    }
    skins_started = true;
}

/* Read the text-side monster representation. A named representation is shared
   by cloning its first live cycle; otherwise the MLS file supplies scalar
   movement settings, visual flags, sound/shake events, skin stages, lights,
   and the list of binary .mon cycles. */
std::unique_ptr<Monster> Monster::ReadAllCycles(World* world, std::string_view monster_name,
                                                int load_value, int location_id)
{
    std::unique_ptr<Monster> monster;
    Monster* shared = findMonsterAsset(*world, monster_name);
    if (shared != 0) {
        monster = std::make_unique<Monster>(*shared, world);
        if (!monster) {
            Fail("*ppMonster");
        }
        monster->RandomizeAppearanceAndMotion();
        monster->asset_name = monster_name;
        return monster;
    }

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
    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (handle == 0) {
        Fail("hFile", fmt::format("Couldn't open {}", path));
    }
    monster = std::make_unique<Monster>(world);
    AllocateLegacyPropSaveId();
    monster->visual.current_cycle = -1;

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
                if (last_sound == 0) {
                    Fail("pSndEvent",
                         "mls pitch: Must specify a sound before specifying parameters");
                }
                fields >> pitch;
                last_sound->pitch = pitch;
            } else if (SDL_strcasecmp(command.c_str(), "volume") == 0) {
                if (last_sound == 0) {
                    Fail("pSndEvent",
                         "mls volume: Must specify a sound before specifying parameters");
                }
                fields >> last_sound->volume_min >> last_sound->volume_max;
            } else if (SDL_strcasecmp(command.c_str(), "sound_falloff") == 0) {
                fields >> sound_falloff;
            } else if (SDL_strcasecmp(command.c_str(), "footstep_vol") == 0) {
                fields >> footstep_volume >> footstep_combat_volume;
            } else if (SDL_strcasecmp(command.c_str(), "probability") == 0) {
                int probability = 0;
                if (last_sound == 0) {
                    Fail("pSndEvent",
                         "mls frequency: Must specify a sound before specifying parameters");
                }
                fields >> probability;
                last_sound->probability = static_cast<unsigned char>(probability);
            } else if (SDL_strcasecmp(command.c_str(), "animscript") == 0) {
            } else if (SDL_strcasecmp(command.c_str(), "script") == 0) {
                fields >> argument;
                monster->SetScript(argument.c_str(), true);
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
                EnsureMonsterBaseSkin(monster.get(), monster_name, damage_stage, skins_started);
                if (SDL_strcasecmp(argument.c_str(), "default") != 0) {
                    ++skin_stage;
                    damage_stage = monster->AddDamageStage(monster_name, skin_stage);
                }
            } else if (SDL_strcasecmp(command.c_str(), "skinswap") == 0) {
                std::string old_name;
                std::string new_name;
                /* 0x004C0FA8..0x004C1070: a SKINSWAP with no SKIN before it
                   creates the base stage first, then swaps into that stage. */
                EnsureMonsterBaseSkin(monster.get(), monster_name, damage_stage, skins_started);
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
                        if (!monster ||
                            monster->IsCycleSupported(static_cast<signed char>(cycle)) == 0 ||
                            GetRenderOptionState(W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS) != 0) {
                            float animation_scale = -1.0f;
                            fields >> animation_scale;

                            success = loadAnimationCycle(
                                argument, monster->visual, cycle, load_value, "data\\monsters",
                                [&](LevelLoadContext& read_info, int read_cycle, int read_value) {
                                    return monster->ReadCycleData(&read_info, read_cycle,
                                                                  read_value) != 0;
                                });
                            if (!monster->visual.particles.empty()) {
                                for (int index = 0;
                                     index < static_cast<int>(monster->visual.particles.size());
                                     ++index) {
                                    ParticleAttachment* event = &monster->visual.particles[index];
                                    if (event->cycle == cycle && event->subcycle == -1) {
                                        event->subcycle = monster->visual.current_subcycle;
                                    }
                                }
                            }
                            if (animation_scale > 0.0f) {
                                Monster* rep = monster.get();
                                int current = rep->visual.current_subcycle;
                                /* 0x004C128A: a missing subcycle is fatal, as in
                                   every other retail cycle lookup. */
                                if (static_cast<int>(rep->cycles[cycle].size()) <= current) {
                                    ShutdownWithErrorBox(
                                        fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                                    rep->name, g_cycle_names[cycle].name, current));
                                }
                                Animation* animation =
                                    monster->cycles[cycle][current].animation.get();
                                animation->playback_scale = animation_scale;
                                monster->cycles[cycle][current].playback_scale = animation_scale;
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
                        last_sound = &monster->visual.AddSoundEvent(std::move(sound));
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
                        monster->visual.AddShakeEffect(effect);
                    } else {
                        Fail("FALSE",
                             fmt::format("Monster::ReadAllCycles: ERROR - Unrecognized MLS "
                                         "string \"{}\" in {}",
                                         command, path));
                    }
                }
            }
        }
        if (handle) handle->close();
        handle.reset();
    }

    Monster* representation = monster.get();
    representation->name = monster_name;
    if (scale_range_start != -1.0f && scale_range_end != -1.0f) {
        representation->minimum_scale = scale_range_start;
        representation->maximum_scale = scale_range_end;
        scale_factor = (scale_range_end - scale_range_start) * 0.5f + scale_range_start;
    }
    representation->scale = scale_factor;
    representation->death_scale = death_scale;

    /* The world scale follows the largest radius any monster reaches. */
    if (walk_radius != 0.0f) {
        walk_radius *= kWorldScale;
        monster->nav.movement.collision_radius = walk_radius;
        if (world->compiledIndex() &&
            world->compiledIndex()->registry.maximum_navigation_radius < walk_radius) {
            world->compiledIndex()->registry.maximum_navigation_radius = walk_radius;
        }
    }
    if (fight_radius != 0.0f) {
        fight_radius *= kWorldScale;
        monster->nav.movement.alternate_radius = fight_radius;
        if (world->compiledIndex() &&
            world->compiledIndex()->registry.maximum_navigation_radius < fight_radius) {
            world->compiledIndex()->registry.maximum_navigation_radius = fight_radius;
        }
    }
    if (target_height != 0.0f) {
        target_height *= kWorldScale;
        if (target_height < 250.0f) target_height = 250.0f;
        monster->nav.movement.height_offset = target_height;
    }
    if (camera_height != 0.0f) {
        monster->nav.movement.secondary_height_offset = camera_height * kWorldScale;
    }

    if (!random_idle_range) {
        idle_fps_start = -3.0f;
        idle_fps_end = 3.0f;
    }
    if (static_cast<int>(representation->cycles[1].size()) < 1) {
        ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                         representation->name, g_cycle_names[1].name, 0));
    }
    Animation* idle = representation->cycles[1][0].animation.get();
    if (idle != 0) {
        representation->random_idle = true;
        representation->idle_playback_scale = idle->playback_scale;
        representation->random_idle_fps_min = idle_fps_start;
        representation->random_idle_fps_max = idle_fps_end;
    }
    if (missile_start > 0) monster->missile_frame = missile_start;
    if (spell_start > 0) monster->spell_frame = spell_start;
    if (has_lod_range) {
        representation->visual.lod_near = lod_range_start * kWorldScale;
        representation->visual.lod_far = lod_range_end * kWorldScale;
    }
    if (opacity >= 0.0f && opacity < 1.0f) {
        monster->visual.opacity = opacity;
    }
    if (glow > 0.0f) {
        std::vector<MeshInstance*> instances;
        monster->CollectModelInstances(instances);
        for (MeshInstance* instance : instances) {
            instance->emissive_override_enabled = true;
            instance->emissive_override = glow;
        }
    }

    monster->hover_base_min = hover_range_start;
    monster->hover_base_max = hover_range_end;
    monster->bob_amplitude_min = bob_range_start;
    monster->bob_amplitude_max = bob_range_end;
    if (shadow_width != 0.0f) {
        if (shadow_width < 0.0f) {
            shadow_width = monster->nav.movement.collision_radius * 0.002f;
        }
        if (shadow_depth == 0.0f) shadow_depth = shadow_width;
        monster->visual.CreateGroundShadow(shadow_width * kWorldScale, shadow_depth * kWorldScale);
    }
    representation->left_handed = left_handed;
    representation->special_movement = (flies || swims || full_transition) ? 1 : 0;

    if (has_light && representation->monster_light == 0) {
        representation->monster_light = std::make_unique<Light>(
            (world->scene.get()), light_pulsing, monster->nav.movement.collision_radius * 3.0f,
            &light_first, &light_second);
        std::get<MonsterLightAnim>(representation->monster_light->animator).vertical_offset =
            monster->nav.movement.height_offset;
    }

    representation->visual.current_subcycle = 0;
    monster->SetCycle(1);
    monster->visual.SetSubCycle(0);
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) return nullptr;
    monster->nav.SetBounds(*bounds);
    if (movement_rate < 0.1f) movement_rate = 2.0f;
    monster->nav.SetMovementScale(movement_rate);
    monster->nav.SetTurnRate(rotation_rate * static_cast<float>(kMotionFullTurnRadians));

    W8NavigatorMode navigation_mode = W8_NAVIGATOR_MODE_GROUNDED;
    if (flies)
        navigation_mode = W8_NAVIGATOR_MODE_FLYING;
    else if (swims)
        navigation_mode = W8_NAVIGATOR_MODE_SWIMMING;
    else if (quadruped)
        navigation_mode = W8_NAVIGATOR_MODE_QUADRUPED;
    else if (crawls)
        navigation_mode = W8_NAVIGATOR_MODE_CRAWLING;
    monster->nav.SetNavigationMode(navigation_mode);
    if (spice_monster) {
        /* 0x004C1CC5..0x004C1CD7: byte stores to +0x332, +0xa0 (navigator
           active) and +0x14e (movement boundary_enabled). */
        monster->hostility_preserved = true;
        monster->nav.active = false;
        monster->nav.movement.boundary_enabled = false;
    }

    monster->RandomizeAppearanceAndMotion();
    monster->asset_name = monster_name;
    return success ? std::move(monster) : nullptr;
}

/* Select one of the four directional states without immediately repeating the
   caller's current state. Random values four and five fold back to zero in the
   original table. */
W8MonsterCycle ParseMonsterCycleName(std::string_view name, signed char* subcycle)
{
    int index = W8_MONSTER_CYCLE_COUNT;

    if (name.empty()) {
        Fail("pacName");
    }

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

/* Read one animation/subcycle into the Monster representation.  The current
   subcycle is the newly appended animation slot; its playback scale and light
   list are owned by a single cycle slot. */
unsigned char Monster::ReadCycleData(LevelLoadContext* info, int cycle_index, int load_all)
{
    EmitterLights lights;
    Animation* animation;
    unsigned char success;
    signed char cycle;

    if (info == 0 || info->hFile == 0) {
        Fail("pInfo && pInfo->hFile");
    }

    auto owner = std::make_unique<Animation>();
    animation = owner.get();
    success = AnimObjReadFromFile(info, animation, load_all, &lights.lights, 0);
    if (!success) return 0;
    if (cycle_index == W8_MONSTER_CYCLE_NONE) {
        cycle_index = static_cast<signed char>(animation->cycle);
    }
    cycle = static_cast<signed char>(cycle_index);
    if (cycle < W8_MONSTER_CYCLE_BIRTH || cycle >= W8_MONSTER_CYCLE_COUNT) {
        Fail("bCycle>=CYCLE_FIRST && bCycle<=CYCLE_LAST");
    }

    auto& installed =
        cycles[cycle].emplace_back(std::move(lights), std::move(owner), animation->playback_scale);
    visual.active_animation = animation;
    visual.current_subcycle = static_cast<signed char>(cycles[cycle].size() - 1);

    visual.rep_active = true;
    visual.frame_direction = W8_ANIMATION_FORWARD;
    visual.m_bLOD = 2;
    visual.timer = GameClockMilliseconds();
    visual.animation_behaviour = animation->behaviour;
    visual.frame_method = animation->frame_method;
    visual.animation_playing = animation->animation_playing;
    if (visual.current_cycle == -1) {
        visual.current_cycle = cycle;
    }

    if (animation->path_lists == 1) {
        signed char list;

        for (list = 0; list < 3; ++list) {
            signed char entry;
            signed char count = static_cast<signed char>(AnimObjListCount(animation, list));

            for (entry = 0; entry < count; ++entry) {
                KeyframeTrack* path = AnimObjListEntry(animation, list, entry);
                if (path != 0) {
                    path->looping = true;
                    path->discrete_mode = true;
                    path->speed = animation->playback_scale;
                }
            }
        }
    }

    if (!installed.lights.lights.empty()) visual.SetLights(&installed.lights.lights);
    return success;
}

std::unique_ptr<Monster> LoadMonsterCycle(World* world, std::string_view mon_name, int cycle,
                                          int value)
{
    auto monster = std::make_unique<Monster>(world);
    AllocateLegacyPropSaveId();
    monster->visual.current_cycle = -1;
    if (!loadAnimationCycle(mon_name, monster->visual, cycle, value, "data\\monsters",
                            [&](LevelLoadContext& read_info, int read_cycle, int read_value) {
                                return monster->ReadCycleData(&read_info, read_cycle, read_value) !=
                                       0;
                            }))
        return nullptr;
    for (auto& event : monster->visual.particles) {
        if (event.cycle == cycle && event.subcycle == -1)
            event.subcycle = monster->visual.current_subcycle;
    }
    return monster;
}

/* Pass a copied position through Navigator's collision adjustment and install
   the adjusted result on the Monster's ordinary Navigator base. */
