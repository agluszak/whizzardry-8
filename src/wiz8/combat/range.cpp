#include "wiz8/combat/range.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "monster_actions.h"
#include "sight_state.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

int g_event_sight_blocked = kFirstRemappedEvent + 30;

/* The range categories are W8RangeCategory, declared with the spell record
   that shares the domain. Above W8_RANGE_SHORT an attack is out of the
   close-quarters band the melee rule restricts itself to. */

/* Whether the slot has any attack of `category` that reaches a valid target
   in the scanned groups for `hand`; the condition interrupt uses it to tell
   usable attacks from merely reachable ones. `flag` == 1 skips the monster
   scan and only the out-of-combat and plain-attack categories also scan the
   party for an opposing-side member the chosen action could actually
   strike. */
bool CanPartySlotAttackAnyTarget(int party_slot, int flag, bool hand)
{
    signed char side;
    int first = party_slot;

    if (!CurrentGame().combat.has_value() ||
        !CurrentGame().combat->characters[party_slot].berserk) {
        side = hand;
    } else {
        side = 1;
    }
    side = (side != 0) + 1;
    bool live_groups = CombatAllowsLiveGroups();
    if (flag != 1) {
        unsigned int index;
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            W8MonsterInfo* monster_info = GetMonsterListEntry(index);
            if (monster_info->fActive && monster_info->hp_current != 0 &&
                monster_info->highest_condition < W8_CONDITION_DEAD &&
                (live_groups || MonsterVsCharDisposition(first, monster_info) == side)) {
                for (unsigned int reach_hand = 0; reach_hand < 2; ++reach_hand) {
                    if (CanHandReachTarget(first, reach_hand) &&
                        AssessMonsterAim(first, reach_hand, monster_info,
                                         W8Action{W8_ACTION_ATTACK, -1, {}, {}})) {
                        return true;
                    }
                }
            }
        }
    }
    {
        W8Character* character = &CurrentGame().party.characters[first];
        for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
            if (slot != first && CanPartySlotParticipate(slot) &&
                CharacterVsCharacterDisposition(first, slot) == side) {
                for (unsigned int reach_hand = 0; reach_hand < 2; ++reach_hand) {
                    if (!CanHandReachTarget(first, reach_hand)) {
                        continue;
                    }
                    if (static_cast<signed char>(first) == slot) {
                        return true;
                    }
                    const auto range = GetCharAttackRange(character, reach_hand);
                    if (range == W8_RANGE_NONE) {
                        continue;
                    }
                    if (range != W8_RANGE_TOUCH) {
                        return true;
                    }
                    if (!FrontRankScreens(first, slot)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* Whether the slot's chosen action reaches the target it was aimed at:
   `hand` selects the attack side (2 asks for the better hand). A monster
   must be aimable, another party member takes the action's range (with the
   front-rank screen on melee), a point on the ground must sit inside the
   spell's distance and its line of sight, and a group target defers to the
   shared group range test. */
bool CharacterActionReachesTarget(int party_slot, int hand, const W8Action& selected)
{
    const W8CombatSlot* target = &selected.target;
    if (!target) return false;
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster_info = FindMonster(target->iMonsterID, false);
        if (monster_info == nullptr) {
            return false;
        }
        GetMonsterDataForInfo(monster_info);
        return AssessMonsterAim(party_slot, hand, monster_info, selected);
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        int target_slot = target->iChar;
        if (static_cast<signed char>(party_slot) != target_slot) {
            W8RangeCategory range = GetCharActionRange(party_slot, hand, selected);
            if (range == W8_RANGE_NONE) {
                return false;
            }
            if (range == W8_RANGE_TOUCH && FrontRankScreens(party_slot, target_slot)) {
                return false;
            }
        }
    } else if (target->iType == W8_TARGET_KIND_PLACE) {
        glm::vec3 camera;
        glm::vec3 camera_top;
        glm::vec3 point;
        float distance;
        bool trace;

        camera = cameraLocation();
        camera_top = cameraLocation();
        point = target->point;
        unsigned int spell_id = GetActionSpellLikeId(CurrentGame().party, selected);
        assert(spell_id != W8_SPELL_NONE);
        W8SpellTargetType target_type = GetSpellTargetType(spell_id, false);
        if (target_type == W8_TARGET_TYPE_CONE) {
            trace = false;
            distance = 1.25e+04f;
        } else {
            assert(target_type == W8_TARGET_TYPE_RADIUS || target_type == W8_TARGET_TYPE_POINT);

            trace = true;
            camera.y -= g_default_world_height;
            point.y -= 1e+03f;
            distance = CalcRangeDistance(Data().spells[spell_id].range_category);
        }
        float dx = point.x - camera.x;
        float dz = point.z - camera.z;
        if (distance + 1.0f <
            sqrtf(dx * dx + (point.y - camera.y) * (point.y - camera.y) + dz * dz) -
                GetWorld()->party_navigator.radius) {
            return false;
        }
        if (trace) {
            return !GetWorldSpatialIndex()->traceSight(camera_top, point);
        }
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        W8MonsterGroup* group = FindMonsterGroup(target->iGroupID, false);
        if (group == nullptr) {
            return false;
        }
        assert(group != 0);
        W8TargetSource source;
        SetTargetSourceToCharacter(party_slot, &source);
        return IsTargetSourceInRangeOfGroup(&source, group,
                                            GetCharActionRange(party_slot, 0, selected));
    }
    return true;
}

/* Whether `party_slot` may aim at `monster_info`. The monster must be a live
   threat; with no targeting/combat/spell/item mode outstanding the pick is a
   plain sighting and the ranged aim flag applies, otherwise the slot's chosen
   action supplies the range category (a combat melee band loses the formation
   rows between slot and monster) and selects the aim flag. An allowed aim
   then has to pass the band distance to the monster. */
W8TargetAssessment AssessMonsterAim(int party_slot, int hand, W8MonsterInfo* monster_info,
                                    const W8Action& selected,
                                    std::optional<W8RangeCategory> attack_range, bool sighting)
{
    if (!monster_info || monster_info->party_threat.sight_state != W8_SIGHT_SEEN)
        return {W8TargetFailure::Unavailable};
    bool spell_sight = true;
    auto range = W8_RANGE_TOUCH;
    if (!sighting) {
        range = GetCharActionRange(party_slot, hand, selected, attack_range);
        if (range == W8_RANGE_NONE) return {W8TargetFailure::Unavailable};
        spell_sight = selected.kind == W8_ACTION_TURN_UNDEAD ||
                      (selected.kind > W8_ACTION_PROTECT && selected.kind < W8_ACTION_EQUIP);
        if (CurrentGame().combat.has_value() && range >= 0 && range < 2) {
            int rows = CountRowsBetween(party_slot, monster_info);
            while (rows-- > 0 && range != W8_RANGE_NONE)
                range = range == W8_RANGE_TOUCH ? W8_RANGE_NONE
                                                : static_cast<W8RangeCategory>(range - 1);
            if (range == W8_RANGE_NONE) return {W8TargetFailure::OutOfRange};
        }
    }
    if (!monster_info->party_threat.los_flags[spell_sight]) return {W8TargetFailure::SightBlocked};
    if (CalcRangeDistance(range) < monster_info->entity->GetDistanceToPlayer())
        return {W8TargetFailure::OutOfRange};
    return {};
}

/* The slot-vs-slot reach check the target-list builders share: the slot's
   chosen action has to have a range, and melee range additionally has to get
   past the front rank. */
bool CharacterActionReachesSlot(int party_slot, int hand, int target_slot, const W8Action& selected)
{
    if (static_cast<signed char>(party_slot) == target_slot) {
        return true;
    }
    if (selected.kind == W8_ACTION_BREATHE) return true;
    const auto range = GetCharActionRange(party_slot, hand, selected);
    if (range == W8_RANGE_NONE) {
        return false;
    }
    if (range != W8_RANGE_TOUCH) {
        return true;
    }
    if (FrontRankScreens(party_slot, target_slot)) {
        return false;
    }
    return true;
}

/* Whether `party_slot`'s action can reach any member of `group_id` at all.
   The character becomes the source for the shared range test; a miss can
   queue the character's complaint event when `notify` asks for it. */
W8TargetAssessment AssessGroupRange(int party_slot, int group_id, const W8Action& selected)
{
    auto* group = FindMonsterGroup(group_id, false);
    if (!group) return {W8TargetFailure::Unavailable};
    W8TargetSource source;
    SetTargetSourceToCharacter(party_slot, &source);
    if (!IsTargetSourceInRangeOfGroup(&source, group, GetCharActionRange(party_slot, 0, selected)))
        return {W8TargetFailure::OutOfRange};
    return {};
}

/* The range category the slot's chosen action works at. Attacks take the
   weapon's reach for the asked hand, a move acts at long range, spells and
   item uses ask their spell record, and anything unknown has no range. */
W8RangeCategory GetCharActionRange(int party_slot, int hand, const W8Action& selected,
                                   std::optional<W8RangeCategory> attack_range)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    switch (selected.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        return attack_range ? *attack_range : GetCharAttackRange(character, hand);
    case W8_ACTION_CAST_SPELL:
        if (selected.detail != 0) {
            return Data().spells[selected.detail].range_category;
        }
        break;
    case W8_ACTION_USE_ITEM:
        return GetItemSpellRange(CurrentGame().party.itemAt(selected.detail_block.item_use.item));
    case W8_ACTION_PROTECT:
        return W8_RANGE_TOUCH;
    case W8_ACTION_BREATHE:
        return W8_RANGE_LONG;
    default:
        break;
    }
    return W8_RANGE_NONE;
}

/* The range category the weapon in one of the character's hands attacks at.
   `hand` of HAND_ANY asks both hands and keeps the better answer; only a
   hand actually in play carrying a melee or thrown wield kind has a range to
   report at all. */
W8RangeCategory GetCharAttackRange(const W8Character* character, unsigned int hand)
{
    unsigned int index;
    W8RangeCategory best;
    W8RangeCategory range;

    if (hand >= 2) {
        assert(hand == 2);
        best = W8_RANGE_NONE;
        for (index = 0; index < 2; ++index) {
            if (character->Hand[index].in_play) {
                range = GetCharAttackRange(character, index);
                if (best < range) {
                    best = range;
                }
            }
        }
        return best;
    }
    if (!character->Hand[hand].in_play) {
        return W8_RANGE_NONE;
    }
    if (character->Hand[hand].uiHolds != 1 && character->Hand[hand].uiHolds != 3) {
        return W8_RANGE_TOUCH;
    }
    return static_cast<W8RangeCategory>(
        Data().items[character->EquippedItem[6 + (hand != 0)].iItemNo].range_category);
}

/* The furthest range category any of this character's hands can reach at. */
W8RangeCategory GetBestHandRangeCategory(const W8Character* character)
{
    W8RangeCategory best = W8_RANGE_NONE;
    W8RangeCategory category;
    unsigned int hand;

    for (hand = 0; hand < 2; ++hand) {
        if (character->Hand[hand].in_play) {
            category = GetCharAttackRange(character, hand);
            if (category > best) {
                best = category;
            }
        }
    }
    return best;
}

/* Whether the first lighting condition applies at distant or extreme range. */
bool RangeCategoryUsesSightCondition(const W8MonsterInfo* monster, W8RangeCategory range_category)
{
    if (range_category >= W8_RANGE_LONG && range_category <= W8_RANGE_EXTREME) {
        return monster->has_projectile_origin;
    }
    return false;
}

/* Whether the monster's attack `attack` reaches anyone at all; `friendly_targets`
   selects the friendly disposition band rather than the hostile band. In
   combat, berserk forces the friendly band. Party members and other monsters defer to the corresponding
   MonsterAttackReachesCharacter/Monster helpers. */
bool MonsterAttackReachesAnyone(W8MonsterInfo* monster_info, unsigned int attack,
                                bool friendly_targets)
{
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8Disposition disposition_needed;
    int party_slot;
    unsigned int index;
    std::size_t count;
    W8MonsterInfo* other;

    if (!monster_info->pCombat || !monster_info->pCombat->berserk) {
        disposition_needed = friendly_targets ? W8_DISPOSITION_FRIENDLY : W8_DISPOSITION_HOSTILE;
    } else {
        disposition_needed = W8_DISPOSITION_FRIENDLY;
    }

    for (party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8Character* character = &CurrentGame().party.characters[party_slot];

        if (!CurrentGame().party.members[party_slot].has_value()) {
            continue;
        }
        if (character->hp_current == 0) {
            continue;
        }
        if (character->highest_condition >= W8_CONDITION_DEAD) {
            continue;
        }
        if (MonsterVsCharDisposition(party_slot, monster_info) != disposition_needed) {
            continue;
        }
        if (MonsterAttackReachesCharacter(monster_info, record, attack, party_slot)) {
            return true;
        }
    }

    count = CurrentLevelPopulation().monster_roster.live().size();
    for (index = 0; index < count; ++index) {
        other = GetMonsterListEntry(index);
        GetMonsterDataForInfo(other);
        if (other != monster_info && other->fActive && other->pCombat && other->hp_current != 0 &&
            other->highest_condition < W8_CONDITION_DEAD &&
            MonsterHostility(monster_info, other) == disposition_needed &&
            MonsterAttackReachesMonster(monster_info, record, attack, other)) {
            return true;
        }
        count = CurrentLevelPopulation().monster_roster.live().size();
    }
    return false;
}

static int GetMonsterActionSightIndex(W8MonsterInfo* monster_info, unsigned int attack)
{
    if (monster_info->action_kind == W8_MONSTER_ACTION_ATTACK) {
        const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
        return RangeCategoryUsesSightCondition(
            monster_info, static_cast<W8RangeCategory>(record->attacks[attack].range_category));
    }
    if (monster_info->action_kind == W8_MONSTER_ACTION_SPELL) {
        return GetMonsterSpellSightIndex(monster_info);
    }
    return monster_info->action_kind == W8_MONSTER_ACTION_SPECIAL_ATTACK ? 2 : 0;
}

static W8RangeCategory GetMonsterPartyTargetRange(W8MonsterInfo* monster_info,
                                                  const W8MonsterRecord* record,
                                                  unsigned int attack, int party_slot)
{
    int sight_index;
    W8RangeCategory range;

    if (monster_info->player_visibility.sight_state != W8_SIGHT_SEEN) {
        return W8_RANGE_NONE;
    }

    sight_index = GetMonsterActionSightIndex(monster_info, attack);
    if (!monster_info->player_visibility.los_flags[sight_index]) {
        return W8_RANGE_NONE;
    }

    range = GetMonsterActionRangeCategory(monster_info, record, attack);
    if (range == W8_RANGE_NONE) {
        return W8_RANGE_NONE;
    }
    CloseFormationGap(monster_info, party_slot, &range);
    if (range == W8_RANGE_NONE) {
        return W8_RANGE_NONE;
    }

    return range;
}

/* Whether the monster's attack `attack` reaches the character in `party_slot`,
   given what it can see and how far away they stand. Combat mode shortens
   touch/short reach by CountRowsBetween. */
bool MonsterAttackReachesCharacter(W8MonsterInfo* monster_info, const W8MonsterRecord* record,
                                   unsigned int attack, int party_slot)
{
    W8RangeCategory range = GetMonsterPartyTargetRange(monster_info, record, attack, party_slot);
    if (range == W8_RANGE_NONE) {
        return false;
    }

    float distance = CalcRangeDistance(range);
    if (monster_info->entity->GetDistanceToPlayer() <= distance) {
        return true;
    }
    return false;
}

/* Whether the monster's attack `attack` reaches another monster. Untargetable
   attackers never reach; the same monster always does. Sight comes from the
   mon-to-mon visibility row rather than the party record. */
bool MonsterAttackReachesMonster(W8MonsterInfo* monster_info, const W8MonsterRecord* record,
                                 unsigned int attack, W8MonsterInfo* target)
{
    W8VisibilityRecord* visibility;
    int sight_index;
    W8RangeCategory range;

    if (record->untargetable != 0) {
        return false;
    }
    if (target == monster_info) {
        return true;
    }
    visibility = FindMonToMonVisibility(monster_info, target);
    if (visibility == 0) {
        return false;
    }
    if (visibility->sight_state != W8_SIGHT_SEEN) {
        return false;
    }

    sight_index = GetMonsterActionSightIndex(monster_info, attack);
    if (!visibility->los_flags[sight_index]) {
        return false;
    }

    range = GetMonsterActionRangeCategory(monster_info, record, attack);
    if (range == W8_RANGE_NONE) {
        return false;
    }

    float distance = CalcRangeDistance(range);
    if (monster_info->entity->GetDistanceToMonster(target->entity.find()) <= distance) {
        return true;
    }
    return false;
}

/* The furthest range category among a monster's three attacks. Asking for the
   close-quarters band only considers the two categories inside it. */
W8RangeCategory GetBestMonsterAttackRange(const W8MonsterRecord* record, bool close_quarters_only)
{
    W8RangeCategory best = W8_RANGE_NONE;
    int attack;
    unsigned char category;

    for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
        if (record->attacks[attack].fHasAttack != 0) {
            category = record->attacks[attack].range_category;
            if ((!close_quarters_only || category < W8_RANGE_LONG) &&
                static_cast<int>(category) > best) {
                best = static_cast<W8RangeCategory>(category);
            }
        }
    }
    return best;
}

/* Furthest attack band, optionally raised to long for certain special-attack
   kinds, then the furthest castable spell band when the gates allow. */
W8RangeCategory GetMonsterBestRangeCategory(W8MonsterInfo* monster_info,
                                            bool skip_capability_checks, int* out_sight)
{
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8RangeCategory best = W8_RANGE_NONE;
    int attack;
    int spell;

    for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
        if (record->attacks[attack].fHasAttack != 0 &&
            static_cast<int>(record->attacks[attack].range_category) > best) {
            best = static_cast<W8RangeCategory>(record->attacks[attack].range_category);
        }
    }

    if (best >= W8_RANGE_LONG && best <= W8_RANGE_EXTREME) {
        *out_sight = monster_info->has_projectile_origin;
    } else {
        *out_sight = 0;
    }

    if (!skip_capability_checks && record->prefer_ranged_actions != 1) {
        return best;
    }
    if (skip_capability_checks || record->flee_chance >= 0x50) {
        if (record->special_attack_kind != 0 &&
            g_special_attack_table[record->special_attack_kind][0] != 6 &&
            (skip_capability_checks || CanMonsterFlee(monster_info, record, true))) {
            if (best < W8_RANGE_LONG) {
                best = W8_RANGE_LONG;
                *out_sight = W8_RANGE_LONG;
            }
        }
    }
    if (!skip_capability_checks &&
        (record->prefer_ranged_actions != 1 || record->spell_chance < 0x50)) {
        return best;
    }

    for (spell = 0; spell < 10; ++spell) {
        unsigned int spell_id = record->spells[spell];

        if (!MonsterCanAimSpell(spell_id)) {
            continue;
        }
        if (!skip_capability_checks &&
            !IsSpellUsableByMonster(monster_info, static_cast<int>(spell_id), true)) {
            continue;
        }
        if (static_cast<int>(best) < static_cast<int>(Data().spells[spell_id].range_category)) {
            best = Data().spells[spell_id].range_category;
            *out_sight = GetMonsterSpellSightIndex(monster_info);
        }
    }
    return best;
}

/* The range category one monster action works at. A spell takes the range off
   the spell record; two of the actions have a fixed answer and the rest have
   none. A plain attack takes it from the attack itself, which has to exist. */
W8RangeCategory GetMonsterActionRangeCategory(const W8MonsterInfo* monster_info,
                                              const W8MonsterRecord* record, unsigned int attack)
{
    switch (monster_info->action_kind) {
    case W8_MONSTER_ACTION_ATTACK:
        break;
    case W8_MONSTER_ACTION_SPELL:
        return Data().spells[monster_info->action_detail].range_category;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        return W8_RANGE_LONG;
    case W8_MONSTER_ACTION_PROTECT:
        return W8_RANGE_TOUCH;
    default:
        return W8_RANGE_NONE;
    }

    assert(attack < W8_MAX_MONSTER_ATTACKS);
    assert(record->attacks[attack].fHasAttack != 0);
    return MonsterAttackRange(*monster_info, *record, attack);
}

/* How far a range category actually is. The four categories step 2, 4, 25, 50
   before the world scale multiplies them; no range at all is zero distance. */
float CalcRangeDistance(W8RangeCategory range_category)
{
    unsigned int steps = 0;

    switch (range_category) {
    case W8_RANGE_NONE:
        steps = 0;
        break;
    case W8_RANGE_TOUCH:
        steps = 2;
        break;
    case W8_RANGE_SHORT:
        steps = 4;
        break;
    case W8_RANGE_LONG:
        steps = 25;
        break;
    case W8_RANGE_EXTREME:
        steps = 50;
        break;
    default:
        return 0.0f;
    }
    return steps * kWorldScale;
}

/* Source-relative action range: same band steps as CalcRangeDistance, then add
   the source's navigator radius - the party navigator for a character source,
   the individual monster's for a monster source. */
float CalcRangeDistance(W8RangeCategory range_category, W8TargetSource* source)
{
    float distance = CalcRangeDistance(range_category);
    if (TargetSourceIsCharacter(source, 0)) {
        return GetWorld()->party_navigator.movement.alternate_radius + distance;
    }
    if (TargetSourceIsMonster(source, 0)) {
        assert(source->iMonsterID != -1);
        W8MonsterInfo* monster_info = FindMonster(source->iMonsterID);
        return monster_info->entity->nav.movement.alternate_radius + distance;
    }
    return distance;
}

/* Party-relative action range for the world cursor: same band steps as
   CalcRangeDistance, then add the startup navigator's movement collision_radius. */
float CalcRangeDistanceFromParty(W8RangeCategory range_category)
{
    return CalcRangeDistance(range_category) +
           GetWorld()->party_navigator.movement.collision_radius;
}

/* Shrink a short-range category by the formation rows CountRowsBetween says
   stand between the monster and `party_slot`. Exhausting the category marks it
   unreachable (-1). */
void CloseFormationGap(W8MonsterInfo* monster_info, int party_slot, W8RangeCategory* range_category)
{
    signed char crossable;

    if (!CurrentGame().combat.has_value()) {
        return;
    }
    if (*range_category < W8_RANGE_TOUCH || *range_category >= W8_RANGE_LONG) {
        return;
    }
    crossable = CountRowsBetween(party_slot, monster_info);
    if (crossable == 0) {
        return;
    }
    while (*range_category != W8_RANGE_TOUCH) {
        --crossable;
        *range_category = static_cast<W8RangeCategory>(*range_category - 1);
        if (crossable == 0) {
            return;
        }
    }
    *range_category = W8_RANGE_NONE;
}

/* Whether anybody standing ahead of this position is still in formation. */
bool AnyoneStandsAhead(unsigned char position)
{
    unsigned char found = 0;
    unsigned int index;
    signed char slot;

    for (index = 0; index < W8_FORMATION_COLUMNS; ++index) {
        slot = CurrentGame().party.formation.cells[position][index];
        if (slot != -1 && CurrentGame().party.characters[slot].bonus.out_of_formation == 0) {
            ++found;
        }
    }
    return found > 0;
}

/* How many formation rows between `party_slot` and the monster block a short
   reach. Same row answers zero; otherwise one when the monster's row is
   occupied, plus one more when the gap is exactly two rows and either that
   row or the front rank screens. */
signed char CountRowsBetween(int party_slot, W8MonsterInfo* monster_info)
{
    unsigned char monster_quadrant = static_cast<unsigned char>(GetMonsterQuadrant(monster_info));
    signed char party_quadrant =
        static_cast<signed char>(CurrentGame().party.formation.rowOf(party_slot));
    signed char rows = 0;
    unsigned int index;
    signed char slot;
    int gap;
    signed char found_front;

    if (monster_quadrant == static_cast<unsigned char>(party_quadrant)) {
        return 0;
    }

    for (index = 0; index < W8_FORMATION_COLUMNS; ++index) {
        slot = CurrentGame().party.formation.cells[monster_quadrant][index];
        if (slot != -1 && CurrentGame().party.characters[slot].bonus.out_of_formation == 0) {
            ++rows;
        }
    }
    if (rows != 0) {
        rows = 1;
    }

    assert(monster_quadrant != 4);
    if (party_quadrant == 4) {
        return rows;
    }

    gap = static_cast<int>(monster_quadrant) - party_quadrant;
    if (gap < 0) {
        gap = -gap;
    }
    if (gap != 2) {
        return rows;
    }
    if (rows != 0) {
        return static_cast<signed char>(rows + 1);
    }

    found_front = 0;
    for (index = 0; index < W8_FORMATION_COLUMNS; ++index) {
        slot = CurrentGame().party.formation.cells[4][index];
        if (slot != -1 && CurrentGame().party.characters[slot].bonus.out_of_formation == 0) {
            ++found_front;
        }
    }
    if (found_front != 0) {
        return static_cast<signed char>(rows + 1);
    }
    return rows;
}

/* Whether the front rank stands between two positions. Only positions exactly
   two rows apart can be screened, and the fifth row is never in the way. */
bool FrontRankScreens(unsigned int from_position, unsigned int to_position)
{
    unsigned char from_row =
        static_cast<unsigned char>(CurrentGame().party.formation.rowOf(from_position));
    unsigned char to_row =
        static_cast<unsigned char>(CurrentGame().party.formation.rowOf(to_position));
    int rows_apart;
    int found;
    unsigned int index;
    signed char slot;

    if (from_row == to_row) {
        return false;
    }
    if (from_row == 4 || to_row == 4) {
        return false;
    }
    rows_apart = from_row - static_cast<signed char>(to_row);
    if (rows_apart < 0) {
        rows_apart = -rows_apart;
    }
    if (rows_apart != 2) {
        return false;
    }

    found = 0;
    for (index = 0; index < W8_FORMATION_COLUMNS; ++index) {
        slot = CurrentGame().party.formation.cells[4][index];
        if (slot != -1 && CurrentGame().party.characters[slot].bonus.out_of_formation == 0) {
            ++found;
        }
    }
    return found != 0;
}

/* Every other seated, living party member whose side matches `relationship`
   and that the slot's chosen action could actually strike is collected; one of
   them is picked at random, or -1 when nobody qualifies. The berserk and
   turncoat paths use it to pick a victim. */
int PickReachableSlotByDisposition(int party_slot, W8Disposition relationship)
{
    int candidates[8];
    int* next = candidates;
    int count = 0;
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (slot == party_slot || !CanPartySlotParticipate(slot) ||
            CharacterVsCharacterDisposition(party_slot, slot) != relationship) {
            continue;
        }
        for (unsigned int hand = 0; hand < 2; ++hand) {
            if (!CanHandReachTarget(party_slot, hand)) {
                continue;
            }
            if (!CharacterActionReachesSlot(
                    party_slot, hand, slot,
                    SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
                continue;
            }
            ++count;
            *next = slot;
            ++next;
            break;
        }
    }
    if (count == 0) {
        return -1;
    }
    return candidates[CurrentGame().rng.below(count)];
}

/* Two seven-byte constant readers the range rules share. */
float GetMonsterEngagementRange(void)
{
    return 2.5e+04f;
}

float GetGroundTargetRange(void)
{
    return 1.25e+04f;
}

/* Cache which origin points this monster can actually provide. Projectile
   attacks need a usable launch point only when their best range is long or
   extreme; spell casting also requires the spell cycle. */
void InitializeMonsterRangeCapabilities(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    monster_info->unknown_379 = 1;
    monster_info->unknown_37b = 1;

    W8RangeCategory best_range = W8_RANGE_NONE;
    for (const auto& attack : record->attacks) {
        if (attack.fHasAttack != 0 && best_range < attack.range_category) {
            best_range = static_cast<W8RangeCategory>(attack.range_category);
        }
    }

    glm::vec3 position;
    monster_info->has_projectile_origin =
        best_range > W8_RANGE_SHORT && monster_info->entity->GetProjectilePosition(&position);

    monster_info->has_spell_origin = record->spell_chance != 0 &&
                                     monster_info->entity->IsCycleSupported(0x19) &&
                                     monster_info->entity->GetSpellPosition(&position);
}

unsigned char TraceModeRejectsNoHit(int mode)
{
    switch (mode) {
    case 0:
    case 1:
        return 0;
    case 2:
    case 3:
        return 1;
    default:
        return static_cast<unsigned char>(mode);
    }
}

/* Choose what one monster aims at: the player when it can see them, otherwise
   the nearest hostile visible monster. Answers the chosen distance and fills
   the target kind and monster id when applicable. */
// FUNCTION: WIZ8 0x0051ac30
float MonsterChooseTarget(W8MonsterInfo* monster_info, W8CombatSlot* out,
                          W8VisibilityCondition condition)
{
    float best = 999999.0f;

    out->iType = W8_TARGET_KIND_NONE;
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
        IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, condition) &&
        (best = monster_info->entity->GetDistanceToPlayer(), best < 999999.0f)) {
        out->iType = W8_TARGET_KIND_CHARACTER;
    }
    if (out->iType == W8_TARGET_KIND_NONE || !monster_info->pCombat->reconsider_action) {
        auto count = CurrentLevelPopulation().monster_roster.live().size();

        for (unsigned int index = 0; index < count; ++index) {
            W8MonsterInfo* other = GetMonsterListEntry(index);

            if (other != monster_info && other->fActive && other->hp_current != 0 &&
                other->pCombat && MonsterHostility(monster_info, other) == W8_DISPOSITION_HOSTILE) {
                W8VisibilityRecord* row = FindMonToMonVisibility(monster_info, other);
                if (IsVisibleUnderConditions(monster_info, row, condition)) {
                    float distance =
                        monster_info->entity->GetDistanceToMonster(other->entity.find());
                    if (distance < best) {
                        out->iType = W8_TARGET_KIND_MONSTER;
                        out->iMonsterID = other->location_id;
                        best = distance;
                    }
                }
            }
            count = CurrentLevelPopulation().monster_roster.live().size();
        }
    }
    return best;
}
/* Whether the monster's current action still reaches the slot `target`
   carries: its own line of sight has to hold for a character or the party, the
   target monster has to be reachable, and a group target is checked against
   every member. */
bool MonsterActionReachesTarget(W8MonsterInfo* monster_info, const W8MonsterRecord* record,
                                unsigned int attack, W8CombatSlot* target)
{
    W8RangeCategory range;

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        int party_slot = target->iChar;
        range = GetMonsterPartyTargetRange(monster_info, record, attack, party_slot);
        if (range == W8_RANGE_NONE) {
            return false;
        }
        if (CalcRangeDistance(range) < monster_info->entity->GetDistanceToPlayer()) {
            return false;
        }
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* target_monster = FindMonster(target->iMonsterID);
        return MonsterAttackReachesMonster(monster_info, record, attack, target_monster);
    } else if (target->iType == W8_TARGET_KIND_PARTY) {
        int party_slot = GetRandomCharacter(1, 1, -1, -1);
        range = GetMonsterPartyTargetRange(monster_info, record, attack, party_slot);
        if (range == W8_RANGE_NONE) {
            return false;
        }
        if (CalcRangeDistance(range) < monster_info->entity->GetDistanceToPlayer()) {
            return false;
        }
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        W8TargetSource source;
        SetTargetSourceToMonster(monster_info, &source);
        W8MonsterGroup* group = FindMonsterGroup(target->iGroupID);
        return IsTargetSourceInRangeOfGroup(&source, group, W8_RANGE_NONE);
    }
    return true;
}

// FUNCTION: WIZ8 0x0051ad60
int FindNearestVisibleGroupMonster(W8MonsterInfo* monster_info, int group_id,
                                   W8VisibilityCondition condition)
{
    int best_id = -1;
    float best = 999999.0f;
    W8MonsterGroup* group = FindMonsterGroup(group_id);
    for (unsigned int index = 0; index < group->MemberIds().size(); ++index) {
        int location_id = group->MemberLocationId(index);
        W8MonsterInfo* target = FindMonster(location_id);
        if (target->fActive && target->hp_current != 0 && target->pCombat) {
            W8VisibilityRecord* row = FindMonToMonVisibility(monster_info, target);
            if (IsVisibleUnderConditions(monster_info, row, condition)) {
                float distance = monster_info->entity->GetDistanceToMonster(target->entity.find());
                if (distance < best) {
                    best_id = target->location_id;
                    best = distance;
                }
            }
        }
    }
    return best_id;
}

/* The attack-origin offset for `kind` 1 (projectile) or 3 (spell): the
   monster's height offset, shifted by the delta between its navigator
   position and the muzzle/hand point when that point resolves. */
void GetMonsterAttackSourceOffset(Monster* monster, int kind, glm::vec3* out)
{
    (*out) = glm::vec3(0.0f, monster->nav.movement.height_offset, 0.0f);
    if (kind == 1) {
        if (monster->GetProjectilePosition(out)) {
            glm::vec3 position = monster->nav.GetPosition();
            *out -= position;
            return;
        }
    } else {
        if (kind != 3) {
            return;
        }
        if (monster->GetSpellPosition(out)) {
            glm::vec3 position = monster->nav.GetPosition();
            *out -= position;
            return;
        }
    }
    // A model without the origin vertex attacks from its height offset.
}

bool SourceActionReachesTarget(W8TargetSource* source, W8CombatSlot* target, const W8Action& action)
{
    if (TargetSourceIsCharacter(source, 0)) {
        return CharacterActionReachesTarget(source->iChar, 0, action);
    }
    if (TargetSourceIsMonster(source, 0)) {
        W8MonsterInfo* monster_info = FindMonster(source->iMonsterID);
        const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
        return MonsterActionReachesTarget(monster_info, record, 0, target);
    }
    return true;
}
