#include "wiz8/combat/fight.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <optional>
#include <string_view>
#include <variant>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "message_box.h"
#include "monster_actions.h"
#include "sight_state.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/group_attacks.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/difficulty.h"
#include "wiz8/entities/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Noise.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSPartyMovement.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/music_playlist.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"

constexpr unsigned int kFleeChance = 15;

// GLOBAL: WIZ8 0x006836a8

/* A friendly NPC's combat-entry script notice was queued by
   StartCombat; while it is set the next call checks the dialogue and script
   deferral gates before clearing it and proceeding. */
/* The once-per-combat difficulty evaluation result - 0 easy,
   1 normal, 2 hard; picks the combat music and gates the victory event. */

/* The per-character combat rows live at +0x18 of the combat state and run
   0xd4 bytes apart; the state's leading 0x18 bytes are its own header. */
/* A hostile NPC's combat-start script notice is queued and not yet shown. */
static bool g_npc_combat_notice_pending;

void ResetPartyCombatRows(void);

/* Enter combat mode: queue a friendly NPC's combat-entry script notice when
   one is still owed, refuse while dialogue or a script event defers it, then
   reset the world, UI and per-monster state and create the fight. */
// FUNCTION: WIZ8 0x004E7090
bool Game::beginCombat(int surprise)
{
    W8MonsterInfo* monster_info;
    W8MonsterInfo* nearest_info;
    W8NpcState* npc;
    float nearest_distance;
    unsigned int index;

    if (!AnyCharacterActive()) {
        return false;
    }
    if (!HasLevelWalkableContact()) {
        return false;
    }
    if (status.world_cursor_gate != 0) {
        ClearMainGameTargetState();
    }
    if (!g_npc_combat_notice_pending) {
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            monster_info = GetMonsterListEntry(index);
            if (monster_info->pCombat && monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
                monster_info->hp_current > 0 &&
                monster_info->highest_condition < W8_CONDITION_PARALYZED) {
                npc = GetNpcStateForMonsterInfo(monster_info, false);
                if (npc != 0 && npc->record->combat_script_notice_enabled != 0) {
                    QueueNpcScriptNotice(npc, 0, -1, false, 0);
                    g_npc_combat_notice_pending = true;
                    BeginScriptedWorldAction();
                    break;
                }
            }
        }
    }
    if (g_npc_combat_notice_pending) {
        if (activity.conversation || CurrentGame().npc_dialogue.DefersCharacterEvents(false)) {
            return false;
        }
        g_npc_combat_notice_pending = false;
        ClearMainGameTargetState();
    }
    if (status.vi_event_stage == 1) {
        status.vi_event_stage = 2;
    }
    if (surprise_possible) {
        ResolveSurpriseHold();
    }
    if (GetViewDistance() != g_sight_default) {
        ResetSight();
    }
    if (auto* screen = MainGame()) screen->dismissOverlays(false);
    AutoSaveIfAllowed(true);
    ResetLevelDataVectors();
    if (!activity.conversation) {
        SelectTextBox(1);
    }
    ResetEditorStatusLine(1);
    // A new fight starts from zero in every field but these.
    Combat& fight = combat.emplace();
    fight.enemies_engaged = CurrentLevelPopulation().monster_roster.hostileCombatCount() > 0;
    fight.round_active = true;
    ApplyWorldAdvancement(*this, advancement.hold);
    party_movement_mode = false;
    events.push(CombatBegan{});
    CheckMonsterGroupsEnterCombat();
    events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x224], 1});
    RollCombatSurprise(static_cast<signed char>(surprise));

    nearest_distance = 999999.0f;
    nearest_info = 0;
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current > 0 &&
            monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
            float distance = monster_info->entity->GetDistanceToPlayer();
            if (distance < nearest_distance) {
                nearest_distance = distance;
                nearest_info = monster_info;
            }
        }
    }
    if (nearest_info != 0) {
        PointCameraAtMonster(nearest_info, false, true);
    }
    RefreshAllSight();
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat) {
            monster_info->action_kind = W8_MONSTER_ACTION_NONE;
            monster_info->action_detail = 0;
            monster_info->pCombat->phase = 0;
            monster_info->pCombat->active = true;
            monster_info->pCombat->settle_ticks = 0;
            CurrentGame().events.push(CombatProgressChanged{});
            TurnMonsterToNewTarget(*monster_info);
            monster_info->death_processed = false;
        }
    }
    ResetPartyCombatRows();
    DetectMonsterGroups();
    CurrentGame().events.push(MonstersChanged{});
    planned_formation = party.formation;
    SaveCombatFormation();
    search.fightBegan();
    if (IsLevelFastMovement()) {
        ClearLevelFastMovement();
    }
    if (g_settings.continuous_combat && !fight.party_surprised) {
        fight.continuous_start_delay =
            std::chrono::milliseconds{g_settings.continuous_combat_start_delay_ms};
    }
    wiz8::sound::play("Data\\Sound\\Misc\\Ready Weapons.wav");
    return true;
}

/* Reset every occupied party slot's combat bookkeeping on combat entry: clear
   the pending and attack-mode words, empty both combat target slots, re-commit
   the chosen action, and mark the combat row when the character cannot act. */
void ResetPartyCombatRows(void)
{
    for (unsigned int slot = 0; slot < 8; ++slot) {
        if (!CurrentGame().party.members[slot].has_value()) {
            continue;
        }
        auto& row = CurrentGame().party.rows[slot];
        row.executing.start(W8_ACTION_NONE, -1);
        ResetCombatSlot(&row.executing.target);

        ResetCombatSlot(&row.committed.target);
        CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(slot)});
        if (slot == static_cast<unsigned int>(CurrentGame().status.selected_character)) {
            CurrentGame().events.push(MonstersChanged{});
        }
        SetCharacterCombatAction(slot, row.remembered.basic(), 0);
        CurrentGame().combat->characters[slot].combat_status = W8_COMBAT_PORTRAIT_EMPTY;
        CurrentGame().combat->characters[slot].portrait_image = -1;
        CurrentGame().combat->characters[slot].portrait_image_alternate = -1;
        CurrentGame().combat->characters[slot].dead =
            CurrentGame().party.characters[slot].hp_current == 0 ||
            CurrentGame().party.characters[slot].highest_condition >= W8_CONDITION_ASLEEP;
        CurrentGame().party.characters[slot].conditions[W8_DEPENDENCE_SUMMON].active = false;
        CurrentGame().party.characters[slot].conditions[W8_DEPENDENCE_SUMMON].level_acquired = 0;
        CurrentGame().party.characters[slot].conditions[W8_DEPENDENCE_SUMMON].source_monster = 0;
    }
}

bool AnyCharacterEngaged(void)
{
    unsigned int party_slot;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (IsPartySlotEligible(party_slot)) {
            return true;
        }
    }
    return false;
}

bool CombatMayAdvanceContinuously(void)
{
    if (CurrentGame().combat.has_value()) {
        for (int slot = 0; slot < 2; ++slot) {
            W8Character* character = &CurrentGame().party.characters[slot];
            if (CurrentGame().party.members[slot].has_value() && character->hp_current != 0 &&
                character->highest_condition < W8_CONDITION_ASLEEP &&
                CurrentGame().combat->npc_combat_script_pending[slot]) {
                return false;
            }
        }
    }
    if (AnyMonsterEngaged()) {
        return true;
    }
    for (int slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[slot];
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.members[slot].has_value() && character->hp_current != 0 &&
            CharacterCanSwitchTo(slot, SelectCombatAction(slot, W8_TARGETING_CONTEXT_IN_COMBAT),
                                 false) &&
            CharacterActionTargetsEnemies(CurrentGame().inventory, character, row->committed.kind,
                                          row->committed.detail, &row->committed.detail_block)) {
            return true;
        }
    }
    return false;
}

/* True when some monster in combat satisfies the predicate. */
template<class Predicate>
static bool AnyCombatMonster(Predicate predicate)
{
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        const W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster->fActive && monster->pCombat && predicate(monster)) {
            return true;
        }
    }
    return false;
}

bool QueueNpcCombatScript(void)
{
    if (!CurrentGame().combat.has_value()) {
        return false;
    }
    for (int slot = 0; slot < 2; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (!CurrentGame().party.members[slot].has_value() || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_INSANE ||
            CurrentGame().combat->npc_combat_script_pending[slot]) {
            continue;
        }
        W8NpcState* npc = GetNpcState(CurrentGame().party.npcIndex(slot));
        if (npc == 0) {
            continue;
        }
        int faction = npc->record->faction;
        bool hostile_group_present = false;
        if (faction != 0 && faction != 1) {
            for (unsigned int index = 0; index < CurrentLevelPopulation().monster_groups.size();
                 ++index) {
                W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[index].get();
                if (group->members_active && group->fInCombat &&
                    group->ubDisposition == W8_DISPOSITION_HOSTILE &&
                    MonsterGroupGetRecord(group)->faction_id == faction) {
                    hostile_group_present = true;
                    break;
                }
            }
        }
        if (!hostile_group_present && npc->name_style == W8_NPC_SPARKLE) {
            for (unsigned int index = 0; index < CurrentLevelPopulation().monster_groups.size();
                 ++index) {
                W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[index].get();
                if (group->members_active && group->fInCombat &&
                    group->ubDisposition == W8_DISPOSITION_HOSTILE &&
                    MonsterGroupGetRecord(group)->faction_id == W8_FACTION_UMPANI) {
                    hostile_group_present = true;
                    break;
                }
            }
        }
        if (hostile_group_present && CanPlaceNpcNearParty(slot)) {
            BeginScriptedWorldAction();
            ShowString(fmt::format("{} {}", npc->record->source_name, Data().strings[0x272]));
            CurrentGame().npc_dialogue.Queue(W8NpcPartyScriptLine{slot});
            CurrentGame().npc_dialogue.Queue(W8NpcLeaveCombat{slot});
            CurrentGame().combat->npc_combat_script_pending[slot] = true;
            return true;
        }
    }
    return false;
}

bool CombatHasContinuingEffects(void)
{
    unsigned int enchantment;
    unsigned int slot;
    unsigned int index;

    for (enchantment = 0; enchantment < 8; ++enchantment) {
        for (slot = 0; slot < 8; ++slot) {
            if (CurrentGame().party.members[slot].has_value() &&
                CurrentGame().party.characters[slot].enchantments[enchantment].turns > 0) {
                return true;
            }
        }
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            W8MonsterInfo* monster = GetMonsterListEntry(index);
            if (monster->enchantments[enchantment].turns > 0) {
                return true;
            }
        }
    }
    for (slot = 0; slot < 9; ++slot) {
        if (CurrentGame().combat->effect_slots[slot].active) {
            return true;
        }
        if (AnyCombatMonster([slot](const W8MonsterInfo* monster) {
                return monster->pCombat->combat_effects[slot].active;
            })) {
            return true;
        }
    }
    for (slot = 0; slot < 6; ++slot) {
        if (CurrentGame().combat->effect_slots0[slot].active) {
            return true;
        }
        if (AnyCombatMonster([slot](const W8MonsterInfo* monster) {
                return monster->pCombat->combat_effects_2[slot].active;
            })) {
            return true;
        }
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster->fActive && monster->hp_current != 0 &&
            monster->uiCondition[W8_CONDITION_DEAD] == 0 &&
            monster->summoned != W8_MONSTER_SUMMON_NONE) {
            return true;
        }
    }
    return false;
}

void ApplyCombatEndEffects(void)
{
    int slot;
    for (slot = 0; slot < 2; ++slot) {
        if (CurrentGame().party.members[slot].has_value()) {
            W8NpcState* npc = GetNpcState(CurrentGame().party.npcIndex(slot));
            if (npc != 0) {
                W8Character* character = &CurrentGame().party.characters[slot];
                if (character->highest_condition == W8_CONDITION_DEAD) {
                    if (npc->healer_assist) {
                        ApplyItemEffectToRandomCharacter(13, -1, 0);
                    } else if (npc->item_assist) {
                        ApplyItemEffectToRandomCharacter(20, -1, 0);
                    }
                }
            }
        }
    }
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.members[slot].has_value() &&
            character->uiCondition[W8_CONDITION_DEAD] == 0 &&
            character->uiCondition[W8_CONDITION_MISSING] != 0 &&
            !CurrentGame().monster_manager_entries[slot].condition_19_latch &&
            (!CurrentGame().status.condition13_clock ||
             CurrentGame().status.pending_condition_party_slot != slot)) {
            ApplyItemEffectToRandomCharacter(40, slot, 0);
        }
    }
}

void BeginCombatExecution(void)
{
    int slot;
    unsigned int index;
    if ((CurrentGame().combat->round_count == 0 && CurrentGame().combat->enemies_engaged) ||
        CurrentGame().combat->uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) {
        AlertWorldNoise();
    }
    CurrentGame().events.push(CombatExecutionBegan{});
    CurrentGame().combat->execution_active = 1;
    CurrentGame().combat->pending_death_count = 0;
    CurrentGame().combat->passive_round = 1;
    CurrentGame().combat->target_picked = false;
    CurrentGame().combat->uiCurrentPartyAction = CurrentGame().combat->uiNextPartyAction;
    CurrentGame().combat->uiCurrentPartyActionStatus = W8_PARTY_ACTION_NOT_STARTED;
    UpdatePartyMovementControl();
    if (CurrentGame().combat->uiNextPartyAction != W8_PARTY_ACTION_NONE) {
        ClearPendingPartyMovement();
    }
    if (!g_settings.continuous_combat) {
        CurrentGame().combat->round_active = false;
        ClearCombatSelection();
    }
    ++CurrentGame().combat->round_count;
    ResetRoundPartyMovement();
    CurrentGame().combat->round_counter = GetPhaseStep();
    if (CombatMayAdvanceContinuously()) {
        CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, {}});
        CurrentGame().events.push(
            TextNotice{W8_FONT_PALETTE_BEIGE,
                       fmt::sprintf(Data().strings[0x227], CurrentGame().combat->round_count)});
    }

    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        W8CombatCharacterRow* row = &CurrentGame().combat->characters[slot];
        if (CurrentGame().party.members[slot].has_value()) {
            row->alternate_hand = false;
            row->berserk = false;
            row->spot_attempts = 0;
            row->pending_action_repick_count = 0;
            row->interception_count = 0;
            row->dead =
                character->hp_current == 0 || character->highest_condition >= W8_CONDITION_ASLEEP;
            row->phase_clock_stamp = 0;
            row->defend_switched = false;
        }
    }
    SettleActiveParty(CurrentGame());
    RepickInvalidCombatTargets();
    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* party = &CurrentGame().party.rows[slot];
        W8CombatCharacterRow* row = &CurrentGame().combat->characters[slot];
        if (CurrentGame().party.members[slot].has_value()) {
            row->extra_swings[0] = 0;
            if (party->executing.kind == W8_ACTION_ATTACK) {
                W8Character* character = &CurrentGame().party.characters[slot];
                if (CharacterHasTrait(character, W8_TRAIT_LIGHTNING_STRIKE) &&
                    character->Hand[0].weapon_skill == W8_SKILL_SWORD &&
                    CurrentGame().rng.below(100) <
                        static_cast<unsigned char>(ScaleValueByProfessionLevel(
                            character, W8_TRAIT_LIGHTNING_STRIKE, 12.0f))) {
                    row->extra_swings[0] = 1;
                }
            }
        }
    }

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster->pCombat) {
            W8MonsterCombatState* state = monster->pCombat.get();
            state->berserk = false;
            state->spot_attempts = 0;
            state->pending_action_repick_count = 0;
            state->interception_count = 0;
            state->active = false;
            state->turn_started = false;
            ++state->settle_ticks;
            state->reconsider_action = CurrentGame().rng.below(100) < 75;
        }
    }
    ++CurrentGame().combat->combat_update_count;
    UpdateAllMonsterAI();
    AssignCombatPhases();
    CurrentGame().events.push(CombatProgressChanged{.whole_party = true});
    CurrentGame().events.push(CombatStanceChanged{});

    if (g_settings.verbose_combat_messages &&
        CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE) {
        for (slot = 0; slot < 8; ++slot) {
            W8PartySlotRow* party = &CurrentGame().party.rows[slot];
            if (!IsPartySlotEligible(slot) ||
                !CharacterCanSwitchTo(
                    slot, SelectCombatAction(slot, W8_TARGETING_CONTEXT_IN_COMBAT), false)) {
                continue;
            }
            if (party->committed.kind == W8_ACTION_DEFEND) {
                CurrentGame().events.push(
                    CharacterNotice{static_cast<unsigned int>(slot), Data().strings[0x228]});
            }
            if (party->committed.kind == W8_ACTION_PROTECT) {
                if (party->committed.target.iType == W8_TARGET_KIND_CHARACTER) {
                    CurrentGame().events.push(CharacterNotice{
                        static_cast<unsigned int>(slot),
                        fmt::sprintf(
                            Data().strings[0x229],
                            CurrentGame().party.characters[party->committed.target.iChar].name)});
                } else if (party->committed.target.iType == W8_TARGET_KIND_MONSTER) {
                    W8MonsterInfo* target = FindMonster(party->committed.target.iMonsterID);
                    CurrentGame().events.push(CharacterNotice{
                        static_cast<unsigned int>(slot),
                        fmt::sprintf(Data().strings[0x229], GetMonsterName(target, 0, 0))});
                }
            }
        }
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (!monster->pCombat || monster->hp_current == 0 ||
            monster->highest_condition >= W8_CONDITION_WEBBED) {
            continue;
        }
        if (monster->action_kind == W8_MONSTER_ACTION_WAIT) {
            PostMonsterNotice(monster, Data().strings[0x228]);
        }
        if (monster->action_kind == W8_MONSTER_ACTION_PROTECT) {
            if (monster->Target.iType == W8_TARGET_KIND_MONSTER) {
                W8MonsterInfo* target = FindMonster(monster->Target.iMonsterID);
                PostMonsterNotice(
                    monster, fmt::sprintf(Data().strings[0x229], GetMonsterName(target, 0, 0)));
            } else if (monster->Target.iType == W8_TARGET_KIND_CHARACTER) {
                PostMonsterNotice(
                    monster,
                    fmt::sprintf(Data().strings[0x229],
                                 CurrentGame().party.characters[monster->Target.iChar].name));
            }
        }
    }
    CurrentGame().combat->pacing_latch = true;
}

/* The queued party action after the active movement phase has started,
   otherwise the current party action. */
W8PartyAction GetEffectivePartyAction(void)
{
    if (CurrentGame().combat->uiCurrentPartyActionStatus != W8_PARTY_ACTION_NOT_STARTED) {
        return CurrentGame().combat->uiNextPartyAction;
    }
    return CurrentGame().combat->uiCurrentPartyAction;
}

/* Whether the party is engaged at all. Being told so outright settles it;
   otherwise, with nothing derived, one conscious character whose combat row is
   flagged is enough. */
bool IsPartyEngaged(void)
{
    unsigned int party_slot;

    if (CurrentGame().combat->uiCurrentPartyActionStatus != W8_PARTY_ACTION_NOT_STARTED) {
        return true;
    }
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE) {
        for (party_slot = 0; party_slot < 8; ++party_slot) {
            if (CurrentGame().party.members[party_slot].has_value() &&
                CurrentGame().party.characters[party_slot].hp_current != 0 &&
                CurrentGame().party.characters[party_slot].highest_condition <
                    W8_CONDITION_ASLEEP &&
                CurrentGame().combat->characters[party_slot].dead) {
                return true;
            }
        }
    }
    return false;
}

void RecordCharacterDeath(int party_slot)
{
    if (CurrentGame().combat.has_value()) {
        ++CurrentGame().combat->pending_death_count;
    }
}

/* Start a fresh round: lift the one action that does not survive it and clear
   the two round flags. */
void BeginCombatRound(void)
{
    unsigned int party_slot;

    if (!CurrentGame().combat.has_value()) {
        return;
    }
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.rows[party_slot].executing.kind == W8_ACTION_EQUIP) {
            CurrentGame().party.rows[party_slot].executing.kind = W8_ACTION_NONE;
        }
    }
    CurrentGame().combat->equip_phase = W8CombatEquipPhase::action_selection;
    CurrentGame().combat->equip_pending = 0;
}

/* Bring one combatant's clock up to the current round: advance it by however
   many rounds have passed since it last caught up, clamp it, and remember
   where it got to. */
void CatchUpCombatActor(W8CombatCharacterRow* row)
{
    row->phase += CurrentGame().combat->round_counter - row->phase_clock_stamp;
    row->phase = row->phase > 100u
                     ? 100u
                     : std::max<unsigned>(row->phase, CurrentGame().combat->round_counter);
    RoundPhaseToStep(&row->phase, CurrentGame().combat->round_counter);
    row->phase_clock_stamp = CurrentGame().combat->round_counter;
}

/* Whether one character can breathe again. The name is the Magic.cpp:5320
   assertion's own - assert(CanCharReBreathe(uiChar)) - and the rule is that
   they have to be free of the condition that forbids it and still hold a fifth
   of their stamina, the same fifth a run costs. */
bool CanCharReBreathe(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    if (!CharacterHasTrait(character, W8_TRAIT_BREATHE)) {
        return false;
    }
    return static_cast<int>(static_cast<unsigned int>(character->uiStaminaMax) / 5) <=
           character->stamina;
}

/* Take one character out of the round: hand back whatever they were aiming at,
   clear the slot, and re-choose an action of the same kind. */
void DropCharacterFromRound(int party_slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];

    ResetCombatSlot(&row->committed.target);
    CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
    if (party_slot == CurrentGame().status.selected_character) {
        CurrentGame().events.push(MonstersChanged{});
    }
    SetCharacterCombatAction(party_slot, row->remembered.basic(), 0);
}

/* Tell every monster within short range about something. A monster has to be
   in combat, alive, not on its way out, free of whatever 0x087 records, and
   in the engaged state before it is told. */
void NotifyNearbyMonsters(int what)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat && monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_WEBBED &&
            monster_info->uiCondition[W8_CONDITION_BLIND] == 0 &&
            monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
            if (monster_info->entity->GetDistanceToPlayer() <= CalcRangeDistance(W8_RANGE_SHORT)) {
                monster_info->entity->ForwardReferencePosition(what);
            }
        }
    }
}

/* Whether the party notices what is coming. The first character in shape to
   act rolls against half their sixth attribute; nobody in shape at all, or the
   global being clear, and the answer is yes by default. */
int PartyAvoidsSurprise(void)
{
    unsigned int party_slot = 0;

    if (!CurrentGame().surprise_possible) {
        return 0;
    }
    while (!CurrentGame().party.members[party_slot].has_value() ||
           CurrentGame().party.characters[party_slot].hp_current == 0 ||
           CurrentGame().party.characters[party_slot].highest_condition > W8_CONDITION_INFATUATED) {
        ++party_slot;
        if (party_slot > 7) {
            return 1;
        }
    }
    CurrentGame().rng.below(1);
    if (CurrentGame().party.characters[party_slot].attributes[W8_ATTRIBUTE_SENSES].effective >> 1 <=
        CurrentGame().rng.below(100)) {
        return 1;
    }
    return 0;
}

/* What one character's whole turn is worth. A character whose turn combat has
   already set up uses the values it saved; anyone else is asked afresh. A
   phase of exactly a hundred is worth one whatever the hands say. */
int GetCharacterTurnValue(int party_slot)
{
    W8CombatCharacterRow* row = &CurrentGame().combat->characters[party_slot];
    int total = 0;
    unsigned int hand;
    int value;

    const auto chosen =
        SelectCombatAction(party_slot, !row->dead ? W8_TARGETING_CONTEXT_IN_COMBAT
                                                  : W8_TARGETING_CONTEXT_OUT_OF_COMBAT)
            .kind;
    if (chosen != W8_ACTION_ATTACK && chosen != W8_ACTION_BERSERK) {
        return 1;
    }

    for (hand = 0; hand < 2; ++hand) {
        if (!row->dead) {
            value = GetHandAttackValue(party_slot, hand);
        } else {
            value = row->saved_attack_value[hand];
        }
        if (row->phase == 100) {
            value = 1;
        }
        total += value;
    }
    return total;
}

/* Whether a wounded character panics. Only a character target counts, they
   have to be below the fraction of their hit points that triggers it, and then
   it is a roll - so the same wound does not always panic. */
bool TryPanicWoundedCharacter(const W8CombatSlot* target)
{
    W8Character* character;

    if (target->iType != W8_TARGET_KIND_CHARACTER) {
        return false;
    }
    character = &CurrentGame().party.characters[target->iChar];
    if ((character->hp_current * 100) / static_cast<unsigned int>(character->uiHPMax) >=
        kFleeHpFraction) {
        return false;
    }
    if (CurrentGame().rng.below(100) >= kFleeChance) {
        return false;
    }
    QueueCharacterEvent(character, 34, 0, kCharacterEventFullVolume);
    return true;
}

/* End one monster's turn: forget what it was doing, mark its combat state
   inactive, and - if it is still in the fight - either stand it down or turn
   it to face whoever it settled on. */
void EndMonsterTurn(W8MonsterInfo* monster_info)
{
    monster_info->action_kind = W8_MONSTER_ACTION_NONE;
    monster_info->action_detail = 0;
    monster_info->pCombat->phase = 0;
    monster_info->pCombat->active = true;
    monster_info->pCombat->settle_ticks = 0;
    CurrentGame().events.push(CombatProgressChanged{});

    TurnMonsterToNewTarget(*monster_info);
    monster_info->death_processed = false;
}

void TurnMonsterToNewTarget(W8MonsterInfo& monster_info)
{
    if (monster_info.hp_current == 0 || monster_info.highest_condition >= W8_CONDITION_WEBBED ||
        monster_info.uiCondition[W8_CONDITION_BLIND] != 0) {
        return;
    }
    W8CombatSlot chosen;
    MonsterChooseTarget(&monster_info, &chosen, W8VisibilityCondition::ThirdLineOfSightFlag);
    if (chosen.iType == W8_TARGET_KIND_PARTY) {
        monster_info.entity->ForwardReferencePosition(0);
    } else if (chosen.iType == W8_TARGET_KIND_MONSTER) {
        if (Monster* target = GetMonsterByLocationID(chosen.iMonsterID))
            monster_info.entity->AimAtMonster(*target, false);
    }
}

/* Set one monster's turn up, once. How fast it moves through the turn depends
   on what it chose - fleeing is half speed and one action is half again - and
   an enchanted monster is quickened by ten per point instead of slowed by the
   condition it is under. */
void SetUpMonsterTurn(W8MonsterInfo* monster_info)
{
    unsigned int speed = 100;
    float scale;

    if (monster_info->pCombat->turn_started) {
        monster_info->entity->nav.movement_complete = false;
        return;
    }

    scale = GetMonsterCombatMoveRange(monster_info);
    if (monster_info->action_kind == W8_MONSTER_ACTION_CONTROLLED_MOVE) {
        speed = 0x32;
    } else {
        if (monster_info->action_kind == W8_MONSTER_ACTION_BACK_OFF) {
            speed = 0x96;
        }
        if (monster_info->enchantments[W8_ENCHANTMENT_HASTE].turns == 0) {
            if (monster_info->uiCondition[W8_CONDITION_SLOWED] != 0) {
                speed -= 0x32;
            }
        } else {
            speed += monster_info->enchantments[W8_ENCHANTMENT_HASTE].power * 10;
        }
    }

    monster_info->entity->nav.SetMonsterTurnSpeed(speed * scale * kMovementSpeedStep);
    /* Four bytes inside cycle eight's block, cleared together. */
    monster_info->entity->nav.movement.callback_progress = 0.0f;
    monster_info->pCombat->turn_started = true;
    monster_info->entity->nav.movement_complete = false;
}

/* Finish one monster's attack: charge it the fatigue, drop the party's
   selection, and give it its next phase if it still has attacks left - which
   divides whatever is left of the round between them. */
void EndMonsterAttack(W8MonsterInfo* monster_info)
{
    W8MonsterCombatState* combat = monster_info->pCombat.get();
    unsigned int next;

    GetMonsterDataForInfo(monster_info);
    FatigueMonster(monster_info, MonsterActionFatigueCost(monster_info), 0);
    monster_info->entity->nav.SetHalted(true);
    CurrentGame().combat->endTurnOf(MonsterActor{monster_info->location_id});

    if (!combat->active) {
        return;
    }
    if (monster_info->action_kind == W8_MONSTER_ACTION_ATTACK && combat->attacks_per_round != 0) {
        if (ChooseRandomMonsterAction(monster_info, true, false, false)) {
            next = combat->phase +
                   (100 - CurrentGame().combat->round_counter) / (combat->attacks_per_round + 1);
            combat->phase = next;
            if (next < 0x65) {
                RoundPhaseToStep(&combat->phase, CurrentGame().combat->round_counter);
                return;
            }
        }
    }
    combat->phase = 0;
}

/* Whether one character may take an action, and take it if asked. A character
   whose turn combat has set up already only agrees to the action they are
   already on; anyone else may switch, except into the fourth action while
   something else forbids it. */
bool TryCharacterAction(int party_slot, W8ActionKind action, bool commit)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    if (character->hp_current == 0 || character->highest_condition >= W8_CONDITION_ASLEEP) {
        return false;
    }
    if (CurrentGame().combat->characters[party_slot].dead) {
        return CurrentGame().party.rows[party_slot].executing.kind == action;
    }
    if (CurrentGame().party.rows[party_slot].committed.kind != action) {
        if (action != W8_ACTION_DEFEND) {
            return false;
        }
        if (CharacterCanSwitchTo(party_slot,
                                 SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT),
                                 false)) {
            return false;
        }
    }
    if (commit) {
        SwitchCharacterTo(party_slot, action);
    }
    return true;
}

/* Put one character on the defend or protect action: record the action on the
   slot row, clear both hands' chosen attack modes, and mark the combat row so
   the target display rebuilds. Protect additionally copies the current target
   into the out-of-combat target slot, and a switch to defend raises the
   combat row's attack flag unless the row is already on that action. */
void SwitchCharacterTo(int party_slot, W8ActionKind action)
{
    assert(action == W8_ACTION_DEFEND || action == W8_ACTION_PROTECT);
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];

    row->executing.start(action, -1);
    if (action == W8_ACTION_PROTECT) {
        row->executing.target = row->committed.target;
    }
    CurrentGame().combat->characters[party_slot].dead = true;
    CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
    CurrentGame().events.push(CombatProgressChanged{});
    if (action == W8_ACTION_DEFEND && row->committed.kind != action) {
        CurrentGame().combat->characters[party_slot].defend_switched = true;
    }
}

/* Take the party out of combat: end the free-turn phase and the current round,
   drop the temporary conditions, finish every engaged monster group, fold the
   surviving characters back into exploration state, release the combat state
   and restore the main-game UI. The mode passes through to the end-of-combat
   monster pass and gates the world reset when zero. */
// FUNCTION: WIZ8 0x004ea310
void Game::endCombat(bool forced_cleanup)
{
    if (party_movement_mode) {
        BeginFreeTurnPhase();
    }
    ReleasePartyMovement();
    CurrentGame().events.push(PartyEffectsChanged{});

    CurrentGame().events.push(TargetingRequested{});
    RemoveConditionFromEveryone(W8_CONDITION_SLOWED);
    LiftConditionFromParty(*this, W8_CONDITION_TURNCOAT);
    RemoveAllEnchantments();
    ResetCombatEffects();
    ProcessMonstersAtCombatEnd(forced_cleanup);
    auto group_count = CurrentLevelPopulation().monster_groups.size();
    for (unsigned int group_index = 0; group_index < group_count; ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[group_index].get();
        if (group->fInCombat) {
            MonsterGroupLeaveCombat(group);
        }
        group_count = CurrentLevelPopulation().monster_groups.size();
    }
    if (combat->enemies_engaged) {
        events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x233], 1});
    }
    unsigned int active = CountActiveCharacters();
    if (active != 0 && combat->experience_pool != 0) {
        if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT) {
            status.level_progress[GetLoadedLevelID()].combat_end_count += 1;
        }
        combat->experience_pool /= active;
        AwardPartyExperience(combat->experience_bonus + combat->experience_pool, true);
        std::ranges::replace(status.status_ints, 1, 2);
    }
    RecordCombatStart();
    if (combat->uiNextPartyAction != W8_PARTY_ACTION_NONE) {
        ClearPendingPartyMovement();
    }
    if (auto* screen = MainGame()) screen->dismissOverlays(false);
    RestoreCombatFormation();
    ReconcilePartyEquipmentAfterCombat();
    // The fight is over; what follows restores the world around it.
    combat.reset();
    search.fightEnded();
    if (auto* screen = MainGame(); screen && screen->radar_map) screen->radar_map->setZoom(false);
    events.push(CombatEnded{});
    if (!activity.conversation) {
        SelectTextBox(0);
    }
    ApplyWorldAdvancement(*this, advancement.hold);
    ResetLivingMonstersAfterCombat();
    // Armor without the combat actions, and the shared load deferred during combat.
    SettleActiveParty(*this);
    ClearLevelMovementState();
    CurrentGame().events.push(MonstersChanged{});
    if (auto* world = level.primary.get()) ResetWorldCursorDistance(*world);
    if (activity.in_world()) {
        ApplyCombatEndEffects();
        ReportSaveFailed(true);
    }
}

/* Record a chosen action on the slot row: outside combat it fills the
   pending-action block, inside combat it fills the chosen-action record and
   dispatches the action's follow-up. */
void ChooseAction(int party_slot, W8ActionKind action, int detail, const W8ActionDetailBlock* data,
                  bool defer_execution, int notify)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];

    if (!CurrentGame().combat.has_value()) {
        row->executing.start(action, detail);
        row->executing.detail_block = data ? *data : W8ActionDetailBlock{};
        if (!defer_execution) {
            ExecuteCharacterAction(CurrentGame(), party_slot);
        }

    } else {
        ApplyPartyCombatAction(party_slot, action, detail, data, notify);
        switch (action) {
        case W8_ACTION_ATTACK:
        case W8_ACTION_BERSERK:
        case W8_ACTION_DEFEND:
        case W8_ACTION_PROTECT:
            row->remembered.chooseBasic(action, detail);
            break;
        default:
            break;
        }
    }
    switch (action) {
    case W8_ACTION_TURN_UNDEAD:
    case W8_ACTION_DEFEND:
    case W8_ACTION_PRAY:
    case W8_ACTION_EQUIP:
        StartBreathCycle(party_slot, false);
        return;
    case W8_ACTION_WALK:
    case W8_ACTION_RUN:
        ApplyItemEffectToRandomCharacter(g_special_event2, -1, 0);
        break;
    default:
        break;
    }
}

/* Apply a chosen in-combat action for party-move kinds 10/11, otherwise record
   the action on the slot row and refresh targeting UI state. */
void ApplyPartyCombatAction(int party_slot, W8ActionKind action, int detail,
                            const W8ActionDetailBlock* data, int notify)
{
    unsigned int party_slot_index;
    W8Character* character;
    W8CombatCharacterRow* row;

    if (action != W8_ACTION_WALK && action != W8_ACTION_RUN) {
        W8Action choice{action, detail};
        if (data) choice.detail_block = *data;
        SetCharacterCombatAction(party_slot, choice, notify);
        return;
    }
    if (CurrentGame().combat->execution_active == 0 ||
        CurrentGame().combat->uiCurrentPartyActionStatus != W8_PARTY_ACTION_NOT_STARTED) {
        SetPendingMoveKind(action);
    } else {
        bool deferred = false;

        if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE) {
            for (party_slot_index = 0; party_slot_index < 8; ++party_slot_index) {
                character = &CurrentGame().party.characters[party_slot_index];
                row = &CurrentGame().combat->characters[party_slot_index];
                if (CurrentGame().party.members[party_slot_index].has_value() &&
                    character->hp_current != 0 &&
                    character->highest_condition < W8_CONDITION_ASLEEP && row->dead) {
                    deferred = true;
                    break;
                }
            }
        }
        if (deferred) {
            SetPendingMoveKind(action);
        } else {
            /* A party slot waiting on the action clock gives way to the move. */
            Combat& state = *CurrentGame().combat;
            if (const auto* scheduled = std::get_if<TurnScheduled>(&state.turn());
                scheduled && std::holds_alternative<PartyActor>(scheduled->actor)) {
                state.endTurnOf(scheduled->actor);
            }
            StartPartyMovementAction(action == W8_ACTION_WALK ? W8_PARTY_ACTION_WALK
                                                              : W8_PARTY_ACTION_RUN);
        }
    }
    ShowPartyMovementPanel();
    CurrentGame().events.push(TargetingRequested{});
}

/* Choosing an attack or berserk readies the primary weapon (and its
   ammunition) when the hands as equipped cannot carry the action out. */
static void ReadyWeaponForAction(int party_slot, W8ActionKind action)
{
    if ((action == W8_ACTION_ATTACK && !CanAnyHandReachTarget(party_slot)) ||
        (action == W8_ACTION_BERSERK && !CanCharacterBerserk(party_slot))) {
        CurrentGame().inventory.readyPrimaryWeapon(party_slot);
    }
}

/* Record the chosen in-combat action on the slot row, copy its detail block,
   then aim and validate that choice for a still-active character. A notified
   choice is the player's selection, which readies the weapon it needs first. */
void SetCharacterCombatAction(int party_slot, W8Action next, int notify)
{
    auto& committed = CurrentGame().party.rows[party_slot].committed;
    const auto previous_action = committed.kind;
    const auto action_kind = next.kind;
    W8TargetSource source;
    W8CombatCharacterRow* row = &CurrentGame().combat->characters[party_slot];
    SetTargetSourceToCharacter(party_slot, &source);
    assert(action_kind <= W8_ACTION_RUN);
    committed = next;
    if (action_kind != W8_ACTION_ATTACK && action_kind != W8_ACTION_BERSERK &&
        action_kind != W8_ACTION_DEFEND && action_kind != W8_ACTION_PROTECT &&
        action_kind != W8_ACTION_WALK && action_kind != W8_ACTION_RUN)
        CurrentGame().party.rows[party_slot].remembered.repeat_action = action_kind;
    row->action_changed = true;
    W8Character* character = &CurrentGame().party.characters[party_slot];
    if (character->hp_current != 0 && character->highest_condition < W8_CONDITION_TURNCOAT &&
        action_kind != W8_ACTION_NONE) {
        if (notify != 0) {
            ReadyWeaponForAction(party_slot, action_kind);
        }
        if (!CharacterCanSwitchTo(
                party_slot, SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT), true)) {
            W8CombatSlot none;
            ResetCombatSlot(&none);
            AimStoredAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, none);
        } else {
            if (!TargetIsInPlay(party_slot, 2,
                                SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT)) &&
                RepickActionTarget(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, notify)) {
                PointCameraAtCombatTarget(&source,
                                          &CurrentGame().party.rows[party_slot].committed.target);
            }
            if (notify != 0) {
                RevalidateSelectedTarget(party_slot);
            }
        }
    }
    if (!CurrentGame().combat.has_value()) {
        return;
    }
    if (action_kind != previous_action && CurrentGame().combat->execution_active != 0 &&
        !row->dead) {
        CatchUpCombatActor(row);
    }
    CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
    CurrentGame().combat->target_picked = false;
    if (action_kind == W8_ACTION_EQUIP) {
        CurrentGame().events.push(
            CharacterNotice{static_cast<unsigned int>(party_slot), Data().strings[0x225]});
    } else if (previous_action == W8_ACTION_EQUIP &&
               !(CurrentGame().combat->actingSlot() == party_slot &&
                 CurrentGame().party.rows[party_slot].executing.kind == W8_ACTION_EQUIP)) {
        CurrentGame().events.push(
            CharacterNotice{static_cast<unsigned int>(party_slot), Data().strings[0x226]});
    }
    SettleActiveParty(CurrentGame());
}

void ReconcileCombatHandChange(Game& game, unsigned int party_slot)
{
    if (!game.combat.has_value()) return;
    const W8ActionKind kind = game.party.rows[party_slot].committed.kind;
    if (kind == W8_ACTION_ATTACK || kind == W8_ACTION_BERSERK) {
        // The new hands keep or re-aim the attack; picking another weapon set is
        // left to the commands that prepare an action, so settling never swaps.
        const auto action = SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
        W8CombatSlot none;
        ResetCombatSlot(&none);
        if (!CharacterCanSwitchTo(party_slot, action, true)) {
            AimStoredAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, none);
        } else if (!TargetIsInPlay(party_slot, 2, action)) {
            AimStoredAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT,
                            ChooseEquippedMonsterTarget(party_slot, action).value_or(none));
        }
    }
    game.combat->characters[party_slot].alternate_hand ^= 1;
    if (party_slot == static_cast<unsigned int>(game.status.selected_character))
        game.events.push(CombatPanelChanged{});
}

namespace {
/* The checks that come before the action itself: the slot has to be able to
   act at all. */
bool CanSlotAct(int party_slot)
{
    return CanPartySlotParticipate(party_slot) &&
           CurrentGame().party.characters[party_slot].highest_condition <= W8_CONDITION_TURNCOAT;
}

/* Whether the slot can carry out a fully specified action with its equipment
   as it is. */
bool CanPerformAction(int party_slot, const W8Action& selected, bool allow_missing_target)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    switch (selected.kind) {
    case W8_ACTION_NONE:
        return false;
    case W8_ACTION_ATTACK:
        if (!CanAnyHandReachTarget(party_slot)) {
            return false;
        }
        break;
    case W8_ACTION_BERSERK:
        if (!CanCharacterBerserk(party_slot)) {
            return false;
        }
        break;
    case W8_ACTION_PROTECT:
        if (!CanCharacterAttack(party_slot)) {
            return false;
        }
        break;
    case W8_ACTION_CAST_SPELL:
        if (selected.detail == 0) {
            return false;
        }
        if (!CanCharacterCastSpell(character, selected.detail)) {
            return false;
        }
        break;
    case W8_ACTION_USE_ITEM:
        if (CurrentGame().inventory.itemAt(selected.detail_block.item_use.item) == 0) {
            return false;
        }
        if (!CanCharacterActivateItem(
                character, CurrentGame().inventory.itemAt(selected.detail_block.item_use.item))) {
            return false;
        }
        break;
    case W8_ACTION_BREATHE:
        if (!CharacterHasTrait(character, W8_TRAIT_BREATHE) ||
            character->stamina <
                static_cast<int>(static_cast<unsigned int>(character->uiStaminaMax) / 5)) {
            return false;
        }
        break;
    case W8_ACTION_DEFEND:
        return true;
    default:
        break;
    }
    if (!allow_missing_target) {
        if (!TargetIsInPlay(party_slot, 2, selected)) {
            return false;
        }
    }
    return true;
}
} // namespace

bool CharacterCanSwitchTo(int party_slot, const W8Action& action, bool allow_missing_target)
{
    return CanSlotAct(party_slot) && CanPerformAction(party_slot, action, allow_missing_target);
}

bool CharacterCanChooseAction(int party_slot, W8ActionKind candidate)
{
    if (!CanSlotAct(party_slot)) {
        return false;
    }
    switch (candidate) {
    case W8_ACTION_CAST_SPELL:
        return CharacterHasCastableSpell(&CurrentGame().party.characters[party_slot]);
    case W8_ACTION_USE_ITEM:
        return true;
    default:
        return CanPerformAction(party_slot, W8Action{candidate}, true);
    }
}

/* Face the monster toward whatever its combat slot targets before the attack
   starts: its own side, a specific monster, the nearest visible group member,
   or a point on the ground. `alternate` picks the immediate versus the
   animated turn. */
void OrientMonsterTowardTarget(W8MonsterInfo* monster_info, bool alternate)
{
    W8MonsterInfo* target;
    int location_id;
    glm::vec3 point;

    switch (monster_info->Target.iType) {
    case W8_TARGET_KIND_NONE:
    case W8_TARGET_KIND_ALL_ENEMIES:
        break;
    default:
        if (IsMonsterFacingParty(monster_info) == 0) {
            monster_info->entity->ForwardReferencePosition(alternate);
        }
        break;
    case W8_TARGET_KIND_MONSTER:
        location_id = monster_info->Target.iMonsterID;
        if (monster_info->location_id != location_id) {
            target = FindMonster(location_id);
            if (IsMonsterFacingMonster(monster_info, target) == 0) {
                monster_info->entity->AimAtMonster(*target->entity, alternate);
                return;
            }
        }
        break;
    case W8_TARGET_KIND_GROUP:
        location_id = FindNearestVisibleGroupMonster(monster_info, monster_info->Target.iGroupID,
                                                     W8VisibilityCondition::ThirdLineOfSightFlag);
        if (location_id != -1) {
            target = FindMonster(location_id);
            if (IsMonsterFacingMonster(monster_info, target) == 0) {
                monster_info->entity->AimAtMonster(*target->entity, alternate);
                return;
            }
        }
        break;
    case W8_TARGET_KIND_PLACE:
        point = monster_info->Target.point;
        if (!IsPartyLookingAt(monster_info, point)) {
            if (alternate) {
                monster_info->entity->nav.SetFacingToward(&point);
                return;
            }
            monster_info->entity->nav.AimAtPosition(&point);
            return;
        }
    }
}

/* While the auto-advance option is on and the party is in combat with nothing
   else capturing input, move the selection to the next party slot that is a
   live member without a combat action to take. */
void AutoAdvanceSelectedCharacter(void)
{
    if (!g_settings.auto_advance_character || CurrentGame().combat->target_picked ||
        !CurrentGame().combat->round_active || ScreenPointer().targeting() != W8_TARGET_NEED_NONE ||
        SpellPicker() || UseItemPicker()) {
        return;
    }
    int selected = CurrentGame().status.selected_character;
    if (!CharacterCanSwitchTo(
            selected, SelectCombatAction(selected, W8_TARGETING_CONTEXT_IN_COMBAT), false) &&
        IsPartySlotEligible(selected)) {
        return;
    }
    assert(selected != -1);
    int party_slot = selected + 1;
    if (party_slot > 7) {
        party_slot = 0;
    }
    if (party_slot == selected) {
        return;
    }
    do {
        if (!CharacterCanSwitchTo(party_slot,
                                  SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT),
                                  false) &&
            IsPartySlotEligible(party_slot)) {
            if (CurrentGame().status.selected_character == party_slot) {
                return;
            }
            SelectPartyCharacter(party_slot);
            return;
        }
        ++party_slot;
        if (party_slot > 7) {
            party_slot = 0;
        }
    } while (party_slot != CurrentGame().status.selected_character);
}

/* Assign each combatant's phase for the round: the living, active party slots
   and in-combat monsters roll initiative into a phase under ninety steps,
   everyone else is parked with no phase. */
void AssignCombatPhases(void)
{
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8PartySlotRow* party_row = &CurrentGame().party.rows[party_slot];
        W8Character* character = &CurrentGame().party.characters[party_slot];
        W8CombatCharacterRow* row = &CurrentGame().combat->characters[party_slot];
        if (!CurrentGame().party.members[party_slot].has_value() || character->hp_current == 0 ||
            character->highest_condition > W8_CONDITION_WEBBED ||
            CurrentGame().combat->party_surprised) {
            party_row->executing.kind = W8_ACTION_NONE;
            row->phase = 0;
            row->dead = true;
        } else {
            int value = CurrentGame().rng.below(4) + character->initiative;
            W8ActionKind action = party_row->committed.kind;
            if (action == W8_ACTION_ATTACK || action == W8_ACTION_BERSERK) {
                int hand = ChooseCharacterAttackHand(party_slot);
                if (hand != -1) {
                    value += character->Hand[hand].damage_bonus;
                }
            }
            value = std::clamp(value, -10, 0x59);
            row->phase = 0x5a - value;
            RoundPhaseToStep(&row->phase, 10);
            if (CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0 &&
                character->skills[W8_SKILL_SNAKESPEED].active && CurrentGame().rng.below(5) == 0) {
                PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_SNAKESPEED, 1, false);
            }
        }
    }
    if (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_NONE) {
        InitializePartyMovementPhase();
    }
    auto monster_count = CurrentLevelPopulation().monster_roster.live().size();
    for (unsigned int monster_index = 0; monster_index < monster_count; ++monster_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_index);
        if (monster_info->pCombat) {
            bool side_done;
            if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
                side_done = CurrentGame().combat->monsters_surprised;
            } else {
                side_done = CurrentGame().combat->party_surprised;
            }
            if (monster_info->hp_current == 0 ||
                monster_info->highest_condition > W8_CONDITION_WEBBED || side_done) {
                monster_info->action_kind = W8_MONSTER_ACTION_NONE;
                monster_info->pCombat->phase = 0;
                monster_info->pCombat->active = true;
            } else {
                int value =
                    CurrentGame().rng.below(4) + GetMonsterDataForInfo(monster_info)->initiative;
                value = std::clamp(value, -10, 0x59);
                monster_info->pCombat->phase = 0x5a - value;
                RoundPhaseToStep(&monster_info->pCombat->phase, 10);
            }
        }
        monster_count = CurrentLevelPopulation().monster_roster.live().size();
    }
}

/* Award the round's skill practice: each hand record's successful-swing count
   rolls its weapon and combat skills, and the three defensive skills flagged
   during the round each earn two usage points. */
void PracticeCombatRoundSkills(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    W8CombatCharacterRow* row = &CurrentGame().combat->characters[party_slot];
    for (auto& hand : row->hand_records) {
        W8CombatHandRecord* hand_record = &hand;
        unsigned int chance;
        if (hand_record->dual_wielding == 0) {
            chance = hand_record->weapon_skill != W8_SKILL_MARTIAL_ARTS ? 100 : 50;
        } else {
            chance = 0x4b;
        }
        while (hand_record->score != 0) {
            if (CurrentGame().rng.below(100) < chance) {
                W8Skill skill_id = W8_SKILL_SWORD;
                int pick = CurrentGame().rng.below((hand_record->dual_wielding != 0) + 2);
                if (pick == 0) {
                    skill_id = hand_record->weapon_skill;
                } else if (pick == 1) {
                    skill_id = hand_record->combat_skill;
                } else if (pick == 2) {
                    skill_id = W8_SKILL_DUAL_WEAPONS;
                }
                PracticeCharacterSkill(CurrentGame(), character, skill_id, 1, false);
                if (skill_id == hand_record->weapon_skill &&
                    hand_record->combat_skill == W8_SKILL_CLOSE_COMBAT &&
                    CurrentGame().rng.below(100) < 0x19) {
                    PracticeCharacterSkill(CurrentGame(), character, skill_id, 1, false);
                }
            }
            --hand_record->score;
        }
    }
    for (int skill_id = 0; skill_id < W8_SKILL_COUNT; ++skill_id) {
        if (row->skill_use_flags[skill_id] != 0 &&
            (skill_id == W8_SKILL_SHIELD || skill_id == W8_SKILL_STEALTH ||
             skill_id == W8_SKILL_REFLEXTION)) {
            PracticeCharacterSkill(CurrentGame(), character, static_cast<W8Skill>(skill_id), 2,
                                   false);
            row->skill_use_flags[skill_id] = 0;
        }
    }
}

/* The round boundary. Restore stamina to resting combatants, award the round's
   skill practice, track how long nobody could engage, advance the clock, and
   refresh engagement, sight and targets for the next round. */
void AdvanceCombatRound(void)
{
    W8Dice dice;
    unsigned int index;

    CurrentGame().combat->combat_ready = false;
    if (CurrentGame().party_movement_mode) {
        BeginFreeTurnPhase();
    }
    if (!g_settings.continuous_combat &&
        CurrentGame().combat->uiNextPartyAction != W8_PARTY_ACTION_NONE) {
        ClearPendingPartyMovement();
    }
    ReconcilePartyFormation(&CurrentGame().planned_formation, &CurrentGame().party.formation);
    if (CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0) {
        CurrentGame().events.push(
            TextNotice{W8_FONT_PALETTE_BEIGE,
                       fmt::sprintf(Data().strings[0x22b], CurrentGame().combat->round_count)});
        wiz8::sound::play("Data\\Sound\\Misc\\EndTurnChime.wav");
    }
    CurrentGame().combat->party_surprised = false;
    CurrentGame().combat->monsters_surprised = false;
    if (CurrentGame().combat->passive_round == 0) {
        CurrentGame().combat->unengaged_rounds = 0;
    } else {
        bool engaged = false;

        for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
            W8MonsterGroup* monster_group = CurrentLevelPopulation().monster_groups[index].get();
            if (monster_group->members_active && monster_group->fInCombat &&
                MonsterGroupCanEngage(monster_group)) {
                engaged = true;
                break;
            }
        }
        if (!engaged) {
            ++CurrentGame().combat->unengaged_rounds;
        }
    }
    AdvanceEnvironmentTime(120000);
    if (g_camera_sway_active) {
        UpdateCampFatigue(10);
    }
    CurrentGame().events.push(PartyEffectsChanged{});
    CurrentGame().combat->execution_active = 0;
    CurrentGame().combat->round_active = true;
    if (CurrentGame().combat->pending_death_count != 0) {
        int active_characters = CountActiveCharacters();
        if (active_characters != 0) {
            if (active_characters == 1) {
                QueueLastSurvivorEvent();
            } else if (CurrentGame().combat->pending_death_count > 1) {
                QueueTurnReactionEvent();
            }
        }
    }
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value()) {
            W8Character* character = &CurrentGame().party.characters[party_slot];
            if (CurrentGame().party.rows[party_slot].executing.kind == W8_ACTION_DEFEND ||
                character->highest_condition == W8_CONDITION_ASLEEP ||
                character->highest_condition == W8_CONDITION_UNCONSCIOUS) {
                int sides = static_cast<unsigned int>(character->uiStaminaMax) / 0x14 - 1;
                if (sides == 0) {
                    sides = 1;
                }
                SetDice(&dice, 2, sides, 0);
                int amount = RollDice(&dice);
                if (CharacterHasTrait(character, W8_TRAIT_STAMINA_REGENERATION)) {
                    amount = static_cast<int>(ScaleValueByProfessionLevel(
                                                  character, W8_TRAIT_STAMINA_REGENERATION, 3.3f) *
                                              amount);
                }
                RestoreCharacterStamina(party_slot, amount,
                                        character->stamina < character->uiStaminaMax);
            }
            PracticeCombatRoundSkills(party_slot);
            CurrentGame().combat->characters[party_slot].dead =
                (character->hp_current == 0 || character->highest_condition > W8_CONDITION_WEBBED)
                    ? 1
                    : 0;
        }
    }
    SettleActiveParty(CurrentGame());
    index = 0;
    while (index < CurrentLevelPopulation().monster_roster.live().size()) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat) {
            if (monster_info->action_kind == W8_MONSTER_ACTION_WAIT ||
                monster_info->highest_condition == W8_CONDITION_ASLEEP ||
                monster_info->highest_condition == W8_CONDITION_UNCONSCIOUS) {
                int sides = static_cast<unsigned int>(monster_info->stamina_max) / 0x14 - 1;
                if (sides == 0) {
                    sides = 1;
                }
                SetDice(&dice, 2, sides, 0);
                RestoreMonsterStamina(monster_info, RollDice(&dice), false);
            }
            if (monster_info->pCombat->special_cooldown != 0 &&
                monster_info->action_kind != W8_MONSTER_ACTION_SPECIAL_ATTACK) {
                monster_info->pCombat->special_cooldown =
                    monster_info->pCombat->special_cooldown - 1;
            }
        }
        ++index;
    }
    CurrentGame().events.push(CombatProgressChanged{.whole_party = true});
    CheckMonsterGroupsLeaveCombat();
    UpdateMonsterGroupEngagement();
    CheckMonsterGroupsEnterCombat();
    DetectMonsterGroups();
    RefreshOutwardSightForAllMonsters();
    RefreshInwardSightForAllMonsters();
    RepickInvalidCombatTargets();
    index = 0;
    while (index < CurrentLevelPopulation().monster_roster.live().size()) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat) TurnMonsterToNewTarget(*monster_info);
        ++index;
    }
}

/* Whether combat is over and how it ended. Returns 1 once the combat has been
   torn down - the all-dead defeat, the no-kill end, or the victory fanfare -
   and 0 while it may continue. */
int CheckCombatEnd(unsigned int end_if_no_hostiles)
{
    bool any_active = AnyCharacterActive();
    RefreshCombatEngagement();
    if (!any_active) {
        CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x230], 0});
        for (int party_slot = 0; party_slot < 8; ++party_slot) {
            W8Character* character = &CurrentGame().party.characters[party_slot];
            if (CurrentGame().party.members[party_slot].has_value() &&
                (character->hp_current != 0 || character->highest_condition < W8_CONDITION_DEAD)) {
                InflictCondition(
                    CurrentGame(), party_slot,
                    {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE});
            }
        }
    } else {
        if ((end_if_no_hostiles == 0 ||
             CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0) &&
            CurrentGame().combat->unengaged_rounds < 2) {
            return 0;
        }
        if (CurrentGame().combat->combat_result == 0) {
            if (CurrentGame().combat->enemies_engaged) {
                StartLevelMusic(1, 1);
                CurrentGame().endCombat(false);
                return 1;
            }
            CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x22e], 0});
            if (CombatHasContinuingEffects()) {
                CurrentGame().events.push(
                    TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x22f], 0});
                StartLevelMusic(1, 1);
                CurrentGame().endCombat(false);
                return 1;
            }
        } else {
            unsigned int party_slot;
            W8CharacterEvent* event;
            CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x22c], 0});
            CurrentGame().events.push(TextNotice{
                W8_FONT_PALETTE_BEIGE,
                fmt::sprintf(Data().strings[0x22d], CurrentGame().combat->combat_result,
                             CurrentGame().combat->combat_result == 1 ? "kill" : "kills"),
                0});
            StartMusicResource("CombatWin.MPL", 0, 1);
            ServiceMusicPlaylist();
            StartLevelMusic(1, 0);
            if (LastCombatDifficulty() != W8_DIFFICULTY_NOVICE &&
                GetRandomPartySlots(0, 0, -1, &party_slot, 1, false) != 0 &&
                (event = QueueCharacterEvent(&CurrentGame().party.characters[party_slot], 35, 2,
                                             kCharacterEventFullVolume),
                 event != 0)) {
                event->DelayDispatch(std::chrono::milliseconds{1500});
                CurrentGame().endCombat(false);
                return 1;
            }
        }
    }
    CurrentGame().endCombat(false);
    return 1;
}

/* Roll for surprise at combat start: the party side through the first
   threatening monster's opposed check plus level/search bonuses, and the
   hostile side when none of them can see the party while surprise is
   possible. Mutual surprise cancels. */
void RollCombatSurprise(signed char party_cannot_be_surprised)
{
    bool search_surprise = false;
    bool level_surprise = false;
    bool found;
    unsigned int index;
    W8MonsterInfo* monster_info;

    assert(CurrentGame().combat.has_value());
    if (party_cannot_be_surprised == 0) {
        if (CurrentGame().party.modifiers.sight_override == 0) {
            /* The party is surprised unless a hostile monster in the fight
               has seen it and PartyAvoidsSurprise returns 0. */
            found = false;
            for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                 ++index) {
                monster_info = GetMonsterListEntry(index);
                if (monster_info->fActive && monster_info->pCombat &&
                    monster_info->hp_current != 0 &&
                    monster_info->highest_condition < W8_CONDITION_PARALYZED &&
                    monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
                    monster_info->party_threat.sight_state == W8_SIGHT_SEEN) {
                    found = true;
                    break;
                }
            }
            if (!found || PartyAvoidsSurprise() != 0) {
                CurrentGame().combat->party_surprised = true;
            }
        }
        if (!CurrentGame().combat->party_surprised) {
            if (IsLevelFastMovement()) {
                if (CurrentGame().rng.below(100) < 0x14) {
                    CurrentGame().combat->party_surprised = true;
                    level_surprise = true;
                }
            } else if (CurrentGame().search.requested() /* searching as the fight began */ &&
                       CurrentGame().rng.below(100) < 0x14) {
                CurrentGame().combat->party_surprised = true;
                search_surprise = true;
            }
        }
    }
    /* The monsters are surprised when none of them can see the party. */
    found = false;
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_PARALYZED &&
            monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
            monster_info->player_visibility.sight_state == W8_SIGHT_SEEN) {
            found = true;
            break;
        }
    }
    if (!found && !CurrentGame().surprise_possible) {
        CurrentGame().combat->monsters_surprised = true;
    }
    if (CurrentGame().combat->party_surprised && CurrentGame().combat->monsters_surprised) {
        CurrentGame().combat->party_surprised = false;
        CurrentGame().combat->monsters_surprised = false;
    }
    if (!CurrentGame().combat->party_surprised) {
        if (CurrentGame().combat->monsters_surprised) {
            CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x241]});
        }
        return;
    }
    CurrentGame().combat->round_active = false;
    if (level_surprise) {
        CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x23e]});
        return;
    }
    if (!search_surprise) {
        CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x240]});
        return;
    }
    CurrentGame().events.push(TextNotice{W8_FONT_PALETTE_BEIGE, Data().strings[0x23f]});
}

/* Condition interrupt rolled before an actor's committed action runs: a
   nausea fit, a webbed actor's break-out check, blindness, insanity, fear,
   control and turncoat. Returns the interrupt category the action scheduler
   substitutes, -1 when the action proceeds. */
int GetConditionInterrupt(W8TargetSource* source)
{
    int party_slot;
    W8Character* character;
    W8MonsterInfo* monster_info;
    unsigned int* condition_turns;
    bool secondary_flag;
    unsigned int attribute;
    bool moving;
    bool controlled;
    bool can_attack;
    bool second_hand_attack;

    monster_info = 0;
    second_hand_attack = false;
    character = 0;
    can_attack = false;
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        assert(source->iChar != -1);
        party_slot = source->iChar;
        character = &CurrentGame().party.characters[party_slot];
        condition_turns = character->uiCondition;
        moving = CurrentGame().party.rows[party_slot].executing.kind == W8_ACTION_RUN;
        if (CanAnyHandReachTarget(party_slot)) {
            can_attack = CanPartySlotAttackAnyTarget(party_slot, 1, false);
            second_hand_attack = CanPartySlotAttackAnyTarget(party_slot, 1, true);
        }
        secondary_flag = CurrentGame().combat->characters[party_slot].berserk;
        attribute = character->attributes[W8_ATTRIBUTE_STRENGTH].effective;
        controlled = false;
    } else {
        assert(source->iType == W8_TARGET_SOURCE_MONSTER);
        assert(source->iMonsterID != -1);
        monster_info = FindMonster(source->iMonsterID);
        moving = monster_info->action_kind == W8_MONSTER_ACTION_ADVANCE;
        condition_turns = monster_info->uiCondition;
        can_attack =
            RateMonsterBestAttack(monster_info, GetMonsterDataForInfo(monster_info), false) == 0;
        secondary_flag = monster_info->pCombat->berserk;
        attribute = monster_info->attributes[W8_MONSTER_ATTRIBUTE_STRENGTH];
        second_hand_attack =
            RateMonsterBestAttack(monster_info, GetMonsterDataForInfo(monster_info), true) == 0;
        controlled = monster_info->control_state == W8_MONSTER_CONTROL_LURED;
    }
    if (condition_turns[W8_CONDITION_NAUSEATED] != 0 && CurrentGame().rng.below(100) < 0x19) {
        return 3;
    }
    if (condition_turns[W8_CONDITION_WEBBED] != 0) {
        return (CurrentGame().rng.below(100) < attribute / 3) ? 7 : 0;
    }
    if (condition_turns[W8_CONDITION_BLIND] != 0 && CurrentGame().rng.below(100) < 0x14) {
        if (source->iType == W8_TARGET_SOURCE_CHARACTER &&
            !CharacterHasTrait(character, W8_TRAIT_EFFECTIVE_WHILE_BLIND)) {
            return 2;
        }
        if (source->iType == W8_TARGET_SOURCE_MONSTER &&
            GetMonsterDataForInfo(monster_info)->kind != 12) {
            return 2;
        }
    }
    if (condition_turns[W8_CONDITION_INSANE] != 0 && CurrentGame().rng.below(100) < 0x50) {
        if (second_hand_attack && !secondary_flag && CurrentGame().rng.below(100) < 0x28) {
            return 8;
        }
        return CurrentGame().rng.below(3) + 4;
    }
    if (condition_turns[W8_CONDITION_AFRAID] != 0 && CurrentGame().rng.below(100) < 0x19 &&
        !moving) {
        return 1;
    }
    if (source->iType == W8_TARGET_SOURCE_MONSTER && condition_turns[W8_CONDITION_AFRAID] != 0 &&
        CurrentGame().rng.below(100) < 0x28) {
        return 0xc;
    }
    if (controlled) {
        return 0xb;
    }
    if (condition_turns[W8_CONDITION_TURNCOAT] == 0) {
        return -1;
    }
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        if (!can_attack) {
            if (!AreAllHandSlotsEmpty(&CurrentGame().party.characters[party_slot]) &&
                CanUnequipSlotItem(Data(), character, W8_EQUIP_SLOT_PRIMARY_WEAPON) &&
                CanUnequipSlotItem(Data(), character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
                CurrentGame().inventory.swapWeaponSets(party_slot, false);
                if (CanAnyHandReachTarget(party_slot) &&
                    CanPartySlotAttackAnyTarget(party_slot, 1, false)) {
                    CurrentGame().events.push(CharacterNotice{static_cast<unsigned int>(party_slot),
                                                              Data().strings[0x23d]});
                    return 9;
                }
                CurrentGame().inventory.swapWeaponSets(party_slot, false);
            }
            return 10;
        }
    } else if (!MonsterActionTargetsEnemies(monster_info->action_kind, monster_info->action_detail,
                                            &monster_info->spell_power_level)) {
        return -1;
    }
    return 9;
}

/* Turn the camera onto the acting combat slot's target so the player sees
   what is about to happen. Rotation mode 2 suppresses it entirely; mode 0
   only turns for the selected character's own actions. Monster and group
   targets resolve through the monster list; a place target aims the camera
   at the point. */
void PointCameraAtCombatTarget(W8TargetSource* source, const W8CombatSlot* target)
{
    int location_id;
    W8MonsterInfo* monster_info;
    W8MonsterGroup* group;
    glm::vec3 position;

    if (g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_DISABLED) {
        return;
    }
    if (!IsTargetStillPresent(target)) {
        return;
    }
    if (g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_SELECTED_CHARACTER &&
        (!TargetSourceIsCharacter(source, 0) ||
         source->iChar != CurrentGame().status.selected_character)) {
        if (target->iType != W8_TARGET_KIND_CHARACTER) {
            return;
        }
        if (target->iChar != CurrentGame().status.selected_character) {
            return;
        }
    }
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        assert(target->iMonsterID != -1);
        location_id = target->iMonsterID;
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        assert(target->iGroupID != -1);
        group = FindMonsterGroup(target->iGroupID);
        location_id = group->leader_location_id;
    } else {
        if (target->iType != W8_TARGET_KIND_PLACE) {
            return;
        }
        position = target->point;
        PointCameraAtTarget(&position, false, false);
        return;
    }
    monster_info = FindMonster(location_id);
    PointCameraAtMonster(monster_info, false, true);
}

short GetCombatActionProgress(int* out_total)
{
    int total = 0;
    int completed = 0;
    assert(CurrentGame().combat.has_value());
    if (CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
        CurrentGame().combat->uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) {
        for (int party_slot = 0; party_slot < 8; ++party_slot) {
            W8Character* character = &CurrentGame().party.characters[party_slot];
            if (CurrentGame().party.members[party_slot].has_value() && character->hp_current != 0 &&
                character->highest_condition < W8_CONDITION_ASLEEP &&
                !TryCharacterAction(party_slot, W8_ACTION_DEFEND, false) &&
                !TryCharacterAction(party_slot, W8_ACTION_PROTECT, false) &&
                !TryCharacterAction(party_slot, W8_ACTION_NONE, false)) {
                total += GetCharacterTurnValue(party_slot);
                W8CombatCharacterRow* row = &CurrentGame().combat->characters[party_slot];
                if (row->dead) {
                    int swings = 0;
                    const auto kind =
                        SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT).kind;
                    if (kind == W8_ACTION_ATTACK || kind == W8_ACTION_BERSERK) {
                        for (int hand = 0; hand < 2; ++hand) {
                            swings += row->saved_attack_value[hand] - row->hand_attack_values[hand];
                        }
                    } else {
                        swings = 1;
                    }
                    completed += swings;
                }
            }
        }
    } else {
        total = 1;
        if (0 < CurrentGame().combat->uiCurrentPartyActionStatus) {
            completed = 1;
        }
    }
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat && monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_ASLEEP && !monster_info->fMotionless &&
            monster_info->action_kind != W8_MONSTER_ACTION_WAIT &&
            monster_info->action_kind != W8_MONSTER_ACTION_PROTECT &&
            monster_info->action_kind != W8_MONSTER_ACTION_NONE) {
            total += monster_info->action_kind == W8_MONSTER_ACTION_ATTACK
                         ? monster_info->pCombat->attacks_per_round0
                         : 1;
            if (monster_info->pCombat->active) {
                completed += monster_info->action_kind == W8_MONSTER_ACTION_ATTACK
                                 ? monster_info->pCombat->attacks_per_round0 -
                                       monster_info->pCombat->attacks_per_round
                                 : 1;
            }
        }
    }
    short percent =
        total == 0 ? 100 : static_cast<short>((completed * 100) / static_cast<unsigned int>(total));
    if (out_total != 0) {
        *out_total = total;
    }
    return percent;
}
