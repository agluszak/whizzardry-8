#include "wiz8/local_code/MonsterManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "navigation_flags.h"
#include "sight_state.h"
#include "wiz8/character_skills.h"
#include "wiz8/compat/native.h"
#include "wiz8/containers.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/difficulty.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/entity_assets.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/ThingEditorShared.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/monster_runtime.h"
#include "wiz8/music_playlist.h"
#include "wiz8/notices.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/quality.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

const float g_monster_record_float_scale = 20.0f;

/* The two cycles that always start regardless of the pending one: 0x14 is the
   cycle a motionless monster is still allowed to enter, and 0x15 is death. */
enum { W8_CYCLE_NONE = 0xff, W8_CYCLE_STOP = 0x14, W8_CYCLE_DEATH = 0x15 };
/* The character array the alternate-name form indexes, and the slot it uses. */

W8MonsterInfo* CreateMonsterInfo(W8MonsterGroup* group, W8MonsterRecord* record,
                                 glm::vec3* position)
{
    std::unique_ptr<W8MonsterInfo> owner(new (std::nothrow) W8MonsterInfo{});
    W8MonsterInfo* monster_info = owner.get();
    int value;

    if (!monster_info) {
        return nullptr;
    }
    monster_info->location_id = CurrentGame().status.next_monster_location_id;
    while (CurrentGame().status.next_monster_location_id++, monster_info->location_id == 0) {
        monster_info->location_id = CurrentGame().status.next_monster_location_id;
    }

    monster_info->monster_group_id = group->group_id;
    monster_info->monster_species = group->monster_id;
    monster_info->ubDisposition = group->ubDisposition;
    monster_info->scale = -1.0f;
    monster_info->fActive = false;
    monster_info->p3D = 0;
    monster_info->pCombat = 0;
    monster_info->position = *position;
    monster_info->derived = GetCameraFacingYaw(position);

    value = RollDice(&record->hit_points);
    monster_info->uiHPMax = value;
    monster_info->hp_current = value;
    value = RollDice(&record->stamina_dice);
    monster_info->stamina_max = value;
    monster_info->stamina = value;
    monster_info->fatigue_band = CalculateMonsterFatigueBand(value, value);

    memset(monster_info->uiCondition, 0, sizeof(monster_info->uiCondition));
    memset(monster_info->enchantments, 0, sizeof(monster_info->enchantments));
    monster_info->highest_condition = W8_CONDITION_NONE;
    monster_info->condition_argument = 0;
    monster_info->charm_strength = 0;
    memset(&monster_info->modifiers, 0, sizeof(monster_info->modifiers));
    monster_info->fMotionless = false;
    monster_info->ai_mode = W8_RT_AI_IDLE;
    monster_info->summoned = W8_MONSTER_SUMMON_NONE;
    monster_info->elemental_summon = -1;
    monster_info->movement_watch_position = glm::vec3(0);

    group->monsters.reserve(group->monsters.size() + 1);
    CurrentLevelPopulation().monster_roster.add(std::move(owner), record->unborn);
    group->monsters.push_back(monster_info->location_id);

    RequestRedrawParty();
    GetWorldSpatialIndex()->VisitPointCopy(static_cast<unsigned short>(monster_info->location_id),
                                           position);
    return monster_info;
}

/* , not : 0x0042E650 ends in `ret 0x4`, and both callers here
   clean only three of the four dwords they push across the tail. */

/* Materialize one inactive script record in the world. Existing engine
   Monsters are reattached without rebuilding their representation; absent or
   reset ones are activated first and choose either the birth cycle or a random
   idle subcycle according to membership in the unborn list. */
void ActivateMonsterInWorld(W8MonsterInfo* monster_info)
{
    if (!monster_info || monster_info->fActive) return;
    ActivateMonster(monster_info, W8_MONSTER_LOAD_ALL_CYCLES);
    PrepareMonsterInWorld(*GetWorld(), *monster_info,
                          CurrentLevelPopulation().monster_roster.isUnborn(monster_info));
    RequestRefreshPartyState();
    RefreshFlaggedMainGameState();
    BindNpcToMonster(MonsterDBFromSpecies(monster_info->monster_species)->npc_kind, true,
                     monster_info->location_id);
}

void PrepareMonsterInWorld(World& world, W8MonsterInfo& info, bool unborn)
{
    auto* monster_info = &info;
    W8MonsterRecord* record;
    glm::vec3 camera_position;

    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    if (monster_info->fActive) {
        return;
    }

    record = MonsterDBFromSpecies(monster_info->monster_species);
    if (monster_info->p3D == 0 || monster_info->p3D->IsPendingFinalize()) {
        if (!monster_info->p3D) throw std::runtime_error("missing prepared monster representation");
        monster_info->p3D->SetLocationId(monster_info->location_id);
        monster_info->p3D->SetAdjustedPosition(&monster_info->position);

        if (unborn) {
            if (!monster_info->p3D->IsCycleSupported(0)) {
                Fail("MonsterIsCycleSupported(pMonsterInfo->p3D, CYCLE_BIRTH)",
                     "Unborn monsters must have CYCLE_BIRTH!");
            }
            monster_info->p3D->visual.SetBehaviour(W8_ANIMATION_PLAY_ONCE);
            monster_info->p3D->SetCycle(0);
            if (monster_info->p3D->Query(W8_MONSTER_QUERY_FRAME_COUNT) == -1) {
                Fail("MonsterQuery(pMonsterInfo->p3D, QUERY_NUM_FRAMES) != -1");
            }
            monster_info->p3D->visual.SetSubCycle(0);
            monster_info->p3D->SetAnimating(false);
            monster_info->p3D->nav.active = false;
            monster_info->p3D->inactive = true;
        } else {
            monster_info->p3D->SetCycle(1);
            monster_info->p3D->visual.SetBehaviour(W8_ANIMATION_NEVER_STOP);
            if (monster_info->p3D->Query(W8_MONSTER_QUERY_FRAME_COUNT) == -1) {
                Fail("MonsterQuery(pMonsterInfo->p3D, QUERY_NUM_FRAMES) != -1");
            }
            monster_info->p3D->visual.SetSubCycle(
                Random(monster_info->p3D->Query(W8_MONSTER_QUERY_FRAME_COUNT)));
            monster_info->p3D->SetAnimating(!monster_info->fMotionless);
        }

        world.active_monsters.push_back(monster_info->p3D);
        monster_info->p3D->SetFacing(monster_info->derived);
        RebuildMonsterDerivedStats(*monster_info);
        monster_info->p3D->nav.movement.leadership_rank =
            static_cast<unsigned int>(record->effective_level) * 0x10000U +
            monster_info->location_id;
        monster_info->p3D->SetLocationId(monster_info->location_id);
        monster_info->p3D->pending_finalize = false;

        InitializeMonsterRangeCapabilities(monster_info,
                                           MonsterDBFromSpecies(monster_info->monster_species));
    }

    camera_position = cameraLocation(world);
    monster_info->p3D->visual.SelectLOD(&camera_position);
    monster_info->p3D->UpdateRepresentation(&world);
    world.compiledIndex()->VisitPointCopy(static_cast<unsigned short>(monster_info->location_id),
                                          &monster_info->position);
    monster_info->fActive = true;
    if (monster_info->p3D != 0) {
        int damage_stage_count = monster_info->p3D->GetDamageStageCount();
        if (damage_stage_count > 1) {
            int damage_stage =
                ((monster_info->uiHPMax - static_cast<int>(monster_info->hp_current)) *
                 damage_stage_count) /
                monster_info->uiHPMax;
            if (damage_stage >= damage_stage_count - 1) {
                damage_stage = damage_stage_count - 1;
            }
            monster_info->p3D->SetDamageStage(damage_stage);
        }
    }
    monster_info->monster_visibility = std::make_unique<W8MonsterVisibility>();
    if (record->can_open_doors) {
        monster_info->p3D->nav.movement.navigation_filter |= W8_NAV_THROUGH_DOORS;
    }
}

/* Activate the representation lazily. The mode selects whether all available
   cycles are loaded or only the startup cycle; both paths share the world
   context and preserve the loader's success result for the source assertion. */
std::unique_ptr<Monster> ReadMonsterRepresentation(World& world, W8MonsterInfo* monster_info,
                                                   W8MonsterActivationMode mode)
{
    W8MonsterRecord* record;
    record = GetMonsterDataForInfo(monster_info);

    auto monster = Monster::ReadAllCycles(&world, record->cycle_name,
                                          mode == W8_MONSTER_LOAD_ALL_CYCLES ? 1 : 0,
                                          monster_info->location_id);
    if (!monster) {
        Fail("loaded monster", "ActivateMonster: MonsterReadAllCycles failed");
    }
    return monster;
}

void ConfigureMonsterRepresentation(World& world, W8MonsterInfo* monster_info)
{
    if (monster_info->scale < 0.0f ||
        CurrentGame().status.level_progress[world.level_id].visited == 0) {
        monster_info->saved_mirror_x = monster_info->p3D->visual.mirror_x;
        monster_info->scale = CalculateMonsterScale(monster_info);
        monster_info->p3D->scale = monster_info->scale;
    } else {
        monster_info->p3D->scale = monster_info->scale;
        monster_info->p3D->visual.mirror_x = monster_info->saved_mirror_x;
    }

    ApplyMonsterRepresentationScale(monster_info->p3D);
    monster_info->p3D->SetCycle(1);
    RefreshMonsterStandingHeight(monster_info->p3D);
}

void ActivateMonster(W8MonsterInfo* monster_info, W8MonsterActivationMode mode)
{
    if (!monster_info || monster_info->fActive) Fail("inactive monster");
    if (monster_info->p3D) return;
    auto monster = ReadMonsterRepresentation(*GetWorld(), monster_info, mode);
    monster_info->p3D = monster.get();
    GetWorld()->monsters.push_back(std::move(monster));
    ConfigureMonsterRepresentation(*GetWorld(), monster_info);
}

void ClearMonsterPathAndResume(W8MonsterInfo* monster_info)
{
    int result;

    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    monster_info->p3D->visual.m_pAI.reset();
    monster_info->p3D->nav.flags &= 0xdfffffff;
    monster_info->p3D->nav.ClearMovement();
    if (!monster_info->fMotionless) {
        result = monster_info->p3D->Query(W8_MONSTER_QUERY_CYCLE);
        if (result != 1 && result != 2 && monster_info->p3D->visual.pending_cycle == -1) {
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_IDLE, W8_ANIMATION_NEVER_STOP);
        }
    }
}

void MonsterStartsDying(W8MonsterInfo* monster_info, bool display_message)
{
    if (!monster_info->p3D->IsDying()) {
        StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_DIE, W8_ANIMATION_PLAY_ONCE);
        DeactivateMonster(monster_info);
        RecordMonsterKill(monster_info, display_message);
        RemoveMonster(monster_info, W8MonsterRemoval::Deactivate);
    }
}

/* The kill bookkeeping a monster's death runs: name the kill on the notice
   channel the killer's kind selects, clear the conditions the dead monster
   sourced, and - unless the record opts out - land the faction hit and bank
   the kill count and experience for a monster that fought the party. */
void RecordMonsterKill(W8MonsterInfo* monster_info, bool announce)
{
    int killer_party_slot = -1;
    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    unsigned int monster_species = monster_info->monster_species;
    W8MonsterRecord* record = MonsterDBFromSpecies(monster_species);
    unsigned int notice_channel;
    if (TargetSourceIsCharacter(&monster_info->condition_target, 0)) {
        killer_party_slot = monster_info->condition_target.iChar;
        if (killer_party_slot != -1 &&
            (killer_party_slot < 0 || killer_party_slot >= W8_PARTY_SLOT_COUNT ||
             !CurrentGame().party.rows[killer_party_slot].fOccupied ||
             !CurrentGame().party.characters[killer_party_slot].fInParty)) {
            Fail("(iKilledByPC == BAD_INDEX) || VALID_CHAR(iKilledByPC)");
        }
        notice_channel = (killer_party_slot == -1) + 8;
    } else {
        notice_channel = 9;
    }
    if (announce && !monster_info->death_processed &&
        monster_info->party_threat.sight_state != W8_SIGHT_UNSEEN) {
        ShowNotice(notice_channel,
                   fmt::format("{} {}!", GetMonsterName(monster_info, 0, 0),
                               Data().strings[g_condition_notices[W8_CONDITION_DEAD].singular]));
    }
    ReleaseMonsterConditionBindings(monster_info);
    if (monster_info->summoned == W8_MONSTER_SUMMON_FRIENDLY) {
        return;
    }
    if (monster_info->death_processed) {
        return;
    }
    if (killer_party_slot == -1) {
        if (!TargetSourceIsMonster(&monster_info->condition_target, 0)) {
            goto done;
        }
        W8MonsterInfo* killer_info = FindMonster(monster_info->condition_target.iMonsterID, false);
        if (killer_info == nullptr) {
            goto done;
        }
        if (killer_info == 0) {
            Fail("pKillerMonsterInfo != NULL");
        }
        if (killer_info->ubDisposition != W8_DISPOSITION_FRIENDLY) {
            goto done;
        }
    }
    if (record->faction_id != W8_FACTION_UNALIGNED && record->faction_id != W8_FACTION_PARTY)
        RecordFactionOffense(static_cast<signed char>(record->faction_id), monster_info);
done:
    MonsterKilled(record->record_id, killer_party_slot);
    if (monster_info->pCombat && ((monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
                                   monster_info->uiCondition[W8_CONDITION_TURNCOAT] == 0) ||
                                  (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY &&
                                   monster_info->uiCondition[W8_CONDITION_TURNCOAT] != 0))) {
        ++CurrentGame().combat.state()->combat_result;
        if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT) {
            ++CurrentGame().status.level_progress[GetLoadedLevelID()].monster_kill_count;
        }
        if (killer_party_slot != -1) {
            W8Character* killer = &CurrentGame().party.characters[killer_party_slot];
            ++killer->kill_count;
            if (record->significant_kill != 0) {
                unsigned int level_total = 0;
                int occupied = 0;
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    if (CurrentGame().party.rows[slot].fOccupied) {
                        level_total += CurrentGame().party.characters[slot].uiExpLevel;
                        ++occupied;
                    }
                }
                int share = occupied == 0
                                ? 1
                                : static_cast<int>(level_total / static_cast<double>(occupied));
                if (share + 2 <= static_cast<int>(record->effective_level)) {
                    QueueCharacterEvent(killer, 37, 0, kCharacterEventFullVolume);
                }
            }
        }
        unsigned int experience = record->experience_override;
        if (experience == 0) {
            experience = record->experience;
            if (experience == 0) {
                DebugMessage(0, fmt::format("DATA ERROR: {} is worth 0 XPs", record->name0));
            }
        }
        CurrentGame().combat.state()->experience_pool += experience;
        if (CurrentGame().status.status_ints[monster_species] == 0) {
            CurrentGame().status.status_ints[monster_species] = 1;
        }
    }
}

namespace {
std::array<std::unique_ptr<W8MonsterRecord>, kMaxMonsterSpecies> monster_record_cache;

struct RecentCombat {
    std::uint64_t until = 0;
    W8Difficulty difficulty = W8_DIFFICULTY_NOVICE;
};
RecentCombat recent_combat;
} // namespace

std::unique_ptr<W8MonsterRecord>& CachedMonsterRecord(unsigned int monster_species)
{
    if (monster_species >= kMaxMonsterSpecies) {
        Fail("uiMonsterSpecies < MAX_MONSTERS_IN_DATABASE");
    }
    return monster_record_cache[monster_species];
}

void RecordCombatStart()
{
    recent_combat.until = w8_get_ticks() + 120000;
}

void ForgetRecentCombat()
{
    recent_combat.until = 0;
}

W8Difficulty LastCombatDifficulty()
{
    return recent_combat.difficulty;
}

W8MonsterRecord* MonsterDBFromSpecies(unsigned int monster_species)
{
    auto& record = CachedMonsterRecord(monster_species);
    if (!record) {
        auto loaded = std::make_unique<W8MonsterRecord>();
        if (!LoadMonsterDatabaseRecord(monster_species, loaded.get())) return nullptr;
        record = std::move(loaded);
    }
    return record.get();
}

W8MonsterRecord* GetMonsterDataForInfo(W8MonsterInfo* monster_info)
{
    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    return MonsterDBFromSpecies(monster_info->monster_species);
}

W8MonsterInfo* FindMonster(int location_id, bool assert_on_failure, std::source_location caller)
{
    if (!GetWorld()) return nullptr;
    W8MonsterInfo* monster = nullptr;
    for (const auto* partition : {&CurrentLevelPopulation().monster_roster.live(),
                                  &CurrentLevelPopulation().monster_roster.unborn()}) {
        for (const auto& candidate : *partition) {
            if (candidate->location_id == location_id) {
                return candidate.get();
            }
        }
    }
    if (monster == 0 && assert_on_failure) {
        Fail("FALSE", fmt::format("FindMonster: ID {:d} not found", location_id), caller);
    }
    return monster;
}

W8MonsterRecord* GetMonsterDataByLocationID(int location_id)
{
    W8MonsterInfo* monster;

    monster = FindMonster(location_id);
    if (monster == 0) {
        return 0;
    }
    return MonsterDBFromSpecies(monster->monster_species);
}

Monster* GetMonsterByLocationID(int location_id)
{
    if (location_id != -1) {
        if (W8MonsterInfo* monster_info = FindMonster(location_id, false)) {
            return monster_info->p3D;
        }
    }
    return 0;
}

/* Scale the monster database's combat movement range by the shared factor at
   0x005ED4F0 before range/path consumers use it. */
float GetMonsterCombatMoveRange(W8MonsterInfo* monster_info)
{
    W8MonsterRecord* record;
    float result;

    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    record = MonsterDBFromSpecies(monster_info->monster_species);
    if (record == 0) {
        Fail("pMonsterDB");
    }
    result = record->combat_move_range * g_monster_record_float_scale;
    return result;
}

void UpdateMonsterDamageAppearance(W8MonsterInfo* monster_info)
{
    Monster* monster = monster_info->p3D;

    if (monster != 0) {
        int count = monster->GetDamageStageCount();
        if (count > 1) {
            int value =
                ((monster_info->uiHPMax - static_cast<int>(monster_info->hp_current)) * count) /
                monster_info->uiHPMax;
            if (value >= count - 1) {
                value = count - 1;
            }
            monster->SetDamageStage(value);
        }
    }
}

int GetMonsterQuadrant(W8MonsterInfo* monster_info)
{
    if (monster_info == 0) {
        Fail("pMonsterInfo");
    }
    return GetQuadrantForPosition(monster_info->p3D->nav.GetPosition());
}

int GetMonsterCycleFallbackValue(unsigned int monster_species)
{
    W8MonsterRecord* record;

    record = MonsterDBFromSpecies(monster_species);
    if (record == 0) {
        return -1;
    }
    if (auto* world = GetWorld(); world && findMonsterAsset(*world, record->cycle_name)) {
        return 0;
    }
    if (GetRenderOptionState(W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS) != 0) {
        return record->alternate_model_index;
    }
    return record->model_index;
}

void ProcessMonstersAtCombatEnd(bool forced_cleanup)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (monster_info->fActive && monster_info->hp_current > 0 &&
            monster_info->uiCondition[W8_CONDITION_DEAD] == 0 &&
            monster_info->summoned != W8_MONSTER_SUMMON_NONE) {
            if (!forced_cleanup) {
                ShowNotice(W8_FONT_PALETTE_RUST,
                           fmt::sprintf(Data().strings[W8_NOTICE_MONSTER_SLAIN],
                                        GetMonsterName(monster_info, 0, 0)),
                           0);
            }
            ReleaseMonsterConditionBindings(monster_info);
            if (!forced_cleanup) {
                monster_info->death_processed = true;
                MonsterStartsDying(monster_info, true);
            }
        }
    }
}

void ConvertMonsterAttributes(W8MonsterInfo* monster_info)
{
    for (unsigned int monster_attribute = 0; monster_attribute < W8_MONSTER_ATTR_COUNT;
         ++monster_attribute) {
        W8MonsterRecord* record;
        W8Attribute attribute_index;
        int value;

        record = GetMonsterDataForInfo(monster_info);
        value = record->attribute_values[monster_attribute];
        attribute_index = W8_ATTRIBUTE_STRENGTH;

        if (monster_attribute >= W8_MONSTER_ATTR_COUNT) {
            Fail("uiMonsterAttribute < MONSTER_ATTR_COUNT");
        }
        switch (monster_attribute) {
        case W8_MONSTER_ATTRIBUTE_STRENGTH:
            attribute_index = W8_ATTRIBUTE_STRENGTH;
            break;
        case W8_MONSTER_ATTRIBUTE_INTELLIGENCE:
            attribute_index = W8_ATTRIBUTE_INTELLIGENCE;
            break;
        case W8_MONSTER_ATTRIBUTE_DEXTERITY:
            attribute_index = W8_ATTRIBUTE_DEXTERITY;
            break;
        case W8_MONSTER_ATTRIBUTE_SPEED:
            attribute_index = W8_ATTRIBUTE_SPEED;
            break;
        case W8_MONSTER_ATTRIBUTE_SENSES:
            attribute_index = W8_ATTRIBUTE_SENSES;
            break;
        default:
            Fail("FALSE", "ConvertMonsterAttribute: ERROR - Invalid monster attribute");
        }

        value += monster_info->modifiers.attribute_adjustments[attribute_index];
        value = std::clamp(value, 1, 125);
        monster_info->attributes[monster_attribute] = static_cast<unsigned char>(value);
    }
}

W8MonsterInfo* FindMonsterInfoBySpecies(unsigned int monster_species)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->monster_species == monster_species) {
            return monster_info;
        }
    }
    return 0;
}

void ResetLivingMonstersAfterCombat(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (monster_info->hp_current > 0) {
            monster_info->p3D->nav.ResetMovementAndGroupState();
            if (monster_info->ai_mode > W8_RT_AI_IDLE &&
                monster_info->ai_mode <= W8_RT_AI_APPROACH_PARTY) {
                monster_info->ai_mode = W8_RT_AI_IDLE;
            }
        }
    }
}

void DestroyUngroupedMonsters(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (monster_info->monster_group_id == 0) {
            Monster* monster = monster_info->p3D;

            if (monster != 0) {
                if ((monster->runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) != 0 &&
                    monster->removal_state != W8_MONSTER_REMOVAL_NONE) {
                    monster->ApplyRemovalStateEffects();
                }
            }
            RemoveMonster(monster_info, W8MonsterRemoval::Despawn);
            --index;
        }
    }
}

void SetMonsterControlState(W8MonsterInfo* monster_info, W8MonsterControlState control_state)
{
    if (monster_info == 0) {
        Fail("pMonsterInfo");
    }
    switch (control_state) {
    case W8_MONSTER_CONTROL_NONE:
    case W8_MONSTER_CONTROL_RESISTED:
        if (monster_info->control_state == W8_MONSTER_CONTROL_LURED &&
            monster_info->p3D->nav.linked_navigator == 0) {
            monster_info->p3D->nav.ClearMovement();
            monster_info->ai_mode = W8_RT_AI_IDLE;
        }
        break;
    default:
        break;
    }
    monster_info->control_state = control_state;
    RequestRedrawParty();
}

void MonsterInfoSetMotionless(W8MonsterInfo* monster_info, bool motionless)
{
    bool previous = monster_info->fMotionless;
    Monster* monster = monster_info->p3D;

    monster_info->fMotionless = motionless;
    if (!motionless) {
        if (previous) {
            monster->SetAnimating(true);
            if (monster_info->p3D->visual.pending_cycle == -1) {
                StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_IDLE, W8_ANIMATION_NEVER_STOP);
            }
        }
    } else if (!previous) {
        signed char cycle_value = monster->visual.pending_cycle;

        if (cycle_value != -1) {
            if (cycle_value == 0x14) {
                return;
            }
            monster->SetPendingCycle(-1);
        }
        monster->SetAnimating(false);
    }
}

void MoveMonsterToLiveList(W8MonsterInfo* monster_info)
{
    if (monster_info == 0) {
        Fail("pMonsterInfo");
    }
    if (!CurrentLevelPopulation().monster_roster.promote(monster_info)) {
        return;
    }
    if (monster_info->p3D->IsCycleSupported(0)) {
        monster_info->p3D->SetCycle(0);
        monster_info->p3D->visual.SetBehaviour(W8_ANIMATION_PLAY_ONCE);
    } else {
        monster_info->p3D->SetCycle(1);
        monster_info->p3D->visual.SetBehaviour(W8_ANIMATION_NEVER_STOP);
    }
    monster_info->p3D->visual.SetSubCycle(0);
    monster_info->p3D->SetAnimating(true);
    monster_info->p3D->nav.active = true;
    monster_info->p3D->inactive = false;
}

static double DistanceBetweenPositions(const glm::vec3* first, const glm::vec3& second)
{
    return glm::length((*first - second));
}

W8MonsterInfo* FindNearestMonsterInfo(const glm::vec3* position, double maximum_distance)
{
    W8MonsterInfo* nearest = nullptr;
    double nearest_distance = 1.0e11;

    for (const auto* monsters : {&CurrentLevelPopulation().monster_roster.live(),
                                 &CurrentLevelPopulation().monster_roster.unborn()}) {
        for (const auto& monster_owner : *monsters) {
            auto* monster_info = monster_owner.get();
            const double distance =
                DistanceBetweenPositions(position, monster_info->p3D->nav.GetPosition());
            if (distance < nearest_distance &&
                (maximum_distance == 0.0 || distance < maximum_distance)) {
                nearest = monster_info;
                nearest_distance = distance;
            }
        }
    }
    return nearest;
}

static void InitializeMonsterRuntimeStatsFor(W8MonsterInfo* monster_info)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    int value = RollDice(&record->hit_points);
    monster_info->uiHPMax = value;
    monster_info->hp_current = value;
    value = RollDice(&record->stamina_dice);
    monster_info->stamina_max = value;
    monster_info->stamina = value;
    monster_info->fatigue_band = CalculateMonsterFatigueBand(value, value);
    monster_info->scale = CalculateMonsterScale(monster_info);
    monster_info->p3D->scale = monster_info->scale;
    ApplyMonsterRepresentationScale(monster_info->p3D);
    RefreshMonsterStandingHeight(monster_info->p3D);
}

void InitializeMonsterRuntimeStats(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        InitializeMonsterRuntimeStatsFor(GetMonsterListEntry(index));
    }

    for (index = 0; index < CurrentLevelPopulation().monster_roster.unborn().size(); ++index) {
        InitializeMonsterRuntimeStatsFor(GetUnbornMonsterEntry(index));
    }
}

float CalculateMonsterScale(W8MonsterInfo* monster_info)
{
    float minimum;
    float maximum;

    monster_info->p3D->GetScaleRange(&minimum, &maximum);
    if (minimum == 0.0f || maximum == 0.0f) {
        return monster_info->p3D->scale;
    }

    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    W8MonsterRecord* record = MonsterDBFromSpecies(monster_info->monster_species);
    int minimum_hp = record->hit_points.Minimum();
    int maximum_hp = record->hit_points.Maximum();
    float scale =
        ((maximum - minimum) * (static_cast<unsigned int>(monster_info->uiHPMax) - minimum_hp)) /
            (maximum_hp - minimum_hp) +
        minimum;
    float result = (maximum - minimum) * (Random(1000) * 0.0004f - 0.2f) + scale;

    if (result < minimum) {
        return minimum;
    }
    if (result > maximum) {
        return maximum;
    }
    return result;
}

// FUNCTION: WIZ8 0x004e67a0
void TryStartMonsterCycle2(W8MonsterInfo* monster_info, Monster* monster,
                           W8MonsterCycle query_state)
{
    if (monster_info->fActive && monster_info->hp_current > 0 &&
        monster_info->uiCondition[W8_CONDITION_DEAD] == 0 &&
        monster_info->within_viewing_distance && query_state == W8_MONSTER_CYCLE_IDLE) {
        int result = monster->Query(W8_MONSTER_QUERY_AT_PLAYBACK_END);

        if (result != 0 && !monster_info->fMotionless) {
            monster->runtime_flags |= 0x80;
            if (monster->IsCycleSupported(2)) {
                signed char cycle = monster->visual.pending_cycle;

                if (cycle == 2 || !monster->IsCycleInterruptable(cycle) ||
                    (CurrentGame().combat.active() &&
                     CurrentGame().combat.state()->holdsTurn(
                         MonsterActor{monster_info->location_id}))) {
                    return;
                }
                W8MonsterGroup* group = FindMonsterGroup(monster_info->monster_group_id);
                unsigned int chance = group->MemberCount() * 20;

                if (!CurrentGame().activity.conversation && Random(chance) == 0) {
                    StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_SPICE, W8_ANIMATION_PLAY_ONCE);
                }
            }
        }
    }
}

/* Return the monster experience value exposed by Monster Info. Retail prefers
   the optional +0x26b override and otherwise uses the +0x181 base. The same
   effective value is also used as an HP-weighted combat-difficulty proxy. */
unsigned int GetMonsterExperience(const W8MonsterRecord* record)
{
    unsigned int value = record->experience_override;

    if (value <= 0) {
        value = record->experience;
    }
    return value;
}

bool AnyMonsterDying(void)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info != 0 && monster_info->p3D->IsDying()) {
            return true;
        }
    }
    return false;
}

static void ClearRemainingMonsterGroups()
{
    CurrentLevelPopulation().monster_groups.clear();
    CurrentLevelPopulation().encounter_groups.clear();
}

/* A monster belongs to exactly one of the live/unborn registries. Destroy
   registered script parts while their groups and runtime bindings still exist. */
static void DestroyAllMonsterInfos()
{
    while (!CurrentLevelPopulation().monster_roster.live().empty()) {
        auto* monster_info = CurrentLevelPopulation().monster_roster.live().front().get();
        RemoveMonster(monster_info, W8MonsterRemoval::Unload);
    }
    while (!CurrentLevelPopulation().monster_roster.unborn().empty()) {
        auto* monster_info = CurrentLevelPopulation().monster_roster.unborn().front().get();
        RemoveMonster(monster_info, W8MonsterRemoval::Unload);
    }
    ClearRemainingMonsterGroups();
}

/* Dispose any previous level's live and unborn records before resetting
   runtime IDs and counters. Both registries borrow from the same owner. */

/* Drain live records while their world is intact. Species records are shared
   session resources and remain cached across level replacement. */
void ShutdownMonsterManager(void)
{
    DestroyAllMonsterInfos();
}

bool RemoveMonster(W8MonsterInfo* monster_info, W8MonsterRemoval removal)
{
    auto& roster = CurrentLevelPopulation().monster_roster;
    const auto owns = [&](const auto& partition) {
        return std::ranges::any_of(partition,
                                   [&](const auto& entry) { return entry.get() == monster_info; });
    };
    if (!owns(roster.live()) && !owns(roster.unborn())) return false;

    if (CurrentGame().combat.active() && monster_info->pCombat) {
        MonsterInfoLeaveCombat(monster_info);
    }
    if (monster_info->monster_group_id != 0) {
        W8MonsterGroup* monster_group = FindMonsterGroup(monster_info->monster_group_id);

        wiz8::erase_first(monster_group->monsters, monster_info->location_id);
        if (monster_group->highlighted_member == monster_info->location_id)
            monster_group->highlighted_member = -1;

        RequestRedrawParty();
        if (monster_group->MemberCount() == 0) {
            DestroyMonsterGroup(monster_group, monster_info);
        } else {
            if (monster_group->leader_location_id == monster_info->location_id) {
                ElectGroupLeaderMember(monster_group);
                if (monster_group->leader_group_id == 0) {
                    RefreshMonsterGroupAndAllies(monster_group);
                }
            }
        }
        monster_info->monster_group_id = 0;
    }
    DeactivateMonster(monster_info);
    if (CurrentGame().combat.state()) {
        CurrentGame().combat.state()->endTurnOf(MonsterActor{monster_info->location_id});
    }
    if (removal == W8MonsterRemoval::Deactivate) return true;
    if (monster_info->p3D) {
        if (removal == W8MonsterRemoval::Despawn)
            monster_info->p3D->visual.RetireParticles(*monster_info->p3D->owner_world);
        DetachMonsterRepresentation(monster_info->p3D);
        DestroyWorldMonster(monster_info->p3D);
        monster_info->p3D = nullptr;
    }
    // Inactive and unborn records also own visibility/NPC relationships.
    ReleaseMonToMonVisibilityList(monster_info);
    if (auto* world = GetWorld(); world && world->compiledIndex())
        world->compiledIndex()->UnregisterLocationObjects(
            static_cast<unsigned short>(monster_info->location_id));
    ReleaseNpcBinding(monster_info->bound_npc_index);
    return roster.erase(monster_info);
}

/* Retires one entry from the live world: it parks the entry at the sentinel
   location 9999, zeroes its two current stats, resets the live Monster's flag
   word to the deactivated value, captures the position the Monster ends at, and
   lowers the live count. In combat it also clears the two selection slots the
   global at 0x006836A8 holds if this entry occupied them. The p3D name for the
   Monster pointer at +0x0c comes from the MonsterManager.cpp:585 assertion. */
void DeactivateMonster(W8MonsterInfo* monster_info)
{
    glm::vec3 position;

    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    if (monster_info->fActive) {
        if (monster_info->p3D == 0) {
            Fail("pMonsterInfo->p3D != NULL");
        }
        monster_info->uiCondition[W8_CONDITION_DEAD] = W8_CONDITION_INDEFINITE;
        monster_info->highest_condition = W8_CONDITION_DEAD;
        monster_info->hp_current = 0;
        monster_info->stamina = 0;
        monster_info->p3D->nav.active = false;
        monster_info->p3D->nav.flags = 0x200000;
        ClearMonsterSpellIcons(monster_info->p3D);
        ReleaseMonToMonVisibilityList(monster_info);
        position = monster_info->p3D->GetRepresentationLocalLocation();
        monster_info->position = position;
        monster_info->fActive = false;
        if (CurrentGame().combat.active()) {
            RefreshAllSight();
            SetTargetToMonster(monster_info->location_id, false);
            RefreshFlaggedMainGameState();
            RefreshCombatEngagement();
            CurrentGame().combat.state()->endTurnOf(MonsterActor{monster_info->location_id});
        }
    }
}

/* Prepare fresh combat state and reset the monster's target before it joins combat. */
void MonsterInfoEnterCombat(W8MonsterInfo* monster_info)
{
    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    if (monster_info->pCombat) {
        Fail("!pMonsterInfo->pCombat");
    }
    ClearMonsterPathAndResume(monster_info);
    monster_info->pCombat = std::make_unique<W8MonsterCombatState>();
    if (monster_info->player_visibility.sight_state == W8_SIGHT_UNSEEN) {
        monster_info->player_visibility.sight_state = W8_SIGHT_RECENT;
        monster_info->player_visibility.last_seen_clock = CurrentGame().status.world_clock;
    }
    ResetCombatSlot(&monster_info->Target);
    monster_info->p3D->nav.flags = 0;
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
        RefreshCombatEngagement();
    }
    if (CurrentGame().combat.active()) {
        EndMonsterTurn(monster_info);
    }
}

/* Release active combat effects before destroying the state and its action queue. */
void MonsterInfoLeaveCombat(W8MonsterInfo* monster_info)
{
    unsigned int index;
    W8EffectSlot* entry;

    if (!CurrentGame().combat.active()) {
        Fail("CurrentGame().combat.active()");
    }
    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    if (!monster_info->pCombat) {
        Fail("pMonsterInfo->pCombat");
    }
    CurrentGame().combat.state()->endTurnOf(MonsterActor{monster_info->location_id});
    for (index = 0; index < 9; ++index) {
        entry = &monster_info->pCombat->combat_effects[index];
        if (entry->active) {
            ClearEffectSlot(monster_info, entry);
        }
    }
    for (index = 0; index < 6; ++index) {
        entry = &monster_info->pCombat->combat_effects_2[index];
        if (entry->active) {
            ClearEffectSlot(monster_info, entry);
        }
    }
    monster_info->pCombat.reset();
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
        RefreshCombatEngagement();
    }
}

/* Toggles the party's combat-ready posture. Outside combat it only flips the
   flag; inside it also swaps the two engine bits at +0x001 and +0xa62 of the
   state block, and in engine mode 7 it announces the new posture with the
   message the block at 0x0068C09C holds for that direction. */
void TogglePartyCombatStance(void)
{
    std::string message;

    if (!GameStarted()) {
        g_settings.continuous_combat = (g_settings.continuous_combat == 0);
        return;
    }
    if (g_settings.continuous_combat != 0) {
        g_settings.continuous_combat = 0;
        if (CurrentGame().combat.active()) {
            CurrentGame().combat.state()->round_active =
                (CurrentGame().combat.state()->execution_active == 0);
            CurrentGame().combat.state()->combat_ready = true;
        }
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
            return;
        }
        message = Data().strings[W8_NOTICE_COMBAT_STANCE_RELAXED];
    } else {
        g_settings.continuous_combat = 1;
        if (CurrentGame().combat.active() && CurrentGame().combat.state()->combat_ready) {
            CurrentGame().combat.state()->round_active = true;
            CurrentGame().combat.state()->combat_ready = false;
        }
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
            return;
        }
        message = Data().strings[W8_NOTICE_COMBAT_STANCE_READY];
    }
    ShowNotice(W8_FONT_PALETTE_BEIGE, message);
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        RequestRedraw(W8_MAIN_REDRAW_COMBAT_STANCE);
    }
}

/* The combat toggle the interface drives: out of combat it starts one, and in
   combat it refuses to end it while any loaded group still qualifies, while any
   character is still engaged, or while the state block's leading flag is
   raised, announcing the reason in each case. Only when all three clear does it
   end combat and, if the level record's field at +0x2ca is not -1, run the
   trailing notification. */
void ToggleCombatMode(void)
{
    unsigned int group_list_index;
    W8MonsterGroup* monster_group;
    Missile* missile;

    if (!CurrentGame().combat.active()) {
        CurrentGame().combat.Start(1);
        return;
    }
    if (CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0 ||
        CurrentGame().combat.state()->enemies_engaged) {
        if (CurrentGame().combat.state()->round_count != 0) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END]);
            return;
        }
        for (group_list_index = 0;
             group_list_index < CurrentLevelPopulation().monster_groups.size();
             ++group_list_index) {
            monster_group = CurrentLevelPopulation().monster_groups[group_list_index].get();
            if (monster_group->members_active && monster_group->fInCombat &&
                MonsterGroupCanEngage(monster_group)) {
                ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END]);
                return;
            }
        }
    }
    for (missile = NextMissile(true); missile != 0; missile = NextMissile(false)) {
        if ((missile == CurrentGame().combat.state()->engaged_missile ||
             Data().missiles[missile->missile_table_index].spell_missile != 0) &&
            missile->BlocksEndingCombat()) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END_ENGAGED]);
            return;
        }
    }
    if (CurrentGame().combat.state()->execution_active != 0) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END_PENDING],
                   W8_NOTICE_TEXT_BOX_AUTOMATIC, W8_NOTICE_WRAP_AUTOMATIC, true);
        return;
    }
    CurrentGame().combat.End(false);
    ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_ENDED]);
    CurrentGame().events.push(CombatCalledOff{});
}

/* Starts one animation cycle on a monster, and refuses in three ways.

   A NEVER_STOP behaviour on a cycle that can be interrupted is rejected
   outright. So is any new cycle while an uninterruptable one is pending, except
   the stop cycle, which is allowed through silently - the diagnostic there names
   both cycles and the monster, which is what the cycle-name table at 0x0060EA08
   is for. And a motionless monster accepts only the stop cycle, death, and cycle
   zero; anything else fails the third assertion with no message at all.

   The combat-mode query on the way in is made for its effect: its result is
   discarded here. */
// FUNCTION: WIZ8 0x004e4db0
void StartMonsterCycle(W8MonsterInfo* monster_info, int cycle, W8AnimationBehaviour behavior)
{
    Monster* monster = monster_info->p3D;
    unsigned char pending;
    std::string detail;

    if (static_cast<signed char>(behavior) == W8_ANIMATION_NEVER_STOP &&
        !monster->IsCycleInterruptable(static_cast<signed char>(cycle))) {
        detail = "Trying to set a NEVER_STOP behaviour with an uninterruptable cycle!";
    } else {
        pending = monster->visual.pending_cycle;
        if (CurrentGame().combat.active()) {
            monster->Query(W8_MONSTER_QUERY_CYCLE);
        }
        if (pending != W8_CYCLE_STOP && pending != W8_CYCLE_NONE &&
            !monster->IsCycleInterruptable(static_cast<signed char>(pending))) {
            if (static_cast<signed char>(cycle) == W8_CYCLE_STOP) {
                return;
            }
            Fail("FALSE",
                 fmt::format(
                     "{} starting new cycle ({}) with an uninterruptable cycle pending ({})!",
                     GetMonsterName(monster_info, 0, 0),
                     g_cycle_names[static_cast<signed char>(cycle)].name,
                     g_cycle_names[static_cast<signed char>(pending)].name));
            return;
        }
        if (static_cast<signed char>(cycle) == W8_CYCLE_STOP ||
            static_cast<signed char>(cycle) == W8_CYCLE_DEATH ||
            static_cast<signed char>(cycle) == 0 || !monster_info->fMotionless) {
            monster->SetAnimating(true);
            monster->SetRuntimeBehaviour(behavior);
            monster->SetPendingCycle(cycle);
            monster->visual.pending_subcycle = 0;
            monster->visual.SubmitTargetValue();
            return;
        }
        detail.clear();
    }
    Fail("FALSE", detail);
}

/* Advance every live monster once. Removal is done in place, so each branch
   that shortens the PList decrements the unsigned index before the common
   increment. A delayed-removal monster is first detached from combat and the
   world; an ordinary monster advances its cycle state or stops animating while
   motionless. */
void ProcessMonsterManagerFrame(void)
{
    unsigned int monster_list_index;

    for (monster_list_index = 0;
         monster_list_index < CurrentLevelPopulation().monster_roster.live().size();
         ++monster_list_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_list_index);
        Monster* monster = monster_info->p3D;

        if ((monster->runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) != 0) {
            if (monster->fade_state == W8_MONSTER_FADE_IDLE) {
                if (monster->removal_state != W8_MONSTER_REMOVAL_NONE) {
                    monster->ApplyRemovalStateEffects();
                }
                RemoveMonster(monster_info, W8MonsterRemoval::Despawn);
                --monster_list_index;
            }
        } else if ((monster->runtime_flags & (W8_MONSTER_REMOVE_NOW | W8_MONSTER_SCRIPT_WAIT)) !=
                   0) {
            RemoveMonster(monster_info, W8MonsterRemoval::Despawn);
            --monster_list_index;
        } else {
            if ((monster->runtime_flags & W8_MONSTER_PARKED) == 0) {
                const auto query_state =
                    static_cast<W8MonsterCycle>(monster->Query(W8_MONSTER_QUERY_CYCLE));
                TryStartMonsterCycle2(monster_info, monster, query_state);
                if (monster->Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                    if (CurrentGame().combat.active() &&
                        query_state == W8_MONSTER_CYCLE_ATTACK_SPECIAL) {
                        monster_info->pCombat->special_ready = true;
                    }
                    switch (query_state) {
                    case W8_MONSTER_CYCLE_IDLE:
                    case W8_MONSTER_CYCLE_TRANSITION:
                    case W8_MONSTER_CYCLE_WALK:
                    case W8_MONSTER_CYCLE_TALK_SPICE:
                    case W8_MONSTER_CYCLE_TALK:
                        break;
                    case W8_MONSTER_CYCLE_DIE:
                        if ((monster->runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) == 0) {
                            monster_info = GetMonsterListEntry(monster_list_index);
                            HandleScriptedNpcDeath(monster_list_index);
                            if (monster_info->monster_group_id != 0) {
                                RemoveMonster(monster_info, W8MonsterRemoval::Deactivate);
                            }
                            DropMonsterLoot(monster_info, -1);
                            monster_info->p3D->BeginDelayedRemoval();
                        }
                        break;
                    default:
                        if (!monster_info->fMotionless) {
                            /* pending_cycle is signed char; the rest of this
                               unit and the representation compare the empty
                               slot to -1. W8_CYCLE_NONE is 0xff as int 255,
                               which a signed char never equals. */
                            if (monster_info->p3D->visual.pending_cycle == -1) {
                                StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_IDLE,
                                                  W8_ANIMATION_NEVER_STOP);
                            }
                        } else if (monster->IsAnimating() != 0) {
                            monster->SetAnimating(false);
                        }
                        break;
                    }
                }
            }
        }
    }
}

/* The display name for one monster. Three things decide it.

   Without a record, GetMonsterDataForInfo fetches the species' cached database row.

   One record id is special-cased entirely: it is shown as a party character's
   name with a prefix, formatted into a shared buffer.

   Otherwise the monster's group decides which of the record's two name sets is
   used, and name_form picks the variant within it - the sets are twenty-four
   wide characters apart. A monster with no group at all is a bug unless it is
   already dying, and says so on the debug channel rather than asserting. */
std::string GetMonsterName(W8MonsterInfo* monster_info, W8MonsterRecord* record,
                           unsigned char name_form)
{
    W8MonsterGroup* monster_group;

    if (record == 0) {
        record = GetMonsterDataForInfo(monster_info);
    }
    if (record->record_id == W8_MONSTER_RECORD_ALTERNATE_NAME) {
        CurrentGame().status.monster_name_buffer = fmt::format(
            "Al-{}", CurrentGame().party.characters[CurrentGame().status.sedexus_party_slot].name);
        return CurrentGame().status.monster_name_buffer;
    }
    if (monster_info->monster_group_id == 0) {
        if (!monster_info->p3D->IsDying()) {
            DebugMessage(
                1, fmt::format("ERROR: Monster ID {:d} has no group", monster_info->location_id));
        }
    } else {
        monster_group = FindMonsterGroup(monster_info->monster_group_id);
        if (monster_group == 0) {
            Fail("pMonsterGroup");
        }
        if (monster_group->alternate_name) {
            return (name_form ? record->name1 : record->name0);
        }
    }
    return (name_form ? record->name3 : record->name2);
}

float GetAveragePartyMemberLevel(void)
{
    float total = 0.0f;
    float count = 0.0f;
    for (int party_slot = 0; party_slot < 6; ++party_slot) {
        if (CurrentGame().party.rows[party_slot].fOccupied) {
            total += CurrentGame().party.characters[party_slot].uiExpLevel;
            count += 1.0f;
        }
    }
    if (count == 0.0f) return 1.0f;
    return total / count;
}

unsigned int GetBestPartySkillLevel(W8Skill skill_index, int* party_slot)
{
    unsigned int best_level = 0;
    int best_slot = -1;
    for (int index = 0; index < 8; ++index) {
        W8Character* character = &CurrentGame().party.characters[index];
        if (CurrentGame().party.rows[index].fOccupied && character->hp_current != 0 &&
            character->highest_condition < W8_CONDITION_TURNCOAT &&
            (character->skills[skill_index].level > best_level || best_slot == -1)) {
            best_level = character->skills[skill_index].level;
            best_slot = index;
        }
    }
    if (party_slot != 0) *party_slot = best_slot;
    return best_level;
}

/* Format the health knowledge the party has earned for one monster. NPC-backed
   records can suppress exact values, and ordinary monsters expose current and
   maximum HP independently at knowledge thresholds ten and five. */
std::string FormatMonsterHealth(W8MonsterInfo* monster_info)
{
    bool suppress_exact_health = false;
    unsigned int health_knowledge;

    if (monster_info->ubDisposition != W8_DISPOSITION_HOSTILE) {
        W8MonsterRecord* record;
        W8NpcState* npc;

        record = GetMonsterDataForInfo(monster_info);
        if ((record->flags & W8_MONSTER_FLAG_NPC) != 0) {
            npc = GetNpcStateByKind(record->npc_kind);
            if (npc != 0 && npc->record->has_group != 0) {
                suppress_exact_health = true;
            }
        }
    }

    if (monster_info->summoned == W8_MONSTER_SUMMON_FRIENDLY) {
        health_knowledge = 125;
    } else {
        float average_party_level = GetAveragePartyMemberLevel();
        W8MonsterRecord* record;
        int best_party_slot;
        int monster_level;

        record = GetMonsterDataForInfo(monster_info);
        monster_level = record->display_level;
        health_knowledge = GetBestPartySkillLevel(W8_SKILL_MYTHOLOGY, &best_party_slot);
        if (static_cast<int>(average_party_level) < monster_level) {
            float adjusted_knowledge =
                health_knowledge - (monster_level - average_party_level) * 3.0f + 0.5f;
            if (adjusted_knowledge < 0.0f) {
                adjusted_knowledge = 0.0f;
            }
            health_knowledge = static_cast<unsigned int>(adjusted_knowledge);
        }
    }

    if (g_dev_mode)
        return fmt::format("{:d}/{:d}", monster_info->hp_current, monster_info->uiHPMax);
    const auto current = health_knowledge < 10 || suppress_exact_health
                             ? "?"
                             : fmt::format("{:d}", monster_info->hp_current);
    const auto maximum = health_knowledge < 5 || suppress_exact_health
                             ? "?"
                             : fmt::format("{:d}", monster_info->uiHPMax);
    return fmt::format("{}/{}", current, maximum);
}

/* Blind, incapacitated and unoccupied party members cannot sense monsters. */
static bool CanNoticeMonsterGroup(unsigned int slot)
{
    const W8Character& character = CurrentGame().party.characters[slot];
    return CurrentGame().party.rows[slot].fOccupied && character.hp_current != 0 &&
           character.highest_condition < W8_CONDITION_INSANE &&
           character.uiCondition[W8_CONDITION_BLIND] == 0;
}

void DetectMonsterGroups(void)
{
    bool noticed = false;

    for (unsigned int group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[group_index].get();
        if (!group->members_active || (group->alternate_name && group->group_state[0] != 0) ||
            (CurrentGame().combat.active() && !group->fInCombat) ||
            !MonsterGroupHasRenderableMember(group, false)) {
            continue;
        }
        if (!group->alternate_name) {
            W8MonsterRecord* record = MonsterGroupGetRecord(group);
            int best_slot = -1;
            unsigned int best_margin = 0;
            for (unsigned int slot = 0; slot < 8; ++slot) {
                if (!CanNoticeMonsterGroup(slot)) continue;
                W8Character* character = &CurrentGame().party.characters[slot];
                int score =
                    (character->monster_awareness[group->monster_id] - record->effective_level) *
                        3 +
                    character->attributes[W8_ATTRIBUTE_INTELLIGENCE].effective / 10 +
                    character->skills[W8_SKILL_MYTHOLOGY].level;
                int roll = Random(100);
                if (roll >= score) {
                    continue;
                }
                unsigned int margin = score - roll;
                if (character->skills[W8_SKILL_MYTHOLOGY].active) {
                    PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_MYTHOLOGY, 10, false);
                }
                if (margin > best_margin) {
                    best_slot = slot;
                    best_margin = margin;
                }
            }
            if (best_slot != -1) {
                PostCharacterNotice(
                    best_slot, fmt::sprintf(Data().strings[0x1ca], GetMonsterGroupName(group)));
                group->alternate_name = true;
                bool vowel;
                switch (toupper(static_cast<unsigned char>(GetMonsterGroupName(group).front()))) {
                case 'A':
                case 'E':
                case 'I':
                case 'O':
                case 'U':
                    vowel = true;
                    break;
                default:
                    vowel = false;
                    break;
                }
                std::string article;
                if (group->MemberCount() == 1) {
                    article = vowel ? Data().strings[0x1cc] : Data().strings[0x1cb];
                } else {
                    article = Data().strings[0x1cd];
                }
                ShowNotice(W8_FONT_PALETTE_WHITE,
                           fmt::format("{} {}!", article, GetMonsterGroupName(group)));
                noticed = true;
            }
        }
        if (group->group_state[0] == 0) {
            for (unsigned int slot = 0; slot < 8; ++slot) {
                if (!CanNoticeMonsterGroup(slot)) continue;
                W8Character* character = &CurrentGame().party.characters[slot];
                unsigned int awareness =
                    character->monster_awareness[group->monster_id] + group->MemberCount();
                if (awareness > 0xff) {
                    awareness = 0xff;
                }
                character->monster_awareness[group->monster_id] =
                    static_cast<unsigned char>(awareness);
            }
            group->group_state[0] = 1;
        }
    }
    if (noticed) {
        RequestRedrawParty();
    }
}

void EvaluateCombatDifficulty(void)
{
    unsigned int hostile_experience = 0;
    unsigned int eligible_count = 0;
    unsigned int party_levels = 0;
    unsigned int monster_index;
    int slot;

    for (monster_index = 0; monster_index < CurrentLevelPopulation().monster_roster.live().size();
         ++monster_index) {
        W8MonsterInfo* monster = GetMonsterListEntry(monster_index);
        if (monster->pCombat && monster->ubDisposition == W8_DISPOSITION_HOSTILE &&
            monster->hp_current > 0 && monster->highest_condition < W8_CONDITION_DEAD) {
            unsigned int health_percent = monster->hp_current * 100 / monster->uiHPMax;
            hostile_experience +=
                GetMonsterExperience(GetMonsterDataForInfo(monster)) * health_percent / 100;
        }
    }
    unsigned int threat_level = EstimateCombatThreatLevel(hostile_experience);
    if (threat_level == 0) {
        threat_level = 1;
    }

    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[slot];
        W8Character* character = &CurrentGame().party.characters[slot];
        if (!CanPartySlotParticipate(slot)) {
            continue;
        }
        bool count_character = true;
        if (slot < 2) {
            W8NpcState* npc = GetNpcState(row->npc_index);
            if (npc != 0) {
                int faction = npc->record->faction;
                if (faction != 0 && faction != 1) {
                    for (unsigned int group_index = 0;
                         group_index < CurrentLevelPopulation().monster_groups.size();
                         ++group_index) {
                        W8MonsterGroup* group =
                            CurrentLevelPopulation().monster_groups[group_index].get();
                        if (group->members_active && group->fInCombat &&
                            group->ubDisposition == W8_DISPOSITION_HOSTILE &&
                            MonsterGroupGetRecord(group)->faction_id == faction) {
                            count_character = false;
                            break;
                        }
                    }
                }
            }
        }
        if (count_character) {
            ++eligible_count;
            party_levels += character->uiExpLevel;
        }
    }

    unsigned int party_power = 0;
    if (eligible_count > 0) {
        party_power = static_cast<unsigned int>(
            party_levels / pow(static_cast<double>(eligible_count) * 6.0f, 0.5) + 0.5);
    }
    int relative_strength = static_cast<int>(party_power * 100 / threat_level) - 100;
    W8Difficulty difficulty;
    if (relative_strength >= 20) {
        difficulty = W8_DIFFICULTY_NOVICE;
    } else if (relative_strength <= -20) {
        difficulty = W8_DIFFICULTY_EXPERT;
    } else {
        difficulty = W8_DIFFICULTY_NORMAL;
    }

    switch (difficulty) {
    case W8_DIFFICULTY_NOVICE:
        StartMusicResource("CombatEasy.MPL", 0, 1);
        break;
    case W8_DIFFICULTY_NORMAL:
        StartMusicResource("Combat.MPL", 0, 1);
        break;
    case W8_DIFFICULTY_EXPERT:
        StartMusicResource("CombatLousy.MPL", 0, 1);
        break;
    }
    if ((CurrentGame().combat.state() != 0 && CurrentGame().combat.state()->party_surprised) ||
        TimeUntilDeadline(recent_combat.until) == 0 || difficulty != recent_combat.difficulty) {
        unsigned int event_type;
        switch (difficulty) {
        case W8_DIFFICULTY_NOVICE:
            event_type = 29;
            break;
        case W8_DIFFICULTY_NORMAL:
            event_type = 30;
            break;
        default:
            event_type = 31;
            break;
        }
        ApplyItemEffectToRandomCharacter(event_type, -1, 0);
    }
    ++CurrentGame().status.combat_difficulty_counts[difficulty];
    recent_combat.difficulty = difficulty;

    for (slot = 0; slot < 2; ++slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[slot];
        if (row->fOccupied) {
            W8NpcState* npc = GetNpcState(row->npc_index);
            if (npc != 0) {
                W8Character* character = &CurrentGame().party.characters[slot];
                if (character->highest_condition == W8_CONDITION_DEAD) {
                    npc->item_assist = 0;
                } else if (npc->name_style == W8_NPC_VI_DOMINA ||
                           npc->name_style == W8_NPC_DRAZIC || npc->name_style == W8_NPC_RODAN) {
                    npc->healer_assist = 1;
                } else {
                    npc->item_assist = 1;
                }
            }
        }
    }
    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[slot];
        W8Character* character = &CurrentGame().party.characters[slot];
        if (row->fOccupied && character->uiCondition[W8_CONDITION_DEAD] == 0) {
            CurrentGame().monster_manager_entries[slot].condition_19_latch =
                character->uiCondition[W8_CONDITION_MISSING] != 0;
        }
    }
}

W8MonsterInfo* GetMonsterListEntry(std::ptrdiff_t index)
{
    if (!GetWorld() || index < 0 ||
        std::cmp_greater_equal(index, CurrentLevelPopulation().monster_roster.live().size())) {
        return nullptr;
    }
    return CurrentLevelPopulation().monster_roster.live()[index].get();
}

W8MonsterInfo* GetUnbornMonsterEntry(int index)
{
    if (!GetWorld() || index < 0 ||
        std::cmp_greater_equal(index, CurrentLevelPopulation().monster_roster.unborn().size())) {
        return nullptr;
    }
    return CurrentLevelPopulation().monster_roster.unborn()[index].get();
}
