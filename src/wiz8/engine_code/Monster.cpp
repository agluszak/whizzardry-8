#include "wiz8/engine_code/Monster.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ReadMesh.h"
#include "application.h"
#include "asset_paths.h"
#include "attack_modes.h"
#include "audio_resources.h"
#include "compat/native.h"
#include "conditions.h"
#include "dice.h"
#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "local_code/SpellEffect.h"
#include "monster_actions.h"
#include "monster_cycles.h"
#include "particle_modes.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srDebug.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "texture_animation.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/MonsterLight.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/emitter_resources.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/ground_shadow.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/resource_cache.h"
#include "wiz8/startup_world.h"
#include "wiz8/surface_storage.h"
#include "wiz8/ui_input.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

struct W8NpcState;

int g_combat_round_counter;

float g_monster_light_scale = 1.0f;

unsigned char g_monster_shadow_updates_enabled;
/* Layout-compatible with glm::vec3 but POD so VC6 emits static .data
   instead of a dynamic initializer into .bss. */
struct W8AttachmentOffset {
    float x;
    float y;
    float z;
};

static W8AttachmentOffset g_monster_attachment_offsets[8][8] = {
    {},
    {{-75.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}},
    {{-75.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f}, {-37.5f, 0.0f, 0.0f}, {37.5f, 0.0f, 0.0f}, {112.5f, 0.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {0.0f, 75.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {-37.5f, 75.0f, 0.0f},
     {37.5f, 75.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {-75.0f, 75.0f, 0.0f},
     {0.0f, 75.0f, 0.0f},
     {75.0f, 75.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {-112.5f, 75.0f, 0.0f},
     {-37.5f, 75.0f, 0.0f},
     {37.5f, 75.0f, 0.0f},
     {112.5f, 75.0f, 0.0f}}};
static float g_monster_attachment_scales[8] = {0.3f,  0.2f,  0.15f, 0.15f,
                                               0.15f, 0.15f, 0.15f, 0.15f};

const float g_monster_rotation_offset = 3.141592502593994f;
const double g_monster_death_rotation_pi = 3.141592653589793;
const float g_monster_attachment_distance_scale = 0.00039999998989515007f;
const float g_monster_attachment_vertical_scale = 30.0f;
const double g_monster_attachment_group_spacing = 15.0;
const float g_monster_linked_vertical_scale = 255.0f;
const float g_monster_poster_vertical_rate = 0.20000000298023224f;
const double g_monster_poster_max_distance = 1000.0;
const double g_monster_script_direction_step = 0.8975978857142857;
const double g_monster_facing_tolerance = 0.78539815;
const double g_monster_group_nearest_range = 12500.0;
static char g_warning_missing_spell_vertex[] =
    "WARNING: %s does not have a SPELL vertex marked! --> Lee";
W8CycleNameRow g_cycle_names[W8_MONSTER_CYCLE_COUNT] = {
    {"BIRTH", 5},
    {"IDLE", 4},
    {"SPICE", 5},
    {"TRANSITION", 10},
    {"WALK", 4},
    {"DRAW", 4},
    {"ATTACK_CLOSE", 12},
    {"ATTACK_RANGED", 13},
    {"unused", 6},
    {"ATTACK_SWING", 12},
    {"ATTACK_THRUST", 13},
    {"ATTACK_BASH", 11},
    {"ATTACK_MELEE", 12},
    {"ATTACK_THROW", 12},
    {"ATTACK_PUNCH", 12},
    {"ATTACK_KICK", 11},
    {"ATTACK_LASH", 11},
    {"ATTACK_SHOOT", 12},
    {"ATTACK_SPECIAL", 14},
    {"DODGE", 5},
    {"GET_HIT", 7},
    {"DIE", 3},
    {"TURN", 4},
    {"TALK_SPICE", 10},
    {"TALK", 4},
    {"SPELL", 5},
    {"SPECIAL", 7},
};

static bool g_monster_gib_option = true;
extern const double g_monster_light_color_scale = 0.00392156862745098;

static std::vector<stModelInstance*> g_monster_model_instances;

/* Highlight triangle bitmaps indexed by the slot's marching-order position:
   the marker SetMonsterPartySlotMarker hangs on a monster. */
const char* g_party_target_marker_bitmaps[8] = {"TriRed.tga",  "TriGreen.tga",  "TriPurple.tga",
                                                "TriBlue.tga", "TriOrange.tga", "TriYellow.tga",
                                                "TriPink.tga", "TriBrown.tga"};

const char* g_monster_script_commands[MONSCR_COUNT] = {"GOTO",
                                                       "WALKTO",
                                                       "FACE",
                                                       "SAY",
                                                       "CYCLE",
                                                       "SHOOT",
                                                       "GIVE",
                                                       "TELEPORT",
                                                       "CAST",
                                                       "DIE",
                                                       "END",
                                                       "IF",
                                                       "ELSE",
                                                       "ENDIF",
                                                       "NPCINTERACTION",
                                                       "DISPOSITION",
                                                       "DISAPPEAR",
                                                       "LOOKHERE",
                                                       "NPCNUMBER",
                                                       "FOLLOW",
                                                       "PATROL",
                                                       "STOPPATROL",
                                                       "TRIGGER",
                                                       "DELAY",
                                                       "BEGINORDERS",
                                                       "ENDORDERS",
                                                       "GUARD",
                                                       "POINTPATROL",
                                                       "RANDOMPOINTPATROL",
                                                       "DEAF",
                                                       "DOACTION",
                                                       "EAST",
                                                       "NORTHEAST",
                                                       "NORTH",
                                                       "NORTHWEST",
                                                       "WEST",
                                                       "SOUTHWEST",
                                                       "SOUTH",
                                                       "SOUTHEAST",
                                                       "TURNTOFACEPARTY",
                                                       "LOOKABOUT",
                                                       "PLAY",
                                                       "FADEOUT",
                                                       "STAYHOME"};

/* Both skin directives ensure the base damage stage exists exactly once. */
static void EnsureMonsterBaseSkin(W8Monster* monster, std::string_view monster_name,
                                  int& damage_stage, bool& skins_started)
{
    if (skins_started) return;
    damage_stage = monster->AddDamageStage(monster_name, 0);
    std::vector<stModelInstance*> instances;
    monster->CollectModelInstances(instances);
    for (stModelInstance* instance : instances) {
        instance->damage_stage = damage_stage;
    }
    skins_started = true;
}

/* Read the text-side monster representation. A named representation is shared
   by cloning its first live cycle; otherwise the MLS file supplies scalar
   movement settings, visual flags, sound/shake events, skin stages, lights,
   and the list of binary .mon cycles. */
std::unique_ptr<W8Monster> W8Monster::ReadAllCycles(W8World* world, std::string_view monster_name,
                                                    int load_value, int location_id)
{
    std::unique_ptr<W8Monster> monster;
    W8Monster* shared = static_cast<W8Monster*>(FindFirstGrCycleByName(monster_name));
    if (shared != 0) {
        monster = std::make_unique<W8Monster>(*shared, world);
        if (!monster) {
            Fail("*ppMonster");
        }
        monster->RandomizeAppearanceAndMotion();
        RegisterGrCycle(monster_name, monster.get());
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
    monster = std::make_unique<W8Monster>(world);
    AllocateLegacyPropSaveId();
    monster->current_cycle = -1;

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

                            success = LoadGrCycle(argument, monster.get(), cycle, load_value,
                                                  "data\\monsters", 0);
                            if (!monster->particles.empty()) {
                                for (int index = 0;
                                     index < static_cast<int>(monster->particles.size()); ++index) {
                                    W8GrCycleParticleAttachment* event = &monster->particles[index];
                                    if (event->cycle == cycle && event->subcycle == -1) {
                                        event->subcycle = monster->current_subcycle;
                                    }
                                }
                            }
                            if (animation_scale > 0.0f) {
                                W8Monster* rep = monster.get();
                                int current = rep->current_subcycle;
                                /* 0x004C128A: a missing subcycle is fatal, as in
                                   every other retail cycle lookup. */
                                if (static_cast<int>(rep->animations[cycle].size()) <= current) {
                                    ShutdownWithErrorBox(
                                        fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                                    rep->name, g_cycle_names[cycle].name, current));
                                }
                                W8AnimObj* animation = monster->animations[cycle][current].get();
                                animation->playback_scale = animation_scale;
                                monster->animation_scales[cycle][current] = animation_scale;
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
                        auto sound = std::make_unique<W8SoundEvent>(
                            sound_type, sound_cycle, frame, subcycle - 1, wave_path.c_str(),
                            SDL_strcasecmp(loop_name.c_str(), "LOOP") == 0);
                        last_sound = sound.get();
                        if (last_sound != nullptr) {
                            monster->AddSoundEvent(std::move(sound));
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
                        monster->AddShakeEffect(effect);
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

    W8Monster* representation = monster.get();
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
        monster->movement.collision_radius = walk_radius;
        if (world->octree && world->octree->max_navigation_radius < walk_radius) {
            world->octree->max_navigation_radius = walk_radius;
        }
    }
    if (fight_radius != 0.0f) {
        fight_radius *= kWorldScale;
        monster->movement.alternate_radius = fight_radius;
        if (world->octree && world->octree->max_navigation_radius < fight_radius) {
            world->octree->max_navigation_radius = fight_radius;
        }
    }
    if (target_height != 0.0f) {
        target_height *= kWorldScale;
        if (target_height < 250.0f) target_height = 250.0f;
        monster->movement.height_offset = target_height;
    }
    if (camera_height != 0.0f) {
        monster->movement.secondary_height_offset = camera_height * kWorldScale;
    }

    if (!random_idle_range) {
        idle_fps_start = -3.0f;
        idle_fps_end = 3.0f;
    }
    if (static_cast<int>(representation->animations[1].size()) < 1) {
        ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                         representation->name, g_cycle_names[1].name, 0));
    }
    W8AnimObj* idle = representation->animations[1][0].get();
    if (idle != 0) {
        representation->random_idle = true;
        representation->idle_playback_scale = idle->playback_scale;
        representation->random_idle_fps_min = idle_fps_start;
        representation->random_idle_fps_max = idle_fps_end;
    }
    if (missile_start > 0) monster->missile_frame = missile_start;
    if (spell_start > 0) monster->spell_frame = spell_start;
    if (has_lod_range) {
        representation->lod_near = lod_range_start * kWorldScale;
        representation->lod_far = lod_range_end * kWorldScale;
    }
    if (opacity >= 0.0f && opacity < 1.0f) {
        monster->opacity = opacity;
    }
    if (glow > 0.0f) {
        std::vector<stModelInstance*> instances;
        monster->CollectModelInstances(instances);
        for (stModelInstance* instance : instances) {
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
            shadow_width = monster->movement.collision_radius * 0.002f;
        }
        if (shadow_depth == 0.0f) shadow_depth = shadow_width;
        monster->CreateGroundShadow(shadow_width * kWorldScale, shadow_depth * kWorldScale);
    }
    representation->left_handed = left_handed;
    representation->special_movement = (flies || swims || full_transition) ? 1 : 0;

    if (has_light && representation->monster_light == 0) {
        representation->monster_light = std::make_unique<MonsterLight>(
            world->dynamic_scene, light_pulsing, monster->movement.collision_radius * 3.0f,
            &light_first, &light_second);
        representation->monster_light->m_vertical_offset = monster->movement.height_offset;
    }

    representation->current_subcycle = 0;
    monster->SetCycle(1);
    monster->SetSubCycle(0);
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) return nullptr;
    monster->SetBounds(*bounds);
    if (movement_rate < 0.1f) movement_rate = 2.0f;
    monster->SetMovementScale(movement_rate);
    monster->SetTurnRate(rotation_rate * static_cast<float>(kMotionFullTurnRadians));

    W8NavigatorMode navigation_mode = W8_NAVIGATOR_MODE_GROUNDED;
    if (flies)
        navigation_mode = W8_NAVIGATOR_MODE_FLYING;
    else if (swims)
        navigation_mode = W8_NAVIGATOR_MODE_SWIMMING;
    else if (quadruped)
        navigation_mode = W8_NAVIGATOR_MODE_QUADRUPED;
    else if (crawls)
        navigation_mode = W8_NAVIGATOR_MODE_CRAWLING;
    monster->SetNavigationMode(navigation_mode);
    if (spice_monster) {
        /* 0x004C1CC5..0x004C1CD7: byte stores to +0x332, +0xa0 (navigator
           active) and +0x14e (movement boundary_enabled). */
        monster->hostility_preserved = true;
        monster->active = false;
        monster->movement.boundary_enabled = false;
    }

    monster->RandomizeAppearanceAndMotion();
    RegisterGrCycle(monster_name, monster.get());
    return success ? std::move(monster) : nullptr;
}

/* Select one of the four directional states without immediately repeating the
   caller's current state. Random values four and five fold back to zero in the
   original table. */
unsigned short ChooseDifferentMonsterDirection(unsigned short previous_direction)
{
    unsigned short direction;

    for (;;) {
        direction = static_cast<unsigned short>(Random(6));
        if (direction > 3) {
            direction = 0;
        }
        switch (previous_direction) {
        case 0:
            if (direction == 0) {
                continue;
            }
            break;
        case 1:
            if (direction == 1) {
                continue;
            }
            break;
        case 2:
            if (direction == 2) {
                continue;
            }
            break;
        case 3:
            if (direction == 3) {
                continue;
            }
            break;
        default:
            continue;
        }
        return direction;
    }
}

/* A loaded Monster receives independent appearance, animation-speed, and
   vertical-motion variation. The cycle-one animation and scale vectors are
   updated together so the representation and its cached scalar values remain
   synchronized. */
void W8Monster::RandomizeAppearanceAndMotion()
{
    unsigned int random_value;

    /* 0x004C1D3D: cmp/setl - a signed test against the LEFTHANDED percent. */
    mirror_x = static_cast<int>(Random(100)) < left_handed;

    if (minimum_scale != 0.0f && maximum_scale != 0.0f) {
        float minimum = minimum_scale;
        float maximum = maximum_scale;

        if (minimum > maximum) {
            Fail("flStart <= flEnd");
        }
        random_value = Random(1000);
        scale = (maximum - minimum) * random_value * 0.001f + minimum;
    }

    if (random_idle) {
        int subcycle;
        float playback_scale = (random_idle_fps_max - random_idle_fps_min) * Random(1000) * 0.001f +
                               random_idle_fps_min;

        for (subcycle = 0; subcycle < static_cast<signed char>(animations[1].size()); ++subcycle) {
            int animation_index = static_cast<signed char>(subcycle);
            W8AnimObj* animation;
            float scale = playback_scale + idle_playback_scale;

            if (animation_index == -1) {
                animation_index = current_subcycle;
            }
            if (animation_index >= static_cast<int>(animations[1].size())) {
                ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                                 name, g_cycle_names[1].name, animation_index));
            }
            animation = animations[1][animation_index].get();
            if (scale < 1.0f) {
                scale = 1.0f;
            }
            animation->playback_scale = scale;
            if (subcycle < static_cast<int>(animation_scales[1].size())) {
                animation_scales[1][subcycle] = scale;
            }
        }
    }

    movement.vertical_base =
        ((hover_base_max - hover_base_min) * static_cast<float>(Random(1000)) * 0.001f +
         hover_base_min) *
        kWorldScale;
    movement.vertical_amplitude =
        ((bob_amplitude_max - bob_amplitude_min) * static_cast<float>(Random(1000)) * 0.001f +
         bob_amplitude_min) *
        kWorldScale;
    movement.vertical_phase = Random(1000) * 0.001f;
    movement.vertical_offset =
        static_cast<float>(sin(movement.vertical_phase * kMotionFullTurnRadians)) *
            movement.vertical_amplitude +
        movement.vertical_base;
    movement.height_offset += movement.vertical_base;
    movement.secondary_height_offset += movement.vertical_base;

    if (opacity < 1.0f) {
        instance_scale = opacity;
        apply_instance_scale = true;
    }
}

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
   list occupy the two parallel vectors for the same cycle. */
unsigned char W8Monster::ReadCycleData(W8ReadLevelInfo* info, int cycle_index, int load_all)
{
    EmitterLights lights;
    W8AnimObj* animation;
    unsigned char success;
    signed char cycle;
    int subcycle;

    if (info == 0 || info->hFile == 0) {
        Fail("pInfo && pInfo->hFile");
    }

    auto owner = std::make_unique<W8AnimObj>();
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

    // Reserve before publishing either resource. Models must retain their lights
    // even if growing a cycle collection fails.
    animations[cycle].reserve(animations[cycle].size() + 1);
    animation_scales[cycle].reserve(animation_scales[cycle].size() + 1);
    auto& installed = light_lists[cycle].emplace_back(std::move(lights));
    animations[cycle].push_back(std::move(owner));
    current_subcycle = static_cast<signed char>(static_cast<int>(animations[cycle].size()) - 1);
    subcycle = current_subcycle;
    if (subcycle < static_cast<int>(animation_scales[cycle].size())) {
        animation_scales[cycle][subcycle] = animation->playback_scale;
    } else {
        animation_scales[cycle].push_back(animation->playback_scale);
    }

    rep_active = true;
    frame_direction = W8_ANIMATION_FORWARD;
    m_bLOD = 2;
    timer = GameClockMilliseconds();
    animation_behaviour = animation->behaviour;
    frame_method = animation->frame_method;
    animation_playing = animation->animation_playing;
    if (current_cycle == -1) {
        current_cycle = cycle;
    }

    if (animation->path_lists == 1) {
        signed char list;

        for (list = 0; list < 3; ++list) {
            signed char entry;
            signed char count = static_cast<signed char>(AnimObjListCount(animation, list));

            for (entry = 0; entry < count; ++entry) {
                W8PathAI* path = AnimObjListEntry(animation, list, entry);
                if (path != 0) {
                    path->looping = true;
                    path->discrete_mode = true;
                    path->speed = animation->playback_scale;
                }
            }
        }
    }

    if (!installed.lights.empty()) SetLights(&installed.lights);
    return success;
}

/* Deep-copy one cycle's animation objects and render lights while retaining
   its per-subcycle scalar values.  The light copies are new scene objects:
   they are registered with the world's light list and detached until the
   owning GrCycle selects this cycle. */
void W8Monster::CopyCycle(signed char cycle, const W8Monster* other, signed char other_cycle)
{
    int index;

    for (index = 0; index < static_cast<int>(other->animations[other_cycle].size()); ++index) {
        animations[cycle].push_back(
            std::make_unique<W8AnimObj>(*other->animations[other_cycle][index]));
        animation_scales[cycle].push_back(other->animation_scales[other_cycle][index]);
    }

    for (const auto& source : other->light_lists[other_cycle]) {
        light_lists[cycle].emplace_back().CloneFrom(source);
    }
}

/* cvdump preserves a terminal space in this generated thunk's demangled name;
   the explicit name reference must preserve it too. */

int g_monster_cycle_registry_weight;

W8Monster::W8Monster(W8World* owner)
    : W8GrCycle(owner), runtime_flags(0), value_1e0(-1), location_id(-1), scale_x(1.0f),
      scale_y(1.0f), scale_z(1.0f), missile_frame(0), spell_frame(0), talking(0),
      animate_mouth(false), mouth_frame_clock(0), mouth_frame(0), talk_start(0), inactive(false),
      pending_finalize(1), disabled(0), nearest_to_party(false), spell_vertex_warned(0),
      missile_point_warned(0), script(0), script_wait(MONSCR_NONE), trigger(0), registry_weight(0),
      spell_effect_armed(0), sector_mesh(0), fade_state(W8_MONSTER_FADE_IDLE),
      hostility_preserved(false), sound(nullptr)
{
    formation = glm::vec3(0);

    trace_mask = 2;
    direction_x = 0;
    direction_y = 0;
    direction_z = 0;
    target_scale = 1.0f;
    current_scale = 1.0f;
    sunlit_state = W8_MONSTER_SUNLIGHT_UNKNOWN;
    orders_finished = false;
    defining_orders = false;
    deaf = false;
    face_party = false;
    stay_home = false;
    patrol_index = 0;
    look_frequency = 0;
    look_duration = 0;
    removal_state = W8_MONSTER_REMOVAL_NONE;
    cycle_callback = 0;
    move_dirty = true;
    order_mode = W8_MONSTER_ORDER_NONE;
}

W8Monster::W8Monster(const W8Monster& rhs, W8World* owner)
    : W8GrCycle(rhs, owner), runtime_flags(rhs.runtime_flags), value_1e0(rhs.value_1e0),
      location_id(rhs.location_id), scale_x(1.0f), scale_y(1.0f), scale_z(1.0f),
      missile_frame(rhs.missile_frame), spell_frame(rhs.spell_frame), talking(0),
      animate_mouth(false), mouth_frame_clock(0), mouth_frame(0), talk_start(0),
      inactive(rhs.inactive), pending_finalize(1), disabled(0), nearest_to_party(false),
      hover_base_min(rhs.hover_base_min), hover_base_max(rhs.hover_base_max),
      bob_amplitude_min(rhs.bob_amplitude_min), bob_amplitude_max(rhs.bob_amplitude_max),
      spell_vertex_warned(0), missile_point_warned(0), script(0), script_wait(MONSCR_NONE),
      trigger(0), registry_weight(rhs.registry_weight), spell_effect_armed(0), sector_mesh(0),
      sound(nullptr)
{
    formation = glm::vec3(0);
    fade_state = W8_MONSTER_FADE_IDLE;
    hostility_preserved = rhs.hostility_preserved;

    name = rhs.name;
    standing_height = rhs.standing_height;
    scale = rhs.scale;
    minimum_scale = rhs.minimum_scale;
    maximum_scale = rhs.maximum_scale;
    death_scale = rhs.death_scale;
    random_idle = rhs.random_idle;
    special_movement = rhs.special_movement;
    idle_playback_scale = rhs.idle_playback_scale;
    random_idle_fps_min = rhs.random_idle_fps_min;
    random_idle_fps_max = rhs.random_idle_fps_max;
    left_handed = rhs.left_handed;
    for (signed char cycle = W8_MONSTER_CYCLE_BIRTH; cycle < W8_MONSTER_CYCLE_COUNT; ++cycle) {
        CopyCycle(cycle, &rhs, cycle);
    }
    if (rhs.monster_light != 0) {
        monster_light = std::make_unique<MonsterLight>(*rhs.monster_light);
    }
    if (monster_light) monster_light->setParent(owner->dynamic_scene, 1);

    defining_orders = false;
    orders_finished = false;
    order_mode = W8_MONSTER_ORDER_NONE;
    deaf = false;
    face_party = false;
    stay_home = false;
    patrol_index = 0;
    direction_x = 0;
    direction_y = 0;
    direction_z = 0;
    look_frequency = 0;
    look_duration = 0;
    sunlit_state = W8_MONSTER_SUNLIGHT_UNKNOWN;
    move_dirty = true;
    target_scale = 1.0f;
    current_scale = 1.0f;
    runtime_flags &= ~(W8_MONSTER_KEEP_FRAME_DIRECTION | W8_MONSTER_SCALING_Y | W8_MONSTER_PARKED |
                       W8_MONSTER_REMOVE_AFTER_FADE | W8_MONSTER_REMOVE_NOW);
    removal_state = W8_MONSTER_REMOVAL_NONE;
    cycle_callback = 0;
    if (hostility_preserved) {
        active = false;
    }
}

W8Monster::~W8Monster()
{
    SetLights(0);
    ClearMonsterSpellIcons(this);
    if (IsSoleRegisteredCycleForName()) {
        g_monster_cycle_registry_weight -= registry_weight;
        RemoveCycleSkinTables();
    }
    UnregisterGrCycle(this);
    script.reset();
    runtime_flags &= ~W8_MONSTER_SCRIPT_WAIT;
    script_line = 0;
    script_wait = MONSCR_NONE;
    sound.reset();
}

/* Navigator is W8Monster's second base at +0x18. VC6 places this override in
   that secondary table and emits the adjusted entry form at 0x004CA840. */
void W8Monster::SetPosition(const glm::vec3* position)
{
    SetLocation(position);
    SetPositionInternal(position);
    position_dirty = true;
}

/* Advance the non-rendering half of one live Monster. This is the main
   Monster.cpp update slot: it maintains distance-driven model scale, the
   sunlight/ground-shadow transition, Navigator state, pending animation
   cycles, attached objects, and the optional scene node. Rendering remains in
   UpdateRepresentation. */
void W8Monster::Update()
{
    glm::vec3 party_position;
    glm::vec3 monster_position;
    W8MonsterInfo* monster_info = 0;
    float distance;
    int cycle;
    int index;

    party_position = cameraLocation();
    {
        glm::vec3 position = GetPosition();
        monster_position = position;
    }
    distance = glm::length((monster_position - party_position));

    if (sunlit_state == W8_MONSTER_SUNLIGHT_UNKNOWN || g_monster_light_scale < 1.0f) {
        current_scale = 0.75f;
        g_monster_model_instances.clear();
        CollectModelInstances(g_monster_model_instances);
        for (index = 0; index < static_cast<int>(g_monster_model_instances.size()); ++index) {
            stModelInstance* model = g_monster_model_instances[index];
            model->light_scale = glm::vec3(0.75f);
        }
        target_scale = 0.75f;
        light_scale_timer.SetDuration(0.025f);
        light_scale_timer.Restart();
        sunlit_state = W8_MONSTER_SUNLIGHT_LIT;
        move_dirty = true;
    }

    if (g_monster_shadow_updates_enabled != 0 && (move_dirty || UpdateTrackedPosition())) {
        move_dirty = false;
        if (distance < GetWorld()->render_range) {
            srNode* sun = FindWorldNode<srNode>("SUN");
            if (sun != 0) {
                glm::vec3 mapped_position;
                glm::dvec3 sun_location = sun->getLocation();
                glm::vec3 sun_position;

                GetMappedPosition(&mapped_position);
                sun_position = glm::vec3(sun_location);
                if (GetWorldOctree()->HasLineOfSight(&mapped_position, &sun_position, true)) {
                    if (sunlit_state == W8_MONSTER_SUNLIGHT_SHADOWED) {
                        target_scale = 0.75f;
                        light_scale_timer.SetDuration(0.025f);
                        light_scale_timer.Restart();
                        sunlit_state = W8_MONSTER_SUNLIGHT_LIT;
                    }
                } else if (sunlit_state == W8_MONSTER_SUNLIGHT_LIT) {
                    target_scale = 0.0f;
                    light_scale_timer.SetDuration(0.025f);
                    light_scale_timer.Restart();
                    sunlit_state = W8_MONSTER_SUNLIGHT_SHADOWED;
                }
            }
        }
    }

    if (current_scale != target_scale && light_scale_timer.GetProgress() >= 1.0f) {
        if (target_scale <= current_scale) {
            current_scale -= g_facing_tolerance0;
            if (current_scale < target_scale) {
                current_scale = target_scale;
            }
        } else {
            current_scale += g_facing_tolerance0;
            if (current_scale > target_scale) {
                current_scale = target_scale;
            }
        }
        RefreshModelLightScale();
        light_scale_timer.Restart();
    }

    if (fade_state != W8_MONSTER_FADE_IDLE) {
        float progress = fade_timer.GetProgress();
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        if (fade_state == W8_MONSTER_FADE_DELAYED_REMOVAL) {
            if (progress == 1.0f) {
                fade_state = W8_MONSTER_FADE_IDLE;
                fade_timer.SetDuration(3.0f);
                fade_timer.Restart();
                if (fade_state < W8_MONSTER_FADE_IN) {
                    instance_scale = 1.0f;
                    apply_instance_scale = true;
                    fade_state = W8_MONSTER_FADE_OUT;
                } else {
                    fade_timer.SetProgress(1.0f - instance_scale);
                    fade_state = W8_MONSTER_FADE_OUT;
                }
            }
        } else {
            if (fade_state < W8_MONSTER_FADE_IN) {
                instance_scale = 1.0f - progress;
            } else {
                instance_scale = progress;
            }
            apply_instance_scale = true;
            if (progress == 1.0f) {
                if (fade_state < W8_MONSTER_FADE_IDLE) {
                    runtime_flags |= W8_MONSTER_FADED_OUT;
                }
                fade_state = W8_MONSTER_FADE_IDLE;
            }
        }
    }

    cycle = Query(W8_MONSTER_QUERY_CYCLE);
    SetGroundShadowVisible(cycle != W8_MONSTER_CYCLE_BIRTH && cycle != W8_MONSTER_CYCLE_DIE &&
                           fade_state == W8_MONSTER_FADE_IDLE &&
                           (runtime_flags & W8_MONSTER_FADED_OUT) == 0);

    if (W8MonsterInfo* found = FindMonster(location_id, false)) {
        monster_info = found;
    }

    if ((cycle == W8_MONSTER_CYCLE_IDLE || cycle == W8_MONSTER_CYCLE_SPICE) &&
        pending_cycle == -1 && !movement_stopped && !halted) {
        flags |= 0x100000;
    }

    if (monster_info == 0) {
        UpdateNavigation(0, false);
    } else {
        UpdateNavigation(monster_info->highest_condition >= W8_CONDITION_WEBBED,
                         monster_info->uiCondition[W8_CONDITION_SLOWED] != 0);
    }

    if (cycle != W8_MONSTER_CYCLE_DIE && script != 0 && !gXStatus.fCombatMode) {
        ProcessScript();
        cycle = Query(W8_MONSTER_QUERY_CYCLE);
    }

    if ((runtime_flags & W8_MONSTER_SCRIPT_WAIT) == 0 && pending_cycle == -1) {
        switch (cycle) {
        case W8_MONSTER_CYCLE_TALK:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                if (talk_duration < w8_get_ticks() - talk_start && IsCycleSupported(0x17)) {
                    pending_cycle = 0x17;
                } else {
                    pending_cycle = 0x18;
                }
                frame_direction = W8_ANIMATION_FORWARD;
                pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                animation_playing = 1;
                timer = GameClockMilliseconds();
                pending_subcycle = 0;
            }
            break;
        case W8_MONSTER_CYCLE_TALK_SPICE:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                if (talking) {
                    /* 0x004C276F..0x004C27D7: the talking path never stores
                           pending_subcycle, so TALK keeps the current frame
                           (clamped by ApplyPendingCycle) instead of frame 0. */
                    talk_start = w8_get_ticks();
                    talk_duration = Random(2000) + 2000;
                    pending_cycle = 0x18;
                    frame_direction = W8_ANIMATION_FORWARD;
                    pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                    animation_playing = 1;
                    timer = GameClockMilliseconds();
                } else {
                    pending_cycle = 1;
                    animation_playing = 1;
                    timer = GameClockMilliseconds();
                    pending_subcycle = 0;
                }
            }
            break;
        case W8_MONSTER_CYCLE_SPELL:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                pending_cycle = 1;
                frame_direction = W8_ANIMATION_FORWARD;
                animation_playing = 1;
                timer = GameClockMilliseconds();
                pending_behaviour = W8_ANIMATION_NEVER_STOP;
                pending_subcycle = 0;
            }
            break;
        case W8_MONSTER_CYCLE_WALK:
            if (movement_stopped || halted) {
                bool transition = Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) != 0 || wrapped;
                if (!transition && !special_movement) {
                    transition =
                        Query(W8_MONSTER_QUERY_FRAME) < Query(W8_MONSTER_QUERY_FRAME_COUNT) / 2;
                }
                if (transition) {
                    while (!sounds.empty()) {
                        wiz8::sound::stop(sounds[0]);
                        sounds.erase(sounds.begin());
                    }
                    if (!IsCycleSupported(3)) {
                        pending_behaviour = W8_ANIMATION_NEVER_STOP;
                        pending_cycle = 1;
                    } else {
                        pending_cycle = 3;
                        frame_direction = W8_ANIMATION_REVERSE;
                        pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                        pending_subcycle =
                            static_cast<unsigned short>(Query(W8_MONSTER_QUERY_FRAME_COUNT) - 1);
                        runtime_flags |= W8_MONSTER_KEEP_FRAME_DIRECTION;
                    }
                    animation_playing = 1;
                    timer = GameClockMilliseconds();
                }
            }
            break;
        case W8_MONSTER_CYCLE_IDLE:
        case W8_MONSTER_CYCLE_SPICE:
            if (!movement_stopped && !halted &&
                (Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) != 0 || wrapped)) {
                flags &= ~0x100000;
                if (!IsCycleSupported(3)) {
                    pending_behaviour = W8_ANIMATION_NEVER_STOP;
                    pending_cycle = 4;
                } else {
                    pending_cycle = 3;
                    frame_direction = W8_ANIMATION_FORWARD;
                    pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                    pending_subcycle = 0;
                }
                animation_playing = 1;
                timer = GameClockMilliseconds();
            }
            break;
        case W8_MONSTER_CYCLE_TRANSITION:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                if (frame_direction == W8_ANIMATION_REVERSE) {
                    frame_direction = W8_ANIMATION_FORWARD;
                    pending_cycle = 1;
                } else {
                    pending_cycle = 4;
                }
                animation_playing = 1;
                timer = GameClockMilliseconds();
                pending_behaviour = W8_ANIMATION_NEVER_STOP;
                pending_subcycle = 0;
            }
            break;
        case W8_MONSTER_CYCLE_BIRTH:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                pending_cycle = 1;
                animation_playing = 1;
                timer = GameClockMilliseconds();
                pending_behaviour = W8_ANIMATION_NEVER_STOP;
                pending_subcycle = 0;
            }
            break;
        }
    }

    if (!rep_active) {
        return;
    }

    UpdateAttachedObjects();
    cycle = Query(W8_MONSTER_QUERY_CYCLE);
    if (((GetInputState().modifiers & CTRL_DOWN) != 0) && g_combat_state != 0 &&
        (g_combat_state->round_active || gXStatus.fPartyMovementMode) &&
        (cycle == W8_MONSTER_CYCLE_IDLE || cycle == W8_MONSTER_CYCLE_SPICE) &&
        (pending_cycle == -1 || pending_cycle == 1 || pending_cycle == 2) &&
        (g_combat_state->eCombatActionStatus != 2 || g_combat_state->pActionMonsterInfo == 0 ||
         g_combat_state->pActionMonsterInfo->location_id != location_id)) {
        timer = GameClockMilliseconds();
    }

    if (monster_info != 0 && monster_info->uiCondition[W8_CONDITION_SLOWED] != 0) {
        TickAnimation(Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_WALK
                          ? movement.movement_speed * 0.5f
                          : 0.5f);
    } else {
        TickAnimation(Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_WALK
                          ? movement.movement_speed
                          : 1.0f);
    }
    InitializeAnimatedTexture();

    if (sound != 0) {
        sound->position = GetPosition();
    }
}

/* Script IF expressions are deliberately small: a name followed by an
   optional comma-separated float list. The retail interpreter supports the
   two predicates below and treats every other name as false. */
bool W8Monster::EvaluateScriptCondition(std::string_view expression)
{
    // Both supported conditions use only the first numeric argument.
    const auto open = expression.find('(');
    const auto name = expression.substr(0, open);
    std::optional<float> parameter;
    if (open != expression.npos) {
        const auto start = expression.find_first_not_of(",)", open + 1);
        if (start != expression.npos)
            parameter = std::strtof(std::string(expression.substr(start)).c_str(), nullptr);
    }

    if (CompareTextIgnoreAsciiCase(name.substr(0, 9), "PARTYNEAR") == 0) {
        glm::vec3 party_position;
        glm::vec3 monster_position;

        party_position = cameraLocation();
        if (!parameter) {
            Fail("lsParmList.Length()",
                 fmt::format("Monscr {} line {:d}: PARTYNEAR expects 1 parameter",
                             script->getName(), script_line));
            return false;
        }

        glm::vec3 current_position = GetPosition();
        monster_position = current_position;
        if (!g_status.world_suspended &&
            glm::length((party_position - monster_position)) < *parameter * kWorldScale &&
            FindMonster(location_id)->player_visibility.line_of_sight != 0) {
            return true;
        }
    } else if (CompareTextIgnoreAsciiCase(name.substr(0, 6), "RANDOM") == 0) {
        if (!parameter) {
            Fail("lsParmList.Length()",
                 fmt::format("Monscr {} line {:d}: RANDOM expects 1 parameter", script->getName(),
                             script_line));
            return false;
        }
        if (Chance(static_cast<unsigned int>(*parameter)) != 0) {
            return true;
        }
    }
    return false;
}

/* Monster scripts are shared by name while any monster runs them. */
static ResourceCache<stScript> g_monster_scripts;

std::shared_ptr<stScript> LoadMonsterScript(std::string_view name)
{
    if (auto cached = g_monster_scripts.find(name)) return cached;
    const std::string path = "Data\\Monsters\\Scripts\\" + std::string(name);
    auto loaded = std::make_shared<stScript>();
    if (loaded->Load(path.c_str()) == 0) return nullptr;
    loaded->setName(std::string(name));
    g_monster_scripts.insert(std::string(name), loaded);
    return loaded;
}

void W8Monster::UseScript(std::shared_ptr<stScript> loaded, bool reset_orders)
{
    script = std::move(loaded);
    runtime_flags &= ~W8_MONSTER_SCRIPT_WAIT;
    script_line = 0;
    script_wait = MONSCR_NONE;
    sound.reset();
    if (reset_orders) {
        orders_finished = false;
    }
}

/* Replace the current Monster script with the named one; a missing script
   leaves the monster without one. */
bool W8Monster::SetScript(std::string script_name, bool reset_orders)
{
    auto loaded = LoadMonsterScript(script_name);
    const bool found = loaded != nullptr;
    UseScript(std::move(loaded), reset_orders);
    return found;
}

bool W8Monster::GetProjectilePosition(glm::vec3* position)
{
    signed char cycle;
    bool result;

    if (position == 0) {
        return false;
    }
    if (IsCycleSupported(0x11) && IsCycleSupported(0x0d)) {
        Fail("!(IsCycleSupported(CYCLE_ATTACK_SHOOT) && "
             "IsCycleSupported(CYCLE_ATTACK_THROW))");
    }
    if (IsCycleSupported(0x0d)) {
        cycle = W8_MONSTER_CYCLE_ATTACK_THROW;
    } else if (IsCycleSupported(0x11)) {
        cycle = W8_MONSTER_CYCLE_ATTACK_SHOOT;
    } else if (IsCycleSupported(7)) {
        cycle = W8_MONSTER_CYCLE_ATTACK_RANGED;
    } else {
        return false;
    }

    result = GetCycleMappedPosition(cycle, 5, position);
    if (!result && !missile_point_warned) {
        if (g_dev_mode) {
            W8MonsterInfo* info = FindMonster(location_id);
            DebugMessage(0,
                         fmt::format("WARNING: {} does not have a MISSILE vertex marked --> Lee!",
                                     GetMonsterDataForInfo(info)->name0));
        }
        missile_point_warned = true;
    }
    return result;
}

/* Cycle 25 mapping six is the spell launch vertex. Missing mappings warn at
   most once per Monster, while the result reports whether a launch position was found. */
bool W8Monster::GetSpellPosition(glm::vec3* position)
{
    bool found;

    if (position == 0) {
        return false;
    }
    found = GetCycleMappedPosition(0x19, 6, position);
    if (!found && !spell_vertex_warned) {
        if (g_dev_mode) {
            W8MonsterInfo* monster_info = FindMonster(location_id);
            W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
            DebugMessage(0, fmt::sprintf(g_warning_missing_spell_vertex, record->name0));
        }
        spell_vertex_warned = true;
    }
    return found;
}

bool W8Monster::GetCycleMappedPosition(signed char cycle, int mapped_index, glm::vec3* position)
{
    srModelInstance* current_model = GetCurrentModelInstance();
    W8AnimObj* animation;
    int subcycle;
    int dispatch_value;

    if (cycle == W8_MONSTER_CYCLE_NONE) {
        cycle = current_cycle;
        subcycle = current_subcycle;
    } else {
        subcycle = 0;
    }
    auto& cycle_animations = animations[cycle];
    if (static_cast<int>(cycle_animations.size()) <= subcycle) {
        ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}", name,
                                         g_cycle_names[cycle].name, subcycle));
    }
    animation = cycle_animations[subcycle].get();

    /* 0x004C7A3A..0x004C7A50: the missile vertex (5) is sampled on
       missile_frame (0x1f4) and the spell vertex (6) on spell_frame (0x1f8). */
    if (mapped_index == 5) {
        dispatch_value = missile_frame;
    } else if (mapped_index == 6) {
        dispatch_value = spell_frame;
    } else {
        dispatch_value = 0;
    }
    if (animation == 0) {
        return false;
    }
    if (animation->path_lists != 0) {
        const auto center = GetAnimationCenter();
        if (!center) return false;
        *position = *center;
        return true;
    }

    srModelInstance* model = AnimObjDispatch(animation, 2, dispatch_value);
    if (model != 0) {
        stMeshModel* mesh = static_cast<stMeshModel*>(model->getModel());
        int vertex = FindMappedIndexInMeshChain(&mesh, mapped_index);
        if (mesh != 0 && vertex != -1) {
            glm::vec3* vertices = mesh->GetVertexLocations(dispatch_value, true, 0.0f);
            if (vertices != 0 && current_model != 0) {
                auto rotation = current_model->getWorldSpaceRotation<glm::mat3>();
                glm::vec3 local = vertices[vertex] * scale;
                glm::vec3 rotated = rotation * local;
                glm::vec3 owner_position = GetPosition();
                (*position) = glm::vec3(rotated.x + owner_position.x,
                                        rotated.y + owner_position.y + movement.vertical_base,
                                        rotated.z + owner_position.z);
                return true;
            }
        }
    }
    return false;
}

bool W8Monster::ResolveScriptPosition(std::string_view name, glm::vec3* position)
{
    if (CompareTextIgnoreAsciiCase(name, "home") == 0) {
        *position = formation;
    } else if (CompareTextIgnoreAsciiCase(name, "off_camera") == 0) {
        position->x = position->y = position->z = -10000000.0f;
    } else if (!FindEntityByName(name, position, 0, 0)) {
        return false;
    }
    return true;
}

/* Execute source lines until a command starts an asynchronous operation, ends
   the script, or the runaway-command guard trips. The two command modes share
   the original table: normal mode performs actions, while BEGINORDERS records
   the persistent movement policy that the Navigator update consumes. */
void W8Monster::ProcessScript()
{
    W8MonsterInfo* monster_info;
    int command_count;
    bool stop;
    /* 0x004C8116 clears this byte once per call, not per command: every
       POINTPATROL/RANDOMPOINTPATROL resolved point in the same pass counts,
       so a later patrol line appends to the earlier one instead of clearing
       the list, and the signed byte test at 0x004C9855/0x004C9A49 decides
       whether the mode is set. */
    signed char patrol_points = 0;

    if (script == 0) {
        return;
    }

    monster_info = FindMonster(location_id);
    if (orders_finished) {
        if (monster_info == 0 || (monster_info->ai_mode & W8_MONSTER_AI_RESTORE_SCRIPT) == 0) {
            return;
        }
        monster_info->ai_mode &= ~W8_MONSTER_AI_RESTORE_SCRIPT;
        return;
    }

    if (monster_info != 0 && (monster_info->ai_mode & W8_MONSTER_AI_RESTORE_SCRIPT) != 0) {
        monster_info->ai_mode &= ~W8_MONSTER_AI_RESTORE_SCRIPT;
        if (script_wait == MONSCR_WALKTO && movement_stopped) {
            if (script_line > 0) {
                --script_line;
            }
        } else if (!CanContinueScript()) {
            return;
        }
    } else if (!CanContinueScript()) {
        return;
    }

    script_wait = MONSCR_NONE;
    stop = false;
    command_count = 0;
    while (script != 0 && script_wait == MONSCR_NONE && !stop) {
        if (script_line >= static_cast<int>(script->lines.size())) break;
        std::istringstream tokens(script->lines[script_line++].text);
        std::string token;
        W8MonsterScriptCommand command;
        if (!(tokens >> token)) continue;

        command = MONSCR_NONE;
        for (int index = 0; index < MONSCR_COUNT; ++index) {
            if (CompareTextIgnoreAsciiCase(token, g_monster_script_commands[index]) == 0) {
                command = static_cast<W8MonsterScriptCommand>(index);
                break;
            }
        }

        if (!script_conditions.empty() && !(script_conditions[0]) && command != MONSCR_ELSE &&
            command != MONSCR_ENDIF) {
            if (++command_count > 50) {
                stop = true;
            }
            continue;
        }

        const auto resolveNamedPosition = [&](glm::vec3& position) {
            if (ResolveScriptPosition(token.c_str(), &position)) return true;
            ShutdownWithErrorBox(fmt::format("MonScript {} Line {:d}: Unknown location {}",
                                             script->getName(),
                                             script->GetSourceLine(script_line - 1), token));
            return false;
        };

        if (!defining_orders) {
            switch (command) {
            case MONSCR_GOTO: {
                tokens >> token;
                if (tokens) {
                    int line_number = script->FindLabelLine(token);
                    if (line_number != -1) {
                        script_conditions.clear();
                        script_line = line_number;
                    }
                }
                stop = true;
                break;
            }
            case MONSCR_WALKTO: {
                glm::vec3 position;
                tokens >> token;
                if (!tokens) {
                    break;
                }
                if (!resolveNamedPosition(position)) {
                    break;
                }
                if (SDL_strcasecmp(token.c_str(), "PARTY") == 0) {
                    SetMovementTargetToNavigator(PartyNavigator(), 5.0);
                } else {
                    ConfigureMovementToPosition(&position);
                }
                tokens >> token;
                if (!tokens || SDL_strcasecmp(token.c_str(), "NOBLOCK") != 0) {
                    script_wait = MONSCR_WALKTO;
                } else {
                    stop = true;
                }
                break;
            }
            case MONSCR_FACE: {
                glm::vec3 position;
                tokens >> token;
                if (!tokens) {
                    break;
                }
                if (!resolveNamedPosition(position)) {
                    break;
                }
                AimAtPosition(&position);
                tokens >> token;
                if (!tokens || SDL_strcasecmp(token.c_str(), "NOBLOCK") != 0) {
                    script_wait = MONSCR_FACE;
                } else {
                    stop = true;
                }
                break;
            }
            case MONSCR_SAY:
            case MONSCR_NPCINTERACTION: {
                int line_number = -1;
                bool suppress = false;
                tokens >> token;
                if (tokens) {
                    line_number = atoi(token.c_str());
                    tokens >> token;
                    if (tokens && SDL_strncasecmp(token.c_str(), "SUPPRESS", 8) == 0) {
                        suppress = true;
                    }
                }
                ForwardNpcScriptNotice(FindNpcBindingForMonster(FindMonster(location_id)), 0,
                                       line_number, command == MONSCR_SAY ? 1 : suppress);
                if (command == MONSCR_NPCINTERACTION) {
                    script_wait = MONSCR_NPCINTERACTION;
                } else if (!tokens || SDL_strcasecmp(token.c_str(), "NOBLOCK") != 0) {
                    script_wait = MONSCR_SAY;
                } else {
                    stop = true;
                }
                break;
            }
            case MONSCR_CYCLE: {
                signed char subcycle;
                tokens >> token;
                if (tokens) {
                    W8MonsterCycle cycle = ParseMonsterCycleName(token.c_str(), &subcycle);
                    if (cycle != W8_MONSTER_CYCLE_NONE) {
                        pending_cycle = static_cast<signed char>(cycle);
                        animation_playing = 1;
                        timer = GameClockMilliseconds();
                        SetSubCycle(0);
                        forced_subcycle = subcycle - 1;
                        if (pending_cycle == -1) {
                            pending_cycle = current_cycle;
                        }
                        runtime_flags |= W8_MONSTER_KEEP_SUBCYCLE;
                        tokens >> token;
                        if (!tokens || SDL_strcasecmp(token.c_str(), "NOBLOCK") != 0) {
                            pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                            runtime_flags |= W8_MONSTER_SCRIPT_WAIT;
                            script_wait = MONSCR_CYCLE;
                        } else {
                            stop = true;
                        }
                    } else {
                        ShutdownWithErrorBox(fmt::format(
                            "MonScript {} Line {:d}: Unknown cycle {}", script->getName(),
                            script->GetSourceLine(script_line - 1), token));
                    }
                }
                break;
            }
            case MONSCR_SHOOT: {
                glm::vec3 source;
                glm::vec3 target;
                tokens >> token;
                if (!tokens) {
                    break;
                }
                unsigned int owner = static_cast<unsigned int>(atoi(token.c_str()));
                tokens >> token;
                if (!tokens) {
                    break;
                }
                if (!ResolveScriptPosition(token.c_str(), &target)) {
                    break;
                }
                if (!GetProjectilePosition(&source)) {
                    GetMappedPosition(&source);
                }
                FireMissile(owner, &source, &target, 0, 0, 1, 50000.0f);
                break;
            }
            case MONSCR_GIVE:
                tokens >> token;
                if (tokens) {
                    int item = FindItemRecordByName(token.c_str());
                    if (item != -1) {
                        CreateItemIntoHandOrPool(item, true);
                    }
                }
                break;
            case MONSCR_TELEPORT: {
                glm::vec3 position;
                tokens >> token;
                if (tokens) {
                    if (!resolveNamedPosition(position)) {
                        break;
                    }
                    SetPositionInternal(&position);
                }
                break;
            }
            case MONSCR_DIE:
                pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                pending_cycle = 0x15;
                animation_playing = 1;
                timer = GameClockMilliseconds();
                SetSubCycle(0);
                break;
            case MONSCR_END:
                stop = true;
                script_line = static_cast<int>(script->lines.size());
                break;
            case MONSCR_IF: {
                bool invert = false;
                bool value;
                // Incomplete IF and IF NOT statements do not push a condition.
                tokens >> token;
                if (!tokens) {
                    break;
                }
                if (SDL_strncasecmp(token.c_str(), "NOT", 3) == 0) {
                    invert = true;
                    tokens >> token;
                }
                if (!tokens) break;
                value = EvaluateScriptCondition(token);
                script_conditions.insert(script_conditions.begin(), invert ? !value : value);
                break;
            }
            case MONSCR_ELSE:
                if (!script_conditions.empty()) {
                    script_conditions[0] = !(script_conditions[0]);
                } else {
                    ShutdownWithErrorBox(
                        fmt::format("MonScript {} Line {:d}: ELSE without matching IF",
                                    script->getName(), script->GetSourceLine(script_line - 1)));
                }
                break;
            case MONSCR_ENDIF:
                if (!script_conditions.empty()) {
                    script_conditions.erase(script_conditions.begin());
                } else {
                    ShutdownWithErrorBox(
                        fmt::format("MonScript {} Line {:d}: ENDIF without matching IF",
                                    script->getName(), script->GetSourceLine(script_line - 1)));
                }
                break;
            case MONSCR_DISPOSITION: {
                W8Disposition disposition = static_cast<W8Disposition>(0xff);
                tokens >> token;
                if (tokens) {
                    if (SDL_strcasecmp(token.c_str(), "DISP_NEUTRAL") == 0)
                        disposition = W8_DISPOSITION_NEUTRAL;
                    else if (SDL_strcasecmp(token.c_str(), "DISP_HOSTILE") == 0)
                        disposition = W8_DISPOSITION_HOSTILE;
                    else if (SDL_strcasecmp(token.c_str(), "DISP_FRIENDLY") == 0)
                        disposition = W8_DISPOSITION_FRIENDLY;
                    monster_info = FindMonster(location_id);
                    if (monster_info != 0) {
                        W8MonsterGroup* group =
                            FindMonsterGroup(monster_info->monster_group_id, false);
                        if (group != 0) {
                            SetMonsterGroupHostility(group, disposition, false);
                        }
                    }
                }
                break;
            }
            case MONSCR_DISAPPEAR:
                runtime_flags |= W8_MONSTER_PARKED;
                break;
            case MONSCR_LOOKHERE:
                PointCameraAtMonster(FindMonster(location_id), true, true);
                tokens >> token;
                if (!tokens || SDL_strcasecmp(token.c_str(), "NOBLOCK") != 0) {
                    script_wait = MONSCR_LOOKHERE;
                } else {
                    stop = true;
                }
                break;
            case MONSCR_PATROL: {
                glm::vec3 home;
                float distance;
                float variation;
                tokens >> token;
                if (!tokens) break;
                distance = static_cast<float>(atof(token.c_str())) * kWorldScale;
                tokens >> token;
                if (!tokens) break;
                variation = static_cast<float>(atof(token.c_str())) * kWorldScale;
                home = formation;
                if (home == glm::vec3(0)) {
                    glm::vec3 current = GetPosition();
                    home = current;
                    formation = home;
                }
                StartPatrol(&home, distance, variation);
                break;
            }
            case MONSCR_STOPPATROL:
                flags &= 0xdfffffff;
                break;
            case MONSCR_TRIGGER:
                tokens >> token;
                if (tokens) {
                    this->trigger = FindTriggerByName(token.c_str());
                    if (this->trigger != 0) {
                        this->trigger->Run(-1);
                        script_wait = MONSCR_TRIGGER;
                    }
                }
                break;
            case MONSCR_DELAY:
                tokens >> token;
                if (tokens && static_cast<float>(atof(token.c_str())) != 0.0f) {
                    script_delay_timer.SetDuration(static_cast<float>(atof(token.c_str())) *
                                                   0.001f);
                    script_delay_timer.Restart();
                    script_wait = MONSCR_DELAY;
                }
                break;
            case MONSCR_BEGINORDERS:
                defining_orders = true;
                break;
            case MONSCR_DEAF:
                deaf = true;
                break;
            case MONSCR_DOACTION:
                tokens >> token;
                if (tokens) {
                    if (SDL_strcasecmp(token.c_str(), "STARTGOLEMATTACK") == 0) {
                        ClearMainGameTargetState();
                        W8MonsterGroup* group = FindFirstMonsterByID(0x68);
                        if (group != 0)
                            SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
                        group = FindFirstMonsterByID(0x13e);
                        if (group != 0) {
                            SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
                            MonsterGroupEnterCombat(group);
                        }
                    } else if (SDL_strcasecmp(token.c_str(), "ENDSAVANTWALK") == 0) {
                        runtime_flags |= W8_MONSTER_PARKED;
                        Trigger* trigger = FindTriggerByName("Path3Trigger");
                        if (trigger != 0) trigger->Run(-1);
                    } else if (SDL_strcasecmp(token.c_str(), "UNLOCKUI") == 0 ||
                               SDL_strcasecmp(token.c_str(), "ENDGARIWALK") == 0) {
                        ClearMainGameTargetState();
                    } else if (SDL_strcasecmp(token.c_str(), "ENDHOGARWALK") == 0) {
                        ClearMainGameTargetState();
                        SetScript("ClosePatrol.msf", true);
                    } else if (SDL_strcasecmp(token.c_str(), "ENDHOGARWALKANDPUTTOSLEEP") == 0) {
                        W8TargetSource source;
                        ClearMainGameTargetState();
                        SetScript("ClosePatrol.msf", true);
                        monster_info = FindMonster(location_id);
                        ResetTargetSource(&source);
                        SetMonsterCondition(monster_info->location_id, W8_CONDITION_ASLEEP, 6, 0,
                                            &source, true);
                    } else if (SDL_strcasecmp(token.c_str(), "ENDBELAWALK") == 0) {
                        runtime_flags |= W8_MONSTER_PARKED;
                        ClearMainGameTargetState();
                    } else if (SDL_strcasecmp(token.c_str(), "BELA_END_CC_WALK") == 0) {
                        W8NpcState* npc = GetNpcStateByKind(0x8d);
                        if (npc != 0) QueueNpcScriptNotice(npc, 0, 6, false, 0);
                        monster_info = FindMonster(location_id);
                        if (monster_info->control_state != W8_MONSTER_CONTROL_LURED) {
                            glm::vec3 party = cameraLocation();
                            AimAtPosition(&party);
                        }
                    }
                }
                break;
            case MONSCR_PLAY: {
                int value = 0;
                float distance = 0.0f;
                tokens >> token;
                if (!tokens) break;
                value = atoi(token.c_str());
                if (value != 0) tokens >> token;
                if (!tokens) break;
                distance = static_cast<float>(atof(token.c_str())) * kWorldScale;
                if (distance != 0.0f) tokens >> token;
                if (!tokens) break;
                sound = std::make_unique<Sound3D>(token.c_str());
                if (sound) {
                    glm::vec3 position = GetPosition();
                    sound->volume = value;
                    sound->position = position;
                    if (distance != 0.0f) sound->falloff = distance;
                    if (sound->Play(false) == 0) {
                        sound.reset();
                    } else {
                        script_wait = MONSCR_PLAY;
                    }
                }
                break;
            }
            case MONSCR_FADEOUT:
                /* 0x004C88F1..0x004C8993 is BeginFadeOutAndRemove(NONE) inlined,
                   including the removal_state store at 0x004C8993. */
                BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
                stop = true;
                script_line = static_cast<int>(script->lines.size());
                break;
            default:
                break;
            }
        } else {
            switch (command) {
            case MONSCR_FACE:
                tokens >> token;
                if (tokens && SDL_strcasecmp(token.c_str(), "PARTY") == 0) {
                    face_party = true;
                } else if (tokens) {
                    int direction = -1;
                    for (int index = MONSCR_EAST; index <= MONSCR_SOUTHEAST; ++index) {
                        if (CompareTextIgnoreAsciiCase(token, g_monster_script_commands[index]) ==
                            0) {
                            direction = index;
                            break;
                        }
                    }
                    if (direction != -1) {
                        double angle = (direction - MONSCR_EAST) * g_monster_script_direction_step;
                        order_mode = W8_MONSTER_ORDER_FACE_DIRECTION;
                        direction_x = static_cast<float>(cos(angle) * 500.0);
                        direction_y = 0.0f;
                        direction_z = static_cast<float>(sin(angle) * 500.0);
                    } else {
                        ShutdownWithErrorBox(fmt::format(
                            "MonScript {} Line {:d}: Unknown direction {}", script->getName(),
                            script->GetSourceLine(script_line - 1), token));
                    }
                }
                break;
            case MONSCR_PATROL:
                /* 0x004C9463: both values are parsed first and stored together
                   (0x004C94B6/0x004C94BC) only when the second token exists. */
                tokens >> token;
                if (tokens) {
                    float distance = static_cast<float>(atof(token.c_str())) * kWorldScale;
                    tokens >> token;
                    if (tokens) {
                        patrol_variation = static_cast<float>(atof(token.c_str())) * kWorldScale;
                        patrol_distance = distance;
                        order_mode = W8_MONSTER_ORDER_PATROL;
                    }
                }
                break;
            case MONSCR_ENDORDERS:
                defining_orders = false;
                orders_finished = true;
                script_wait = MONSCR_ENDORDERS;
                break;
            case MONSCR_GUARD:
                /* 0x004C94CE: one location; the list is cleared and the point
                   added only after it resolves (0x004C9568), the mode is
                   written even if the Add fails (0x004C95D4/0x004C965E), and
                   orders_finished is left alone. No token: nothing changes. */
                tokens >> token;
                if (tokens) {
                    glm::vec3 position;
                    if (!resolveNamedPosition(position)) {
                        break;
                    }
                    vector.clear();
                    vector.push_back(position);
                    order_mode = W8_MONSTER_ORDER_GUARD;
                }
                break;
            case MONSCR_POINTPATROL:
            case MONSCR_RANDOMPOINTPATROL: {
                while (tokens >> token) {
                    glm::vec3 position;
                    if (!resolveNamedPosition(position)) {
                        continue;
                    }
                    if (patrol_points == 0) {
                        vector.clear();
                    }
                    ++patrol_points;
                    vector.push_back(position);
                }
                if (patrol_points > 0) {
                    order_mode = command == MONSCR_POINTPATROL
                                     ? W8_MONSTER_ORDER_POINT_PATROL
                                     : W8_MONSTER_ORDER_RANDOM_POINT_PATROL;
                }
                break;
            }
            case MONSCR_DEAF:
                deaf = true;
                break;
            case MONSCR_TURNTOFACEPARTY:
                face_party = true;
                break;
            case MONSCR_LOOKABOUT:
                tokens >> token;
                if (tokens) {
                    look_frequency = atoi(token.c_str());
                    monster_info->look_time = static_cast<unsigned char>(look_frequency);
                    tokens >> token;
                    if (tokens) {
                        look_duration = atoi(token.c_str());
                    } else {
                        ShutdownWithErrorBox(
                            fmt::format("MonScript {} Line {:d}: Missing lookabout duration",
                                        script->getName(), script->GetSourceLine(script_line - 1)));
                    }
                } else {
                    ShutdownWithErrorBox(
                        fmt::format("MonScript {} Line {:d}: Missing lookabout frequency",
                                    script->getName(), script->GetSourceLine(script_line - 1)));
                }
                break;
            case MONSCR_STAYHOME:
                stay_home = true;
                break;
            default:
                break;
            }
        }

        if (++command_count > 50) {
            stop = true;
        }
    }

    if (script != 0 && script_line >= static_cast<int>(script->lines.size()) &&
        script_wait == MONSCR_NONE) {
        script.reset();
        runtime_flags &= ~W8_MONSTER_SCRIPT_WAIT;
        script_line = 0;
        script_wait = MONSCR_NONE;
        sound.reset();
    }
}

/* A script command that requested blocking leaves its command number here.
   Each command family has one concrete completion condition; commands without
   a condition are immediately ready. */
bool W8Monster::CanContinueScript()
{
    switch (script_wait) {
    case MONSCR_WALKTO:
        if (!movement_stopped) {
            return false;
        }
        break;
    case MONSCR_FACE:
        if (static_cast<float>(fabs(movement.target_yaw - movement.yaw)) >=
            g_camera_transition_epsilon) {
            return false;
        }
        break;
    case MONSCR_SAY:
        if (ShouldDeferCharacterEventForNpcScript(false)) {
            return false;
        }
        break;
    case MONSCR_CYCLE:
        if (Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) == 0) {
            return false;
        }
        runtime_flags &= ~W8_MONSTER_SCRIPT_WAIT;
        return true;
    case MONSCR_NPCINTERACTION:
        if (gXStatus.fNpcDialogueMode) {
            return false;
        }
        break;
    case MONSCR_TRIGGER:
        if (trigger != 0 && (trigger->flags & W8_TRIGGER_RUNNING) != 0) {
            return false;
        }
        trigger = 0;
        return true;
    case MONSCR_DELAY:
        if (script_delay_timer.GetProgress() < 1.0f) {
            return false;
        }
        break;
    case MONSCR_PLAY:
        if (sound->IsPlaying()) {
            return false;
        }
        sound.reset();
        break;
    default:
        break;
    }
    return true;
}

bool W8Monster::SetScriptLabel(std::string_view label)
{
    int line;

    if (script != 0) {
        line = script->FindLabelLine(label);
        if (line >= 0) {
            script_line = line;
            return true;
        }
    }
    return false;
}

bool W8Monster::IsPendingFinalize() const
{
    return pending_finalize;
}

bool W8Monster::IsWithinWorldRange()
{
    if (sector_mesh != 0) {
        return sector_mesh->testFlag(srNode::FLAG_DISABLE) == 0;
    } else {
        double far_clip = WorldGetFarClip(GetWorld());
        glm::vec3 position = GetPosition();
        glm::vec3 reference = PartyNavigator()->GetPosition();
        glm::vec3 delta = reference - position;

        return glm::dot(delta, delta) <=
               static_cast<float>(far_clip) * static_cast<float>(far_clip);
    }
}

/* Exercise the inexpensive elevated-origin sight query from this Monster to
   the player, and answer with the trace's own result. */
bool W8Monster::CheckLineOfSightToPlayer()
{
    glm::vec3 monster_position;
    glm::vec3 player_position;

    monster_position = movement.position;
    monster_position.y += movement.height_offset;
    player_position = cameraLocation();
    return GetWorldOctree()->HasLineOfSight(&monster_position, &player_position, true);
}

/* The sight code keeps the engine trace's three outcomes as two independent
   flags. A clear trace sets both false, the special -1 result sets only the
   secondary flag, and every other obstructed result sets both. */
void W8Monster::GetPlayerSightFlags(bool* primary, bool* secondary)
{
    glm::vec3 monster_position;
    glm::vec3 player_position;
    short result;

    monster_position = movement.position;
    monster_position.y += movement.height_offset;
    player_position = cameraLocation();
    result = GetWorldOctree()->TraceLineOfSight(&monster_position, &player_position, true,
                                                location_id, -1, true, 0);
    if (result == -1) {
        *secondary = true;
        *primary = false;
    } else if (result != 1) {
        *secondary = true;
        *primary = true;
    } else {
        *secondary = false;
        *primary = false;
    }
}

/* The inexpensive visibility path tests the Monster's elevated origin. The
   detailed path tests the translated animation bounds at their centre and
   corners. */
bool W8Monster::IsVisibleToPlayer(bool use_bounds)
{
    glm::vec3 player_position = cameraLocation();
    if (use_bounds) {
        const auto bounds = this->GetWorldAnimationBounds();
        return bounds && hasLineOfSightToBounds(player_position, *bounds);
    }

    glm::vec3 monster_position = movement.position;
    monster_position.y += movement.height_offset;
    return GetWorldOctree()->HasLineOfSight(&player_position, &monster_position, true);
}

void W8Monster::GetPlayerToMonsterSightFlags(bool* primary, bool* secondary,
                                             const glm::vec3* source)
{
    glm::vec3 monster_position;
    glm::vec3 player_position;
    short result;

    monster_position = movement.position;
    monster_position.y += movement.height_offset;
    if (source == 0) {
        player_position = cameraLocation();
    } else {
        player_position = *source;
    }
    result = GetWorldOctree()->TraceLineOfSight(&player_position, &monster_position, true, -1,
                                                location_id, true, 0);
    if (result == -1) {
        *secondary = true;
        *primary = false;
    } else if (result != 1) {
        *secondary = true;
        *primary = true;
    } else {
        *secondary = false;
        *primary = false;
    }
}

bool W8Monster::HasLineOfSightToMonster(W8Monster* monster)
{
    glm::vec3 from;
    glm::vec3 to;

    from = movement.position;
    from.y += movement.height_offset;
    to = monster->movement.position;
    to.y += monster->movement.height_offset;
    return GetWorldOctree()->HasLineOfSight(&from, &to, true);
}

void W8Monster::GetMonsterSightFlags(W8Monster* monster, bool* primary, bool* secondary)
{
    glm::vec3 from;
    glm::vec3 to;
    short result;

    from = movement.position;
    from.y += movement.height_offset;
    to = monster->movement.position;
    to.y += monster->movement.height_offset;
    result = GetWorldOctree()->TraceLineOfSight(&from, &to, true, location_id, monster->location_id,
                                                true, 0);
    if (result == -1) {
        *secondary = true;
        *primary = false;
    } else if (result != 1) {
        *secondary = true;
        *primary = true;
    } else {
        *secondary = false;
        *primary = false;
    }
}

bool W8Monster::HasLineOfSightFromPoint(glm::vec3 point)
{
    glm::vec3 monster_position;

    monster_position = movement.position;
    monster_position.y += movement.height_offset;
    return GetWorldOctree()->TraceLineOfSight(&point, &monster_position, true, -3, -3, true, 0) !=
           1;
}

int W8Monster::IsFacingMonster(W8Monster* monster)
{
    float bearing;
    float facing;
    glm::vec3 from;
    glm::vec3 to;

    if (monster == 0) {
        Fail("pMonsterB");
    }
    to = monster->GetPosition();
    from = GetPosition();
    bearing = NormalizeAngle(GetHeadingAngle(&from, &to));
    facing = NormalizeAngle(GetYaw());
    return fabs(bearing - facing) <= g_monster_facing_tolerance;
}

int W8Monster::IsFacingPlayer()
{
    float bearing;
    float facing;
    glm::vec3 from;
    glm::vec3 to;

    if (PartyNavigator() == 0) {
        Fail("pPlayer");
    }
    to = PartyNavigator()->GetPosition();
    from = GetPosition();
    bearing = NormalizeAngle(GetHeadingAngle(&from, &to));
    facing = NormalizeAngle(GetYaw());
    return fabs(bearing - facing) <= g_monster_facing_tolerance;
}

/* Attach or remove the party slot's target-marker triangle on a monster's
   representation: on set it hangs the slot's colored Tri*.tga object on
   attachment index `party_slot`, on clear it detaches and deletes it. The
   rep's object count tracks the live markers. */
void SetMonsterPartySlotMarker(int party_slot, int location_id, char on)
{
    W8Monster* rep;
    W8MonsterInfo* info;
    WorldItemEntity* item;
    std::string path;

    info = FindMonster(location_id);
    rep = info->p3D;
    if (on == 0) {
        item = rep->objects[party_slot].get();
        if (item != 0) {
            item->DetachMesh();
            rep->objects[party_slot].reset();
            --rep->icon_count;
        }
    } else {
        if (rep->objects[party_slot] == 0) {
            path = fmt::format("Data\\Monsters\\Bitmaps\\{}",
                               g_party_target_marker_bitmaps[g_status.buffers.XChar[party_slot]
                                                                 .party_order_index]);
            rep->objects[party_slot] = CreateMonsterIconItem(*info->p3D->owner_world, path.c_str());
            ++rep->icon_count;
        }
    }
    info->p3D->UpdateAttachedObjects();
}

/* Start making the representation visible.  Reversing an active fade-out
   preserves its current scale by seeding the opposite timer at that progress;
   an idle monster starts from zero instead. */
void W8Monster::BeginFadeIn(float duration)
{
    if (fade_state <= W8_MONSTER_FADE_IDLE) {
        fade_timer.SetDuration(duration);
        fade_timer.Restart();
        if (fade_state < W8_MONSTER_FADE_IDLE) {
            W8Monster* rep = this;
            fade_timer.SetProgress(rep->instance_scale);
        } else {
            W8Monster* rep = this;
            rep->instance_scale = 0.0f;
            rep->apply_instance_scale = true;
        }
        fade_state = W8_MONSTER_FADE_IN;
        runtime_flags &= ~W8_MONSTER_FADED_OUT;
    }
}

void W8Monster::BeginDelayedRemoval()
{
    runtime_flags |= W8_MONSTER_REMOVE_AFTER_FADE;
    fade_state = W8_MONSTER_FADE_DELAYED_REMOVAL;
    fade_timer.SetDuration(5.0f);
    fade_timer.Restart();
}

/* Begin the three-second disappearance transition, remove the live monster
   from the manager without destroying it, and retain the requested terminal
   state for the transition's completion. */
void W8Monster::BeginFadeOutAndRemove(W8MonsterRemovalState state)
{
    runtime_flags |= W8_MONSTER_REMOVE_AFTER_FADE;
    if (fade_state >= W8_MONSTER_FADE_IDLE) {
        fade_timer.SetDuration(3.0f);
        fade_timer.Restart();
        W8Monster* rep = this;
        if (fade_state > W8_MONSTER_FADE_IDLE) {
            fade_timer.SetProgress(1.0f - rep->instance_scale);
        } else {
            rep->instance_scale = 1.0f;
            rep->apply_instance_scale = true;
        }
        fade_state = W8_MONSTER_FADE_OUT;
    }
    RemoveMonster(FindMonster(location_id), W8MonsterRemoval::Deactivate);
    removal_state = state;
}

void W8Monster::BeginFadeOut(float duration)
{
    if (fade_state < W8_MONSTER_FADE_IDLE) {
        return;
    }
    fade_timer.SetDuration(duration);
    fade_timer.Restart();
    if (fade_state > W8_MONSTER_FADE_IDLE) {
        W8Monster* rep = this;
        fade_timer.SetProgress(1.0f - rep->instance_scale);
        fade_state = W8_MONSTER_FADE_OUT;
        return;
    }
    W8Monster* rep = this;
    rep->instance_scale = 1.0f;
    rep->apply_instance_scale = true;
    fade_state = W8_MONSTER_FADE_OUT;
}

void W8Monster::StartTalking(bool animate_mouth)
{
    talking = true;
    this->animate_mouth = animate_mouth;
    mouth_frame_clock = w8_get_ticks();
    mouth_open = false;
    talk_start = w8_get_ticks();
    talk_duration = Random(2000) + 2000;
    pending_cycle = 0x18;
    pending_behaviour = W8_ANIMATION_PLAY_ONCE;
}

void W8Monster::StopTalking()
{
    srModelInstance* model;
    stTextureAnim* mouth;

    talking = false;
    model = GetCurrentModelInstance();
    if (model != 0) {
        mouth = static_cast<stModelInstance*>(model)->FindMouthTexture();
        if (mouth != 0) {
            mouth->animation_mode = W8_TEXTURE_ANIM_MANUAL;
            mouth->SetFrame(0);
        }
    }
    if (current_cycle != 0x15) {
        pending_behaviour = W8_ANIMATION_NEVER_STOP;
        pending_cycle = 1;
    }
}

void W8Monster::SetCycleCallback(int cycle, CycleCallback callback)
{
    cycle_callback = callback;
    callback_cycle = cycle;
}

bool W8Monster::GetPatrolPoint(glm::vec3* point)
{
    if (!orders_finished || patrol_index < 0) {
        return false;
    }
    if (vector.empty()) {
        if (formation == glm::vec3(0)) {
            glm::vec3 position = GetPosition();
            formation = position;
        }
        vector.push_back(formation);
    }

    if (order_mode == W8_MONSTER_ORDER_GUARD) {
        *point = vector.front();
    } else if (order_mode > W8_MONSTER_ORDER_PATROL &&
               order_mode < W8_MONSTER_ORDER_FACE_DIRECTION) {
        *point = vector[patrol_index];
    } else {
        return false;
    }

    return true;
}

std::optional<srBoundingBox> W8Monster::GetWorldAnimationBounds()
{
    auto bounds = GetAnimationBounds();
    if (bounds) {
        const auto position = GetPosition();
        bounds->minimum += position;
        bounds->maximum += position;
    }
    return bounds;
}

void W8Monster::TrackSound(wiz8::sound::Playback playback)
{
    if (sounds.size() > 6) {
        for (const auto& sound : sounds) wiz8::sound::stop(sound);
        sounds.clear();
    }
    sounds.push_back(std::move(playback));
    std::erase_if(sounds, [](const auto& sound) { return !wiz8::sound::is_playing(sound); });
}

/* Mark the closest live member of each loaded group inside the selection
   range. Members that do not improve the current candidate are unmarked. */
void UpdateNearestMonsterGroupMembers()
{
    glm::vec3 player_position;
    unsigned int group_index;

    player_position = cameraLocation();
    for (group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[group_index].get();

        if (group != 0) {
            double nearest_distance = 1e11;
            W8MonsterInfo* nearest = 0;
            unsigned int member_index;

            for (member_index = 0; member_index < group->MemberIds().size(); ++member_index) {
                W8MonsterInfo* member = FindMonster(group->MemberLocationId(member_index));

                if (member != 0 && member->p3D != 0) {
                    glm::vec3 position = member->p3D->GetPosition();
                    float distance = glm::length((position - player_position));

                    if (distance < g_monster_group_nearest_range && distance < nearest_distance) {
                        nearest_distance = distance;
                        nearest = member;
                    } else {
                        member->p3D->nearest_to_party = false;
                    }
                }
            }
            if (nearest != 0) {
                nearest->p3D->nearest_to_party = true;
            }
        }
    }
}

float W8Monster::GetDistanceToPlayer()
{
    glm::vec3 position = GetPosition();
    return GetPointDistanceToPlayer(position);
}

float W8Monster::GetPointDistanceToPlayer(glm::vec3 point)
{
    glm::vec3 player_position;
    float distance;

    player_position = cameraLocation();
    player_position.y -= g_default_world_height;
    distance = glm::length((point - player_position)) - movement.alternate_radius -
               PartyNavigator()->movement.alternate_radius;
    if (distance < 0.0f) {
        distance = 0.0f;
    }
    return distance;
}

float W8Monster::GetDistanceToMonster(W8Monster* monster)
{
    glm::vec3 position = GetPosition();
    glm::vec3 other_position = monster->GetPosition();
    float distance = glm::length((position - other_position)) - movement.alternate_radius -
                     monster->movement.alternate_radius;

    if (distance < 0.0f) {
        distance = 0.0f;
    }
    return distance;
}

float W8Monster::GetPointDistanceToMonster(W8Monster* monster, glm::vec3 point)
{
    glm::vec3 position = monster->GetPosition();
    float distance = glm::length((point - position)) - movement.alternate_radius -
                     monster->movement.alternate_radius;

    if (distance < 0.0f) {
        distance = 0.0f;
    }
    return distance;
}

/* Decide whether a requested cycle can replace the current one. Monster adds
   combat, motionless, and death rules to W8GrCycle's primary-table operation;
   W8Navigator's distinct slot 3 remains inherited in the secondary table. */
unsigned char W8Monster::CanEnterCycle(signed char cycle)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);

    if (gXStatus.fCombatMode && PartyCamera().m_transition_active) {
        return 0;
    }
    if (animation_playing == 0) {
        if (cycle != W8_MONSTER_CYCLE_GET_HIT && cycle != W8_MONSTER_CYCLE_DIE &&
            cycle != W8_MONSTER_CYCLE_BIRTH && monster_info->fMotionless) {
            if (!g_dev_mode) {
                return 0;
            }
            Fail("FALSE");
            return 0;
        }
    } else {
        if (!IsCycleInterruptable(static_cast<signed char>(Query(W8_MONSTER_QUERY_CYCLE))) &&
            Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) == 0) {
            return 0;
        }
        if (cycle == W8_MONSTER_CYCLE_DIE && monster_info->monster_species == 0x199 &&
            Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) == 0) {
            return 0;
        }
    }
    return 1;
}

/* Report whether a cycle may be interrupted. The original diagnostic names
   this operation `Monster::CycleInterruptable`; its spelling is retained here
   because it is the only source-level name available. */
bool W8Monster::IsCycleInterruptable(signed char cycle)
{
    const char* pending_name;
    const char* current_name;
    const char* requested_name;

    if (animation_playing == 0) {
        signed char current = static_cast<signed char>(Query(W8_MONSTER_QUERY_CYCLE));
        if (pending_cycle != -1 && CanEnterCycle(pending_cycle) != 0) {
            pending_name = g_cycle_names[pending_cycle].name;
            if (current != -1) {
                current_name = g_cycle_names[current].name;
            } else {
                current_name = "";
            }
            if (cycle != W8_MONSTER_CYCLE_NONE) {
                requested_name = g_cycle_names[cycle].name;
            } else {
                requested_name = "";
            }
            DebugMessage(
                1, fmt::format("Monster::CycleInterruptable (ID {:d}) - WARNING: Monster is not "
                               "animating - Cycle {:d}({}), current {:d}({}), pending {:d}({})",
                               location_id, static_cast<int>(cycle), requested_name,
                               static_cast<int>(current), current_name,
                               static_cast<int>(pending_cycle), pending_name));
        }
        return true;
    }

    switch (cycle) {
    case W8_MONSTER_CYCLE_NONE:
    case W8_MONSTER_CYCLE_IDLE:
    case W8_MONSTER_CYCLE_SPICE:
    case W8_MONSTER_CYCLE_TRANSITION:
    case W8_MONSTER_CYCLE_WALK:
    case W8_MONSTER_CYCLE_TURN:
        return true;
    }
    return false;
}

/* Apply the two script-specific side effects selected before an ungrouped
   monster is removed: state two returns it to Balbrak's home marker, while
   state three clears the ScregActive trigger variable. */
void W8Monster::ApplyRemovalStateEffects()
{
    glm::vec3 position;

    switch (removal_state) {
    case W8_MONSTER_REMOVAL_NONE:
        break;
    case W8_MONSTER_REMOVAL_RETURN_BALBRAK_HOME:
        if (FindEntityByName("NP_Balbrakhome", &position, 0, 0)) {
            SetPosition(&position);
        }
        break;
    case W8_MONSTER_REMOVAL_CLEAR_SCREG_ACTIVE:
        SetTriggerVariableByName("ScregActive", 0);
        break;
    }
}

unsigned char W8Monster::GetNumSubsPerCycle(signed char bCycle)
{
    if (bCycle >= W8_MONSTER_CYCLE_COUNT) {
        Fail("bCycle < CYCLE_NUM_UNIQUE", "GetNumSubsPerCycle() -> Invalid cycle num.");
    }
    if (bCycle == -1) {
        bCycle = current_cycle;
    }
    return static_cast<unsigned char>(animations[bCycle].size());
}

/* Select the active AnimObj for a cycle and dispatch the requested LOD/frame.
   This is the concrete implementation behind AnimRep's third vtable slot. */
srModelInstance* W8Monster::SetCycleFrameLod(signed char cycle, signed char frame, signed char lod)
{
    W8AnimObj* animation = animations[cycle][current_subcycle].get();
    if (animation->path_lists == 0) {
        return AnimObjDispatch(animation, lod, frame);
    }
    return AnimObjDispatchList(animation, lod, 0);
}

/* The selected subcycle's AniMesh for one animation cycle. */
W8AniMesh* W8Monster::GetEmitterAniMesh(signed char cycle)
{
    W8AnimObj* animation = animations[cycle][current_subcycle].get();

    if (animation == 0) {
        return 0;
    }
    return AnimObjEntry(animation, m_bLOD, 0);
}

/* Synchronize the live world representation with the Navigator state, update
   transient mouth/scale effects, and attach or hide the cycle's light graph.
   This is Monster's primary slot four; the world is the ordinary stack
   argument also consumed by the inherited GrCycle implementation. */
void W8Monster::UpdateRepresentation(W8World* world)
{
    glm::vec3 position;
    glm::mat3 rotation;
    srModelInstance* model;
    stTextureAnim* mouth;
    std::vector<std::unique_ptr<stLight>>* lights;
    int index;
    int count;

    position = movement.position;
    position.y += movement.vertical_offset;
    SetLocation(&position);

    rotation = glm::mat3(1);
    {
        float angle = NormalizeAngle(GetYaw() + g_monster_rotation_offset);
        if (angle != 0.0f) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        }
    }
    {
        float angle = GetPitch();
        if (angle != 0.0f) {
            {
                if (static_cast<double>(angle) != 0.0) {
                    rotation = rotation * glm::mat3(glm::mat3_cast(glm::angleAxis(
                                              static_cast<double>(angle), glm::dvec3(1, 0, 0))));
                }
            }
        }
    }
    if (movement.roll != 0.0f) {
        rotation *= glm::mat3_cast(
            glm::angleAxis(float(static_cast<double>(movement.roll)), glm::vec3(0, 0, 1)));
    }
    SetRotation(&rotation);

    if ((runtime_flags & W8_MONSTER_SCALING_Y) != 0) {
        model = GetCurrentModelInstance();
        const auto source_scale = model->getScale();
        float scale_y = static_cast<float>(source_scale.y) * this->scale_y;
        float scale_z = static_cast<float>(source_scale.z);
        glm::dvec3 scale;
        scale.x = source_scale.x;
        scale.y = static_cast<double>(scale_y);
        scale.z = static_cast<double>(scale_z);
        model->setScale(scale);
        this->scale_y -= 0.1f;
    }

    if (talking && animate_mouth) {
        if (mouth_open) {
            model = GetCurrentModelInstance();
            if (model != 0 &&
                (mouth = static_cast<stModelInstance*>(model)->FindMouthTexture()) != 0) {
                mouth->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                mouth->SetFrame(0);
            }
        } else if (w8_get_ticks() - mouth_frame_clock > 120) {
            unsigned short frame;
            mouth_frame_clock = w8_get_ticks();
            do {
                frame = static_cast<unsigned short>(Random(6));
                if (frame > 3) {
                    frame = 0;
                }
            } while (frame == static_cast<unsigned short>(mouth_frame));
            mouth_frame = frame;
            model = GetCurrentModelInstance();
            if (model != 0 &&
                (mouth = static_cast<stModelInstance*>(model)->FindMouthTexture()) != 0) {
                mouth->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                mouth->SetFrame(frame);
            }
        }
    }

    if (position_dirty || movement.position_adjusted) {
        world->octree->UpdateMonsterLocation(static_cast<unsigned short>(location_id), &position);
    }

    /* Only a monster whose current world sector mesh is enabled (or that has
       none yet) is re-attached; this is the octree sector cache at 0x308, not
       the navigator's scene node. */
    if ((sector_mesh == 0 || sector_mesh->testFlag(srNode::FLAG_DISABLE) == 0) &&
        IsRenderable(false)) {
        W8GrCycle::UpdateRepresentation(world);
        model = GetCurrentModelInstance();
        if (model != 0) {
            static_cast<stModelInstance*>(model)->frame_interpolation =
                g_settings.smooth_monster_animations != 0 ? frame_fraction : 0.0f;
            SetModelInstanceChainExclusionMask(model, 4);
        }
        if (monster_light != 0) {
            monster_light->Update(&position);
        }
        if (!enabled) {
            enabled = true;
            SetShakeEventVisibility(current_cycle);
            lights = &light_lists[current_cycle][current_subcycle].lights;
            if (lights != 0 && (count = static_cast<int>(lights->size())) != 0) {
                for (index = 0; index < count; ++index) {
                    ((*lights)[index])->clearFlag(srNode::FLAG_DISABLE);
                }
            }
            if (monster_light != 0) {
                monster_light->SetVisible(true);
            }
        }
    } else if (enabled) {
        enabled = false;
        SetShakeEventVisibility(current_cycle);
        lights = &light_lists[current_cycle][current_subcycle].lights;
        if (lights != 0 && (count = static_cast<int>(lights->size())) != 0) {
            for (index = 0; index < count; ++index) {
                ((*lights)[index])->setFlag(srNode::FLAG_DISABLE);
            }
        }
        if (monster_light != 0) {
            monster_light->SetVisible(false);
        }
    }
}

/* Enable only the shake particles belonging to the requested cycle and the
   currently selected subcycle. A ranged particle is left to the frame-driven
   update path; this method only toggles particles without a distinct range. */
void W8Monster::SetShakeEventVisibility(signed char cycle)
{
    int index;
    int count;

    if ((count = static_cast<int>(particles.size())) == 0) {
        return;
    }

    for (index = 0; index < count; ++index) {
        W8GrCycleParticleAttachment* event = &particles[index];

        if (event->cycle == cycle && event->subcycle == current_subcycle && enabled) {
            W8AnimObj* animation = animations[current_cycle][current_subcycle].get();

            if (animation == 0 || animation->start_frame == 0 ||
                animation->end_frame < animation->start_frame) {
                stParticle* particle = event->m_pstParticles.get();
                if (particle->start_frame != -1 && particle->end_frame != -1 &&
                    particle->start_frame != particle->end_frame) {
                    continue;
                }
                particle->SetActive(true);
            }
        } else {
            event->m_pstParticles->SetActive(false);
        }
    }
}

/* The Monster vtable's slot-three method selects the active subcycle's
   AnimObj (falling back to entry zero) and submits the Monster's current
   animation index.  The assertion's `pao` spelling establishes the pointee's
   AnimObj identity without supplying a name for this Monster method. */
unsigned int W8Monster::ApplyEmitterSetting(signed char cycle)
{
    W8AnimObj* animation = animations[cycle][current_subcycle].get();
    if (animation == 0) {
        Fail("pao");
    }
    return AnimObjValue(animation, m_bLOD);
}

signed char W8Monster::GetNumSubCycles()
{
    W8Monster* representation = this;
    W8AnimObj* animation =
        representation->animations[representation->current_cycle][representation->current_subcycle]
            .get();
    return static_cast<signed char>(AnimObjValue(animation, representation->m_bLOD));
}

/* W8Monster stores its animation object immediately after the shared
   0x1d8-byte GrCycle base. */
bool W8Monster::IsCycleSupported(signed char cycle)
{
    if (cycle >= W8_MONSTER_CYCLE_COUNT) {
        Fail("bCycle < CYCLE_NUM_UNIQUE", "IsCycleSupported() -> Invalid cycle num.");
    }
    return static_cast<int>(animations[cycle].size()) != 0;
}

/* Select a concrete animation subcycle and rebuild the renderer-facing light
   and model state for it.  The one stack argument and RET 4 establish the
   primary-vtable slot as an ordinary signed-byte cycle setter. */
void W8Monster::SetCycle(signed char cycle)
{
    std::vector<std::unique_ptr<stLight>>* lights;
    W8AnimObj* animation;
    signed char subcycle;
    int count;
    int index;

    if (cycle < W8_MONSTER_CYCLE_BIRTH || cycle >= W8_MONSTER_CYCLE_COUNT) {
        Fail("bCycle >= CYCLE_FIRST && bCycle <= CYCLE_LAST");
    }

    auto& cycle_animations = animations[cycle];
    count = static_cast<int>(cycle_animations.size());
    if (count == 0) {
        if (static_cast<int>(animations[1].size()) < 1) {
            W8MonsterInfo* monster_info = owner_world->population->findMonster(location_id);
            Fail("FALSE", fmt::format("ERROR: Monster {} has no IDLE cycle!",
                                      GetMonsterName(monster_info, 0, 0)));
            return;
        }

        W8MonsterInfo* monster_info = owner_world->population->findMonster(location_id);
        DebugMessage(0, fmt::format("WARNING: Monster {} is missing anim cycle {}",
                                    GetMonsterName(monster_info, 0, 0), g_cycle_names[cycle].name));
        CopyCycle(cycle, this, 1);
        count = static_cast<int>(cycle_animations.size());
        if (count == 0) {
            return;
        }
    }

    if (count == 1) {
        subcycle = 0;
    } else if (count < 2) {
        Fail("0");
        subcycle = 0;
    } else if (forced_subcycle == -1 || count <= forced_subcycle) {
        if ((runtime_flags & W8_MONSTER_KEEP_SUBCYCLE) == 0) {
            subcycle = static_cast<signed char>(w8_get_ticks() % count);
        } else {
            runtime_flags &= ~W8_MONSTER_KEEP_SUBCYCLE;
            subcycle = current_subcycle;
        }
    } else {
        runtime_flags &= ~W8_MONSTER_KEEP_SUBCYCLE;
        subcycle = forced_subcycle;
        forced_subcycle = -1;
    }

    if (cycle_callback != 0 && callback_cycle == current_cycle) {
        cycle_callback(this);
        cycle_callback = 0;
    }

    if (current_cycle != 0) {
        lights = &light_lists[current_cycle][current_subcycle].lights;
        if (lights != 0) {
            count = static_cast<int>(lights->size());
            for (index = 0; index < count; ++index) {
                stLight* light = (*lights)[index].get();

                light->setParent(0, 1);
            }
        }
    }

    current_cycle = cycle;
    current_subcycle = subcycle;
    animation = cycle_animations[subcycle].get();
    if (animation == 0) {
        Fail("pao");
    }
    frame_method = animation->frame_method;

    {
        glm::dvec3 camera_location = owner_world->camera->getLocation();
        glm::vec3 listener;
        listener = glm::vec3(camera_location);
        SelectLOD(&listener);
    }

    GetAnimationRadius(&animation_radius);
    if ((runtime_flags & W8_MONSTER_KEEP_FRAME_DIRECTION) != 0) {
        runtime_flags &= ~W8_MONSTER_KEEP_FRAME_DIRECTION;
    } else {
        frame_direction = W8_ANIMATION_FORWARD;
    }

    lights = &light_lists[cycle][subcycle].lights;
    SetLights(lights);
    if (lights != 0) {
        count = static_cast<int>(lights->size());
        for (index = 0; index < count; ++index) {
            stLight* light = (*lights)[index].get();

            light->setParent(owner_world->dynamic_scene, 1);
            light->Reset();
        }
    }

    runtime_flags &= ~W8_MONSTER_TEXTURE_CHECKED;
    first_frame = 0;
    last_frame = GetNumSubCycles() - 1;
    SetShakeEventVisibility(cycle);

    if (lights != 0) {
        count = static_cast<int>(lights->size());
        for (index = 0; index < count; ++index) {
            if (enabled) {
                ((*lights)[index])->clearFlag(srNode::FLAG_DISABLE);
            } else {
                ((*lights)[index])->setFlag(srNode::FLAG_DISABLE);
            }
        }
    }
    if (monster_light != 0) {
        monster_light->SetVisible(enabled);
    }

    RefreshModelLightScale();

    if (cycle == W8_MONSTER_CYCLE_DIE) {
        glm::vec4 empty;
        empty = glm::vec4(0);
        highlight_colour = empty;
        // Gib behavior belongs to the source asset, not a generated renderer label.
        const auto* death_mesh = GetEmitterAniMesh(current_cycle);
        if (death_mesh && wiz8::path_to_utf8(death_mesh->source_file.filename()).contains("gib")) {
            SetAngles(
                static_cast<float>(g_monster_death_rotation_pi * 0.0055555557f * Random(0x168)));
        }
        if (monster_light != 0) {
            monster_light->StartFadeOut();
        }
    }
}

signed char W8Monster::GetTotalAnimationCount()
{
    signed char total = 0;
    int cycle;

    for (cycle = W8_MONSTER_CYCLE_BIRTH; cycle < W8_MONSTER_CYCLE_COUNT; ++cycle) {
        total += static_cast<signed char>(animations[cycle].size());
    }
    return total;
}

float W8Monster::GetCurrentAnimationScale()
{
    W8Monster* representation = this;

    return representation
        ->animation_scales[representation->current_cycle][representation->current_subcycle];
}

std::optional<srBoundingBox> W8Monster::GetAnimationBounds()
{
    auto bounds = W8GrCycle::GetAnimationBounds();
    if (bounds) {
        bounds->minimum *= scale;
        bounds->maximum *= scale;
    }
    return bounds;
}

unsigned char W8Monster::GetAnimationRadius(float* arg_radius)
{
    unsigned char result = W8GrCycle::GetAnimationRadius(arg_radius);

    *arg_radius *= scale;
    return result;
}

static const float g_monster_bounds_vertical_factor = 0.66f;

std::optional<glm::vec3> W8Monster::GetAnimationCenter()
{
    return GetAnimationBounds().transform([this](const auto& bounds) {
        auto center = GetPosition();
        center.y += (bounds.maximum.y - bounds.minimum.y) * g_monster_bounds_vertical_factor;
        return center;
    });
}

/* Keep equipped items, spell icons, and temporary poster model instances in
   the camera-facing attachment layout selected by the representation. */
void W8Monster::UpdateAttachedObjects(bool hovered)
{
    W8Monster* representation = this;
    int attachment_layout = representation->icon_count;
    unsigned int linked_count = representation->spell_icons.size();
    int poster_count = static_cast<int>(representation->linked_runtime_objects.size());
    glm::mat3 camera_rotation;
    glm::vec3 base_position;
    glm::vec3 party_position;
    float distance_scale;
    unsigned int elapsed;
    int index;

    if (attachment_layout == 0 && linked_count == 0 && poster_count == 0) {
        return;
    }

    elapsed = GameClockMilliseconds() - representation->timer;
    camera_rotation = cameraRotation(*GetWorld());
    base_position = movement.position;
    base_position.y += movement.vertical_base + movement.vertical_amplitude;

    party_position = cameraLocation();
    party_position -= base_position;
    distance_scale = glm::length(party_position) * g_monster_attachment_distance_scale;
    if (!hovered && distance_scale > 1.0f) {
        distance_scale = 1.0f;
    }

    if (attachment_layout != 0) {
        /* 0x004C40C4 indexes the layout row with a count of the markers
           already placed (0x28(%esp), bumped at 0x004C4243 only for a present
           slot), so the live markers pack into the first row entries
           whatever party slots they belong to. */
        int placed = 0;

        for (index = 0; index < 8; ++index) {
            WorldItemEntity* item = representation->objects[index].get();

            if (item != 0) {
                const W8AttachmentOffset& raw =
                    g_monster_attachment_offsets[attachment_layout - 1][placed];
                glm::vec3 source(raw.x, raw.y, raw.z);
                glm::vec3 offset;
                glm::vec3 location;
                srNode* mesh;
                float mesh_scale;
                glm::dvec3 widened_scale;

                offset = source * distance_scale;
                glm::vec3 rotated = camera_rotation * offset;
                location = base_position + rotated;
                location.y += representation->standing_height +
                              distance_scale * g_monster_attachment_vertical_scale;

                item->SetLocation(&location);
                mesh = item->GetMesh();
                mesh_scale = distance_scale * g_monster_attachment_scales[attachment_layout - 1];
                widened_scale = glm::dvec3(mesh_scale);
                mesh->setScale(widened_scale);
                if ((runtime_flags & W8_MONSTER_FADED_OUT) == 0) {
                    mesh->clearFlag(srNode::FLAG_DISABLE);
                } else {
                    mesh->setFlag(srNode::FLAG_DISABLE);
                }
                item->ApplyRepTransform();
                ++placed;
            }
        }
    }

    index = 0;
    {
        int group = 0;

        while (index < static_cast<int>(linked_count)) {
            int chunk_count = static_cast<int>(linked_count) - index;
            int chunk_index;
            float group_height;

            if (chunk_count > 4) {
                chunk_count = 4;
            }
            group_height = static_cast<float>(group * g_monster_attachment_group_spacing);

            for (chunk_index = 0; chunk_index < chunk_count; ++chunk_index, ++index) {
                const auto* entry = &representation->spell_icons[index];
                WorldItemEntity* item = entry->item.get();
                const W8AttachmentOffset& raw =
                    g_monster_attachment_offsets[chunk_count - 1][chunk_index];
                glm::vec3 source(raw.x, raw.y, raw.z);
                glm::vec3 offset;
                glm::vec3 location;
                srNode* mesh;
                float mesh_scale;
                glm::dvec3 widened;

                offset = source * distance_scale;
                offset.y += group_height * distance_scale;
                glm::vec3 rotated = camera_rotation * offset;
                location = base_position + rotated;
                location.y += representation->standing_height +
                              distance_scale * g_monster_linked_vertical_scale;

                item->SetLocation(&location);
                mesh = item->GetMesh();
                mesh_scale = distance_scale * g_monster_attachment_scales[chunk_count - 1];
                widened = glm::dvec3(mesh_scale);
                mesh->setScale(widened);
                widened = glm::dvec3(location);
                mesh->setLocation(widened);
                if ((runtime_flags & W8_MONSTER_FADED_OUT) == 0) {
                    mesh->clearFlag(srNode::FLAG_DISABLE);
                } else {
                    mesh->setFlag(srNode::FLAG_DISABLE);
                }
            }
            ++group;
        }
    }

    if (poster_count != 0) {
        glm::vec3 mapped_position;
        float vertical_offset = elapsed * g_monster_poster_vertical_rate;
        int poster_index = 0;

        GetMappedPosition(&mapped_position);
        while (poster_index < poster_count) {
            stModelInstance* poster = representation->linked_runtime_objects[poster_index].get();
            glm::dvec3 location = poster->getLocation();
            glm::vec3 poster_position(static_cast<float>(location.x),
                                      static_cast<float>(location.y) + vertical_offset,
                                      static_cast<float>(location.z));

            if (glm::length((poster_position - mapped_position)) <=
                static_cast<float>(g_monster_poster_max_distance)) {
                location = glm::dvec3(poster_position);
                poster->setLocation(location);
                ++poster_index;
            } else {
                representation->linked_runtime_objects.erase(
                    representation->linked_runtime_objects.begin() + poster_index);
                --poster_count;
            }
        }
    }
}

/* Query the current animation state. The selector is an internal ten-entry
   interface used by MonsterManager and the animation driver; selector eight is
   intentionally unsupported and returns -1 with out-of-range selectors. */
int W8Monster::Query(W8MonsterQueryKind query)
{
    int result = -1;
    unsigned int animation_value;

    switch (query) {
    case W8_MONSTER_QUERY_FRAME_COUNT:
        result = ApplyEmitterSetting(current_cycle);
        break;
    case W8_MONSTER_QUERY_ANIMATION_COUNT:
        result = GetTotalAnimationCount();
        break;
    case W8_MONSTER_QUERY_AT_PLAYBACK_END:
        if (frame_direction != W8_ANIMATION_FORWARD &&
            frame_direction != W8_ANIMATION_FORWARD_COMPLETE) {
            result = subcycle == 0;
            break;
        }
        animation_value = ApplyEmitterSetting(current_cycle);
        result = subcycle == animation_value - 1;
        break;
    case W8_MONSTER_QUERY_AT_PLAYBACK_START:
        if (frame_direction == W8_ANIMATION_FORWARD ||
            frame_direction == W8_ANIMATION_FORWARD_COMPLETE) {
            result = subcycle == 0;
            break;
        }
        animation_value = ApplyEmitterSetting(current_cycle);
        result = subcycle == animation_value - 1;
        break;
    case W8_MONSTER_QUERY_FRAME:
        result = subcycle;
        break;
    case W8_MONSTER_QUERY_HAS_FRAMES:
        result = ApplyEmitterSetting(current_cycle) != static_cast<unsigned int>(-1);
        break;
    case W8_MONSTER_QUERY_CYCLE:
        result = current_cycle;
        break;
    case W8_MONSTER_QUERY_SUBCYCLE:
        result = current_subcycle;
        break;
    case W8_MONSTER_QUERY_CYCLE_COMPLETE:
        result = 0;
        if (animation_behaviour == W8_ANIMATION_NEVER_STOP) {
            if (animation_playing == 0) {
                result = 1;
            }
            break;
        }

        animation_value = ApplyEmitterSetting(current_cycle);
        if ((frame_direction == W8_ANIMATION_FORWARD && subcycle == animation_value - 1) ||
            (frame_direction == W8_ANIMATION_REVERSE && subcycle == 0) ||
            frame_direction == W8_ANIMATION_REVERSE_COMPLETE ||
            frame_direction == W8_ANIMATION_FORWARD_COMPLETE) {
            result = 1;
        }
        break;
    default:
        break;
    }
    return result;
}

void W8Monster::AdvanceAnimationFrame(int value, int)
{
    unsigned char previous_frame = subcycle;

    W8GrCycle::AdvanceAnimationFrame(value, 0);
    if ((current_cycle == 7 || current_cycle == 13 || current_cycle == 17) &&
        ((missile_frame > 0 && previous_frame < missile_frame && missile_frame <= subcycle) ||
         (missile_frame == 0 && subcycle == 1))) {
        HandleAnimationThreshold();
    }
    HandleAnimationFrame(previous_frame);
    if (!particles.empty()) {
        UpdateShakeEvents(previous_frame);
    }
}

static int g_spell_effect_frame = 1;
static int g_spell_index;

/* Cycle 25 launches either the queued spell visual or the monster's pending
   spell action when its animation crosses the configured frame. The cast's
   return is the stamina charge passed directly to FatigueMonster, establishing
   MonsterCastsSpell's non-void return. */
void W8Monster::HandleAnimationFrame(unsigned char previous_frame)
{
    W8MonsterInfo* monster_info;
    W8MonsterActionKind action_kind;
    int action_detail;
    unsigned int power_level;
    unsigned int fatigue;

    if (current_cycle == 25 &&
        ((spell_frame > 0 && previous_frame < spell_frame && spell_frame <= subcycle) ||
         (spell_frame == 0 && subcycle == 1))) {
        if (spell_effect_armed) {
            spell_effect_armed = false;
            CreateAttachedSpellEffect(g_spell_records[g_spell_index].resource_name,
                                      g_spell_effect_frame, this, 0, 0);
            return;
        }

        monster_info = FindMonster(location_id);
        action_kind = monster_info->action_kind;
        action_detail = monster_info->action_detail;
        power_level = monster_info->spell_power_level;
        if (action_kind == W8_MONSTER_ACTION_SPELL && action_detail != 0 && power_level != 0) {
            fatigue = MonsterCastsSpell(monster_info, action_detail, power_level);
            FatigueMonster(monster_info, fatigue, 0);
            monster_info->fSpellReleased = true;
        }
    }
}

/* Launch the missile at the frame shared by attack cycles 7, 13 and 17. In
   combat an already-selected attack is reused; otherwise the monster picks a
   live character and the first database attack that permits a missile mode. */
void W8Monster::HandleAnimationThreshold()
{
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;
    W8TargetSource source;
    W8SpellEffectDefinition attack_block;
    W8MonsterAttack* attack;
    unsigned int attack_index;
    W8RangeCategory range_category;
    int missile_type;
    int accuracy;
    bool selected_attack;
    unsigned char monster_value;

    selected_attack = false;
    monster_info = FindMonster(location_id);
    record = GetMonsterDataForInfo(monster_info);
    SetTargetSourceToMonster(monster_info, &source);

    if (gXStatus.fCombatMode && g_combat_state->eCombatActionStatus == 2 &&
        g_combat_state->pActionMonsterInfo == monster_info) {
        attack_index = monster_info->pCombat->attack_index;
        selected_attack = true;
        goto prepare_attack;
    }

    monster_info->Target.iType = W8_TARGET_KIND_CHARACTER;
    monster_info->Target.iChar = GetRandomCharacter(1, 1, -1, -1);
    monster_info->Target.iMonsterID = -1;
    if (monster_info->Target.iChar == -1) {
        return;
    }

    for (attack_index = 0; attack_index < W8_MAX_MONSTER_ATTACKS; ++attack_index) {
        attack = &record->attacks[attack_index];
        if (attack->fHasAttack != 0 && (attack->attack_modes & 0x110) != 0) {
            monster_info->action_detail = ChooseAttackMode(attack->attack_modes);
            goto prepare_attack;
        }
    }

    missile_type = 0;
    range_category = W8_RANGE_EXTREME;
    ClearAttackBlock(&attack_block);
    accuracy = 50;
    goto fire_missile;

prepare_attack:
    if (attack_index >= W8_MAX_MONSTER_ATTACKS) {
        Fail("uiAttack < MAX_MONSTER_ATTACKS");
    }
    attack = &record->attacks[attack_index];
    missile_type = attack->missile_type;
    if (static_cast<unsigned int>(missile_type) >= g_missile_table.size()) {
        DebugMessage(0, fmt::format("WARNING: {} has invalid missile type {:d} for attack {:d}",
                                    record->name0, missile_type, attack_index));
        missile_type = 0;
    }

    range_category = static_cast<W8RangeCategory>(attack->range_category);
    ClearAttackBlock(&attack_block);
    attack_block.magnitude = attack->damage_dice;
    memcpy(attack_block.condition_chances, attack->missile_values, 0x10);
    monster_value = record->effective_level;
    attack_block.power_level = monster_value + (monster_value < 15 ? monster_value : 15);
    attack_block.magnitude_base = attack->missile_magnitude;

    if (selected_attack) {
        accuracy = GetMonsterAttackScore(monster_info, attack,
                                         static_cast<W8AttackMode>(monster_info->action_detail), 0);
        CombatLog(fmt::format("TO HIT (MISSILE ACCURACY): Chance {:d}", accuracy));
    } else {
        accuracy = 50;
    }

fire_missile:
    monster_info->fMissileReleased = true;
    FireMissileSourceToTarget(missile_type, &source, &monster_info->Target, &attack_block,
                              !selected_attack, range_category, accuracy);
}

void W8MonsterShakeCallback::RestoreAnimation()
{
    W8Monster* representation;

    if (m_pMonster == 0) {
        Fail("m_pMonster");
    }
    if (m_pParticles == 0) {
        Fail("m_pParticles");
    }

    m_pParticles->SetActive(false);
    representation = m_pMonster;
    if (saved_behaviour < 1 || saved_behaviour > 3) {
        Fail("bBehaviour >= BEHAVIOUR_FIRST && bBehaviour <= BEHAVIOUR_LAST");
    }
    representation->pending_behaviour = saved_behaviour;
    representation->SetFrameMethod(saved_frame_method);
    representation->frame_direction = W8_ANIMATION_FORWARD;
    representation->first_frame = 0;
    representation->last_frame = m_pMonster->GetNumSubCycles() - 1;
}

/* Drive the particles attached to the active cycle/subcycle. A particle with
   no distinct frame range fires when the animation crosses its own start
   frame; a ranged particle is switched on and off at its explicit bounds. */
void W8Monster::UpdateShakeEvents(unsigned char previous_frame)
{
    W8AnimObj* animation;
    W8GrCycleParticleAttachment* event;
    stParticle* particle;
    int count;
    int index;
    bool animation_has_range;

    count = static_cast<int>(particles.size());
    animation = animations[current_cycle][current_subcycle].get();
    animation_has_range = animation != 0 && animation->start_frame != 0 &&
                          animation->end_frame >= animation->start_frame;

    for (index = 0; index < count; ++index) {
        event = &particles[index];
        if (event->cycle != current_cycle || event->subcycle != current_subcycle) {
            continue;
        }

        particle = event->m_pstParticles.get();
        if (animation_has_range &&
            (particle->start_frame == -1 || particle->end_frame == -1 ||
             particle->start_frame == particle->end_frame) &&
            previous_frame < animation->start_frame && animation->start_frame <= subcycle) {
            if (enabled) {
                particle->SetActive(true);
                particle->emission_count = 0;
                if (index == 0) {
                    auto callback = std::make_unique<W8MonsterShakeCallback>();
                    callback->m_pMonster = this;
                    callback->m_pParticles = particle;
                    callback->saved_behaviour = animation_behaviour;
                    callback->saved_frame_method = frame_method;
                    particle->callback = std::move(callback);

                    pending_behaviour = W8_ANIMATION_NEVER_STOP;
                    if (animation->start_frame == animation->end_frame) {
                        SetFrameMethod(W8_ANIMATION_UNKNOWN_FRAME_METHOD);
                        frame_direction = W8_ANIMATION_FORWARD;
                    } else {
                        SetFrameMethod(W8_ANIMATION_PING_PONG);
                        first_frame = animation->start_frame;
                        if (animation->end_frame < GetNumSubCycles()) {
                            last_frame = animation->end_frame;
                        } else {
                            last_frame = GetNumSubCycles() - 1;
                        }
                    }
                }
            }
            continue;
        }

        if (particle->start_frame != -1 && particle->end_frame != -1 &&
            particle->start_frame != particle->end_frame) {
            if (static_cast<unsigned int>(previous_frame) ==
                static_cast<unsigned int>(particle->start_frame)) {
                if (enabled) {
                    particle->SetActive(true);
                    particle->emission_count = 0;
                }
            } else if (static_cast<unsigned int>(subcycle) ==
                       static_cast<unsigned int>(particle->end_frame)) {
                particle->SetActive(false);
            }
        }
    }
}

W8AnimObj* W8Monster::GetCurrentAnimation()
{
    W8Monster* representation = this;

    return representation
        ->animations[representation->current_cycle][representation->current_subcycle]
        .get();
}

/* Resolve the active cycle/subcycle AnimObj at the representation's selected LOD. */
W8AniMesh* W8Monster::GetCurrentAniMesh()
{
    W8AnimObj* animation = animations[current_cycle][current_subcycle].get();
    if (animation == 0) {
        Fail("pao");
    }
    return AnimObjEntry(animation, m_bLOD, 0);
}

/* Store one value in the two cycle records used as its compact mirrors, then
   propagate it to every attached object's +0x28 field.  The body consumes two
   cdecl arguments; callers that reserve another stack slot clean it themselves. */
void W8Monster::SetLocationId(int value)
{
    location_id = value;
    movement.location_id = static_cast<unsigned short>(value);
    for (const auto& event : sound_events) {
        event->location_id = value;
    }
}

bool W8Monster::HasPendingCycle()
{
    return pending_cycle != -1;
}

/* Compare the cycle's selected frame against the renderer's typed current-model
   slot.  Prop.cpp independently compares that slot with srModelInstance values. */
bool W8GrCycle::UsesCurrentModelInstance()
{
    srModelInstance* current = GetCurrentModelInstance();
    return current == g_current_model_instance;
}

glm::vec3 W8Monster::GetRepresentationLocation() const
{
    return GetLocation();
}

glm::vec3 W8Monster::GetRepresentationLocalLocation() const
{
    return GetLocalLocation();
}

/* Reset pitch, update the Navigator's facing, and rebuild the representation's
   complete yaw/pitch/roll matrix. */
void W8Monster::SetFacing(float angle)
{
    glm::mat3 rotation;

    SetAngles(NormalizeAngle(angle));
    SetPitch(0.0f);

    rotation = glm::mat3(1);

    angle = NormalizeAngle(GetYaw() + g_monster_rotation_offset);
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));

    angle = GetPitch();
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(1, 0, 0)));

    angle = movement.roll;
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 0, 1)));

    SetRotation(&rotation);
}

void W8Monster::GetScaleRange(float* minimum, float* maximum)
{
    W8Monster* runtime = this;

    *minimum = runtime->minimum_scale;
    *maximum = runtime->maximum_scale;
}

/* Returns the previous animation state and timestamps every update through the
   recovered shared SurRender timer. */
unsigned char W8Monster::SetAnimating(bool animating)
{
    W8Monster* runtime = this;
    unsigned char previous = runtime->animation_playing;

    runtime->animation_playing = animating;
    runtime->timer = GameClockMilliseconds();
    return previous;
}

unsigned char W8Monster::IsAnimating()
{
    return animation_playing;
}

/* A blocking script cycle keeps its pending-cycle slot; otherwise the request
   is stored as the representation's pending cycle. */
void W8Monster::SetPendingCycle(int cycle)
{
    if ((runtime_flags & W8_MONSTER_SCRIPT_WAIT) == 0) {
        pending_cycle = static_cast<signed char>(cycle);
    }
}

void W8Monster::SetRuntimeBehaviour(W8AnimationBehaviour behaviour)
{
    if (behaviour < 1 || behaviour > 3) {
        Fail("bBehaviour >= BEHAVIOUR_FIRST && bBehaviour <= BEHAVIOUR_LAST");
    }
    pending_behaviour = behaviour;
}

bool W8Monster::IsScalingY()
{
    return (runtime_flags & W8_MONSTER_SCALING_Y) != 0;
}

void W8Monster::SetActive(bool state)
{
    active = state;
}

bool W8Monster::IsDying()
{
    bool dying = Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_DIE || pending_cycle == 0x15;

    return dying;
}

/* Resolve mapped vertex zero on the current model and transform it into world
   space. Models without that mapping use the Navigator position plus the
   Monster's vertical offset. */
void W8Monster::GetMappedPosition(glm::vec3* position)
{
    srModelInstance* instance = GetCurrentModelInstance();

    if (instance != 0) {
        stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());
        while (mesh != 0) {
            int index = mesh->FindMappedIndex(0);

            if (index >= 0) {
                glm::vec3* vertices;
                if ((mesh->flags & W8_MESH_HAS_FRAME_STORAGE) == 0) {
                    vertices = mesh->getVertexLoc();
                } else {
                    vertices = mesh->GetVertexLocations(0, true, 0.0f);
                }
                if (vertices != 0) {
                    glm::mat4 matrix;

                    *position = vertices[index];
                    matrix = instance->getWorldSpaceMatrix<glm::mat4>();
                    *position = glm::vec3{glm::vec3(matrix * glm::vec4((*position), 1))};
                    return;
                }
            }
            mesh = mesh->Next();
        }
    }

    *position = movement.position;
    position->y += movement.height_offset;
}

/* Six thin bodies over the live animation object. Each is a null check and a
   forward, or a single member read; nothing here says what the members and
   slots are for, so each is named for what it reaches. */

std::unique_ptr<W8Monster> LoadMonsterCycle(W8World* world, std::string_view mon_name, int cycle,
                                            int value)
{
    auto monster = std::make_unique<W8Monster>(world);
    AllocateLegacyPropSaveId();
    monster->current_cycle = -1;
    if (!LoadGrCycle(mon_name, monster.get(), cycle, value, "data\\monsters", 0)) return nullptr;
    for (auto& event : monster->particles) {
        if (event.cycle == cycle && event.subcycle == -1)
            event.subcycle = monster->current_subcycle;
    }
    return monster;
}

/* Pass a copied position through Navigator's collision adjustment and install
   the adjusted result on the Monster's ordinary Navigator base. */
void W8Monster::SetAdjustedPosition(const glm::vec3* position)
{
    glm::vec3 current;
    glm::vec3 adjusted;
    glm::vec3* adjusted_position;
    glm::vec3 result;

    current = *position;
    adjusted_position = AdjustPosition(&adjusted, &current, &current, *owner_world->octree);
    result = *adjusted_position;
    SetPositionInternal(&result);
}

unsigned short W8Monster::ApproachStartupNavigator(double separation)
{
    unsigned short result = SetMovementTargetToNavigator(PartyNavigator(), separation);
    if (result != 0) {
        movement.boundary_enabled = false;
    }
    return result;
}

unsigned char W8Monster::LinkToStartupNavigator()
{
    W8Navigator* target = PartyNavigator();

    return LinkToNavigator(target, WorldGetFarClip(GetWorld()) * 2.0);
}

unsigned short W8Monster::ConfigureMovementToPlayer(float separation, float maximum_distance,
                                                    glm::vec3 position, int trace_mode,
                                                    unsigned char* probe_result)
{
    W8Navigator* target = PartyNavigator();

    return ConfigureMovementToNavigator(target, separation, maximum_distance, position, trace_mode,
                                        GetYaw(), probe_result);
}

unsigned short W8Monster::ConfigureMovementToMonster(W8Monster* target, float separation,
                                                     float maximum_distance, glm::vec3 position,
                                                     int trace_mode, unsigned char* probe_result)
{
    if (target == 0) {
        return 0;
    }
    return ConfigureMovementToNavigator(target, separation, maximum_distance, position, trace_mode,
                                        GetYaw(), probe_result);
}

/* Hands the shared reference position to one of Navigator's two position
   sinks. The monster's script part is found the long way round - the location
   id lives in the cycle array at 0x1e4, and the lookup pair turns it into the
   W8MonsterInfo whose control state gates the whole body - which is what puts
   this in Monster.cpp rather than in the engine: the assertion path the lookup
   carries names this file and line 5299.
   Control state one is the only value that suppresses the update; every other
   value falls through. The position is fetched before the flag is read, so it
   is read even on the path that turns out not to need one sink over the other,
   and the flag decides only which sink receives it. */
void W8Monster::ForwardReferencePosition(char alternate)
{
    W8MonsterInfo* monster_info;
    glm::vec3 position;

    monster_info = FindMonster(location_id);
    if (monster_info->control_state != W8_MONSTER_CONTROL_LURED) {
        position = cameraLocation();
        if (alternate != 0) {
            SetFacingToward(&position);
        } else {
            AimAtPosition(&position);
        }
    }
}

/* Aim one live Monster at another unless the source is controlled directly.
   The alternate path uses Navigator's second position sink, matching the
   corresponding player-position helper above. */
void W8Monster::AimAtMonster(W8Monster* target, bool alternate)
{
    W8MonsterInfo* monster_info;
    glm::vec3 target_position;
    glm::vec3 position;

    if (target != 0) {
        monster_info = FindMonster(location_id);
        if (monster_info->control_state != W8_MONSTER_CONTROL_LURED) {
            target_position = target->GetPosition();
            position = target_position;
            if (alternate) {
                SetFacingToward(&position);
            } else {
                AimAtPosition(&position);
            }
        }
    }
}

/* Apply the active light scale to the current model chain. */
void W8Monster::RefreshModelLightScale()
{
    g_monster_model_instances.clear();
    CollectModelInstances(g_monster_model_instances);
    for (stModelInstance* model : g_monster_model_instances) {
        model->light_scale = glm::vec3(current_scale);
    }
}

/* Flatten every model instance reachable from every cycle and subcycle. The
   temporary vectors used by the damage-appearance accessors below prove the
   element type: AniMesh's frame lookup returns stModelInstance objects and the
   consumers read their first-party fields beyond the srModelInstance base. */
void W8Monster::CollectModelInstances(std::vector<stModelInstance*>& instances)
{
    int cycle;

    GetTotalAnimationCount();
    for (cycle = W8_MONSTER_CYCLE_BIRTH; cycle < W8_MONSTER_CYCLE_COUNT; ++cycle) {
        int subcycle;

        for (subcycle = 0; subcycle < GetNumSubsPerCycle(static_cast<signed char>(cycle));
             ++subcycle) {
            auto& cycle_animations = animations[cycle];
            W8AnimObj* animation;

            if (subcycle >= static_cast<int>(cycle_animations.size())) {
                ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}",
                                                 name, g_cycle_names[cycle].name, subcycle));
            }
            animation = cycle_animations[subcycle].get();
            if (animation == 0) {
                continue;
            }

            if (animation->path_lists == 0) {
                int list_index;

                for (list_index = 0; list_index < 3; ++list_index) {
                    W8AniMesh* mesh = animation->entries[list_index].get();
                    if (mesh != 0) {
                        int frame_count = AniMeshValue(mesh);

                        if (mesh->flags.single_instance) {
                            instances.push_back(GetAniMeshFrame(mesh, 0));
                        } else {
                            int frame;

                            for (frame = 0; frame < frame_count; ++frame) {
                                instances.push_back(GetAniMeshFrame(mesh, frame));
                            }
                        }
                    }
                }
            } else if (animation->path_lists == 1) {
                int list_index;

                for (list_index = 0; list_index < 3; ++list_index) {
                    for (const auto& owned_mesh : animation->meshes[list_index]) {
                        auto* mesh = owned_mesh.get();
                        if (!mesh) continue;
                        const int frame_count = AniMeshValue(mesh);
                        for (int frame = 0; frame < frame_count; ++frame) {
                            instances.push_back(GetAniMeshFrame(mesh, frame));
                        }
                    }
                }
            }
        }
    }
}

/* Replace one named texture in a damage stage. Model instances own the normal
   skin tables; shake particles are the fallback when no model uses the name. */
bool W8Monster::ReplaceSkinTexture(int stage, std::string_view old_name, std::string_view new_name)
{
    std::string path;
    bool replaced = false;

    path = fmt::format("Data\\Monsters\\Bitmaps\\{}", new_name);
    auto texture = LoadTextureFromPath(wiz8::asset_path(path), nullptr, true);
    if (texture == 0) {
        ShutdownWithErrorBox(fmt::format("Missing skin texture: {}", new_name));
        return false;
    }

    std::vector<stModelInstance*> instances;
    CollectModelInstances(instances);
    for (int index = 0; index < static_cast<int>(instances.size()); ++index) {
        if (instances[index]->ReplaceDamageStageTexture(stage, old_name, texture) != 0) {
            replaced = true;
        }
    }

    if (!replaced && !particles.empty()) {
        for (auto& attachment : particles) {
            auto* event = &attachment;
            if (event->m_pstParticles->ReplaceTexture(old_name, texture) != 0) {
                replaced = true;
            }
        }
    }
    return replaced;
}

/* Damage table names are the parsed skin name followed by its numeric stage.
   A frame either creates the table or attaches the already-created table. */
int W8Monster::AddDamageStage(std::string_view base_name, int stage)
{
    int result = -1;
    std::vector<stModelInstance*> instances;

    const auto name = fmt::format("{}{}", base_name, stage);
    CollectModelInstances(instances);
    for (stModelInstance* instance : instances) {
        if (instance->FindDamageStage(name.c_str()) == -1) {
            result = instance->AddDamageStage(name.c_str());
        } else {
            result = instance->AddExistingDamageStage(name.c_str());
        }
    }
    return result;
}

/* The final registered Monster for a cycle name owns removal of that name's
   per-mesh skin tables. */
void W8Monster::RemoveCycleSkinTables()
{
    const std::string_view cycle_name = GetRegisteredName();
    std::vector<stModelInstance*> instances;

    if (!cycle_name.empty()) {
        CollectModelInstances(instances);
        for (int index = 0; index < static_cast<int>(instances.size()); ++index) {
            stMeshModel* mesh = static_cast<stMeshModel*>(instances[index]->getModel());
            for (; mesh != 0; mesh = mesh->Next()) {
                mesh->RemoveSkinTablesForCycle(cycle_name);
            }
        }
    }
}

/* Select the damage-stage model on every frame instance owned by this
   Monster. UpdateMonsterDamageAppearance supplies the HP-derived stage. */
void W8Monster::SetDamageStage(int stage)
{
    std::vector<stModelInstance*> instances;
    int index;

    CollectModelInstances(instances);
    for (index = 0; index < static_cast<int>(instances.size()); ++index) {
        instances[index]->damage_stage = stage;
    }
}

/* Every frame instance in one Monster carries the same number of available
   damage stages, so the first instance supplies the count. */
int W8Monster::GetDamageStageCount()
{
    std::vector<stModelInstance*> instances;

    CollectModelInstances(instances);
    if (!instances.empty()) {
        return static_cast<int>(instances[0]->damage_stage_tables.size());
    }
    return 0;
}

/* Floating damage feedback: render the amount into a scratch ARGB1555
   surface, wrap it in a camera-facing poster ahead of the monster, and fire a
   ten-particle blood burst at the same spot. The poster scales with camera
   distance so the number stays legible. */
void W8Monster::SpawnDamageNumber(unsigned int amount)
{
    if ((runtime_flags & W8_MONSTER_FADED_OUT) != 0) {
        return;
    }

    auto surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(0x100, 0x100, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
    SDL_FillSurfaceRect(surface.get(), nullptr, 0);
    unsigned char* data = static_cast<unsigned char*>(surface->pixels);
    if (data != 0) {
        std::string text;
        glm::vec3 position;
        glm::vec3 camera_position;
        glm::vec3 facing;
        glm::dvec3 location;
        glm::vec4 colour;
        glm::mat3 rotation;
        glm::dmat3 world;
        stParticle* particle;
        srShader shader;
        float distance;
        float scale;
        float pitch;
        float yaw;

        {

            text = fmt::format("{:d}", amount);
            SDLSurfaceOwner text_surface{SDL_CreateSurfaceFrom(surface->w, surface->h,
                                                               SDL_PIXELFORMAT_RGB565, data,
                                                               surface->pitch),
                                         SDL_DestroySurface};
            if (text_surface)
                DrawText(*text_surface, g_monster_damage_font,
                         {0x80 - g_monster_damage_font.textWidth(text) / 2,
                          0x80 - g_monster_damage_font.height() / 2},
                         text);
        }

        GetMappedPosition(&position);
        facing = GetPosition();
        pitch = ElevationToTargetCPP(&facing);
        facing = GetPosition();
        yaw = HeadingToTargetCPP(&facing);
        OffsetPositionByYawPitch(500.0f, &position, yaw, pitch);
        camera_position = cameraLocation();
        distance = glm::length((position - camera_position));
        if (distance > 5e+04f) {
            distance = 5e+04f;
        }
        scale = (distance * 0.0002f + 1.0f) * 375.0f;
        {
            auto poster = VideoMakePoster(surface, scale, scale, true);
            poster->alignment_flags |= 1u;
            location = glm::dvec3(position);
            poster->setLocation(location);
            poster->setParent(GetWorld()->dynamic_scene, 1);
            linked_runtime_objects.push_back(std::move(poster));

            auto owned_particle = std::make_unique<stParticle>(GetWorld()->dynamic_scene, 0xa);
            particle = owned_particle.get();
            auto material = std::make_shared<srMaterial>();
            colour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            material->setEmissive(colour);
            material->setDiffuse(colour);
            particle->SetMaterial(std::move(material));
            particle->SetTexture(LoadTextureFromPath(
                wiz8::asset_path("data/monsters/bitmaps/BloodParticle.tga"), nullptr, true));
            shader.value = 0x100c4b3;
            particle->SetRenderFlags(shader);
            particle->particle_size = 20.0;
            particle->expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
            particle->bounds_origin = {0, 0, 0};
            particle->cone_yaw = 1.5707963f;
            particle->cone_pitch = 1.5707963f;
            particle->emission_interval = 1;
            particle->has_acceleration = true;
            particle->speed_mode = W8_PARTICLE_SPEED_RANDOM;
            particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
            particle->lifetime_ms = 10000;
            particle->direction_mode = W8_PARTICLE_DIRECTION_CONE;
            particle->speed_min = 1000.0f;
            particle->speed_max = 3000.0f;
            particle->emission_limit = 8;
            particle->release_when_done = true;
            particle->bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
            particle->bounds_radius = 1000.0f;
            location = glm::dvec3(position);
            particle->setLocation(location);
            rotation = GetCurrentModelInstance()->getRotation<glm::mat3>();
            rotation *= glm::mat3_cast(glm::angleAxis(float(g_camera_pi), glm::vec3(0, 1, 0)));

            world = glm::dmat3(rotation);
            particle->rotate(world);
            RetireParticle(*owner_world, std::move(owned_particle));
        }
    }
}

/* Resolve the database-controlled render gate after the Monster's transient
   runtime overrides. The alternate argument selects the secondary live-info
   flag used by the world-update path. */
bool W8Monster::IsRenderable(bool alternate)
{
    bool disabled = this->disabled;
    int location_id = this->location_id;
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;

    if (disabled) {
        return false;
    }
    if (inactive) {
        return true;
    }
    if (location_id == -1) {
        return true;
    }
    if ((runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) != 0) {
        return true;
    }
    if (fade_state != W8_MONSTER_FADE_IDLE) {
        return true;
    }

    monster_info = owner_world->population->findMonster(location_id);
    record = GetMonsterDataForInfo(monster_info);
    if (record->camouflage0 > 0) {
        return monster_info->party_threat.party_detected;
    }
    if (alternate) {
        return monster_info->party_threat.visible_to_player;
    }
    return monster_info->within_viewing_distance;
}

/* Discover animated material state once, cache it in the monster flags, and restart
   the selected model's animated texture on frame zero when present. */
void W8Monster::InitializeAnimatedTexture()
{
    srModelInstance* instance = 0;

    if (subcycle == 0) {
        if ((runtime_flags & W8_MONSTER_TEXTURE_CHECKED) == 0) {
            srMeshModel* model;

            instance = SelectCycleFrameLod(current_cycle, 0, m_bLOD);
            model = static_cast<srMeshModel*>(instance->getModel());
            if (!MeshHasAnimatedTexture(model)) {
                runtime_flags &= ~W8_MONSTER_ANIMATED_TEXTURE;
            } else {
                runtime_flags |= W8_MONSTER_ANIMATED_TEXTURE;
            }
            runtime_flags |= W8_MONSTER_TEXTURE_CHECKED;
        }
        if ((runtime_flags & W8_MONSTER_ANIMATED_TEXTURE) != 0) {
            if (instance == 0) {
                instance = SelectCycleFrameLod(current_cycle, 0, m_bLOD);
            }
            SetModelAnimatedTextureFrame(instance, 0);
        }
    }
}

/* Advance a cycle's representation through its own virtual layout. */
void UpdateCycleRepresentation(W8GrCycle* cycle)
{
    cycle->UpdateRepresentation(GetWorld());
}

void W8Monster::RefreshStandingHeight()
{
    W8Monster* representation = this;
    signed char previous_cycle = representation->current_cycle;
    SetCycle(1);
    SetSubCycle(0);
    glm::vec3 camera_location;
    camera_location = glm::vec3(owner_world->camera->getLocation());
    SelectLOD(&camera_location);
    if (const auto bounds = GetAnimationBounds())
        representation->standing_height = bounds->maximum.y;
    if (previous_cycle != -1) {
        SetCycle(previous_cycle);
    }
}

void W8Monster::ApplyRepresentationScale()
{
    float old_scale = movement.scale;
    SetScale(scale);
    for (int cycle = 0; cycle < W8_MONSTER_CYCLE_COUNT; ++cycle) {
        float cycle_scale = scale;
        if (cycle == W8_MONSTER_CYCLE_DIE) {
            cycle_scale = death_scale * scale;
        }
        float x_scale = cycle_scale;
        if (mirror_x) {
            x_scale = cycle_scale * -1.0f;
        }
        int animation_count = static_cast<int>(animations[cycle].size());
        for (int index = 0; index < animation_count; ++index) {
            W8AnimObj* animation = animations[cycle][index].get();
            // The retail query precedes the null check.
            int frame_count = AnimObjValue(animation, 2);
            if (animation != 0 && frame_count != 0) {
                for (int frame = 0; frame < frame_count; ++frame) {
                    srModelInstance* instance =
                        AnimObjDispatch(animation, 2, static_cast<unsigned char>(frame));
                    instance->setScale(glm::dvec3(x_scale, cycle_scale, cycle_scale));
                }
            }
        }
    }
    if (!particles.empty()) {
        for (int index = 0; index < static_cast<int>(particles.size()); ++index) {
            stParticle* particle = particles[index].m_pstParticles.get();
            particle->SetParticleScale(scale);
            if (mirror_x) {
                particles[index].position.x *= -1.0f;
                particle->setScale(glm::dvec3(-1.0, 1.0, 1.0));
            }
        }
    }
    if (m_ground_shadow != 0) {
        float ratio = scale / old_scale;
        m_ground_shadow->width *= ratio;
        m_ground_shadow->depth *= ratio;
    }
    if (monster_light != 0) {
        monster_light->SetRange(movement.collision_radius * 3.0f);
        monster_light->m_vertical_offset = movement.height_offset;
    }
}

/* Build a floating icon item from a bitmap path: clamp-wrapped texture, a
   500-unit-wide poster quad relocated 250 units up the Y axis with alignment
   enabled, wrapped in a WorldItemEntity whose mesh attachment and bounds are refreshed
   for the world. */
std::unique_ptr<WorldItemEntity> CreateMonsterIconItem(W8World& world, std::string_view path)
{
    auto texture = std::make_shared<stTextureFile>(wiz8::asset_path(path), false);
    texture->setWrapS(srTexture::WRAP_CLAMP);
    texture->setWrapT(srTexture::WRAP_CLAMP);
    texture->loadSurface();
    auto poster = MakePosterQuad(texture, 500.0f, 0.0f, true);
    auto* instance = poster.get();
    const glm::vec3 offset(0.0f, 250.0f, 0.0f);
    static_cast<srMeshModel*>(instance->getModel())->relocateVertices(offset);
    instance->setAlignment(1);
    auto item = std::make_unique<WorldItemEntity>();
    item->flags |= W8_ITEM_ENTITY_MESH_LOADED;
    item->m_psrMesh = std::move(poster);
    item->RefreshBounds();
    item->AttachMesh(&world);
    instance->light_scale = glm::vec3(0);
    return item;
}

void ApplyMonsterRepresentationScale(W8Monster* target)
{
    target->ApplyRepresentationScale();
}

void DestroyWorldMonster(W8Monster* monster)
{
    std::erase(GetWorld()->active_monsters, monster);
    std::erase_if(GetWorld()->monsters,
                  [monster](const auto& owner) { return owner.get() == monster; });
}

void DetachMonsterRepresentation(W8Monster* monster)
{
    if (monster != 0 && GetWorld() != nullptr) {
        monster->DetachRepresentation(GetWorld());
    }
}

void RefreshMonsterStandingHeight(W8Monster* monster)
{
    if (monster != 0) {
        monster->RefreshStandingHeight();
    }
}

void SetCombatInactiveFlag(unsigned char value)
{
    g_combat_inactive = value;
    g_combat_round_counter = 0;
}
