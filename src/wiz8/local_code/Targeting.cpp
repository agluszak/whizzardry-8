#include "wiz8/local_code/Targeting.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/cursor.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

namespace {
/* The monster and group the target cycling keys last picked. */
int picked_monster = -1;
int picked_group = -1;
} // namespace

void ResetTargetPicks()
{
    picked_monster = -1;
    picked_group = -1;
}

constexpr int BAD_INDEX = -1;

/* Point a source at one party character. Everything is cleared first and the
   monster id invalidated, so a source built this way never reads as a monster;
   the character id is the only thing left set. */
void SetTargetSourceToCharacter(int party_slot, W8TargetSource* source)
{
    assert(source != 0);
    *source = {.iType = W8_TARGET_SOURCE_CHARACTER, .iChar = party_slot};
}

/* The same for a monster, and the mirror image of it: the character id is the
   one invalidated and the monster id the one left set. The monster is passed
   as its info record rather than as an id, so the id is read out of it here -
   which is what makes the two builders take different kinds of argument for
   the same job. */
void SetTargetSourceToMonster(const W8MonsterInfo* monster_info, W8TargetSource* source)
{
    assert(source != 0);
    *source = {.iType = W8_TARGET_SOURCE_MONSTER, .iMonsterID = monster_info->location_id};
}

bool TargetSourceIsCharacter(const W8TargetSource* source, int allow_indirect)
{
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        assert(source->iChar != BAD_INDEX);
        return true;
    }
    if (allow_indirect == 1 && source->iType == W8_TARGET_SOURCE_INDIRECT &&
        source->iChar != BAD_INDEX) {
        assert(source->fBackfire || source->fReflection);
        return true;
    }
    return false;
}

bool TargetSourceIsMonster(const W8TargetSource* source, int allow_indirect)
{
    if (source->iType == W8_TARGET_SOURCE_MONSTER) {
        assert(source->iMonsterID != BAD_INDEX);
        return true;
    }
    if (allow_indirect == 1 && source->iType == W8_TARGET_SOURCE_INDIRECT &&
        source->iMonsterID != BAD_INDEX) {
        assert(source->fBackfire || source->fReflection);
        return true;
    }
    return false;
}

signed char GetSourceNoticeColor(const W8TargetSource* source)
{
    if (TargetSourceIsCharacter(source, 1)) {
        return 8;
    }
    if (TargetSourceIsMonster(source, 1)) {
        return 9;
    }
    return 12;
}

signed char GetTargetNoticeColor(const W8TargetSource* source, const W8CombatSlot* target)
{
    if (TargetSourceIsCharacter(source, 1)) {
        return 8;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        return CurrentGame().party.orderIndexOf(target->iChar);
    }
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        return 9;
    }
    return 12;
}

/* Whether a peer aiming at the just-applied target should drop that aim. */
bool ShouldClearAimForAppliedTarget(W8TargetSource* source, W8CombatSlot* target, bool in_combat,
                                    bool action_targets_enemies)
{
    bool source_hostile;
    bool target_hostile;

    if (!in_combat) {
        return true;
    }
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        assert(source->iChar != BAD_INDEX);
        source_hostile =
            CurrentGame().party.characters[source->iChar].uiCondition[W8_CONDITION_TURNCOAT] != 0;
    } else {
        assert(source->iType == W8_TARGET_SOURCE_MONSTER);
        assert(source->iMonsterID != BAD_INDEX);
        source_hostile = FindMonster(source->iMonsterID)->ubDisposition == W8_DISPOSITION_HOSTILE;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        target_hostile =
            CurrentGame().party.characters[target->iChar].uiCondition[W8_CONDITION_TURNCOAT] != 0;
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        target_hostile = FindMonster(target->iMonsterID)->ubDisposition == W8_DISPOSITION_HOSTILE;
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        target_hostile =
            FindMonsterGroup(target->iGroupID)->ubDisposition == W8_DISPOSITION_HOSTILE;
    } else {
        return false;
    }
    if (source_hostile == target_hostile) {
        return action_targets_enemies;
    }
    return !action_targets_enemies;
}

/* The faction names, thirty bytes apart, in the same order as the faction ids.
   Twenty-one of them, which is the whole faction domain. */
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

void SetFactionFlag(signed char faction, bool flag)
{
    CurrentGame().story.factions[faction].encountered = flag;
}

/* Build an empty target block: no kind, both ids BAD_INDEX, everything else
   zero. */
void ResetTargetSource(W8TargetSource* source)
{
    *source = {};
}

void ResetCombatSlot(W8CombatSlot* slot)
{
    *slot = {.iType = W8_TARGET_KIND_NONE,
             .iChar = BAD_INDEX,
             .iMonsterID = BAD_INDEX,
             .iGroupID = BAD_INDEX};
}

/* Clear whatever a monster was aiming at, and report that it now has no
   target. */
bool ClearMonsterCombatSlot(W8MonsterInfo* monster_info)
{
    ResetCombatSlot(&monster_info->Target);
    return false;
}

/* Eligibility and range both use the caller's selected action and target. */
bool IsSpellTargetStillValid(int party_slot, int spell_id, const W8Action& selected)
{
    const auto needed = GetTargetNeededForSpellFriendly(spell_id, false, true);
    return TargetMatchesNeeded(&selected.target, needed) &&
           CharacterActionReachesTarget(party_slot, 0, selected);
}

/* The same check without the range half, and with one target kind that a
   global override always accepts. */
bool IsSpellTargetOfNeededKind(const W8CombatSlot* target, int spell_id, bool needs_item_target)
{
    W8TargetNeed needed = GetTargetNeededForSpellFriendly(spell_id, false, needs_item_target);

    if (needed == W8_TARGET_NEED_ENEMY && g_settings.autotarget_spells != 0) {
        return true;
    }
    return TargetMatchesNeeded(target, needed);
}

/* What an item's spell needs picked before it can be cast. An item with no
   spell needs nothing. */
W8TargetNeed GetTargetNeededForItem(const W8ItemInstance* item, bool needs_item_target)
{
    const W8ItemDatabaseRecord* record;

    if (item == 0 || item->iItemNo == -1) {
        return W8_TARGET_NEED_NONE;
    }
    record = &Data().items[item->iItemNo];
    if (record->spell_id == W8_SPELL_NONE) {
        return W8_TARGET_NEED_NONE;
    }
    return GetTargetNeededForSpellFriendly(record->spell_id, ItemClassNormalizesTarget(record),
                                           needs_item_target);
}

/* Aim one party slot at a clicked monster location. The pending action's
   needed target kind decides between the single monster and its whole group:
   a group-needing action (needed 5) aims at the location's group instead. */
void AimAtMonsterLocation(int party_slot, int location_id, bool allow_single_target)
{
    const auto editing_context = GetEditedActionContext(party_slot);
    const auto edited_action = party_slot < 0 ? W8Action{W8_ACTION_NONE, -1, {}, {}}
                                              : SelectCombatAction(party_slot, editing_context);
    const bool sighting = editing_context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT;

    W8CombatSlot target;
    W8TargetNeed needed;

    const auto assessment =
        AssessMonsterTarget(party_slot, location_id, edited_action, allow_single_target, sighting);
    if (!assessment) {
        const int event =
            assessment.failure == W8TargetFailure::SightBlocked ? g_event_sight_blocked
            : assessment.failure == W8TargetFailure::OutOfRange ? g_event_target_out_of_range
                                                                : -1;
        if (event != -1)
            QueueCharacterEvent(&CurrentGame().party.characters[party_slot], event, 0,
                                kCharacterEventFullVolume);
    }
    if (assessment) {
        needed = W8_TARGET_NEED_UNSET;
        if (!sighting) {
            needed = GetTargetNeededForAction(CurrentGame().party, edited_action);
            if (needed == W8_TARGET_NEED_GROUP) {
                target = {};
                target.iMonsterID = BAD_INDEX;
                target.iChar = BAD_INDEX;
                target.iType = W8_TARGET_KIND_GROUP;
                target.iGroupID = FindMonster(location_id)->monster_group_id;
                AimAtTarget(party_slot, &target, editing_context);
                StartBreathCycle(party_slot, false);
                return;
            }
        }
        target = {};
        target.iChar = BAD_INDEX;
        target.iGroupID = BAD_INDEX;
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = location_id;
        AimAtTarget(party_slot, &target, editing_context);
        StartBreathCycle(party_slot, false);
    }
}

/* Aim at whatever the caller names, by kind. The other three fields are left
   at BAD_INDEX, so only the kind's own field is meaningful. */
void AimByKind(int actor, W8TargetKind kind, W8TargetingContext context)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = kind;
    AimAtTarget(actor, &target, context);
}

void AimAtCharacter(int actor, int character_slot, W8TargetingContext context)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iChar = character_slot;
    target.iType = W8_TARGET_KIND_CHARACTER;
    AimAtTarget(actor, &target, context);
}

void AimAtCharacterIndirect(int actor, int character_slot, W8TargetingContext context)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iChar = character_slot;
    target.iType = W8_TARGET_KIND_CHARACTER_INDIRECT;
    AimAtTarget(actor, &target, context);
}

/* Aim at the place the party is looking, drop the marker and let the display
   know. */
void AimAtPlace(int actor)
{
    W8CombatSlot target;
    glm::vec3 position;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    GetWorldCursorTargetPosition(&position);
    AimAtTarget(actor, &target, GetEditedActionContext(actor));
}

/* Aim at the ground point the camera is looking at, the world-cursor-free
   twin of AimAtPlace used by click-to-move targeting. */
void AimAtGroundTarget(int party_slot)
{
    W8CombatSlot target;
    glm::vec3 position;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    GetCameraForwardPoint(GetGroundTargetRange(), &position);
    target.point = position;
    AimAtTarget(party_slot, &target, GetEditedActionContext(party_slot));
    StartBreathCycle(party_slot, false);
}

/* The three wrappers that set the party's own target rather than a
   combatant's, one per kind that names something. */
void SetTargetToCharacter(int character_slot, bool in_combat)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = character_slot;
    ApplyTarget(&target, in_combat);
}

void SetTargetToMonster(int monster_id, bool in_combat)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_MONSTER;
    target.iMonsterID = monster_id;
    ApplyTarget(&target, in_combat);
}

void SetTargetToGroup(int group_id, bool in_combat)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_GROUP;
    target.iGroupID = group_id;
    ApplyTarget(&target, in_combat);
}

/* Walk every party slot and every live monster: anyone already aiming at
   `target` drops that aim when combat and their action require it. */
void ApplyTarget(W8CombatSlot* target, bool in_combat)
{
    W8TargetSource source;
    W8PartySlotRow* row;
    W8Character* character;
    W8MonsterInfo* monster_info;
    bool action_targets_enemies;
    int party_slot;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        row = &CurrentGame().party.rows[party_slot];
        character = &CurrentGame().party.characters[party_slot];
        if (!CanPartySlotParticipate(party_slot)) {
            continue;
        }
        SetTargetSourceToCharacter(party_slot, &source);
        if (CurrentGame().combat.has_value()) {
            if ((row->committed.target == *target)) {
                action_targets_enemies = CharacterActionTargetsEnemies(
                    CurrentGame().party, character, row->committed.kind, row->committed.detail,
                    &row->committed.detail_block);
                if (ShouldClearAimForAppliedTarget(&source, target, in_combat,
                                                   action_targets_enemies)) {
                    if (row->committed.kind == W8_ACTION_PROTECT) {
                        DropCharacterFromRound(party_slot);
                    } else {
                        RepickActionTarget(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, 0);
                    }
                }
            }
        }
        if ((row->executing.target == *target)) {
            action_targets_enemies = CharacterActionTargetsEnemies(
                CurrentGame().party, character, row->executing.kind, row->executing.attack_mode[0],
                &row->executing.detail_block);
            if (ShouldClearAimForAppliedTarget(&source, target, in_combat,
                                               action_targets_enemies)) {
                RepickActionTarget(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, 0);
            }
        }
    }

    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
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
unsigned char ShowMonsterTargetMarker(W8MonsterInfo* monster_info)
{
    glm::vec3 eye;

    assert(monster_info != 0);
    eye = cameraLocation();
    const auto bounds = monster_info->p3D->GetWorldAnimationBounds();
    return bounds && targetMarkerVisible(eye, *bounds);
}

/* Whether a recorded target is of the kind a caller needs. Each needed kind
   admits one or two target kinds that additionally have to be present.
   Other requirements are accepted after the null-target check. */
bool TargetMatchesNeeded(const W8CombatSlot* target, W8TargetNeed needed)
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
        if (target->iType == W8_TARGET_KIND_ITEM && CurrentGame().party.itemAt(target->item) != 0) {
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
void RepickInvalidCombatTargets(void)
{
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8Character* character = &CurrentGame().party.characters[party_slot];
        if (!CurrentGame().party.members[party_slot].has_value() || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_TURNCOAT ||
            !CharacterCanSwitchTo(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, true, false)) {
            continue;
        }
        const auto selected = SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
        if (!TargetIsInPlay(party_slot, 2, selected)) {
            RepickActionTarget(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, 0);
        }
    }
}

W8TargetNeed GetTargetNeededForAction(const W8Party& party, const W8Action& selected,
                                      bool needs_item_target)
{
    switch (selected.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        return W8_TARGET_NEED_ENEMY;
    case W8_ACTION_CAST_SPELL:
        return GetTargetNeededForSpellFriendly(selected.detail, false, needs_item_target);
    case W8_ACTION_BREATHE:
        return W8_TARGET_NEED_CONE;
    case W8_ACTION_PROTECT:
        return W8_TARGET_NEED_ALLY;
    case W8_ACTION_USE_ITEM:
        return GetTargetNeededForItem(party.itemAt(selected.detail_block.item_use.item),
                                      needs_item_target);
    default:
        break;
    }
    return W8_TARGET_NEED_NONE;
}

/* Whether a slot's recorded target suits the item it would be used with. An
   item with no spell needs nothing picked, and one target kind a global
   override always accepts. */
bool IsItemTargetOfNeededKind(const W8CombatSlot* target, const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record;
    W8TargetNeed needed = W8_TARGET_NEED_NONE;

    if (item != 0 && item->iItemNo != -1) {
        record = &Data().items[item->iItemNo];
        if (record->spell_id != W8_SPELL_NONE) {
            needed = GetTargetNeededForSpellFriendly(record->spell_id,
                                                     ItemClassNormalizesTarget(record), true);
            if (needed == W8_TARGET_NEED_ENEMY && g_settings.autotarget_spells != 0) {
                return true;
            }
        }
    }
    return TargetMatchesNeeded(target, needed);
}

static bool IsMonsterHoverSelectable(W8MonsterInfo* monster_info)
{
    if (!monster_info->fActive || monster_info->p3D->IsDying() ||
        monster_info->p3D->hostility_preserved ||
        monster_info->p3D->visual.GetCurrentModelInstance() != g_current_model_instance) {
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
int PickNearestMonsterUnderCursor(int cursor_x, int cursor_y)
{
    int result;
    float best_distance;
    unsigned int index;

    result = -1;
    best_distance = 999999.0f;
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        float distance;

        if (!IsMonsterHoverSelectable(monster_info)) {
            continue;
        }
        distance = MonsterDistanceToCamera(monster_info->p3D);
        if (distance < best_distance) {
            result = monster_info->location_id;
            best_distance = distance;
        }
    }
    if (g_dev_mode) {
        for (index = 0; index < CurrentLevelPopulation().monster_roster.unborn().size(); ++index) {
            W8MonsterInfo* monster_info = GetUnbornMonsterEntry(index);
            float distance;

            if (!IsMonsterHoverSelectable(monster_info)) {
                continue;
            }
            distance = MonsterDistanceToCamera(monster_info->p3D);
            if (distance < best_distance) {
                result = monster_info->location_id;
                best_distance = distance;
            }
        }
    }
    return result;
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
bool ResolveTargetPoint(W8CombatSlot* target, bool sight_probe)
{
    glm::vec3 point;
    Monster* monster;

    if (target->iType == W8_TARGET_KIND_PARTY || target->iType == W8_TARGET_KIND_CHARACTER) {
        if (sight_probe) {
            point = PartyNavigator()->GetPosition();
        } else {
            point = cameraLocation();
        }
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster = GetMonsterByLocationID(target->iMonsterID);
        if (!monster) return false;
        if (sight_probe) {
            point = monster->nav.GetPosition();
        } else {
            point = monster->nav.movement.position;
            point.y += monster->nav.movement.height_offset;
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
static void CommitAutomaticWeaponChoice(int party_slot, const W8WeaponTarget& choice);

static int ChooseMonsterTarget(int party_slot, const W8Action& action,
                               std::optional<W8RangeCategory> attack_range = {})
{
    std::optional<W8MonsterTargetCandidate> best;
    const auto priority = [](const W8MonsterTargetCandidate& candidate) {
        return std::tuple(candidate.state, !candidate.in_reach, candidate.distance);
    };
    for (unsigned index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        if (!monster_info->fActive || !monster_info->pCombat || monster_info->hp_current == 0 ||
            MonsterVsCharDisposition(party_slot, monster_info) != W8_DISPOSITION_HOSTILE ||
            !AssessMonsterAim(party_slot, 2, monster_info, action, attack_range)) {
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
        assert(monster_info->uiHPMax != 0);
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
bool IsTargetStillPresent(const W8CombatSlot* target)
{
    if (target == 0) {
        return false;
    }

    switch (target->iType) {
    case W8_TARGET_KIND_CHARACTER:
        assert(target->iChar != BAD_INDEX);
        if (!CanPartySlotParticipate(target->iChar)) {
            return false;
        }
        break;

    case W8_TARGET_KIND_CHARACTER_INDIRECT:
        assert(target->iChar != BAD_INDEX);
        if (CurrentGame().party.members[target->iChar].has_value() == 0 ||
            CurrentGame().party.characters[target->iChar].hp_current != 0 ||
            CurrentGame()
                    .party.characters[target->iChar]
                    .uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] == 0 ||
            CurrentGame().party.characters[target->iChar].uiCondition[W8_CONDITION_BEYOND_REACH] !=
                0) {
            return false;
        }
        break;

    case W8_TARGET_KIND_MONSTER: {
        W8MonsterInfo* monster_info;

        assert(target->iMonsterID != BAD_INDEX);
        monster_info = FindMonster(target->iMonsterID, false);
        if (monster_info == 0) {
            return false;
        }
        assert(monster_info != 0);
        if (monster_info->hp_current == 0 ||
            monster_info->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0 ||
            !monster_info->fActive) {
            return false;
        }
        break;
    }

    case W8_TARGET_KIND_GROUP: {
        W8MonsterGroup* group;

        assert(target->iGroupID != BAD_INDEX);
        group = FindMonsterGroup(target->iGroupID, false);
        if (group == nullptr) {
            return false;
        }
        assert(group != 0);
        if (group->MemberCount() == 0 || !group->members_active) {
            return false;
        }
        break;
    }

    case W8_TARGET_KIND_ITEM:
        assert(CurrentGame().party.itemAt(target->item) != 0);
        if (CurrentGame().party.itemAt(target->item)->iItemNo == BAD_INDEX) {
            return false;
        }
        break;

    default:
        break;
    }
    return true;
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

   Geometry-only preview and line-of-sight eligibility are explicit rules;
   collecting targets never changes their presentation. */
void CollectMonstersWithinRadius(const glm::vec3* centre, const glm::vec3* eye,
                                 std::vector<int>& found, float radius, signed char side,
                                 W8AreaSightRule sight_rule)
{
    unsigned int index;

    if (radius == 0.0f) {
        return;
    }

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        Monster* monster = monster_info->p3D;
        const W8MonsterRecord* record;
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

        position = monster->nav.GetPosition();
        if (radius < glm::length((*centre - position)) - monster->nav.radius) {
            continue;
        }
        if (sight_rule == W8AreaSightRule::GeometryOnly ||
            monster_info->p3D->HasLineOfSightFromPoint(*eye)) {
            found.push_back(monster_info->location_id);
        }
    }
}

void SetMonsterCombatTarget(W8MonsterInfo* monster_info, int location_id)
{
    W8CombatSlot* target = &monster_info->Target;

    *target = {};
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
bool MonsterTargetMatchesSpell(W8MonsterInfo* monster_info, int spell_id)
{
    return TargetMatchesNeeded(&monster_info->Target, GetTargetNeededForSpellHostile(spell_id));
}

/* Map TargetingMode() to a cursor-table index for SetTargetCursor.
   Returned values are cursor slots (including 10..12), not W8TargetingContext. */
// FUNCTION: WIZ8 0x0053A3D0
int GetTargetingCursorForState(bool alternate)
{
    switch (TargetingMode()) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        return alternate + 3;
    case W8_TARGET_NEED_ENEMY:
        return alternate;
    case W8_TARGET_NEED_GROUP:
        return alternate ? 10 : 11;
    case W8_TARGET_NEED_PLACE:
        return 2;
    case W8_TARGET_NEED_CONE:
        return 12;
    default:
        return W8_CURSOR_NONE;
    }
}

/* Whether this spell choice needs an explicit target in the interaction. */
bool ActionNeedsExplicitTarget(const W8Action& action, bool conversation_aside)
{
    switch (GetSpellTargetType(action.detail, false)) {
    case W8_TARGET_TYPE_ENEMY:
        if (conversation_aside) {
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
unsigned int GetActionSpellLikeId(const W8Party& party, const W8Action& selected)
{
    if (selected.kind == W8_ACTION_BREATHE) {
        return 0x77;
    }
    if (selected.kind == W8_ACTION_CAST_SPELL) {
        return selected.detail;
    }
    if (selected.kind == W8_ACTION_USE_ITEM) {
        return GetItemSpell(party.itemAt(selected.detail_block.item_use.item));
    }
    return 0;
}

/* plsMonsterList index that last satisfied AnyMonsterVisible. */
static int g_last_visible_monster = -1;

/* Re-check the selected character's committed target after its action has
   changed. A slot that cannot act at all falls back to no targeting;
   otherwise the action's needed kind is worked out again and the recorded
   target either still satisfies it - leaving no targeting mode - or the mode
   matching what the action now needs is entered so the player can pick. */
void RevalidateSelectedTarget(int party_slot)
{
    W8TargetNeed needed;

    if (party_slot != CurrentGame().status.selected_character) {
        return;
    }
    const auto context = GetEditedActionContext(party_slot);
    if (!CharacterCanSwitchTo(party_slot, context, true, false)) {
        SetTargetingMode(W8_TARGET_NEED_NONE);
        return;
    }
    const auto selected = SelectCombatAction(party_slot, context);
    needed = W8_TARGET_NEED_NONE;
    if (context != W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
        needed = GetTargetNeededForAction(CurrentGame().party, selected, MainGameHud() == nullptr);
    }
    if (TargetMatchesNeeded(&selected.target, needed)) {
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
bool IsTargetSourceInRangeOfGroup(const W8TargetSource* source, W8MonsterGroup* group,
                                  W8RangeCategory range)
{
    Monster* source_monster;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;
    unsigned int index;
    float distance;
    float max_distance;

    source_monster = 0;
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        assert(source->iChar != BAD_INDEX);
    } else {
        assert(source->iType == W8_TARGET_SOURCE_MONSTER);
        assert(source->iMonsterID != BAD_INDEX);
        assert(source->iMonsterID != BAD_INDEX);
        monster_info = FindMonster(source->iMonsterID);
        source_monster = GetMonsterByLocationID(source->iMonsterID);
        if (!source_monster) return false;
        record = GetMonsterDataForInfo(monster_info);
        range = GetMonsterActionRangeCategory(monster_info, record, 0);
    }
    max_distance = CalcRangeDistance(range);
    for (index = 0; index < group->MemberIds().size(); ++index) {
        Monster* member = GetMonsterByLocationID(group->MemberLocationId(index));
        if (!member) continue;

        if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
            assert(source->iChar != BAD_INDEX);
            distance = member->GetDistanceToPlayer();
        } else if (source->iType == W8_TARGET_SOURCE_MONSTER) {
            assert(source->iMonsterID != BAD_INDEX);
            distance = member->GetDistanceToMonster(source_monster);
        }
        if (distance <= max_distance) {
            return true;
        }
    }
    return false;
}

/* Clear the current draft target before restoring the ordinary targeting UI. */
void SelectSpellCastingPartySlot(int party_slot)
{
    W8CombatSlot target;

    ResetCombatSlot(&target);
    AimAtTarget(party_slot, &target, W8_TARGETING_CONTEXT_SHARED);
    SetTargetingMode(W8_TARGET_NEED_NONE);
}

/* Whether the slot holds a dead character a dead-targeting mode may still
   reach - occupied, actually dead, still reachable while down, and not yet
   beyond reach. */
bool IsDeadCharacterTargetable(int party_slot)
{
    if (!CurrentGame().party.members[party_slot].has_value()) {
        return false;
    }
    W8Character* character = &CurrentGame().party.characters[party_slot];
    if (character->hp_current > 0 ||
        character->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] == 0) {
        return false;
    }
    return character->uiCondition[W8_CONDITION_BEYOND_REACH] <= 0;
}

/* A party slot can participate only while occupied, alive, and below the
   terminal character-state threshold. */
bool CanPartySlotParticipate(int party_slot)
{
    return CurrentGame().party.members[party_slot].has_value() &&
           CurrentGame().party.characters[party_slot].hp_current != 0 &&
           CurrentGame().party.characters[party_slot].highest_condition < W8_CONDITION_DEAD;
}

/* Whether a party slot could aim its chosen action at one whole monster group.
   An untargetable group is refused before anything else is worked out. What
   the action needs then decides how the question is asked: a group-wide need
   is a range test against the group, and a single-monster need is satisfied by
   any one member the slot can reach.

   The source block is built here rather than passed in, so this always asks on
   the character's own behalf. */
bool CanTargetMonsterGroup(int party_slot, W8MonsterGroup* group, const W8Action& selected,
                           bool sighting)
{
    W8TargetSource source;
    W8TargetNeed needed;
    unsigned int index;
    int reachable;

    if (MonsterGroupGetRecord(group)->untargetable != 0) {
        return false;
    }

    if (sighting) {
        needed = W8_TARGET_NEED_NONE;
    } else {
        needed = GetTargetNeededForAction(CurrentGame().party, selected);
    }

    SetTargetSourceToCharacter(party_slot, &source);

    if (needed == W8_TARGET_NEED_GROUP) {
        return IsTargetSourceInRangeOfGroup(&source, group,
                                            GetCharActionRange(party_slot, 0, selected));
    }
    if (needed != W8_TARGET_NEED_ENEMY && needed != W8_TARGET_NEED_ALLY) {
        return false;
    }

    reachable = 0;
    for (index = 0; index < group->MemberIds().size(); ++index) {
        W8MonsterInfo* monster_info = FindMonster(group->MemberLocationId(index));

        if (AssessMonsterAim(party_slot, 2, monster_info, selected)) {
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
bool RepickActionTarget(int party_slot, W8TargetingContext context, int arg)
{
    W8CombatSlot new_target;
    bool is_attack_kind = false;
    bool result = false;
    W8TargetNeed needed;
    W8TargetKind previous_kind;
    int selected;

    if (!CanPartySlotParticipate(party_slot)) {
        return false;
    }

    const auto selected_action = SelectCombatAction(party_slot, context);
    const auto kind = selected_action.kind;
    const auto* target = &selected_action.target;
    assert(target != 0);
    needed = GetTargetNeededForAction(CurrentGame().party, selected_action);
    previous_kind = target->iType;
    if (CurrentGame().combat.has_value()) {
        if (context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
            ++CurrentGame().combat->characters[party_slot].pending_action_repick_count;
        }
        is_attack_kind = kind == W8_ACTION_ATTACK || kind == W8_ACTION_BERSERK;
        if ((is_attack_kind || kind == W8_ACTION_CAST_SPELL || kind == W8_ACTION_USE_ITEM) &&
            needed == W8_TARGET_NEED_ENEMY) {
            selected = ChooseMonsterTarget(party_slot, selected_action);
            if (selected == -1 && is_attack_kind) {
                if (const auto fallback =
                        ChooseFallbackMonsterTarget(party_slot, selected_action)) {
                    CommitAutomaticWeaponChoice(party_slot, *fallback);
                    selected = fallback->location_id;
                }
            }
            if (selected != -1) {
                new_target = {};
                new_target.iChar = -1;
                new_target.iGroupID = -1;
                new_target.iType = W8_TARGET_KIND_MONSTER;
                new_target.iMonsterID = selected;
                // 0x00536876: the new target goes into the selected action's block.
                AimAtTarget(party_slot, &new_target, context);
                result = true;
                if (arg != 0) {
                    StartBreathCycle(party_slot, false);
                }
                if (context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
                    if (g_settings.verbose_combat_messages != 0) {
                        PostCharacterNotice(party_slot, Data().strings[0x26b]);
                    }
                    const auto committed =
                        SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
                    if (!TargetIsInPlay(party_slot, 2, committed)) {
                        AimAtTarget(party_slot, &new_target, W8_TARGETING_CONTEXT_IN_COMBAT);
                    }
                }
            }
        }
    }
    if (!result) {
        new_target = {};
        new_target.iMonsterID = -1;
        new_target.iChar = -1;
        new_target.iGroupID = -1;
        new_target.iType = W8_TARGET_KIND_NONE;
        AimAtTarget(party_slot, &new_target, context);
    }
    if (CurrentGame().combat.has_value() && context == W8_TARGETING_CONTEXT_IN_COMBAT &&
        new_target.iType != previous_kind) {
        CurrentGame().events.push(PartySlotChanged{static_cast<unsigned int>(party_slot)});
    }
    return result;
}

// Eligibility and reach use the same action and target selected by the caller.
bool TargetIsInPlay(int party_slot, int hand, const W8Action& selected)
{
    return TargetMatchesNeeded(&selected.target,
                               GetTargetNeededForAction(CurrentGame().party, selected)) &&
           CharacterActionReachesTarget(party_slot, hand, selected);
}

/* Whether the supplied action can be aimed at a live, standing and reachable
   monster.

   What the action needs decides the rest: a group need is answered against the
   monster's group, a single-monster need only when the caller allows one, and
   an action needing nothing is refused outright in combat. The database's
   untargetable flag is checked last rather than first, so an action needing
   nothing still reaches a monster carrying it. */
W8TargetAssessment AssessMonsterTarget(int party_slot, int location_id, const W8Action& selected,
                                       bool allow_single_target, bool sighting)
{
    W8MonsterInfo* monster_info = FindMonster(location_id, false);
    const W8MonsterRecord* record;
    W8TargetNeed needed;

    if (!monster_info || !monster_info->fActive) {
        return {W8TargetFailure::Unavailable};
    }
    if (monster_info->hp_current == 0) {
        return {W8TargetFailure::Unavailable};
    }
    if (monster_info->uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0) {
        return {W8TargetFailure::Unavailable};
    }

    if (sighting) {
        needed = W8_TARGET_NEED_NONE;
        if (CurrentGame().combat.has_value()) {
            return {W8TargetFailure::Unavailable};
        }
    } else {
        needed = GetTargetNeededForAction(CurrentGame().party, selected);

        if (needed != W8_TARGET_NEED_ENEMY && needed != W8_TARGET_NEED_GROUP) {
            if (needed == W8_TARGET_NEED_ALLY) {
                if (!allow_single_target) {
                    return {W8TargetFailure::Unavailable};
                }
            } else {
                if (needed != W8_TARGET_NEED_NONE) {
                    return {W8TargetFailure::Unavailable};
                }
                if (CurrentGame().combat.has_value()) {
                    return {W8TargetFailure::Unavailable};
                }
            }
        }
    }

    record = GetMonsterDataForInfo(monster_info);
    if (record->untargetable != 0 && needed != W8_TARGET_NEED_NONE) {
        return {W8TargetFailure::Unavailable};
    }
    if (needed == W8_TARGET_NEED_GROUP) {
        return AssessGroupRange(party_slot, monster_info->monster_group_id, selected);
    }
    return AssessMonsterAim(party_slot, 2, monster_info, selected, {}, sighting);
}

/* Whether a submenu candidate has anything at all to aim at. An attack looks
   for one targetable monster and protecting looks at the other party members
   first and then at the monsters.

   An action this does not know about answers yes, so the check narrows rather
   than gates - only the actions with a way of being impossible can fail it.
   Casting and item use answer yes as well, since no spell or item has been
   chosen yet. A slot that cannot take part has no action to aim. */
bool CandidateHasAnyValidTarget(int party_slot, W8ActionKind candidate)
{
    if (!CanPartySlotParticipate(party_slot) && candidate != W8_ACTION_WALK &&
        candidate != W8_ACTION_RUN) {
        return true;
    }
    const W8Action edited_action{candidate};

    W8CombatSlot target;
    unsigned int index;
    unsigned int other_slot;

    switch (candidate) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            if (AssessMonsterTarget(party_slot, GetMonsterListEntry(index)->location_id,
                                    edited_action, false, false)) {
                return true;
            }
        }
        return false;

    case W8_ACTION_PROTECT:
        for (other_slot = 0; other_slot < 8; ++other_slot) {
            if (other_slot == static_cast<unsigned int>(party_slot)) {
                continue;
            }
            target = {};
            target.iMonsterID = BAD_INDEX;
            target.iGroupID = BAD_INDEX;
            target.iType = W8_TARGET_KIND_CHARACTER;
            target.iChar = other_slot;
            if (CharacterHasAttackOn(party_slot, &target)) {
                return true;
            }
        }
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            target = {};
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

/* CurrentLevelPopulation().monster_groups: every monster group in the level. */

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
bool SpellHasAnyValidTarget(int party_slot, int spell_id, bool normalize)
{
    const W8Action edited_action{W8_ACTION_CAST_SPELL, spell_id, {}, {}};
    const bool sighting = false;

    unsigned int index;

    switch (GetTargetNeededForSpellFriendly(spell_id, normalize, true)) {
    case W8_TARGET_NEED_ENEMY:
        if (g_settings.autotarget_spells != 0) {
            return true;
        }
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            if (AssessMonsterTarget(party_slot, GetMonsterListEntry(index)->location_id,
                                    edited_action, false, sighting)) {
                return true;
            }
        }
        return false;

    case W8_TARGET_NEED_GROUP:
        for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
            W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[index].get();

            if (group->members_active &&
                CanTargetMonsterGroup(party_slot, group, edited_action, sighting)) {
                return true;
            }
        }
        return false;

    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        for (index = 0; index < 8; ++index) {
            if (CurrentGame().party.members[index].has_value() &&
                CurrentGame().party.characters[index].hp_current == 0 &&
                CurrentGame()
                        .party.characters[index]
                        .uiCondition[W8_CONDITION_REACHABLE_WHEN_DOWN] != 0 &&
                CurrentGame().party.characters[index].uiCondition[W8_CONDITION_BEYOND_REACH] == 0) {
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
        glm::vec3 position = monster_info->p3D->nav.GetPosition();

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
static int PickNextTargetableMonster(int party_slot, const W8Action& edited_action, bool sighting)
{
    std::vector<int> targetable;
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (AssessMonsterTarget(party_slot, monster_info->location_id, edited_action, true,
                                sighting)) {
            targetable.push_back(monster_info->location_id);
        }
    }
    return SelectNextGroupMemberByAngle(targetable, picked_monster);
}

int PickNextTargetableGroupMember(int party_slot, const W8MonsterGroup* group,
                                  const W8Action& edited_action, bool sighting)
{
    std::vector<int> targetable;
    unsigned int index;

    for (index = 0; index < group->MemberIds().size(); ++index) {
        int location_id = group->MemberLocationId(index);

        if (AssessMonsterTarget(party_slot, location_id, edited_action, true, sighting)) {
            targetable.push_back(location_id);
        }
    }
    return SelectNextGroupMemberByAngle(targetable, group->highlighted_member);
}

/* Commit the group target or cycle its remembered member in angular order.
   The current selection supplies presentation at the next draw. */
void AimAtMonsterGroupMember(int party_slot, W8MonsterGroup* group)
{
    const auto context = GetEditedActionContext(party_slot);
    const auto selected = SelectCombatAction(party_slot, context);
    const bool sighting = context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT;
    W8CombatSlot target;
    W8TargetSource source;
    int picked;

    if (!sighting) {
        if (GetTargetNeededForAction(CurrentGame().party, selected) == W8_TARGET_NEED_GROUP) {
            target = {};
            target.iChar = BAD_INDEX;
            target.iMonsterID = BAD_INDEX;
            target.iType = W8_TARGET_KIND_GROUP;
            target.iGroupID = group->group_id;
            AimAtTarget(party_slot, &target, context);
            StartBreathCycle(party_slot, false);
            SetTargetSourceToCharacter(party_slot, &source);
            PointCameraAtCombatTarget(&source, &target);
            return;
        }
    }

    picked = PickNextTargetableGroupMember(party_slot, group, selected, sighting);
    if (picked == BAD_INDEX) {
        return;
    }

    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_MONSTER;
    target.iMonsterID = picked;
    AimAtTarget(party_slot, &target, context);

    group->highlighted_member = picked;
    if (auto& combat = CurrentGame().combat) combat->target_picked = true;

    StartBreathCycle(party_slot, false);
    SetTargetSourceToCharacter(party_slot, &source);
    PointCameraAtCombatTarget(&source, &target);
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
static int PickNextTargetableGroup(int party_slot, const W8Action& edited_action, bool sighting)
{
    const auto& groups = CurrentLevelPopulation().monster_groups;
    if (groups.empty()) return BAD_INDEX;
    const auto current = std::ranges::find_if(
        groups, [](const auto& group) { return group->group_id == picked_group; });
    std::size_t start = 0;
    if (current == groups.end())
        picked_group = BAD_INDEX;
    else
        start = (std::distance(groups.begin(), current) + 1) % groups.size();
    for (std::size_t offset = 0; offset < groups.size(); ++offset) {
        auto* group = groups[(start + offset) % groups.size()].get();
        if (CanTargetMonsterGroup(party_slot, group, edited_action, sighting))
            return group->group_id;
    }
    return BAD_INDEX;
}

/* The cycle-target key: step the party's pick to the next thing the slot's
   action can aim at. An action that needs a whole group steps between groups
   and remembers the new pick; anything else steps between monsters. Nothing
   targetable leaves the current pick alone. Either way the new aim is
   committed and the slot starts breathing again. */
void CycleToNextTarget(int party_slot)
{
    const auto context = GetEditedActionContext(party_slot);
    const auto selected = SelectCombatAction(party_slot, context);
    const bool sighting = context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT;
    W8CombatSlot target;
    W8TargetSource source;
    int pick;
    bool pick_group = false;

    if (!sighting) {
        pick_group = GetTargetNeededForAction(CurrentGame().party, selected,
                                              MainGameHud() == nullptr) == W8_TARGET_NEED_GROUP;
    }
    if (pick_group) {
        pick = PickNextTargetableGroup(party_slot, selected, sighting);
        if (pick == BAD_INDEX) {
            return;
        }
        target = {};
        target.iMonsterID = BAD_INDEX;
        target.iChar = BAD_INDEX;
        target.iType = W8_TARGET_KIND_GROUP;
        target.iGroupID = pick;
        AimAtTarget(party_slot, &target, context);
        picked_group = pick;
    } else {
        pick = PickNextTargetableMonster(party_slot, selected, sighting);
        if (pick == BAD_INDEX) {
            return;
        }
        target = {};
        target.iChar = BAD_INDEX;
        target.iGroupID = BAD_INDEX;
        target.iType = W8_TARGET_KIND_MONSTER;
        target.iMonsterID = pick;
        AimAtTarget(party_slot, &target, context);
        picked_monster = pick;
    }
    StartBreathCycle(party_slot, false);
    SetTargetSourceToCharacter(party_slot, &source);
    PointCameraAtCombatTarget(&source, &target);
    if (auto& combat = CurrentGame().combat) combat->target_picked = true;
}

/* Refresh each slot's selected target after a sight or range change, repicking
   invalid targets and committing a useful automatic weapon choice. */
void RefreshAllPartyTargets(void)
{
    for (int party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
        W8Character* character = &CurrentGame().party.characters[party_slot];

        if (CurrentGame().party.members[party_slot].has_value() &&
            (character->hp_current != 0 || character->highest_condition < W8_CONDITION_DEAD)) {
            const auto context = GetEditedActionContext(party_slot);
            const auto selected = SelectCombatAction(party_slot, context);
            const W8CombatSlot* target = &selected.target;
            bool can_switch = CharacterCanSwitchTo(party_slot, context, false, false);

            if (!can_switch) {
                can_switch = CharacterCanSwitchTo(party_slot, context, true, false);
                if (can_switch) {
                    RepickActionTarget(party_slot, context, 0);
                } else if (target->iType != W8_TARGET_KIND_NONE) {
                    W8CombatSlot action;

                    action = {};
                    action.iChar = BAD_INDEX;
                    action.iMonsterID = BAD_INDEX;
                    action.iGroupID = BAD_INDEX;
                    AimAtTarget(party_slot, &action, context);
                }
            }

            if (IsPartySlotEligible(party_slot) &&
                (row->committed.kind == W8_ACTION_ATTACK ||
                 row->committed.kind == W8_ACTION_BERSERK) &&
                (row->weapon_swap_pending ||
                 CharacterCanSwitchTo(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, false, false))) {
                const auto fallback = ChooseFallbackMonsterTarget(
                    party_slot, SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT));
                if (fallback) {
                    CommitAutomaticWeaponChoice(party_slot, *fallback);
                    const int selected = fallback->location_id;
                    W8CombatSlot action;

                    action = {};
                    action.iType = W8_TARGET_KIND_MONSTER;
                    action.iChar = BAD_INDEX;
                    action.iMonsterID = selected;
                    action.iGroupID = BAD_INDEX;
                    AimAtTarget(party_slot, &action, W8_TARGETING_CONTEXT_IN_COMBAT);
                }
            }
        }
    }
}

/* Combat-panel refresh: recount each active group's on-screen and selectable
   members; when either count changed, ask for a party redraw and store the new
   pair in the group's scratch slot. */
void RefreshMonsterTargetCounts(void)
{
    glm::vec3 camera;
    unsigned int group_index;

    for (group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[group_index].get();
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
            assert(monster_info != 0);
            camera = cameraLocation();
            const auto bounds = monster_info->p3D->GetWorldAnimationBounds();
            if (bounds && targetMarkerVisible(camera, *bounds) != 0) {
                on_screen_count += 1;
            }
            if (CurrentGame().status.selected_character == -1 ||
                AssessMonsterAim(
                    CurrentGame().status.selected_character, 2, monster_info,
                    SelectCombatAction(
                        CurrentGame().status.selected_character,
                        GetEditedActionContext(CurrentGame().status.selected_character)))) {
                selectable_count += 1;
            }
        }
        if (on_screen_count != group->visible_member_count ||
            selectable_count != group->selectable_member_count) {
            CurrentGame().events.push(MonstersChanged{});
            group->visible_member_count = on_screen_count;
            group->selectable_member_count = selectable_count;
        }
    }
}

/* Whether `monster` is closer than `max_distance` to the player and its
   elevated centre or either animation-bound corner still projects on the world
   camera. `position` is unused; the distance is measured to the player. */
bool IsMonsterVisibleWithinDistance(Monster* monster, const glm::vec3* position, float max_distance)
{
    if (!(monster->GetDistanceToPlayer() < max_distance)) return false;
    const auto bounds = monster->GetWorldAnimationBounds();
    if (!bounds) return false;
    auto center = monster->nav.movement.position;
    center.y += monster->nav.movement.height_offset;
    const std::array points{center, bounds->minimum, bounds->maximum};
    return GetWorld()->camera->projectsAny(points);
}

/* Any live monster visible to the camera within the far-clip range, resuming
   the scan at the last match. */
bool AnyMonsterVisible(void)
{
    glm::vec3 camera;
    float limit;
    std::ptrdiff_t count;
    int index;

    limit = static_cast<float>(WorldGetFarClip(GetWorld())) * 1.5f;
    if (GetWorld() == 0 || GetWorld()->camera == 0) {
        return false;
    }
    camera = cameraLocation();
    count = CurrentLevelPopulation().monster_roster.live().size();
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

/* Commit the chosen target and, when requested, remember it for this action
   kind. Presentation is updated separately by the interaction controller. */
void CommitActionTarget(int actor, W8TargetingContext context, const W8CombatSlot& target,
                        bool remember_action_target)
{
    auto& row = CurrentGame().party.rows[actor];
    const auto action = SelectCombatAction(actor, context);
    switch (context) {
    case W8_TARGETING_CONTEXT_OUT_OF_COMBAT:
        row.executing.target = target;
        break;
    case W8_TARGETING_CONTEXT_IN_COMBAT:
        row.committed.target = target;
        break;
    case W8_TARGETING_CONTEXT_SHARED:
        if (gpSCSV)
            gpSCSV->draft.target = target;
        else if (auto* draft = ItemUseDraft())
            draft->target = target;
        break;
    case W8_TARGETING_CONTEXT_SPELL:
        row.remembered.spell.target = target;
        break;
    case W8_TARGETING_CONTEXT_ITEM:
        row.remembered.item_target = target;
        break;
    case W8_TARGETING_CONTEXT_BREATH:
        row.remembered.breath_target = target;
        break;
    default:
        return;
    }
    if (remember_action_target && target.iType != W8_TARGET_KIND_NONE) {
        switch (action.kind) {
        case W8_ACTION_CAST_SPELL:
            row.remembered.spell.target = target;
            break;
        case W8_ACTION_USE_ITEM:
            row.remembered.item_target = target;
            break;
        case W8_ACTION_BREATHE:
            row.remembered.breath_target = target;
            break;
        default:
            break;
        }
    }
}

/* Whether `source` can currently see `monster_info`: the party threat record's
   sight flag for a character, the mon-to-mon visibility record for a monster
   source, and a point line-of-sight probe for an indirect source. */
unsigned char SourceCanSeeMonster(const W8TargetSource* source, W8MonsterInfo* monster_info,
                                  unsigned char flag_index, int sight_flag)
{
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        assert(source->iChar != -1);
        return monster_info->party_threat.los_flags[flag_index];
    }
    if (source->iType == W8_TARGET_SOURCE_MONSTER) {
        assert(source->iMonsterID != -1);
        assert(source->iMonsterID != -1);
        W8MonsterInfo* source_info = FindMonster(source->iMonsterID);
        W8VisibilityRecord* visibility = FindMonToMonVisibility(source_info, monster_info);
        if (visibility != 0 && visibility->los_flags[sight_flag]) {
            return 1;
        }
        return 0;
    }
    assert(source->iType == W8_TARGET_SOURCE_INDIRECT);
    return monster_info->p3D->HasLineOfSightFromPoint(source->point);
}

/* Whether `target` is inside the aim cone: within `bonus` + `eye_radius` of
   `eye`, and within the heading and elevation arc bounds. */
bool TargetInRangeAndArcs(const glm::vec3* target, float bonus, const glm::vec3* eye,
                          float eye_radius, float heading, float elevation, float range)
{
    float dx = target->x - eye->x;
    float dy = target->y - eye->y;
    float dz = target->z - eye->z;
    float excess =
        static_cast<float>(floor(sqrt(dx * dx + dy * dy + dz * dz) - bonus - eye_radius));
    if (range >= excess) {
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
int CollectConeMonsterTargets(const W8TargetSource* source, const glm::vec3* eye, float heading,
                              float elevation, std::vector<int>& found, unsigned char disposition,
                              int sight_flag, float range)
{
    int start_count = static_cast<int>(found.size());
    float radius;
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        radius = PartyNavigator()->radius;
    } else if (source->iType == W8_TARGET_SOURCE_MONSTER) {
        W8MonsterInfo* source_info = FindMonster(source->iMonsterID);
        radius = source_info->p3D->nav.radius;
    } else {
        radius = 0.0f;
    }
    for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* monster_info = monster_owner.get();
        if (monster_info->fActive && monster_info->hp_current != 0 &&
            monster_info->uiCondition[W8_CONDITION_DEAD] == 0) {
            const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
            if (record->untargetable == 0 &&
                (monster_info->ubDisposition == disposition || disposition == 3)) {
                Monster* monster = monster_info->p3D;
                glm::vec3 point;
                point = glm::vec3(monster->nav.movement.position.x,
                                  monster->nav.movement.position.y +
                                      monster->nav.movement.height_offset,
                                  monster->nav.movement.position.z);
                if (TargetInRangeAndArcs(&point, monster->nav.radius, eye, radius, heading,
                                         elevation, range) &&
                    SourceCanSeeMonster(source, monster_info, 1, sight_flag) != 0) {
                    found.push_back(monster_info->location_id);
                }
            }
        }
    }
    return static_cast<int>(found.size()) - start_count;
}

/* Whether the pending item use needs a target picked: most spell target types
   need one, type 3 only when auto-targeting cannot pick it (never while a
   conversation is set aside, whose NPC is the target). */
bool ItemUseNeedsTarget(int party_slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    W8ItemInstance* item = CurrentGame().party.itemAt(row->remembered.item_detail.item_use.item);
    bool normalize = ItemClassNormalizesTarget(&Data().items[item->iItemNo]);
    switch (GetSpellTargetType(GetItemSpell(item), normalize)) {
    case 3:
        if (CurrentGame().activity.conversation_aside) {
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

// Query the proposed equipment without moving items or recalculating live state.
std::optional<W8WeaponTarget> ChooseFallbackMonsterTarget(int party_slot, const W8Action& action)
{
    const auto& row = CurrentGame().party.rows[party_slot];
    const auto& character = CurrentGame().party.characters[party_slot];
    if (!g_settings.autoswap_weapons || row.remembered.weapon_sets_locked ||
        (CurrentGame().combat && CurrentGame().combat->execution_active &&
         CurrentGame().combat->characters[party_slot].dead &&
         CurrentGame().combat->characters[party_slot].phase != 0) ||
        IsItemBoundToWearer(&character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON]) ||
        IsItemBoundToWearer(&character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON]))
        return {};
    const auto choice = PlanWeaponSetSwap(Data(), character);
    if (!choice) return {};
    const auto& primary =
        character.EquippedItem[choice->primary ? W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON
                                               : W8_EQUIP_SLOT_PRIMARY_WEAPON];
    const auto& secondary =
        character.EquippedItem[choice->secondary ? W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON
                                                 : W8_EQUIP_SLOT_SECONDARY_WEAPON];
    const auto hands = EvaluateWeaponHands(Data(), primary, secondary);
    W8RangeCategory range = W8_RANGE_NONE;
    for (unsigned int hand = 0; hand < 2; ++hand) {
        if (!hands[hand].in_play) continue;
        const auto& item = hand == 0 ? primary : secondary;
        const auto reach =
            hands[hand].uiHolds == 1 || hands[hand].uiHolds == 3
                ? static_cast<W8RangeCategory>(Data().items[item.iItemNo].range_category)
                : W8_RANGE_TOUCH;
        range = std::max(range, reach);
    }
    int target = ChooseMonsterTarget(party_slot, action, range);
    if (target == -1) return {};
    if (action.target.iType == W8_TARGET_KIND_MONSTER) {
        auto* remembered = FindMonster(action.target.iMonsterID, false);
        if (remembered && AssessMonsterAim(party_slot, 2, remembered, action, range))
            target = remembered->location_id;
    }
    return W8WeaponTarget{target, *choice};
}

static void CommitAutomaticWeaponChoice(int party_slot, const W8WeaponTarget& choice)
{
    CurrentGame().party.rows[party_slot].weapon_swap_pending ^= 1;
    CommitWeaponSetSwap(CurrentGame(), party_slot, choice.equipment);
    PostCharacterNotice(party_slot, Data().strings[0x26d]);
}

/* When combat ends, swap each occupied, living slot whose pending swap flag is
   set back to its primary weapon set, provided auto-swap is on and nothing is
   already swapping. */
void ReconcilePartyEquipmentAfterCombat(void)
{
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
        W8Character* character = &CurrentGame().party.characters[party_slot];
        if (CurrentGame().party.members[party_slot].has_value() &&
            (character->hp_current != 0 || character->highest_condition < W8_CONDITION_TURNCOAT) &&
            row->weapon_swap_pending && g_settings.autoswap_weapons != 0 &&
            !CurrentGame().party.rows[party_slot].remembered.weapon_sets_locked) {
            SwapWeaponSetSlots(CurrentGame(), party_slot, false);
            row->weapon_swap_pending = false;
        }
    }
}

/* Ask the slot's currently selected action which context its outputs hold:
   the chosen action kind plus three context-dependent words. */
W8Action SelectCombatAction(int party_slot, W8TargetingContext context)
{
    const auto& row = CurrentGame().party.rows[party_slot];
    W8Action action;
    switch (context) {
    case W8_TARGETING_CONTEXT_OUT_OF_COMBAT:
        action = row.executing.action();
        break;
    case W8_TARGETING_CONTEXT_IN_COMBAT:
        action = row.committed;
        break;
    case W8_TARGETING_CONTEXT_SHARED:
        if (gpSCSV) {
            action = gpSCSV->draft;
        } else {
            action = ItemUseDraft() ? *ItemUseDraft() : W8Action{W8_ACTION_USE_ITEM, -1};
        }
        break;
    case W8_TARGETING_CONTEXT_SPELL:
        action = row.remembered.spell;
        break;
    case W8_TARGETING_CONTEXT_ITEM:
        action = {W8_ACTION_USE_ITEM, -1, row.remembered.item_target, row.remembered.item_detail};
        break;
    case W8_TARGETING_CONTEXT_BREATH:
        action = {W8_ACTION_BREATHE, -1, row.remembered.breath_target};
        break;
    case W8_TARGETING_CONTEXT_ATTACK:
        action = {W8_ACTION_ATTACK};
        break;
    default:
        throw std::invalid_argument("invalid action selection");
    }
    if (!CanPartySlotParticipate(party_slot) && action.kind != W8_ACTION_WALK &&
        action.kind != W8_ACTION_RUN)
        return {};
    return action;
}
