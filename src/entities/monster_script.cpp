#include <cmath>
#include <cstdlib>
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
#include <glm/geometric.hpp>

#include "application.h"
#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "monster_cycles.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/script.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/picking.h"
#include "wiz8/resource_cache.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/unicode.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

const double g_monster_script_direction_step = 0.8975978857142857;
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

/* Script IF expressions are deliberately small: a name followed by an
   optional comma-separated float list. The retail interpreter supports the
   two predicates below and treats every other name as false. */
bool Monster::EvaluateScriptCondition(std::string_view expression)
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

        party_position = owner_world.party.eye();
        if (!parameter)
            throw std::runtime_error(
                fmt::format("monster script {} line {}: PARTYNEAR needs a value", script->getName(),
                            script_line));

        glm::vec3 current_position = nav.GetPosition();
        monster_position = current_position;
        if (!CurrentGame().status.world_suspended &&
            glm::length((party_position - monster_position)) < *parameter * kWorldScale &&
            owner_world.population->findMonster(location_id)->player_visibility.line_of_sight !=
                0) {
            return true;
        }
    } else if (CompareTextIgnoreAsciiCase(name.substr(0, 6), "RANDOM") == 0) {
        if (!parameter)
            throw std::runtime_error(fmt::format("monster script {} line {}: RANDOM needs a value",
                                                 script->getName(), script_line));
        if (CurrentGame().rng.chance(static_cast<unsigned int>(*parameter)) != 0) {
            return true;
        }
    }
    return false;
}

/* Monster scripts are shared by name while any monster runs them. */
static ResourceCache<Script> g_monster_scripts;

std::shared_ptr<Script> LoadMonsterScript(std::string_view name)
{
    if (auto cached = g_monster_scripts.find(name)) return cached;
    const std::string path = "Data\\Monsters\\Scripts\\" + std::string(name);
    auto loaded = std::make_shared<Script>();
    if (loaded->Load(path.c_str()) == 0) return nullptr;
    loaded->setName(std::string(name));
    g_monster_scripts.insert(std::string(name), loaded);
    return loaded;
}

void Monster::UseScript(std::shared_ptr<Script> loaded, bool reset_orders)
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
bool Monster::SetScript(std::string script_name, bool reset_orders)
{
    auto loaded = LoadMonsterScript(script_name);
    const bool found = loaded != nullptr;
    UseScript(std::move(loaded), reset_orders);
    return found;
}

bool Monster::ResolveScriptPosition(std::string_view name, glm::vec3* position)
{
    if (CompareTextIgnoreAsciiCase(name, "home") == 0) {
        *position = formation;
    } else if (CompareTextIgnoreAsciiCase(name, "off_camera") == 0) {
        position->x = position->y = position->z = -10000000.0f;
    } else if (const auto found = owner_world.namedPosition(name)) {
        *position = *found;
    } else {
        return false;
    }
    return true;
}

/* Execute source lines until a command starts an asynchronous operation, ends
   the script, or the runaway-command guard trips. The two command modes share
   the original table: normal mode performs actions, while BEGINORDERS records
   the persistent movement policy that the Navigator update consumes. */
void Monster::ProcessScript()
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

    monster_info = owner_world.population->findMonster(location_id);
    if (orders_finished) {
        if (monster_info == 0 || (monster_info->ai_mode & W8_MONSTER_AI_RESTORE_SCRIPT) == 0) {
            return;
        }
        monster_info->ai_mode &= ~W8_MONSTER_AI_RESTORE_SCRIPT;
        return;
    }

    if (monster_info != 0 && (monster_info->ai_mode & W8_MONSTER_AI_RESTORE_SCRIPT) != 0) {
        monster_info->ai_mode &= ~W8_MONSTER_AI_RESTORE_SCRIPT;
        if (script_wait == MONSCR_WALKTO && nav.movement_stopped) {
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
                    nav.SetMovementTargetToNavigator(&owner_world.party.navigator, 5.0);
                } else {
                    nav.ConfigureMovementToPosition(&position);
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
                nav.AimAtPosition(&position);
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
                ForwardNpcScriptNotice(
                    (owner_world.population->findMonster(location_id))->boundNpc(), 0, line_number,
                    command == MONSCR_SAY ? 1 : suppress);
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
                        visual.pending_cycle = static_cast<signed char>(cycle);
                        visual.animation_playing = 1;
                        visual.timer = owner_world.clocks.gameMilliseconds();
                        visual.SetSubCycle(0);
                        visual.forced_subcycle = subcycle - 1;
                        if (visual.pending_cycle == -1) {
                            visual.pending_cycle = visual.current_cycle;
                        }
                        runtime_flags |= W8_MONSTER_KEEP_SUBCYCLE;
                        tokens >> token;
                        if (!tokens || SDL_strcasecmp(token.c_str(), "NOBLOCK") != 0) {
                            visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
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
                const int missile_type = atoi(token.c_str());
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
                owner_world.launchMissile(
                    CurrentGame().data,
                    {.kind = missile_type, .origin = source, .destination = target});
                break;
            }
            case MONSCR_GIVE:
                tokens >> token;
                if (tokens) {
                    auto& game = CurrentGame();
                    int item = FindItemRecordByName(game.data, token.c_str());
                    if (item != -1) {
                        game.inventory.replaceHeld(
                            MakeItem(game.data, game.rng, item, true, true, false));
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
                    nav.SetPositionInternal(&position);
                }
                break;
            }
            case MONSCR_DIE:
                visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                visual.pending_cycle = 0x15;
                visual.animation_playing = 1;
                visual.timer = owner_world.clocks.gameMilliseconds();
                visual.SetSubCycle(0);
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
                    CurrentGame().setMonsterGroupDisposition(location_id, disposition);
                }
                break;
            }
            case MONSCR_DISAPPEAR:
                runtime_flags |= W8_MONSTER_PARKED;
                break;
            case MONSCR_LOOKHERE:
                CurrentGame().pointCameraAtMonster(
                    *owner_world.population->findMonster(location_id), true, true);
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
                    glm::vec3 current = nav.GetPosition();
                    home = current;
                    formation = home;
                }
                nav.StartPatrol(&home, distance, variation);
                break;
            }
            case MONSCR_STOPPATROL:
                nav.flags &= 0xdfffffff;
                break;
            case MONSCR_TRIGGER:
                tokens >> token;
                if (tokens) {
                    this->trigger = FindTriggerByName(token.c_str());
                    if (this->trigger != 0) {
                        this->trigger->Run(-1);
                        if (owner_world.isRetiring(*this)) return;
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
                        CurrentGame().startGolemAttack();
                    } else if (SDL_strcasecmp(token.c_str(), "ENDSAVANTWALK") == 0) {
                        runtime_flags |= W8_MONSTER_PARKED;
                        Trigger* trigger = FindTriggerByName("Path3Trigger");
                        if (trigger != 0) trigger->Run(-1);
                        if (owner_world.isRetiring(*this)) return;
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
                        monster_info = owner_world.population->findMonster(location_id);
                        ResetTargetSource(&source);
                        CurrentGame().setMonsterCondition(monster_info->location_id,
                                                          W8_CONDITION_ASLEEP, 6, 0, &source, true);
                    } else if (SDL_strcasecmp(token.c_str(), "ENDBELAWALK") == 0) {
                        runtime_flags |= W8_MONSTER_PARKED;
                        ClearMainGameTargetState();
                    } else if (SDL_strcasecmp(token.c_str(), "BELA_END_CC_WALK") == 0) {
                        W8NpcState* npc = CurrentGame().story.npcOfKind(0x8d);
                        if (npc != 0) CurrentGame().queueNpcNotice(*npc, nullptr, 6);
                        monster_info = owner_world.population->findMonster(location_id);
                        if (monster_info->control_state != W8_MONSTER_CONTROL_LURED) {
                            glm::vec3 party = owner_world.party.eye();
                            nav.AimAtPosition(&party);
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
                    glm::vec3 position = nav.GetPosition();
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
bool Monster::CanContinueScript()
{
    switch (script_wait) {
    case MONSCR_WALKTO:
        if (!nav.movement_stopped) {
            return false;
        }
        break;
    case MONSCR_FACE:
        if (static_cast<float>(fabs(nav.movement.target_yaw - nav.movement.yaw)) >=
            g_camera_transition_epsilon) {
            return false;
        }
        break;
    case MONSCR_SAY:
        if (CurrentGame().npc_dialogue.DefersCharacterEvents(false)) {
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
        if (CurrentGame().activity.conversation) {
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

bool Monster::SetScriptLabel(std::string_view label)
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
