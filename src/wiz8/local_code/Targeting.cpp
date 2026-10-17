#include "wiz8/local_code/Targeting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "Navigator.h"
#include "conditions.h"
#include "equipment_slots.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "spell_ids.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srDebug.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Cursor3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/startup_world.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

#define TARGETING_CPP "C:\\Projects\\Wizardry 8\\Local Code\\Targeting.cpp"

constexpr int BAD_INDEX = -1;

/* Point a source at one party character. Everything is cleared first and the
   monster id invalidated, so a source built this way never reads as a monster;
   the character id is the only thing left set. */
// FUNCTION: WIZ8 0x0053be00
void SetTargetSourceToCharacter(int party_slot, W8TargetSource* source)
{
    if (source == 0) {
        srAssertFail("pSource != NULL", TARGETING_CPP, 0xcc9, 0);
    }
    memset(source, 0, sizeof(W8TargetSource));
    source->iMonsterID = BAD_INDEX;
    source->iType = W8_TARGET_SOURCE_CHARACTER;
    source->iChar = party_slot;
}

/* The same for a monster, and the mirror image of it: the character id is the
   one invalidated and the monster id the one left set. The monster is passed
   as its info record rather than as an id, so the id is read out of it here -
   which is what makes the two builders take different kinds of argument for
   the same job. */
// FUNCTION: WIZ8 0x0053be50
void SetTargetSourceToMonster(const W8MonsterInfo* monster_info, W8TargetSource* source)
{
    if (source == 0) {
        srAssertFail("pSource != NULL", TARGETING_CPP, 0xcd5, 0);
    }
    memset(source, 0, sizeof(W8TargetSource));
    source->iMonsterID = BAD_INDEX;
    source->iChar = BAD_INDEX;
    source->iType = W8_TARGET_SOURCE_MONSTER;
    source->iMonsterID = monster_info->location_id;
}

// FUNCTION: WIZ8 0x0053bea0
bool TargetSourceIsCharacter(const W8TargetSource* source, int allow_indirect)
{
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        if (source->iChar == BAD_INDEX) {
            srAssertFail("pSource->iChar != BAD_INDEX", TARGETING_CPP, 0xce3, 0);
        }
        return true;
    }
    if (allow_indirect == 1 && source->iType == W8_TARGET_SOURCE_INDIRECT &&
        source->iChar != BAD_INDEX) {
        if (!source->fBackfire && !source->fReflection) {
            srAssertFail("pSource->fBackfire || pSource->fReflection", TARGETING_CPP, 0xceb, 0);
        }
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x0053bf10
bool TargetSourceIsMonster(const W8TargetSource* source, int allow_indirect)
{
    if (source->iType == W8_TARGET_SOURCE_MONSTER) {
        if (source->iMonsterID == BAD_INDEX) {
            srAssertFail("pSource->iMonsterID != BAD_INDEX", TARGETING_CPP, 0xcf8, 0);
        }
        return true;
    }
    if (allow_indirect == 1 && source->iType == W8_TARGET_SOURCE_INDIRECT &&
        source->iMonsterID != BAD_INDEX) {
        if (!source->fBackfire && !source->fReflection) {
            srAssertFail("pSource->fBackfire || pSource->fReflection", TARGETING_CPP, 0xd00, 0);
        }
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x0053c320
char GetSourceNoticeColor(const W8TargetSource* source)
{
    if (TargetSourceIsCharacter(source, 1)) {
        return 8;
    }
    if (TargetSourceIsMonster(source, 1)) {
        return 9;
    }
    return 12;
}

// FUNCTION: WIZ8 0x0053c3f0
char GetTargetNoticeColor(const W8TargetSource* source, const W8CombatSlot* target)
{
    if (TargetSourceIsCharacter(source, 1)) {
        return 8;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        return g_status.buffers.XChar[target->iChar].party_order_index;
    }
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        return 9;
    }
    return 12;
}

/* Whether a peer aiming at the just-applied target should drop that aim. */
// FUNCTION: WIZ8 0x0053C490
bool ShouldClearAimForAppliedTarget(W8TargetSource* source, W8CombatSlot* target, bool in_combat,
                                    bool action_targets_enemies)
{
    bool source_hostile;
    bool target_hostile;

    if (!in_combat) {
        return true;
    }
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        if (source->iChar == BAD_INDEX) {
            srAssertFail("pSource->iChar != BAD_INDEX", TARGETING_CPP, 0xce3, 0);
        }
        source_hostile =
            g_status.buffers.Char[source->iChar].uiCondition[W8_CONDITION_TURNCOAT] != 0;
    } else {
        if (source->iType != W8_TARGET_SOURCE_MONSTER) {
            srAssertFail("FALSE", TARGETING_CPP, 0xdf9, 0);
            return false;
        }
        if (source->iMonsterID == BAD_INDEX) {
            srAssertFail("pSource->iMonsterID != BAD_INDEX", TARGETING_CPP, 0xcf8, 0);
        }
        source_hostile = FindMonster(source->iMonsterID)->ubDisposition == W8_DISPOSITION_HOSTILE;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        target_hostile =
            g_status.buffers.Char[target->iChar].uiCondition[W8_CONDITION_TURNCOAT] != 0;
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        target_hostile = FindMonster(target->iMonsterID)->ubDisposition == W8_DISPOSITION_HOSTILE;
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        target_hostile =
            FindMonsterGroup(target->iGroupID)->ubDisposition == W8_DISPOSITION_HOSTILE;
    } else {
        srAssertFail("FALSE", TARGETING_CPP, 0xe11, 0);
        return false;
    }
    if (source_hostile == target_hostile) {
        return action_targets_enemies;
    }
    return !action_targets_enemies;
}

/* The faction names, thirty bytes apart, in the same order as the faction ids.
   Twenty-one of them, which is the whole faction domain. */
// GLOBAL: WIZ8 0x0061CE74
static char g_faction_names[W8_FACTION_COUNT][0x1e] = {
    "UNALIGNED",
    "FACTION_PARTY",
    "FACTION_DARK_SAVANT",
    "FACTION_COSMIC_LORDS",
    "FACTION_UMPANI",
    "FACTION_TRANG",
    "FACTION_MOOK",
    "FACTION_RATTKIN_COMMON",
    "FACTION_RATTKIN_MAFIA",
    "FACTION_BROTHERHOOD",
    "FACTION_HIGARDI_BANK",
    "FACTION_HIGARDI_HLL",
    "FACTION_HIGARDI_COMMON",
    "FACTION_TRYNNIE",
    "FACTION_MAD_MARTEN",
    "FACTION_RAPAX_COMMON",
    "FACTION_RAPAX_TEMPLAR",
    "FACTION_RAPAX_ARMY",
    "FACTION_KINGS_ASSASINS",
    "FACTION_filler3",
    "FACTION_filler4",
};

/* Look a faction up by name, case-insensitively. -1 for a name that is not one
   of the twenty-one. */
// FUNCTION: WIZ8 0x005360b0
signed char FindFactionByName(const char* name)
{
    signed char faction;

    for (faction = 0; faction < W8_FACTION_COUNT; ++faction) {
        if (SDL_strcasecmp(g_faction_names[faction], name) == 0) {
            return faction;
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x005360f0
int GetFactionValue(signed char faction)
{
    return g_factions[faction].band_changed_clock;
}

// FUNCTION: WIZ8 0x00536110
void SetFactionFlag(signed char faction, bool flag)
{
    g_factions[faction].encountered = flag;
}

// FUNCTION: WIZ8 0x00536130
unsigned char GetFactionFlag(signed char faction)
{
    return g_factions[faction].encountered;
}

/* Build an empty target block: everything zeroed, then the two ids set to
   BAD_INDEX. The kind is written twice, once by the clear and once on its
   own. */
// FUNCTION: WIZ8 0x00536150
void ResetTargetSource(W8TargetSource* source)
{
    memset(source, 0, sizeof(W8TargetSource));
    source->iType = W8_TARGET_SOURCE_NONE;
    source->iChar = BAD_INDEX;
    source->iMonsterID = BAD_INDEX;
}

// FUNCTION: WIZ8 0x00536170
void ResetCombatSlot(W8CombatSlot* slot)
{
    memset(slot, 0, sizeof(W8CombatSlot));
    slot->iType = W8_TARGET_KIND_NONE;
    slot->iMonsterID = BAD_INDEX;
    slot->iChar = BAD_INDEX;
    slot->iGroupID = BAD_INDEX;
}

/* Clear whatever a monster was aiming at, and report that it now has no
   target. */
// FUNCTION: WIZ8 0x005369f0
bool ClearMonsterCombatSlot(W8MonsterInfo* monster_info)
{
    ResetCombatSlot(&monster_info->Target);
    return false;
}

/* Whether one party slot's target is still good in a given context: it has to
   be of the kind the spell needs and within range. */
// FUNCTION: WIZ8 0x00537220
bool IsSpellTargetStillValidIn(int party_slot, int spell_id, const W8ActionSelection& selection)
{
    W8TargetNeed needed =
        GetTargetNeededForSpellFriendly(spell_id, false,
                                        selection.source == W8_TARGETING_CONTEXT_SPELL ||
                                            selection.source == W8_TARGETING_CONTEXT_ITEM);

    if (!TargetMatchesNeeded(selection.target, needed)) {
        return false;
    }
    return CharacterActionReachesTarget(party_slot, 0, selection);
}

/* The same check without the range half, and with one target kind that a
   global override always accepts. */
// FUNCTION: WIZ8 0x00537270
bool IsSpellTargetOfNeededKind(int party_slot, int spell_id, const W8ActionSelection& selection)
{
    W8CombatSlot* target = selection.target;
    W8TargetNeed needed = GetTargetNeededForSpellFriendly(spell_id, false, false);

    if (needed == W8_TARGET_NEED_ENEMY && g_settings.autotarget_spells != 0) {
        return true;
    }
    return TargetMatchesNeeded(target, needed);
}

/* What an item's spell needs picked before it can be cast. An item with no
   spell needs nothing. */
// FUNCTION: WIZ8 0x00537330
W8TargetNeed GetTargetNeededForItem(const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record;

    if (item == 0 || item->iItemNo == -1) {
        return W8_TARGET_NEED_NONE;
    }
    record = &g_item_records[item->iItemNo];
    if (record->spell_id == W8_SPELL_NONE) {
        return W8_TARGET_NEED_NONE;
    }
    return GetTargetNeededForSpellFriendly(record->spell_id, ItemClassNormalizesTarget(record),
                                           false);
}

/* Aim one party slot at a clicked monster location. The pending action's
   needed target kind decides between the single monster and its whole group:
   a group-needing action (needed 5) aims at the location's group instead. */
// FUNCTION: WIZ8 0x00537950
void AimAtMonsterLocation(int party_slot, int location_id, bool allow_single_target)
{
    const bool sight_only = IsMonsterSighting();
    const auto selected_action = ChooseEditedAction(party_slot);
    W8CombatSlot target;
    W8TargetNeed needed;

    if (CanTargetMonster(party_slot, location_id, allow_single_target, true, selected_action,
                         sight_only)) {
        needed = W8_TARGET_NEED_UNSET;
        if (selected_action.source != W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
            needed = GetTargetNeededForAction(selected_action.kind, selected_action.detail,
                                              selected_action.detail_block);
            if (needed == W8_TARGET_NEED_GROUP) {
                memset(&target, 0, sizeof(target));
                target.iMonsterID = BAD_INDEX;
                target.iChar = BAD_INDEX;
                target.iType = W8_TARGET_KIND_GROUP;
                target.iGroupID = FindMonster(location_id)->monster_group_id;
                AimAtTarget(party_slot, &target, selected_action);
                StartBreathCycle(party_slot, false);
                return;
            }
        }
        memset(&target, 0, sizeof(target));
        target.iChar = BAD_INDEX;
        target.iGroupID = BAD_INDEX;
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = location_id;
        AimAtTarget(party_slot, &target, selected_action);
        StartBreathCycle(party_slot, false);
    }
}

/* Aim at whatever the caller names, by kind. The other three fields are left
   at BAD_INDEX, so only the kind's own field is meaningful. */
// FUNCTION: WIZ8 0x00538620
void AimByKind(int actor, W8TargetKind kind, const W8ActionSelection& selection)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = kind;
    AimAtTarget(actor, &target, selection);
}

// FUNCTION: WIZ8 0x00538670
void AimAtCharacter(int actor, int character_slot, const W8ActionSelection& selection)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iChar = character_slot;
    target.iType = W8_TARGET_KIND_CHARACTER;
    AimAtTarget(actor, &target, selection);
}

// FUNCTION: WIZ8 0x005386C0
void AimAtCharacterIndirect(int actor, int character_slot, const W8ActionSelection& selection)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iChar = character_slot;
    target.iType = W8_TARGET_KIND_CHARACTER_INDIRECT;
    AimAtTarget(actor, &target, selection);
}

/* Aim at the place the party is looking, drop the marker and let the display
   know. */
// FUNCTION: WIZ8 0x00538710
void AimAtPlace(int actor)
{
    W8CombatSlot target;
    glm::vec3 position;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    GetWorldCursorTargetPosition(&position);
    AimAtTarget(actor, &target, ChooseEditedAction(actor));
    ClearTargetMarker();
}

/* Aim at the ground point the camera is looking at, the world-cursor-free
   twin of AimAtPlace used by click-to-move targeting. */
// FUNCTION: WIZ8 0x00538770
void AimAtGroundTarget(int party_slot)
{
    W8CombatSlot target;
    glm::vec3 position;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    GetCameraForwardPoint(kGroundTargetRange, &position);
    target.point = position;
    AimAtTarget(party_slot, &target, ChooseEditedAction(party_slot));
    StartBreathCycle(party_slot, false);
}

/* The three wrappers that set the party's own target rather than a
   combatant's, one per kind that names something. */
// FUNCTION: WIZ8 0x00538d10
void SetTargetToCharacter(int character_slot, bool in_combat)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = character_slot;
    ApplyTarget(&target, in_combat);
}

// FUNCTION: WIZ8 0x00538d60
void SetTargetToMonster(int monster_id, bool in_combat)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_MONSTER;
    target.iMonsterID = monster_id;
    ApplyTarget(&target, in_combat);
}

// FUNCTION: WIZ8 0x00538db0
void SetTargetToGroup(int group_id, bool in_combat)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_GROUP;
    target.iGroupID = group_id;
    ApplyTarget(&target, in_combat);
}

/* Walk every party slot and every live monster: anyone already aiming at
   `target` drops that aim when the combat flag and their action say so. A
   monster target also clears its highlight bit before the walk. */
// FUNCTION: WIZ8 0x00538E00
void ApplyTarget(W8CombatSlot* target, bool in_combat)
{
    W8TargetSource source;
    W8PartySlotRow* row;
    W8Character* character;
    W8MonsterInfo* monster_info;
    bool action_targets_enemies;
    int party_slot;

    if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        MonsterSetHighlightMask(monster_info->p3D, 0);
    }

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        row = &g_status.buffers.XChar[party_slot];
        character = &g_status.buffers.Char[party_slot];
        if (!CanPartySlotParticipate(party_slot)) {
            continue;
        }
        if (target->iType == W8_TARGET_KIND_MONSTER) {
            NotifyMonsterHighlight(party_slot, target->iMonsterID, 0);
        }
        SetTargetSourceToCharacter(party_slot, &source);
        if (gXStatus.fCombatMode) {
            if ((row->target_in_combat == *target)) {
                action_targets_enemies = CharacterActionTargetsEnemies(
                    character, row->action, row->action_detail0, &row->action_detail1);
                if (ShouldClearAimForAppliedTarget(&source, target, in_combat,
                                                   action_targets_enemies)) {
                    if (row->action == W8_ACTION_PROTECT) {
                        DropCharacterFromRound(party_slot);
                    } else {
                        RepickActionTarget(
                            party_slot,
                            ChooseCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT), 0);
                    }
                }
            }
        }
        if ((row->target_out_of_combat == *target)) {
            action_targets_enemies = CharacterActionTargetsEnemies(
                character, row->pending_action, row->attack_mode[0], &row->pending_action_detail);
            if (ShouldClearAimForAppliedTarget(&source, target, in_combat,
                                               action_targets_enemies)) {
                RepickActionTarget(
                    party_slot, ChooseCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT),
                    0);
            }
        }
    }

    for (unsigned int index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        SetTargetSourceToMonster(monster_info, &source);
        if ((monster_info->Target == *target)) {
            action_targets_enemies =
                MonsterActionTargetsEnemies(monster_info->action_kind, monster_info->action_detail,
                                            &monster_info->spell_power_level);
            if (ShouldClearAimForAppliedTarget(&source, target, in_combat,
                                               action_targets_enemies)) {
                ResetCombatSlot(&monster_info->Target);
            }
        }
    }
}

/* Put the on-screen marker over one monster, from the party's eye to the
   monster's own bounds. */
// FUNCTION: WIZ8 0x00539870
unsigned char ShowMonsterTargetMarker(W8MonsterInfo* monster_info)
{
    glm::vec3 eye;

    if (monster_info == 0) {
        srAssertFail("pMonsterInfo", TARGETING_CPP, 2040, 0);
    }
    eye = cameraLocation();
    const auto bounds = MonsterGetWorldAnimationBounds(monster_info->p3D);
    return bounds && targetMarkerVisible(eye, *bounds);
}

/* Whether a recorded target is of the kind a caller needs. Each needed kind
   admits one or two target kinds that additionally have to be present.
   Other requirements are accepted after the null-target check. */
// FUNCTION: WIZ8 0x00537160
bool TargetMatchesNeeded(W8CombatSlot* target, W8TargetNeed needed)
{
    bool matched = false;

    if (target == 0) {
        return false;
    }
    switch (needed) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ENEMY:
    case W8_TARGET_NEED_CASTER:
        if (target->iType == W8_TARGET_KIND_CHARACTER && target->iChar != BAD_INDEX) {
            matched = true;
        }
        if (target->iType == W8_TARGET_KIND_MONSTER && target->iMonsterID != BAD_INDEX) {
            return IsTargetStillPresent(target);
        }
        if (matched) {
            return IsTargetStillPresent(target);
        }
        break;
    case W8_TARGET_NEED_PLACE:
    case W8_TARGET_NEED_CONE:
        if (target->iType == W8_TARGET_KIND_PLACE) {
            return IsTargetStillPresent(target);
        }
        break;
    case W8_TARGET_NEED_GROUP:
        if (target->iType == W8_TARGET_KIND_GROUP && target->iGroupID != BAD_INDEX) {
            matched = true;
        }
        if (target->iType == W8_TARGET_KIND_PARTY) {
            return IsTargetStillPresent(target);
        }
        if (matched) {
            return IsTargetStillPresent(target);
        }
        break;
    case W8_TARGET_NEED_ITEM:
        if (target->iType == W8_TARGET_KIND_ITEM && target->pPCItem != 0) {
            return IsTargetStillPresent(target);
        }
        break;
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        if (target->iType == W8_TARGET_KIND_CHARACTER_INDIRECT && target->iChar != BAD_INDEX) {
            return IsTargetStillPresent(target);
        }
        break;
    default:
        return true;
    }
    return false;
}

/* What the interface has to ask the player to pick for one action. Most
   actions answer a fixed kind; casting asks the spell and using an item asks
   the item's own spell, which is the same two-step the item path takes. */
// FUNCTION: WIZ8 0x00536400
void RepickInvalidCombatTargets(void)
{
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
        W8Character* character = &g_status.buffers.Char[party_slot];
        const auto selected_action = ChooseCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
        if (!row->fOccupied || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_TURNCOAT ||
            !CharacterCanSwitchTo(party_slot, selected_action, true, false)) {
            continue;
        }
        W8TargetNeed needed = GetTargetNeededForAction(selected_action.kind, selected_action.detail,
                                                       selected_action.detail_block);
        if (!TargetMatchesNeeded(selected_action.target, needed) ||
            !CharacterActionReachesTarget(party_slot, 2, selected_action)) {
            RepickActionTarget(party_slot, selected_action, 0);
        }
    }
}

// FUNCTION: WIZ8 0x00536a20
W8TargetNeed GetTargetNeededForAction(W8ActionKind action, int spell_id,
                                      const W8ActionDetailBlock* detail_block)
{
    const W8ItemDatabaseRecord* record;

    switch (action) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        return W8_TARGET_NEED_ENEMY;
    case W8_ACTION_CAST_SPELL:
        return GetTargetNeededForSpellFriendly(spell_id, false, false);
    case W8_ACTION_BREATHE:
        return W8_TARGET_NEED_CONE;
    case W8_ACTION_PROTECT:
        return W8_TARGET_NEED_ALLY;
    case W8_ACTION_USE_ITEM:
        if (detail_block && detail_block->item_use.item != 0 &&
            detail_block->item_use.item->iItemNo != -1) {
            record = &g_item_records[detail_block->item_use.item->iItemNo];
            if (record->spell_id != W8_SPELL_NONE) {
                return GetTargetNeededForSpellFriendly(record->spell_id,
                                                       ItemClassNormalizesTarget(record), false);
            }
        }
        break;
    default:
        break;
    }
    return W8_TARGET_NEED_NONE;
}

/* The target kind the slot's current action needs right now. The ordinary
   combat-or-not context is overridden while the main game screen has a
   pending selection - a settled spell or item pick asks through that context,
   anything else pending reads as dialogue - and while the slot is the
   selected character in spell or item mode the shared context applies. */
// FUNCTION: WIZ8 0x00537380
W8TargetNeed GetTargetNeededForCurrentAction(int party_slot)
{
    const auto selection = ChooseEditedAction(party_slot);
    if (selection.source == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) return W8_TARGET_NEED_NONE;
    return GetTargetNeededForAction(selection.kind, selection.detail, selection.detail_block);
}

/* Whether a slot's recorded target suits the item it would be used with. An
   item with no spell needs nothing picked, and one target kind a global
   override always accepts. */
// FUNCTION: WIZ8 0x005372b0
bool IsItemTargetOfNeededKind(int party_slot, const W8ItemInstance* item,
                              const W8ActionSelection& selection)
{
    W8CombatSlot* target = selection.target;
    const W8ItemDatabaseRecord* record;
    W8TargetNeed needed = W8_TARGET_NEED_NONE;

    if (item != 0 && item->iItemNo != -1) {
        record = &g_item_records[item->iItemNo];
        if (record->spell_id != W8_SPELL_NONE) {
            needed = GetTargetNeededForSpellFriendly(record->spell_id,
                                                     ItemClassNormalizesTarget(record), false);
            if (needed == W8_TARGET_NEED_ENEMY && g_settings.autotarget_spells != 0) {
                return true;
            }
        }
    }
    return TargetMatchesNeeded(target, needed);
}

/* Tint one monster for whoever is highlighting it. Three tints are named -
   clear, green and red. Bug: other values leave the color uninitialized. */
// FUNCTION: WIZ8 0x00539480
void TintHighlightedMonster(W8Monster* monster, W8TargetHighlight tint)
{
    glm::vec4 block;

    switch (tint) {
    case W8_TARGET_HIGHLIGHT_NONE:
        block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        break;
    case W8_TARGET_HIGHLIGHT_GREEN:
        block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        break;
    case W8_TARGET_HIGHLIGHT_RED:
        block = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        break;
    default:
        break;
    }
    MonsterSetHighlightColour(monster, block);
}

/* Raise or lower one character's bit in a monster's highlight mask, and tell
   whatever draws it. */
// FUNCTION: WIZ8 0x00539630
void SetMonsterHighlight(int party_slot, int location_id, bool on)
{
    W8MonsterInfo* monster_info = FindMonster(location_id, false);
    W8Monster* monster;
    unsigned char bit;
    if (monster_info == 0) {
        return;
    }
    monster = monster_info->p3D;
    if (monster == 0) {
        srAssertFail("pMonster", TARGETING_CPP, 1888, 0);
    }

    bit = static_cast<unsigned char>(1 << (party_slot & 0x1f));
    unsigned char mask = MonsterGetHighlightMask(monster);
    if (on) {
        MonsterSetHighlightMask(monster, mask | bit);
        NotifyMonsterHighlight(party_slot, location_id, 1);
        return;
    }
    MonsterSetHighlightMask(monster, mask & ~bit);
    NotifyMonsterHighlight(party_slot, location_id, 0);
}

static bool IsMonsterHoverSelectable(W8MonsterInfo* monster_info)
{
    if (!monster_info->fActive || monster_info->p3D->IsDying() ||
        monster_info->p3D->hostility_preserved ||
        !MonsterUsesCurrentModelInstance(monster_info->p3D)) {
        return false;
    }
    if (!monster_info->party_threat.use_bounds) {
        UpdateMonsterSight(monster_info, true, true);
    }
    return monster_info->p3D->IsRenderable(true);
}

/* The location id of the nearest live monster whose current model instance is
   under the cursor, or -1. Born monsters come first; the unborn list joins the
   scan only while g_dev_mode is set. The cursor coordinates are carried but
   unused - the hover test is MonsterUsesCurrentModelInstance. */
// FUNCTION: WIZ8 0x005396d0
int PickNearestMonsterUnderCursor(int cursor_x, int cursor_y)
{
    int result;
    float best_distance;
    unsigned int index;

    result = -1;
    best_distance = 999999.0f;
    for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        float distance;

        if (!IsMonsterHoverSelectable(monster_info)) {
            continue;
        }
        distance = MonsterDistanceToCamera(GetWorld(), monster_info->p3D);
        if (distance < best_distance) {
            result = monster_info->location_id;
            best_distance = distance;
        }
    }
    if (g_dev_mode) {
        for (index = 0; index < gXStatus.monster_roster.unborn().size(); ++index) {
            W8MonsterInfo* monster_info = GetUnbornMonsterEntry(index);
            float distance;

            if (!IsMonsterHoverSelectable(monster_info)) {
                continue;
            }
            distance = MonsterDistanceToCamera(GetWorld(), monster_info->p3D);
            if (distance < best_distance) {
                result = monster_info->location_id;
                best_distance = distance;
            }
        }
    }
    return result;
}

/* The same over a whole group, one member at a time. The count is re-read each
   step because highlighting can remove a member. */
// FUNCTION: WIZ8 0x00538c00
void SetGroupHighlight(int party_slot, int group_id, bool on)
{
    W8MonsterGroup* group = FindMonsterGroup(group_id, false);

    if (group == 0) {
        return;
    }
    for (unsigned int member = 0; member < group->MemberIds().size(); ++member) {
        SetMonsterHighlight(party_slot, group->MemberLocationId(member), on);
    }
}

/* Re-tint every monster for one character. A character whose highlight is
   overridden tints for whoever overrode it instead, and then only the monsters
   that character was already highlighting. */
// FUNCTION: WIZ8 0x00539570
void UpdateAllMonsterHighlights(int party_slot, int location_id)
{
    bool overridden = false;
    int owner = party_slot;
    unsigned int index;
    W8MonsterInfo* monster_info;
    int tint;

    const int target_slot = g_level_block->highlight.SlotFor(W8_HIGHLIGHT_TARGET);
    if (target_slot != -1 && gXStatus.iTargetingMode == W8_TARGET_NEED_NONE) {
        overridden = true;
        owner = target_slot;
    }

    for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (!monster_info->fActive) {
            continue;
        }
        tint = (location_id == monster_info->location_id ||
                (overridden &&
                 ((1 << (owner & 0x1f)) & MonsterGetHighlightMask(monster_info->p3D)) != 0))
                   ? 1
                   : 0;
        HighlightMonsterAsTarget(monster_info->location_id, owner, static_cast<char>(tint));
    }
}

// Only the fields that determine auto-target priority are retained.
struct W8MonsterTargetCandidate {
    int location_id;
    W8Condition state;
    bool in_reach;
    float distance;
};

/* Resolve where a target physically is into the slot's own point: the camera
   for the character and party kinds, the monster's model position plus its
   height for the monster kind. The sight-probe variant takes the navigator
   position lifted by the sight offset instead. Answers zero for any kind
   without a place. */
// FUNCTION: WIZ8 0x0053c630
bool ResolveTargetPoint(W8CombatSlot* target, bool sight_probe)
{
    glm::vec3 point;
    W8Monster* monster;

    if (target->iType == W8_TARGET_KIND_PARTY || target->iType == W8_TARGET_KIND_CHARACTER) {
        if (sight_probe) {
            point = g_startup_world->GetPosition();
        } else {
            point = cameraLocation();
        }
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster = GetMonsterByLocationID(target->iMonsterID);
        if (sight_probe) {
            point = monster->GetPosition();
        } else {
            point = monster->movement.position;
            point.y += monster->movement.height_offset;
        }
    } else {
        return false;
    }
    if (sight_probe) {
        point.y += 1e+03f;
    }
    target->point = point;
    return true;
}

// Select the best live enemy directly; no owning scratch array or sort is needed.
// FUNCTION: WIZ8 0x0053c720
static int ChooseMonsterTarget(int party_slot, const W8ActionSelection& selection,
                               const std::array<W8RangeCategory, 2>* attack_ranges = nullptr)
{
    std::optional<W8MonsterTargetCandidate> best;
    const auto priority = [](const W8MonsterTargetCandidate& candidate) {
        return std::tuple(candidate.state, !candidate.in_reach, candidate.distance);
    };
    for (unsigned index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        if (!monster_info->fActive || !monster_info->fInCombat || monster_info->hp_current == 0 ||
            MonsterVsCharDisposition(party_slot, monster_info) != W8_DISPOSITION_HOSTILE ||
            !CanPartyMemberAimAtMonster(party_slot, 2, monster_info, selection, false, false,
                                        attack_ranges)) {
            continue;
        }
        W8MonsterTargetCandidate candidate{monster_info->location_id,
                                           monster_info->highest_condition, false,
                                           monster_info->p3D->GetDistanceToPlayer()};
        const auto* record = GetMonsterDataForInfo(monster_info);
        if (GetBestMonsterAttackRange(record, true) != -1) {
            const auto quadrant = static_cast<unsigned char>(GetMonsterQuadrant(monster_info));
            candidate.in_reach =
                quadrant == 2 || (!AnyoneStandsAhead(quadrant) && AnyoneStandsAhead(4));
        }
        if (monster_info->uiHPMax == 0) {
            srAssertFail("pMonsterInfo->uiHPMax > 0", TARGETING_CPP, 0xeac, 0);
        }
        if (!best || priority(candidate) < priority(*best)) best = candidate;
    }
    return best ? best->location_id : BAD_INDEX;
}

/* The two conditions the indirect character kind reads: eighteen has to be
   running and nineteen not. The monster kind reads the same eighteenth entry of
   its own array, which is what pairs the two arrays entry for entry. */
enum { W8_CONDITION_REACHABLE_WHEN_DOWN = 18, W8_CONDITION_BEYOND_REACH = 19 };

/* Whether whatever a target names is still there to be acted on. Each kind
   checks its own field and then whatever that field points at.

   The two character kinds differ in what "still there" means: the direct one
   wants somebody alive and in a state under 0x12, and the indirect one wants
   the opposite, somebody with no hit points left whose two death fields say
   they can still be reached. Everything else - a place, the party as a whole -
   is always there and is not answered here at all. */
// FUNCTION: WIZ8 0x00536190
bool IsTargetStillPresent(const W8CombatSlot* target)
{
    if (target == 0) {
        return false;
    }

    switch (target->iType) {
    case W8_TARGET_KIND_CHARACTER:
        if (target->iChar == BAD_INDEX) {
            srAssertFail("pTarget->iChar != BAD_INDEX", TARGETING_CPP, 0x6c, 0);
        }
        if (!CanPartySlotParticipate(target->iChar)) {
            return false;
        }
        break;

    case W8_TARGET_KIND_CHARACTER_INDIRECT:
        if (target->iChar == BAD_INDEX) {
            srAssertFail("pTarget->iChar != BAD_INDEX", TARGETING_CPP, 0x75, 0);
        }
        if (g_status.buffers.XChar[target->iChar].fOccupied == 0 ||
            g_status.buffers.Char[target->iChar].hp_current != 0 ||
            g_status.buffers.Char[target->iChar].uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] ==
                0 ||
            g_status.buffers.Char[target->iChar].uiCondition[W8_CONDITION_BEYOND_REACH] != 0) {
            return false;
        }
        break;

    case W8_TARGET_KIND_MONSTER: {
        W8MonsterInfo* monster_info;

        if (target->iMonsterID == BAD_INDEX) {
            srAssertFail("pTarget->iMonsterID != BAD_INDEX", TARGETING_CPP, 0x7e, 0);
        }
        monster_info = FindMonster(target->iMonsterID, false);
        if (monster_info == 0) {
            return false;
        }
        if (monster_info == 0) {
            srAssertFail("pMonsterInfo != NULL", TARGETING_CPP, 0x88, 0);
        }
        if (monster_info->hp_current == 0 ||
            monster_info->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0 ||
            !monster_info->fActive) {
            return false;
        }
        break;
    }

    case W8_TARGET_KIND_GROUP: {
        W8MonsterGroup* group;

        if (target->iGroupID == BAD_INDEX) {
            srAssertFail("pTarget->iGroupID != BAD_INDEX", TARGETING_CPP, 0x99, 0);
        }
        group = FindMonsterGroup(target->iGroupID, false);
        if (group == nullptr) {
            return false;
        }
        if (group == 0) {
            srAssertFail("pMonsterGroup != NULL", TARGETING_CPP, 0xa3, 0);
        }
        if (group->member_count == 0 || !group->members_active) {
            return false;
        }
        break;
    }

    case W8_TARGET_KIND_ITEM:
        if (target->pPCItem == 0) {
            srAssertFail("pTarget->pPCItem != NULL", TARGETING_CPP, 0xb4, 0);
        }
        if (target->pPCItem->iItemNo == BAD_INDEX) {
            return false;
        }
        break;

    default:
        break;
    }
    return true;
}

/* Tint one monster to say whether the character could act on it, and move the
   cursor to match. Green means yes and red means no; asking for no highlight at
   all tints it to nothing and answers no without touching the cursor.

   With no character named the answer is yes outright, so a bare hover over a
   monster reads as valid - it is the action, not the monster, that makes a
   target invalid. The cursor is only moved while nothing modal is up, and the
   valid case additionally waits for the screen to be idle, which is what keeps
   a cursor from flickering under a menu. */
// FUNCTION: WIZ8 0x00539110
char HighlightMonsterAsTarget(int location_id, int party_slot, char highlight)
{
    W8MonsterInfo* monster_info = FindMonster(location_id, false);
    W8Monster* monster;
    glm::vec4 block;
    bool valid = false;
    if (monster_info == 0) {
        return 0;
    }
    monster = monster_info->p3D;

    if (highlight == 0) {
        block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        MonsterSetHighlightColour(monster, block);
        return 0;
    }

    if (party_slot == BAD_INDEX ||
        (CanEditCharacterAction(party_slot, ChooseEditedAction(party_slot), true, false) &&
         CanTargetMonster(party_slot, location_id, true, false, ChooseEditedAction(party_slot),
                          IsMonsterSighting()))) {
        valid = true;
    }
    CombatAllowsLiveGroups();

    if (!valid) {
        block = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        MonsterSetHighlightColour(monster, block);
        if (g_modal_owner == 0 && gXStatus.iCurrentCursor == W8_CURSOR_VALID_TARGET) {
            UpdateHeldItemCursor();
        }
        return 0;
    }

    block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    MonsterSetHighlightColour(monster, block);
    if (g_modal_owner == 0 && gXStatus.iTargetingMode == W8_TARGET_NEED_NONE && IsScreenIdle() &&
        gXStatus.iCurrentCursor != W8_CURSOR_INVALID_TARGET) {
        SetTargetCursor(W8_CURSOR_VALID_TARGET);
    }
    return valid;
}

/* Take down whatever one party slot was highlighting. A slot with a highlight
   list of its own empties that and nothing else; only a slot with none falls
   through to the target it was given, and then a monster and a group are
   handled separately because each has its own way of naming its monsters.

   Clearing a monster drops this slot's bit out of the monster's own highlight
   mask, so a monster several slots are highlighting stays lit for the rest. */
// FUNCTION: WIZ8 0x0053ac30
void ClearTargetHighlights(int party_slot, const W8CombatSlot* target)
{
    W8MonsterManagerEntry* slot = &gXStatus.monster_manager_entries[party_slot];
    unsigned int index;

    if (!slot->highlighted_monsters.empty()) {
        for (index = 0; index < slot->highlighted_monsters.size(); ++index) {
            SetMonsterHighlight(party_slot, slot->highlighted_monsters[index], false);
        }
        slot->highlighted_monsters.clear();
        return;
    }

    if (target->iType == W8_TARGET_KIND_MONSTER && target->iMonsterID != BAD_INDEX) {
        SetMonsterHighlight(party_slot, target->iMonsterID, false);
    }

    if (target->iType == W8_TARGET_KIND_GROUP && target->iGroupID != BAD_INDEX) {
        SetGroupHighlight(party_slot, target->iGroupID, false);
    }
}

/* Combat's end: untint every live monster for each party slot whose bit it
   still carries, then drop the whole mask. */
// FUNCTION: WIZ8 0x0053ae00
void ClearAllMonsterHighlights(void)
{
    unsigned int index;
    unsigned int party_slot;

    for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        W8Monster* monster = monster_info->p3D;
        unsigned char flags;

        if (!monster_info->fActive || monster == 0) {
            continue;
        }
        flags = MonsterGetHighlightMask(monster);
        if (flags == 0) {
            continue;
        }
        for (party_slot = 0; party_slot < 8; ++party_slot) {
            if ((flags & (1 << party_slot)) != 0) {
                NotifyMonsterHighlight(party_slot, monster_info->location_id, 0);
            }
        }
        MonsterSetHighlightMask(monster, 0);
    }
}

/* The float that stands for "no distance given". GameData.cpp
   reads the same constant as the level vector's absent value. */
/* The side selector that means any side at all. */
enum { W8_SIDE_ANY = 3 };

/* Gather every monster within one distance of a point, appending their
   location ids to the caller's vector. The distance is measured to the
   monster's surface rather than its centre, which is what the radius
   subtraction is; a distance of the "no distance given" constant gathers
   nothing at all rather than everything.

   The highlighting caller and the line-of-sight caller share the body: with
   highlighting on, a monster out of range has its tint cleared and one in
   range is taken without looking, and with it off nothing is tinted and a
   monster in range still has to be visible from the given eye point. */
// FUNCTION: WIZ8 0x00539e70
void CollectMonstersWithinRadius(const glm::vec3* centre, const glm::vec3* eye,
                                 std::vector<int>& found, float radius, char side,
                                 char highlighting)
{
    unsigned int index;

    if (radius == 0.0f) {
        return;
    }

    for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        W8Monster* monster = monster_info->p3D;
        W8MonsterRecord* record;
        glm::vec3 position;

        if (!monster_info->fActive || monster_info->hp_current == 0 ||
            monster_info->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0) {
            continue;
        }
        record = GetMonsterDataForInfo(monster_info);
        if (record->untargetable != 0) {
            continue;
        }
        if (monster_info->ubDisposition != side && side != W8_SIDE_ANY) {
            continue;
        }

        position = monster->GetPosition();
        if (radius < glm::length((*centre - position)) - monster->radius) {
            if (highlighting != 0) {
                glm::vec4 block;
                block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                MonsterSetHighlightColour(monster, block);
            }
            continue;
        }
        if (highlighting != 0 || monster_info->p3D->HasLineOfSightFromPoint(*eye)) {
            found.push_back(monster_info->location_id);
        }
    }
}

// FUNCTION: WIZ8 0x0053A2C0
void SetMonsterCombatTarget(W8MonsterInfo* monster_info, int location_id)
{
    W8CombatSlot* target = &monster_info->Target;

    memset(target, 0, sizeof(*target));
    target->iType = W8_TARGET_KIND_NONE;
    target->iMonsterID = BAD_INDEX;
    target->iChar = BAD_INDEX;
    target->iGroupID = BAD_INDEX;
    target->iMonsterID = location_id;
    target->iType = W8_TARGET_KIND_MONSTER;
}

/* Probe whether a monster's current combat target is one of the kinds its
   chosen hostile spell accepts. The caller only needs the validator's side
   effects, so this wrapper discards its answer. */
// FUNCTION: WIZ8 0x0053A300
bool MonsterTargetMatchesSpell(W8MonsterInfo* monster_info, int spell_id)
{
    return TargetMatchesNeeded(&monster_info->Target, GetTargetNeededForSpellHostile(spell_id));
}

/* Map gXStatus.iTargetingMode to a cursor-table index for SetTargetCursor.
   Returned values are cursor slots (including 10..12), not W8TargetingContext. */
// FUNCTION: WIZ8 0x0053A3D0
int GetTargetingCursorForState(int alternate)
{
    switch (gXStatus.iTargetingMode) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        return (alternate != 0) + 3;
    case W8_TARGET_NEED_ENEMY:
        return alternate != 0;
    case W8_TARGET_NEED_GROUP:
        return alternate != 0 ? 10 : 11;
    case W8_TARGET_NEED_PLACE:
        return 2;
    case W8_TARGET_NEED_CONE:
        return 12;
    default:
        return W8_CURSOR_NONE;
    }
}

/* Whether the pending spell in one party row needs an explicit target. */
// FUNCTION: WIZ8 0x0053A700
bool ActionNeedsExplicitTarget(int party_slot)
{
    switch (GetSpellTargetType(g_status.buffers.XChar[party_slot].spell_id, false)) {
    case W8_TARGET_TYPE_ENEMY:
        if (gXStatus.fCampMode) {
            return false;
        }
        return g_settings.autotarget_spells == 0;
    case W8_TARGET_TYPE_CASTER:
    case W8_TARGET_TYPE_PARTY:
    case W8_TARGET_TYPE_ALL_ENEMIES:
    case W8_TARGET_TYPE_LOCK_OR_TRAP:
        return false;
    default:
        return true;
    }
}

/* Return the spell-like id carried by a chosen action: the fixed attack id,
   a spell's detail word, or the spell attached to an item use. */
// FUNCTION: WIZ8 0x0053A8D0
unsigned int GetActionSpellLikeId(int party_slot, const W8ActionSelection& selection)
{

    if (selection.kind == W8_ACTION_BREATHE) {
        return 0x77;
    }
    if (selection.kind == W8_ACTION_CAST_SPELL) {
        return selection.detail;
    }
    if (selection.kind == W8_ACTION_USE_ITEM) {
        return GetItemSpell(selection.detail_block->item_use.item);
    }
    return 0;
}

/* plsMonsterList index that last satisfied AnyMonsterVisible. */
// GLOBAL: WIZ8 0x0061D14C
static int g_last_visible_monster = -1;

/* Select the cursor and renderer-side targeting mode for one targeting state,
   then clear the cached world point so the following refresh recomputes it. */
// FUNCTION: WIZ8 0x0053A320
void SetTargetingMode(W8TargetNeed state)
{
    int cursor;

    gXStatus.iTargetingMode = state;
    switch (state) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        cursor = 3;
        break;
    case W8_TARGET_NEED_ENEMY:
        cursor = 0;
        break;
    case W8_TARGET_NEED_PLACE:
        cursor = 2;
        break;
    case W8_TARGET_NEED_CONE:
        cursor = 12;
        break;
    case W8_TARGET_NEED_GROUP:
        cursor = 11;
        break;
    default:
        cursor = W8_CURSOR_NONE;
        break;
    }
    if (cursor != gXStatus.iCurrentCursor) {
        SetTargetCursor(cursor);
    }
    gXStatus.target_position = glm::vec3(0);
    RequestRefreshPartyState();
    if (state == W8_TARGET_NEED_CONE) {
        SetTargetConeEnabled(true);
        PauseMainGameWorld();
    } else {
        SetTargetConeEnabled(false);
        if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
            ResumeMainGameWorld();
        }
    }
}

/* Re-check the selected character's committed target after its action has
   changed. A slot that cannot act at all falls back to no targeting;
   otherwise the action's needed kind is worked out again and the recorded
   target either still satisfies it - leaving no targeting mode - or the mode
   matching what the action now needs is entered so the player can pick. */
// FUNCTION: WIZ8 0x00537540
void RevalidateSelectedTarget(int party_slot)
{
    const auto selected_action = ChooseEditedAction(party_slot);
    W8CombatSlot* target;
    W8TargetNeed needed;

    if (party_slot != g_status.selected_character) {
        return;
    }
    if (!CanEditCharacterAction(party_slot, selected_action, true, false)) {
        SetTargetingMode(W8_TARGET_NEED_NONE);
        return;
    }
    target = selected_action.target;
    needed = W8_TARGET_NEED_NONE;
    if (selected_action.source != W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
        switch (selected_action.kind) {
        case W8_ACTION_ATTACK:
        case W8_ACTION_BERSERK:
            needed = W8_TARGET_NEED_ENEMY;
            break;
        case W8_ACTION_BREATHE:
            needed = W8_TARGET_NEED_CONE;
            break;
        case W8_ACTION_PROTECT:
            needed = W8_TARGET_NEED_ALLY;
            break;
        case W8_ACTION_CAST_SPELL:
            needed = GetTargetNeededForSpellFriendly(selected_action.detail, false, false);
            break;
        case W8_ACTION_USE_ITEM:
            needed = GetTargetNeededForItem(selected_action.detail_block->item_use.item);
            break;
        default:
            needed = W8_TARGET_NEED_NONE;
            break;
        }
    }
    if (TargetMatchesNeeded(target, needed)) {
        SetTargetingMode(W8_TARGET_NEED_NONE);
    } else {
        SetTargetingMode(needed);
    }
}

/* Whether anything in `group` is close enough to `source` for the source's
   action to reach it. A character source asks the range of its chosen action;
   a monster source asks the range of its own pending action. The group's
   members are then walked until one stands inside that distance - the source
   measures to the party for a character and to its own monster for a
   monster. */
// FUNCTION: WIZ8 0x00537780
bool IsTargetSourceInRangeOfGroup(const W8TargetSource* source, W8MonsterGroup* group,
                                  W8RangeCategory range)
{
    if (range == W8_RANGE_NONE) return false;
    const auto maximum = CalcRangeDistance(range);
    auto* source_monster = source->iType == W8_TARGET_SOURCE_MONSTER
                               ? GetMonsterByLocationID(source->iMonsterID)
                               : nullptr;
    for (const int id : group->MemberIds()) {
        auto* member = GetMonsterByLocationID(id);
        const auto distance = source_monster ? member->GetDistanceToMonster(source_monster)
                                             : member->GetDistanceToPlayer();
        if (distance <= maximum) return true;
    }
    return false;
}

/* Remove one party slot's highlight bit from every live monster that carries
   it, notifying the render-side highlight owner for each changed monster. */
// FUNCTION: WIZ8 0x0053AEB0
void ClearPartySlotMonsterHighlights(unsigned int party_slot)
{
    unsigned int index;

    for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        W8Monster* monster = monster_info->p3D;

        if (monster_info->fActive && monster != 0) {
            unsigned char flags = MonsterGetHighlightMask(monster);
            unsigned char bit = static_cast<unsigned char>(1 << (party_slot & 31));

            if ((flags & bit) != 0) {
                MonsterSetHighlightMask(monster, static_cast<unsigned char>(flags & ~bit));
                NotifyMonsterHighlight(party_slot, monster_info->location_id, 0);
            }
        }
    }
}

/* Select the party member whose spell-targeting state is being edited. Any
   monster highlights owned by that slot and its shared targeting record are
   cleared before the ordinary targeting UI is restored. */
// FUNCTION: WIZ8 0x0053AF40
void SelectSpellCastingPartySlot(int party_slot)
{
    W8CombatSlot target;

    ClearPartySlotMonsterHighlights(party_slot);
    ResetCombatSlot(&target);
    AimAtTarget(party_slot, &target, ChooseSharedAction(party_slot));
    SetTargetingMode(W8_TARGET_NEED_NONE);
    ClearTargetMarker();
}

/* Commit the shared spell target into the selected character's active action
   target and restart that portrait's breathing cycle. */
// FUNCTION: WIZ8 0x0053A830
void CommitSelectedSpellTarget(void)
{
    int party_slot = g_status.selected_character;
    W8TargetingContext context =
        gXStatus.fCombatMode ? W8_TARGETING_CONTEXT_IN_COMBAT : W8_TARGETING_CONTEXT_OUT_OF_COMBAT;

    const auto selected_action = ChooseEditedAction(party_slot);
    if (selected_action.kind == W8_ACTION_USE_ITEM) {
        GetItemSpell(selected_action.detail_block->item_use.item);
    }
    *ChooseCombatAction(party_slot, context).target = gXStatus.shared_target;
    StartBreathCycle(party_slot, true);
}

/* Recompute one party slot's combat-target highlights from its current action.
   Spell actions repopulate the slot's highlighted-monster list through the
   magic helper; other actions set or clear direct monster and group highlights. */
// FUNCTION: WIZ8 0x0053A930
void RefreshCombatTargetHighlights(int party_slot, W8CombatSlot* target)
{
    unsigned int spell_id;
    W8MonsterManagerEntry* entry = &gXStatus.monster_manager_entries[party_slot];

    entry->highlighted_monsters.clear();
    const auto selected_action = ChooseEditedAction(party_slot);
    spell_id = W8_SPELL_NONE;
    if (selected_action.kind == W8_ACTION_BREATHE) {
        spell_id = W8_SPELL_SPECIAL_ATTACK_CONE;
    } else if (selected_action.kind == W8_ACTION_CAST_SPELL) {
        spell_id = selected_action.detail;
    } else if (selected_action.kind == W8_ACTION_USE_ITEM) {
        spell_id = GetItemSpell(selected_action.detail_block->item_use.item);
    }

    if (spell_id != W8_SPELL_NONE) {
        if (target == 0) {
            srAssertFail("pSource != NULL", TARGETING_CPP, 0xcc9, 0);
        }

        {
            W8TargetSource source;
            std::vector<int> scratch;

            memset(&source, 0, sizeof(source));
            source.iType = W8_TARGET_SOURCE_CHARACTER;
            source.iChar = party_slot;
            PopulateSpellTargetMarkers(spell_id, 1, &source, target, entry->highlighted_monsters,
                                       scratch, 0);
        }

        ClearPartySlotMonsterHighlights(party_slot);

        unsigned int count = static_cast<int>(entry->highlighted_monsters.size());
        if (count > 0) {
            for (int highlight_index = 0; highlight_index < static_cast<int>(count);
                 ++highlight_index) {
                SetMonsterHighlight(party_slot, entry->highlighted_monsters[highlight_index], true);
            }
        }
        return;
    }

    if (target->iType == W8_TARGET_KIND_MONSTER && target->iMonsterID != BAD_INDEX) {
        SetMonsterHighlight(party_slot, target->iMonsterID, true);
    }

    if (target->iType == W8_TARGET_KIND_GROUP && target->iGroupID != BAD_INDEX) {
        SetGroupHighlight(party_slot, target->iGroupID, true);
    }
}

/* When the ranged target point moves, clear any stale spell-target tint and
   rebuild highlights for the current action at the new position. */
// FUNCTION: WIZ8 0x0053B310
void RefreshSpellTargetHighlightsAtRange(void)
{
    glm::vec3 position;

    GetCameraForwardPoint(kGroundTargetRange, &position);
    if ((position == gXStatus.target_position)) {
        return;
    }

    gXStatus.target_position = position;
    for (const auto& owned_monster : gXStatus.monster_roster.live()) {
        auto* monster_info = owned_monster.get();
        if (monster_info->fActive && monster_info->hp_current != 0 &&
            monster_info->uiCondition[W8_CONDITION_DEAD] == 0) {
            W8Monster* monster = monster_info->p3D;
            glm::vec4 channels;

            channels = monster->m_pRep->highlight_colour;
            if (channels.x != 0.0f || channels.y != 0.0f || channels.z != 0.0f ||
                channels.w != 0.0f) {
                glm::vec4 block;
                block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                MonsterSetHighlightColour(monster, block);
            }
        }
    }
    HighlightSpellTargetsAtCachedPosition();
}

/* Tint every monster the current spell action can reach at the cached target
   point. Non-spell actions leave every monster cleared to the default block. */
// FUNCTION: WIZ8 0x0053B480
void HighlightSpellTargetsAtCachedPosition(void)
{
    int party_slot = g_status.selected_character;
    unsigned int spell_id = GetActionSpellLikeId(party_slot, ChooseEditedAction(party_slot));

    if (spell_id == W8_SPELL_NONE) {
        return;
    }

    W8TargetSource source;
    W8CombatSlot target;
    std::vector<int> markers;
    std::vector<int> scratch;

    SetTargetSourceToCharacter(party_slot, &source);
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    target.point = gXStatus.target_position;
    PopulateSpellTargetMarkers(spell_id, 1, &source, &target, markers, scratch, 0);

    for (int marker : markers) {
        W8Monster* monster = GetMonsterByLocationID(marker);

        glm::vec4 block;
        block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        MonsterSetHighlightColour(monster, block);
    }
}

/* Rebuild the world-space target marker from the selected character's current
   combat action and one world point. When the chosen action carries no
   spell-like id, the marker vector is left empty. */
// FUNCTION: WIZ8 0x0053B660
void PopulateTargetMarkerForCurrentAction(const glm::vec3* position,
                                          std::vector<int>& marker_vector, int enabled)
{
    int party_slot = g_status.selected_character;
    unsigned int spell_id;

    const auto selected_action = ChooseEditedAction(party_slot);
    if (selected_action.kind == W8_ACTION_BREATHE) {
        spell_id = W8_SPELL_SPECIAL_ATTACK_CONE;
    } else if (selected_action.kind == W8_ACTION_CAST_SPELL) {
        spell_id = selected_action.detail;
    } else if (selected_action.kind == W8_ACTION_USE_ITEM) {
        spell_id = GetItemSpell(selected_action.detail_block->item_use.item);
    } else {
        return;
    }
    if (spell_id == W8_SPELL_NONE) {
        return;
    }

    marker_vector.clear();

    if (position == 0) {
        srAssertFail("pSource != NULL", TARGETING_CPP, 0xcc9, 0);
    }

    W8TargetSource source;
    memset(&source, 0, sizeof(source));
    source.iType = W8_TARGET_SOURCE_CHARACTER;
    source.iChar = party_slot;

    W8CombatSlot target;
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    target.point = *position;

    std::vector<int> scratch;
    PopulateSpellTargetMarkers(spell_id, 1, &source, &target, marker_vector, scratch, enabled);
}

/* Clear the target marker and request the party-display refresh that consumes
   the change. */
// FUNCTION: WIZ8 0x0053B160
void ClearTargetMarker(void)
{
    gXStatus.target_markers.clear();
    RequestRefreshPartyState();
}

/* Recompute the target point and refresh the marker only when it differs
   from the cached three-float position. */
// FUNCTION: WIZ8 0x0053B170
void RefreshTargetMarker(void)
{
    glm::vec3 position;

    GetWorldCursorTargetPosition(&position);
    if (!(position == gXStatus.target_position)) {
        gXStatus.target_position = position;
        PopulateTargetMarkerForCurrentAction(&position, gXStatus.target_markers, 1);
    }
}

/* Whether the slot holds a dead character a dead-targeting mode may still
   reach - occupied, actually dead, still reachable while down, and not yet
   beyond reach. */
// FUNCTION: WIZ8 0x0053C2C0
bool IsDeadCharacterTargetable(int party_slot)
{
    if (!g_status.buffers.XChar[party_slot].fOccupied) {
        return false;
    }
    W8Character* character = &g_status.buffers.Char[party_slot];
    if (character->hp_current > 0 ||
        character->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] == 0) {
        return false;
    }
    return character->uiCondition[W8_CONDITION_BEYOND_REACH] <= 0;
}

/* A party slot can participate only while occupied, alive, and below the
   terminal character-state threshold. */
// FUNCTION: WIZ8 0x0053C270
bool CanPartySlotParticipate(int party_slot)
{
    return g_status.buffers.XChar[party_slot].fOccupied &&
           g_status.buffers.Char[party_slot].hp_current != 0 &&
           g_status.buffers.Char[party_slot].highest_condition < W8_CONDITION_DEAD;
}

/* Whether a party slot could aim its chosen action at one whole monster group.
   An untargetable group is refused before anything else is worked out. What
   the action needs then decides how the question is asked: a group-wide need
   is a range test against the group, and a single-monster need is satisfied by
   any one member the slot can reach.

   The source block is built here rather than passed in, so this always asks on
   the character's own behalf. */
// FUNCTION: WIZ8 0x00536d60
bool CanTargetMonsterGroup(int party_slot, W8MonsterGroup* group,
                           const W8ActionSelection& selection, bool sight_only)
{
    W8TargetSource source;
    W8TargetNeed needed;
    unsigned int index;
    int reachable;

    if (MonsterGroupGetRecord(group)->untargetable != 0) {
        return false;
    }

    if (selection.source == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
        needed = W8_TARGET_NEED_NONE;
    } else {

        needed = GetTargetNeededForAction(selection.kind, selection.detail, selection.detail_block);
    }

    SetTargetSourceToCharacter(party_slot, &source);

    if (needed == W8_TARGET_NEED_GROUP) {
        return IsTargetSourceInRangeOfGroup(&source, group,
                                            GetCharActionRange(party_slot, 0, selection));
    }
    if (needed != W8_TARGET_NEED_ENEMY && needed != W8_TARGET_NEED_ALLY) {
        return false;
    }

    reachable = 0;
    for (index = 0; index < group->MemberIds().size(); ++index) {
        W8MonsterInfo* monster_info = FindMonster(group->MemberLocationId(index));

        if (CanPartyMemberAimAtMonster(party_slot, 2, monster_info, selection, false, sight_only)) {
            ++reachable;
        }
    }
    return reachable != 0;
}

/* Re-aim a party slot whose pending action's target can no longer be used:
   outside combat the stored target is simply cleared, while in combat a live
   attack, spell or item action repicks the best monster, optionally trying
   the alternate weapon set, and aims the resolved context's
   block at it. A nonzero `arg` restarts the breath cycle. Repicking the
   executing (out-of-combat) target also copies it to the in-combat target when
   that one no longer matches or reaches. */
// FUNCTION: WIZ8 0x00536570
bool RepickActionTarget(int party_slot, const W8ActionSelection& selection, int arg)
{
    W8CombatSlot new_target;
    W8TargetingContext action_context;
    W8TargetingContext resolved;
    bool is_attack_kind = false;
    bool result = false;
    W8TargetNeed needed;
    W8TargetKind previous_kind;
    int selected;

    if (!CanPartySlotParticipate(party_slot)) {
        return false;
    }

    resolved = selection.source;
    action_context = resolved;

    if (selection.target == 0) {
        srAssertFail("pTarget", TARGETING_CPP, 0xfc, 0);
    }
    needed = GetTargetNeededForAction(selection.kind, selection.detail, selection.detail_block);
    previous_kind = selection.target->iType;
    if (gXStatus.fCombatMode) {
        if (resolved == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
            ++g_combat_state->characters[party_slot].pending_action_repick_count;
        }
        is_attack_kind = selection.kind == W8_ACTION_ATTACK || selection.kind == W8_ACTION_BERSERK;
        if ((is_attack_kind || selection.kind == W8_ACTION_CAST_SPELL ||
             selection.kind == W8_ACTION_USE_ITEM) &&
            needed == W8_TARGET_NEED_ENEMY) {
            selected = ChooseMonsterTarget(party_slot, selection);
            if (selected == -1 && is_attack_kind) {
                if (const auto fallback = ChooseFallbackMonsterTarget(party_slot, selection)) {
                    CommitFallbackWeaponChoice(party_slot, *fallback);
                    selected = fallback->location_id;
                }
            }
            if (selected != -1) {
                memset(&new_target, 0, sizeof(new_target));
                new_target.iChar = -1;
                new_target.iGroupID = -1;
                new_target.iType = W8_TARGET_KIND_MONSTER;
                new_target.iMonsterID = selected;
                // 0x00536876: the new target goes into the selected action's target.
                AimAtTarget(party_slot, &new_target, selection);
                result = true;
                if (arg != 0) {
                    StartBreathCycle(party_slot, false);
                }
                if (resolved == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
                    if (g_settings.verbose_combat_messages != 0) {
                        PostCharacterNotice(party_slot, g_localized_strings[0x26b]);
                    }
                    const auto selected_action =
                        ChooseCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
                    needed = GetTargetNeededForAction(selected_action.kind, selected_action.detail,
                                                      selected_action.detail_block);
                    if (!TargetMatchesNeeded(selected_action.target, needed) ||
                        !CharacterActionReachesTarget(party_slot, 2, selected_action)) {
                        AimAtTarget(party_slot, selection.target, selected_action);
                    }
                }
            }
        }
    }
    if (!result) {
        memset(&new_target, 0, sizeof(new_target));
        new_target.iMonsterID = -1;
        new_target.iChar = -1;
        new_target.iGroupID = -1;
        new_target.iType = W8_TARGET_KIND_NONE;
        AimAtTarget(party_slot, &new_target, selection);
    }
    if (gXStatus.fCombatMode && action_context == W8_TARGETING_CONTEXT_IN_COMBAT &&
        selection.target->iType != previous_kind) {
        RequestPartySlotRedraw(party_slot);
    }
    return result;
}

/* Test the already selected action against its target kind and reach. */
// FUNCTION: WIZ8 0x00536f60
bool TargetIsInPlay(int party_slot, int hand, const W8ActionSelection& selection)
{
    if (!CanPartySlotParticipate(party_slot)) return false;
    const auto needed =
        GetTargetNeededForAction(selection.kind, selection.detail, selection.detail_block);
    return TargetMatchesNeeded(selection.target, needed) &&
           CharacterActionReachesTarget(party_slot, hand, selection);
}

/* Whether a party slot's chosen action can be aimed at one monster. The
   monster has to be live, standing and reachable; while the camp screen is up
   only the monster it is showing can be aimed at at all.

   What the action needs decides the rest: a group need is answered against the
   monster's group, a single-monster need only when the caller allows one, and
   an action needing nothing is refused outright in combat. The database's
   untargetable flag is checked last rather than first, so an action needing
   nothing still reaches a monster carrying it. */
// FUNCTION: WIZ8 0x00536ad0
bool CanTargetMonster(int party_slot, int location_id, bool allow_single_target,
                      bool notify_failure, const W8ActionSelection& selection, bool sight_only)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);
    W8MonsterRecord* record;
    W8TargetNeed needed;

    if (!monster_info->fActive) {
        return false;
    }
    if (monster_info->hp_current == 0) {
        return false;
    }
    if (monster_info->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0) {
        return false;
    }
    if (gXStatus.fCampMode && g_npc_interaction_state->target_location_id != location_id) {
        return false;
    }

    if (selection.source == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
        needed = W8_TARGET_NEED_NONE;
        if (gXStatus.fCombatMode) {
            return false;
        }
    } else {

        needed = GetTargetNeededForAction(selection.kind, selection.detail, selection.detail_block);

        if (needed != W8_TARGET_NEED_ENEMY && needed != W8_TARGET_NEED_GROUP) {
            if (needed == W8_TARGET_NEED_ALLY) {
                if (!allow_single_target) {
                    return false;
                }
            } else {
                if (needed != W8_TARGET_NEED_NONE) {
                    return false;
                }
                if (gXStatus.fCombatMode) {
                    return false;
                }
            }
        }
    }

    record = GetMonsterDataForInfo(monster_info);
    if (record->untargetable != 0 && needed != W8_TARGET_NEED_NONE) {
        return false;
    }
    if (needed == W8_TARGET_NEED_GROUP) {
        return IsSlotInRangeOfGroup(party_slot, monster_info->monster_group_id, selection,
                                    notify_failure);
    }
    return CanPartyMemberAimAtMonster(party_slot, 2, monster_info, selection, notify_failure,
                                      sight_only);
}

/* Whether a party slot's chosen action has anything at all to aim at. Each
   action asks its own question: an attack looks for one targetable monster, a
   fifth-kind action looks at the other party members first and then at the
   monsters, and casting or using an item hands the question to the spell.

   An action this does not know about answers yes, so the check narrows rather
   than gates - only the actions with a way of being impossible can fail it.
   The two dialogue-driven actions answer yes as well until the dialogue has
   settled, since until then there is no spell or item to ask about. */
// FUNCTION: WIZ8 0x0053cdf0
bool SlotHasAnyValidTarget(int party_slot, const W8ActionSelection& selection, bool sight_only)
{
    W8CombatSlot target;
    unsigned int index;
    unsigned int other_slot;

    switch (selection.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
            if (CanTargetMonster(party_slot, GetMonsterListEntry(index)->location_id, false, false,
                                 selection, sight_only)) {
                return true;
            }
        }
        return false;

    case W8_ACTION_CAST_SPELL:
        if (g_level_block->selection_settled) {
            return SpellHasAnyValidTarget(party_slot, selection.detail, false);
        }
        break;

    case W8_ACTION_USE_ITEM:
        if (g_level_block->selection_settled) {
            const W8ItemInstance* item = selection.detail_block->item_use.item;

            if (item == 0) {
                return false;
            }
            return SpellHasAnyValidTarget(
                party_slot, g_item_records[item->iItemNo].spell_id,
                ItemClassNormalizesTarget(&g_item_records[item->iItemNo]));
        }
        break;
    case W8_ACTION_PROTECT:
        for (other_slot = 0; other_slot < 8; ++other_slot) {
            if (other_slot == static_cast<unsigned int>(party_slot)) {
                continue;
            }
            memset(&target, 0, sizeof(target));
            target.iMonsterID = BAD_INDEX;
            target.iGroupID = BAD_INDEX;
            target.iType = W8_TARGET_KIND_CHARACTER;
            target.iChar = other_slot;
            if (CharacterHasAttackOn(party_slot, &target)) {
                return true;
            }
        }
        for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
            memset(&target, 0, sizeof(target));
            target.iChar = BAD_INDEX;
            target.iGroupID = BAD_INDEX;
            target.iType = W8_TARGET_KIND_MONSTER;
            target.iMonsterID = GetMonsterListEntry(index)->location_id;
            if (CharacterHasAttackOn(party_slot, &target)) {
                return true;
            }
        }
        return false;
    default:
        break;
    }
    return true;
}

/* gXStatus.monster_groups: every monster group in the level. */

/* The spell target kinds this has an opinion about. Everything else is
   answered yes outright, so the question only ever narrows. */
/* Whether a spell has anything to be cast at. What it needs decides the sweep:
   one monster looks for a targetable one, a group looks for a targetable
   group, and the raise-the-dead kind looks for a party member who is down but
   still reachable - occupied, out of hit points, carrying the eighteenth
   condition and not the nineteenth, which is the same pair IsTargetStillPresent
   reads for its indirect character kind.

   Out of combat the one-monster kind answers yes without looking, since
   anything in the level can be walked up to. */
// FUNCTION: WIZ8 0x0053d010
bool SpellHasAnyValidTarget(int party_slot, int spell_id, bool normalize)
{
    const W8ActionSelection selection{W8_ACTION_CAST_SPELL, spell_id, nullptr, nullptr,
                                      gXStatus.fCombatMode ? W8_TARGETING_CONTEXT_IN_COMBAT
                                                           : W8_TARGETING_CONTEXT_OUT_OF_COMBAT};
    unsigned int index;

    switch (GetTargetNeededForSpellFriendly(spell_id, normalize, false)) {
    case W8_TARGET_NEED_ENEMY:
        if (g_settings.autotarget_spells != 0) {
            return true;
        }
        for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
            if (CanTargetMonster(party_slot, GetMonsterListEntry(index)->location_id, false, false,
                                 selection, false)) {
                return true;
            }
        }
        return false;

    case W8_TARGET_NEED_GROUP:
        for (index = 0; index < gXStatus.monster_groups.size(); ++index) {
            W8MonsterGroup* group = GetMonsterGroupByListIndex(index);

            if (group->members_active &&
                CanTargetMonsterGroup(party_slot, group, selection, false)) {
                return true;
            }
        }
        return false;

    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        for (index = 0; index < 8; ++index) {
            if (g_status.buffers.XChar[index].fOccupied &&
                g_status.buffers.Char[index].hp_current == 0 &&
                g_status.buffers.Char[index].uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0 &&
                g_status.buffers.Char[index].uiCondition[W8_CONDITION_BEYOND_REACH] == 0) {
                return true;
            }
        }
        return false;

    default:
        return true;
    }
}

/* One candidate in the angle sort: the screen angle to the monster and the
   monster itself. The angle leads so that the ordinary signed comparison sorts
   on it. */
struct W8GroupMemberByAngle {
    int angle;
    int location_id;
};

/* Step to the next member of a group, going round the party rather than
   through the list: the candidates are sorted by the angle from the party to
   each of them, and the one after whichever is currently picked is taken,
   wrapping at the end. With nothing picked yet the leftmost is taken.

   The angle is what makes this feel like cycling across the screen rather than
   jumping about, and it is why the sort buffer holds a pair per candidate
   rather than just the ids. */
// FUNCTION: WIZ8 0x005383e0
static int SelectNextGroupMemberByAngle(const std::vector<int>& candidates, int current)
{
    unsigned int count = static_cast<unsigned int>(candidates.size());
    std::vector<W8GroupMemberByAngle> sorted(count);
    glm::vec3 party;
    unsigned int index;
    int result;

    if (count == 0) {
        return BAD_INDEX;
    }
    party = cameraLocation();

    for (index = 0; index < count; ++index) {
        int location_id = candidates[static_cast<int>(index)];
        W8MonsterInfo* monster_info = FindMonster(location_id);
        glm::vec3 position = monster_info->p3D->GetPosition();

        sorted[index].location_id = location_id;
        /* Whole degrees: sorting on the radian heading would leave seven buckets. */
        sorted[index].angle =
            static_cast<int>(NormalizeAngle(GetHeadingAngle(&party, &position)) * 57.295776f);
    }
    std::ranges::sort(sorted, {}, &W8GroupMemberByAngle::angle);

    result = sorted[0].location_id;
    if (current != BAD_INDEX) {
        for (index = 0; index < count; ++index) {
            if (sorted[index].location_id == current) {
                result = sorted[index + 1 < count ? index + 1 : 0].location_id;
                break;
            }
        }
    }
    return result;
}

/* Which member of a group the party should pick out next. Every member the
   slot could aim at becomes a candidate - including the single-target case,
   which is what the third argument allows - and the group's own record of
   where it got to decides which of them comes next. */
/* Which monster the party should pick out next when the action wants a single
   monster. Every live monster the slot could aim at becomes a candidate and
   the angular sweep steps on from whichever monster is currently picked. */
// FUNCTION: WIZ8 0x00538140
int PickNextTargetableMonster(int party_slot, const W8ActionSelection& selection, bool sight_only)
{
    std::vector<int> targetable;
    unsigned int index;

    for (index = 0; index < gXStatus.monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (CanTargetMonster(party_slot, monster_info->location_id, true, false, selection,
                             sight_only)) {
            targetable.push_back(monster_info->location_id);
        }
    }
    return SelectNextGroupMemberByAngle(targetable, gXStatus.picked_monster);
}

// FUNCTION: WIZ8 0x00538280
static int PickNextTargetableGroupMember(int party_slot, W8MonsterGroup* group,
                                         const W8ActionSelection& selection, bool sight_only)
{
    std::vector<int> targetable;
    unsigned int index;

    for (index = 0; index < group->MemberIds().size(); ++index) {
        int location_id = group->MemberLocationId(index);

        FindMonster(location_id);
        if (CanTargetMonster(party_slot, location_id, true, false, selection, sight_only)) {
            targetable.push_back(location_id);
        }
    }
    return SelectNextGroupMemberByAngle(targetable, group->highlighted_member);
}

/* Point one party slot at a monster group, or at one monster inside it. An
   action that wants the whole group is aimed at the group and nothing else
   happens; anything else steps to the next targetable member and moves the
   highlight to it.

   The step is taken three times over: once to find where to go, once - before
   the group's record is updated - to find where the highlight currently is so
   it can be put out, and once after to find it again so it can be lit. That
   the same call answers differently each time is exactly what the group's own
   record of where it got to is for. */
// FUNCTION: WIZ8 0x00537b00
void AimAtMonsterGroupMember(int party_slot, W8MonsterGroup* group)
{
    const bool sight_only = IsMonsterSighting();
    const auto selected_action = ChooseEditedAction(party_slot);
    W8CombatSlot target;
    W8TargetSource source;
    int picked;
    int previous;
    glm::vec4 block;

    if (selected_action.source != W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
        if (GetTargetNeededForAction(selected_action.kind, selected_action.detail,
                                     selected_action.detail_block) == W8_TARGET_NEED_GROUP) {
            memset(&target, 0, sizeof(target));
            target.iChar = BAD_INDEX;
            target.iMonsterID = BAD_INDEX;
            target.iType = W8_TARGET_KIND_GROUP;
            target.iGroupID = group->group_id;
            AimAtTarget(party_slot, &target, selected_action);
            StartBreathCycle(party_slot, false);
            SetTargetSourceToCharacter(party_slot, &source);
            PointCameraAtCombatTarget(&source, selected_action.target);
            return;
        }
    }

    picked = PickNextTargetableGroupMember(party_slot, group, selected_action, sight_only);
    if (picked == BAD_INDEX) {
        return;
    }

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_MONSTER;
    target.iMonsterID = picked;
    AimAtTarget(party_slot, &target, selected_action);

    previous = PickNextTargetableGroupMember(party_slot, group, selected_action, sight_only);
    if (previous != BAD_INDEX) {
        block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        MonsterSetHighlightColour(GetMonsterByLocationID(previous), block);
    }

    group->highlighted_member = picked;

    picked = PickNextTargetableGroupMember(party_slot, group, selected_action, sight_only);
    if (picked != BAD_INDEX) {
        block = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        MonsterSetHighlightColour(GetMonsterByLocationID(picked), block);
    }
    g_level_block->pick_changed = true;

    StartBreathCycle(party_slot, false);
    SetTargetSourceToCharacter(party_slot, &source);
    PointCameraAtCombatTarget(&source, selected_action.target);
}

/* The group the party currently has picked out, by id, and -1 when
   none. It is where the sweep below starts from and wraps back to. The
   declaration lives in targeting.h (C linkage); the definition is with the
   other targeting globals near the top of this file. */

/* Which monster group the party should pick out next. The sweep starts at the
   one after whichever is currently picked and goes round until it comes back
   to where it began, so the same group is only offered again when nothing else
   will do. A picked group that has since gone away resets the starting point
   rather than ending the sweep. */
// FUNCTION: WIZ8 0x00537ed0
int PickNextTargetableGroup(int party_slot, const W8ActionSelection& selection, bool sight_only)
{
    unsigned int count = gXStatus.monster_groups.size();
    unsigned int start;
    unsigned int index;

    if (count == 0) {
        return BAD_INDEX;
    }

    if (gXStatus.picked_group == BAD_INDEX) {
        start = 0;
    } else {
        start = GetMonsterGroupIndexByID(gXStatus.picked_group, false);
        if (start == 0xffffffff) {
            start = 0;
            gXStatus.picked_group = BAD_INDEX;
        } else {
            ++start;
            if (start == gXStatus.monster_groups.size()) {
                start = 0;
            }
        }
    }

    index = start;
    do {
        W8MonsterGroup* group = GetMonsterGroupByListIndex(index);

        if (CanTargetMonsterGroup(party_slot, group, selection, sight_only)) {
            return group->group_id;
        }
        ++index;
        if (index == gXStatus.monster_groups.size()) {
            index = 0;
        }
    } while (index != start);

    return BAD_INDEX;
}

/* The cycle-target key: step the party's pick to the next thing the slot's
   action can aim at. An action that needs a whole group steps between groups
   and remembers the new pick; anything else steps between monsters. Nothing
   targetable leaves the current pick alone. Either way the new aim is
   committed and the slot starts breathing again. */
// FUNCTION: WIZ8 0x00537D20
void CycleToNextTarget(int party_slot)
{
    const bool sight_only = IsMonsterSighting();
    const auto selected_action = ChooseEditedAction(party_slot);
    W8CombatSlot target;
    W8TargetSource source;
    int pick;
    bool pick_group = false;

    if (selected_action.source != W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
        switch (selected_action.kind) {
        case W8_ACTION_CAST_SPELL:
            pick_group = GetTargetNeededForSpellFriendly(selected_action.detail, false, false) ==
                         W8_TARGET_NEED_GROUP;
            break;
        case W8_ACTION_USE_ITEM:
            pick_group = GetTargetNeededForItem(selected_action.detail_block->item_use.item) ==
                         W8_TARGET_NEED_GROUP;
            break;
        default:
            break;
        }
    }
    if (pick_group) {
        pick = PickNextTargetableGroup(party_slot, selected_action, sight_only);
        if (pick == BAD_INDEX) {
            return;
        }
        memset(&target, 0, sizeof(target));
        target.iMonsterID = BAD_INDEX;
        target.iChar = BAD_INDEX;
        target.iType = W8_TARGET_KIND_GROUP;
        target.iGroupID = pick;
        AimAtTarget(party_slot, &target, selected_action);
        gXStatus.picked_group = pick;
    } else {
        pick = PickNextTargetableMonster(party_slot, selected_action, sight_only);
        if (pick == BAD_INDEX) {
            return;
        }
        memset(&target, 0, sizeof(target));
        target.iChar = BAD_INDEX;
        target.iGroupID = BAD_INDEX;
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = pick;
        AimAtTarget(party_slot, &target, selected_action);
        gXStatus.picked_monster = pick;
    }
    StartBreathCycle(party_slot, false);
    SetTargetSourceToCharacter(party_slot, &source);
    PointCameraAtCombatTarget(&source, selected_action.target);
    g_level_block->pick_changed = true;
}

/* Re-evaluate each edited action after sight or range changes, then consider
   the committed action's alternate equipment. */
// FUNCTION: WIZ8 0x0053bf80
void RefreshAllPartyTargets(void)
{
    for (int party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
        W8Character* character = &g_status.buffers.Char[party_slot];

        if (row->fOccupied &&
            (character->hp_current != 0 || character->highest_condition < W8_CONDITION_DEAD)) {
            const auto selected_action = ChooseEditedAction(party_slot);
            W8CombatSlot* target = selected_action.target;
            bool can_switch = CanEditCharacterAction(party_slot, selected_action, false, false);

            if (can_switch) {
                RefreshCombatTargetHighlights(party_slot, target);
            } else {
                can_switch = CanEditCharacterAction(party_slot, selected_action, true, false);
                if (can_switch) {
                    RepickActionTarget(party_slot, selected_action, 0);
                } else if (target && target->iType != W8_TARGET_KIND_NONE) {
                    W8CombatSlot action;

                    memset(&action, 0, sizeof(action));
                    action.iChar = BAD_INDEX;
                    action.iMonsterID = BAD_INDEX;
                    action.iGroupID = BAD_INDEX;
                    AimAtTarget(party_slot, &action, selected_action);
                }
            }

            const auto committed_action =
                ChooseCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
            if (IsPartySlotEligible(party_slot) &&
                (row->action == W8_ACTION_ATTACK || row->action == W8_ACTION_BERSERK) &&
                (row->weapon_swap_pending ||
                 CharacterCanSwitchTo(party_slot, committed_action, false, false))) {
                const auto fallback = ChooseFallbackMonsterTarget(party_slot, committed_action);
                if (fallback) {
                    CommitFallbackWeaponChoice(party_slot, *fallback);
                    const auto selected = fallback->location_id;
                    W8CombatSlot action;

                    memset(&action, 0, sizeof(action));
                    action.iType = W8_TARGET_KIND_MONSTER;
                    action.iChar = BAD_INDEX;
                    action.iMonsterID = selected;
                    action.iGroupID = BAD_INDEX;
                    AimAtTarget(party_slot, &action, committed_action);
                }
            }
        }
    }
}

/* Combat-panel refresh: recount each active group's on-screen and selectable
   members; when either count changed, ask for a party redraw and store the new
   pair in the group's scratch slot. */
// FUNCTION: WIZ8 0x005398D0
void RefreshMonsterTargetCounts(void)
{
    glm::vec3 camera;
    unsigned int group_index;

    for (group_index = 0; group_index < gXStatus.monster_groups.size(); ++group_index) {
        W8MonsterGroup* group = GetMonsterGroupByListIndex(group_index);
        int on_screen_count;
        int selectable_count;

        if (!group->members_active) {
            continue;
        }
        on_screen_count = 0;
        selectable_count = 0;
        for (unsigned int index = 0; index < group->MemberIds().size(); ++index) {
            int location_id = group->MemberLocationId(index);
            W8MonsterInfo* monster_info = FindMonster(location_id);

            if (!monster_info->p3D->IsWithinWorldRange()) {
                continue;
            }
            if (monster_info == 0) {
                srAssertFail("pMonsterInfo", TARGETING_CPP, 0x7f8, 0);
            }
            camera = cameraLocation();
            const auto bounds = MonsterGetWorldAnimationBounds(monster_info->p3D);
            if (bounds && targetMarkerVisible(camera, *bounds) != 0) {
                on_screen_count += 1;
            }
            if (g_status.selected_character == -1 ||
                CanPartyMemberAimAtMonster(g_status.selected_character, 2, monster_info,
                                           ChooseEditedAction(g_status.selected_character), false,
                                           IsMonsterSighting())) {
                selectable_count += 1;
            }
        }
        if (on_screen_count != group->visible_member_count ||
            selectable_count != group->selectable_member_count) {
            RequestRedrawParty();
            group->visible_member_count = on_screen_count;
            group->selectable_member_count = selectable_count;
        }
    }
}

/* Whether `monster` is closer than `max_distance` to the player and its
   elevated centre or either animation-bound corner still projects on the world
   camera. `position` is unused; the distance is measured to the player. */
// FUNCTION: WIZ8 0x0053A060
bool IsMonsterVisibleWithinDistance(W8Monster* monster, const glm::vec3* position,
                                    float max_distance)
{
    if (!(monster->GetDistanceToPlayer() < max_distance)) return false;
    const auto bounds = MonsterGetWorldAnimationBounds(monster);
    if (!bounds) return false;
    auto center = monster->movement.position;
    center.y += monster->movement.height_offset;
    const std::array points{center, bounds->minimum, bounds->maximum};
    return g_world->camera->projectsAny(points);
}

/* Any live monster visible to the camera within the far-clip range, resuming
   the scan at the last match. */
// FUNCTION: WIZ8 0x0053A1D0
bool AnyMonsterVisible(void)
{
    glm::vec3 camera;
    float limit;
    int count;
    int index;

    limit = static_cast<float>(WorldGetFarClip(GetWorld())) * 1.5f;
    if (g_world == 0 || g_world->camera == 0) {
        return false;
    }
    camera = cameraLocation();
    count = gXStatus.monster_roster.live().size();
    if (0 <= g_last_visible_monster && g_last_visible_monster < count) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(g_last_visible_monster);
        if (monster_info->p3D != 0 &&
            IsMonsterVisibleWithinDistance(monster_info->p3D, &camera, limit)) {
            return true;
        }
    }
    for (index = 0; index < count; ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (monster_info->p3D != 0 &&
            IsMonsterVisibleWithinDistance(monster_info->p3D, &camera, limit)) {
            g_last_visible_monster = index;
            return true;
        }
    }
    return false;
}

/* Mode-3 targeting tick: pop the front marker off the queue and tint the
   monster green when it has line of sight from the target point. */
// FUNCTION: WIZ8 0x0053B1D0
void UpdateTargetMarkerHighlight(void)
{
    glm::vec4 block;
    glm::vec3 point;
    if (static_cast<int>(gXStatus.target_markers.size()) <= 0) {
        return;
    }
    {
        int location_id = gXStatus.target_markers[0];
        gXStatus.target_markers.erase(gXStatus.target_markers.begin());
        W8Monster* monster;
        monster = GetMonsterByLocationID(location_id);
        point = gXStatus.target_position;
        if (monster->HasLineOfSightFromPoint(point)) {
            block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            MonsterSetHighlightColour(monster, block);
            return;
        }
        memset(&block, 0, sizeof(block));
        MonsterSetHighlightColour(monster, block);
    }
}

/* Pick the next targetable member of the group and paint it with the party
   slot's highlight color - clear for 0, green for 1, red for anything the
   callers pass as 2. */
// FUNCTION: WIZ8 0x00538510
void HighlightPickedGroupMember(int party_slot, W8MonsterGroup* group, W8TargetHighlight color)
{
    glm::vec4 block;

    int location_id = PickNextTargetableGroupMember(
        party_slot, group, ChooseEditedAction(party_slot), IsMonsterSighting());
    if (location_id != -1) {
        W8Monster* monster = GetMonsterByLocationID(location_id);
        if (color == W8_TARGET_HIGHLIGHT_NONE) {
            block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        } else if (color == W8_TARGET_HIGHLIGHT_GREEN) {
            block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (color == W8_TARGET_HIGHLIGHT_RED) {
            block = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        MonsterSetHighlightColour(monster, block);
    }
}

/* Commit to the selected storage and retain action-specific remembered targets. */
// FUNCTION: WIZ8 0x005387f0
bool CommitActionTarget(int actor, const W8ActionSelection& action, const W8CombatSlot& target)
{
    if (!action.target || *action.target == target) return false;
    *action.target = target;
    if (action.source == W8_TARGETING_CONTEXT_IN_COMBAT && target.iType != W8_TARGET_KIND_NONE) {
        auto& row = g_status.buffers.XChar[actor];
        switch (action.kind) {
        case W8_ACTION_CAST_SPELL:
            row.spell_target = target;
            break;
        case W8_ACTION_USE_ITEM:
            row.item_target = target;
            break;
        case W8_ACTION_BREATHE:
            row.breath_target = target;
            break;
        default:
            break;
        }
    }
    return true;
}

/* Tint every member of a monster group with the party-slot highlight color.
   The debug format keeps the authored name ModifyGroupColor. */
// FUNCTION: WIZ8 0x005392e0
void ModifyGroupColor(int group_id, W8TargetHighlight color)
{
    glm::vec4 block;

    if (group_id == -1) {
        srAssertFail("uiGroupID != BAD_INDEX", TARGETING_CPP, 0x6e5, 0);
    }
    W8MonsterGroup* group = FindMonsterGroup(group_id, false);
    if (group == nullptr) {
        if (color != W8_TARGET_HIGHLIGHT_NONE) {
            DebugMessage(
                1, fmt::format("ERROR: ModifyGroupColor - invalid ID, groupID {:d}, color {:d}",
                               group_id, fmt::underlying(color)));
        }
        return;
    }
    if (color == W8_TARGET_HIGHLIGHT_NONE) {
        block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    } else if (color == W8_TARGET_HIGHLIGHT_GREEN) {
        block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    } else if (color == W8_TARGET_HIGHLIGHT_RED) {
        block = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    for (unsigned int index = 0; index < group->MemberIds().size(); ++index) {
        W8Monster* monster = GetMonsterByLocationID(group->MemberLocationId(index));
        MonsterSetHighlightColour(monster, block);
    }
}

/* Whether `source` can currently see `monster_info`: the party threat record's
   sight flag for a character, the mon-to-mon visibility record for a monster
   source, and a point line-of-sight probe for an indirect source. */
// FUNCTION: WIZ8 0x00539a30
unsigned char SourceCanSeeMonster(const W8TargetSource* source, W8MonsterInfo* monster_info,
                                  unsigned char flag_index, int sight_flag)
{
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        if (source->iChar == -1) {
            srAssertFail("pSource->iChar != BAD_INDEX", TARGETING_CPP, 0xce3, 0);
        }
        return monster_info->party_threat.los_flags[flag_index];
    }
    if (source->iType == W8_TARGET_SOURCE_MONSTER) {
        if (source->iMonsterID == -1) {
            srAssertFail("pSource->iMonsterID != BAD_INDEX", TARGETING_CPP, 0xcf8, 0);
        }
        if (source->iMonsterID == -1) {
            srAssertFail("pSource->iMonsterID != -1", TARGETING_CPP, 0x84c, 0);
        }
        W8MonsterInfo* source_info = FindMonster(source->iMonsterID);
        W8VisibilityRecord* visibility = FindMonToMonVisibility(source_info, monster_info);
        if (visibility != 0 && visibility->los_flags[sight_flag]) {
            return 1;
        }
        return 0;
    }
    if (source->iType != W8_TARGET_SOURCE_INDIRECT) {
        srAssertFail("pSource->iType == SOURCE_TYPE_3D_POINT", TARGETING_CPP, 0x854, 0);
    }
    return monster_info->p3D->HasLineOfSightFromPoint(source->point);
}

/* Whether `target` is inside the aim cone: within `bonus` + `eye_radius` of
   `eye`, and within the heading and elevation arc bounds. */
// FUNCTION: WIZ8 0x00539b70
bool TargetInRangeAndArcs(const glm::vec3* target, float bonus, const glm::vec3* eye,
                          float eye_radius, float heading, float elevation, float maximum_range)
{
    float dx = target->x - eye->x;
    float dy = target->y - eye->y;
    float dz = target->z - eye->z;
    float excess =
        static_cast<float>(floor(sqrt(dx * dx + dy * dy + dz * dz) - bonus - eye_radius));
    if (maximum_range >= excess) {
        float angle = GetHeadingAngle(eye, target);
        if (NormalizeAngle(heading - angle) < kTargetingQuarterPi ||
            NormalizeAngle(angle - heading) < kTargetingQuarterPi) {
            angle = GetElevationAngle(eye, target);
            if (NormalizeAngle(elevation - angle) < kTargetingQuarterPi ||
                NormalizeAngle(angle - elevation) < kTargetingQuarterPi) {
                return true;
            }
        }
    }
    return false;
}

/* Append the location ids of live targetable monsters inside the aim cone to
   `found`. `disposition` filters ubDisposition, with 3 admitting every
   disposition; `sight_flag` picks the visibility flag the source must have. */
// FUNCTION: WIZ8 0x00539ca0
int CollectConeMonsterTargets(const W8TargetSource* source, const glm::vec3* eye, float heading,
                              float elevation, std::vector<int>& found, unsigned char disposition,
                              int sight_flag, float maximum_range)
{
    int start_count = static_cast<int>(found.size());
    float radius;
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        radius = g_startup_world->radius;
    } else if (source->iType == W8_TARGET_SOURCE_MONSTER) {
        W8MonsterInfo* source_info = FindMonster(source->iMonsterID);
        radius = source_info->p3D->radius;
    } else {
        radius = 0.0f;
    }
    for (const auto& owned_monster : gXStatus.monster_roster.live()) {
        auto* monster_info = owned_monster.get();
        if (monster_info->fActive && monster_info->hp_current != 0 &&
            monster_info->uiCondition[W8_CONDITION_DEAD] == 0) {
            W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
            if (record->untargetable == 0 &&
                (monster_info->ubDisposition == disposition || disposition == 3)) {
                W8Monster* monster = monster_info->p3D;
                glm::vec3 point;
                point = glm::vec3(monster->movement.position.x,
                                  monster->movement.position.y + monster->movement.height_offset,
                                  monster->movement.position.z);
                if (TargetInRangeAndArcs(&point, monster->radius, eye, radius, heading, elevation,
                                         maximum_range) &&
                    SourceCanSeeMonster(source, monster_info, 1, sight_flag) != 0) {
                    found.push_back(monster_info->location_id);
                }
            }
        }
    }
    return static_cast<int>(found.size()) - start_count;
}

/* Set the targeting filter for the spell being aimed: build the context
   target for the spell's target type, aim the selected slot at it, then pick
   the cursor, cone and world-pause state the resulting mode needs. */
// FUNCTION: WIZ8 0x0053a440
void ConfigureSpellTargetFilter(W8SpellTargetType target_type, W8TargetNeed needed_kind)
{
    W8CombatSlot target;
    int cursor = -1;
    int selected = g_status.selected_character;

    switch (target_type) {
    case W8_TARGET_TYPE_CASTER:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = selected;
        break;
    case W8_TARGET_TYPE_PARTY:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_PARTY;
        break;
    case W8_TARGET_TYPE_ENEMY:
        if (gXStatus.fCampMode) {
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_MONSTER;
            target.iMonsterID = g_npc_interaction_state->target_location_id;
            AimAtTarget(selected, &target, ChooseEditedAction(selected));
            needed_kind = W8_TARGET_NEED_NONE;
        } else if (g_settings.autotarget_spells != 0 &&
                   RepickActionTarget(selected, ChooseSharedAction(selected), 0)) {
            W8TargetSource source;
            SetTargetSourceToCharacter(selected, &source);
            PointCameraAtCombatTarget(&source, &g_status.buffers.XChar[selected].target_in_combat);
        }
        goto aim_done;
    case W8_TARGET_TYPE_ALL_ENEMIES:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_ALL_ENEMIES;
        break;
    case W8_TARGET_TYPE_LOCK_OR_TRAP:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_EIGHT;
        break;
    default:
        ResetCombatSlot(&target);
        break;
    }
    AimAtTarget(selected, &target, ChooseEditedAction(selected));
aim_done:
    gXStatus.iTargetingMode = needed_kind;
    switch (needed_kind) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        cursor = 3;
        break;
    case W8_TARGET_NEED_ENEMY:
        cursor = 0;
        break;
    case W8_TARGET_NEED_PLACE:
        cursor = 2;
        break;
    case W8_TARGET_NEED_CONE:
        cursor = 0xc;
        break;
    case W8_TARGET_NEED_GROUP:
        cursor = 0xb;
        break;
    default:
        break;
    }
    if (cursor != gXStatus.iCurrentCursor) {
        SetTargetCursor(cursor);
    }
    gXStatus.target_position = glm::vec3(0);
    RequestRefreshPartyState();
    if (needed_kind == W8_TARGET_NEED_CONE) {
        SetTargetConeEnabled(true);
        PauseMainGameWorld();
        return;
    }
    SetTargetConeEnabled(false);
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        ResumeMainGameWorld();
    }
}

/* Whether the pending item use needs a target picked: most spell target types
   need one, type 3 only when auto-targeting cannot pick it (never in camp). */
// FUNCTION: WIZ8 0x0053a770
bool ItemUseNeedsTarget(int party_slot)
{
    W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
    W8ItemInstance* item = FindCharacterItemAt(party_slot, row->item_origin, row->item_slot);
    bool normalize = ItemClassNormalizesTarget(&g_item_records[item->iItemNo]);
    switch (GetSpellTargetType(GetItemSpell(item), normalize)) {
    case 3:
        if (gXStatus.fCampMode) {
            return false;
        }
        return g_settings.autotarget_spells == 0;
    case 0:
    case 2:
    case 7:
    case 10:
        return false;
    default:
        return true;
    }
}

/* Drop the party slot's per-monster highlight bits and take the interface out
   of targeting: cursor, target cone and world pause all return to normal, and
   in combat the character's own target highlights come back up. */
// FUNCTION: WIZ8 0x0053b050
void ClearSlotTargeting(int party_slot)
{
    ClearPartySlotMonsterHighlights(party_slot);
    gXStatus.iTargetingMode = W8_TARGET_NEED_NONE;
    if (gXStatus.iCurrentCursor != W8_CURSOR_NONE) {
        SetTargetCursor(W8_CURSOR_NONE);
    }
    gXStatus.target_position = glm::vec3(0);
    RequestRefreshPartyState();
    SetTargetConeEnabled(false);
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        ResumeMainGameWorld();
    }
    if (gXStatus.fCombatMode &&
        CharacterCanSwitchTo(party_slot,
                             ChooseCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT), false,
                             false)) {
        RefreshCombatTargetHighlights(party_slot,
                                      &g_status.buffers.XChar[party_slot].target_in_combat);
    }
}

/* Raise or clear the party slot's highlight bit on every active monster.
   Clearing while spell targeting is up repaints the spell's own marks. */
// FUNCTION: WIZ8 0x0053c130
void UpdateSlotMonsterHighlights(int party_slot, bool enable)
{
    if ((party_slot != g_status.selected_character) ||
        (gXStatus.iTargetingMode == W8_TARGET_NEED_NONE)) {
        for (const auto& owned_monster_info : gXStatus.monster_roster.live()) {
            auto* monster_info = owned_monster_info.get();
            W8Monster* monster = monster_info->p3D;
            if (monster_info->fActive && monster != 0) {
                unsigned char flag = MonsterGetHighlightMask(monster);
                glm::vec4 block;
                if (enable && (flag & (1 << (party_slot & 0x1f))) != 0) {
                    block = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                } else {
                    block = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                }
                MonsterSetHighlightColour(monster, block);
            }
        }
        RequestRedrawParty();
        if (!enable && gXStatus.iTargetingMode == W8_TARGET_NEED_CONE) {
            HighlightSpellTargetsAtCachedPosition();
        }
    }
}

/* Pick an attack fallback, allowing the character's alternate weapon set when
   the ordinary group selection has no usable monster. Equipment is only
   evaluated here; the caller commits the returned choice after success. */
// FUNCTION: WIZ8 0x0053c990
std::optional<W8FallbackMonsterTarget>
ChooseFallbackMonsterTarget(int party_slot, const W8ActionSelection& selection)
{
    const auto& row = g_status.buffers.XChar[party_slot];
    const auto& character = g_status.buffers.Char[party_slot];
    if (!g_settings.autoswap_weapons ||
        gXStatus.monster_manager_entries[party_slot].item_swap_in_progress ||
        row.item_action_pending ||
        (g_combat_state->execution_active && g_combat_state->characters[party_slot].dead &&
         g_combat_state->characters[party_slot].phase != 0) ||
        IsItemBoundToWearer(&character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON]) ||
        IsItemBoundToWearer(&character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON]))
        return std::nullopt;
    const auto plan = PlanWeaponSetSwap(character);
    if (!plan || (!plan->swap_hands[0] && !plan->swap_hands[1])) return std::nullopt;
    const auto ranges = WeaponSetAttackRanges(*plan->weapons[0], *plan->weapons[1]);
    int result = ChooseMonsterTarget(party_slot, selection, &ranges);
    if (result == -1) return std::nullopt;
    if (selection.target && selection.target->iType == W8_TARGET_KIND_MONSTER) {
        auto* current = FindMonster(selection.target->iMonsterID, false);
        if (current &&
            CanPartyMemberAimAtMonster(party_slot, 2, current, selection, false, false, &ranges))
            result = current->location_id;
    }
    return W8FallbackMonsterTarget{result, *plan};
}

/* When combat ends, swap each occupied, living slot whose pending swap flag is
   set back to its primary weapon set, provided auto-swap is on and nothing is
   already swapping. */
// FUNCTION: WIZ8 0x0053cd60
void ReconcilePartyEquipmentAfterCombat(void)
{
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
        W8Character* character = &g_status.buffers.Char[party_slot];
        if (row->fOccupied &&
            (character->hp_current != 0 || character->highest_condition < W8_CONDITION_TURNCOAT) &&
            row->weapon_swap_pending && g_settings.autoswap_weapons != 0 &&
            !row->item_action_pending) {
            SwapWeaponSetSlots(party_slot, false, true);
            row->weapon_swap_pending = false;
        }
    }
}
