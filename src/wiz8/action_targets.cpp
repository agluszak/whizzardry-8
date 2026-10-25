#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/character_event_queue.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/monster_roster.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/sight_state.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

bool Game::targetPresent(const W8CombatSlot& target) const
{
    const auto* population = level.primary ? level.primary->population.get() : nullptr;
    switch (target.iType) {
    case W8_TARGET_KIND_CHARACTER:
        return party.participates(target.iChar);
    case W8_TARGET_KIND_CHARACTER_INDIRECT:
        return static_cast<unsigned int>(target.iChar) < W8_PARTY_SLOT_COUNT &&
               party.members[target.iChar] && party.characters[target.iChar].hp_current == 0 &&
               party.characters[target.iChar].uiCondition[W8_CONDITION_DEAD] != 0 &&
               party.characters[target.iChar].uiCondition[W8_CONDITION_MISSING] == 0;
    case W8_TARGET_KIND_MONSTER: {
        const auto* monster = population ? population->findMonster(target.iMonsterID) : nullptr;
        return monster && monster->hp_current != 0 &&
               monster->uiCondition[W8_CONDITION_DEAD] == 0 && monster->fActive;
    }
    case W8_TARGET_KIND_GROUP: {
        const auto* group = population ? population->findGroup(target.iGroupID) : nullptr;
        return group && group->MemberCount() != 0 && group->members_active;
    }
    case W8_TARGET_KIND_ITEM: {
        const auto* item = inventory.itemAt(target.item);
        return item && item->iItemNo != -1;
    }
    default:
        return true;
    }
}

bool Game::targetMatches(const W8CombatSlot& target, W8TargetNeed needed) const
{
    bool matches;
    switch (needed) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ENEMY:
    case W8_TARGET_NEED_CASTER:
        matches = (target.iType == W8_TARGET_KIND_CHARACTER && target.iChar != -1) ||
                  (target.iType == W8_TARGET_KIND_MONSTER && target.iMonsterID != -1);
        break;
    case W8_TARGET_NEED_PLACE:
    case W8_TARGET_NEED_CONE:
        matches = target.iType == W8_TARGET_KIND_PLACE;
        break;
    case W8_TARGET_NEED_GROUP:
        matches = (target.iType == W8_TARGET_KIND_GROUP && target.iGroupID != -1) ||
                  target.iType == W8_TARGET_KIND_PARTY;
        break;
    case W8_TARGET_NEED_ITEM:
        matches = target.iType == W8_TARGET_KIND_ITEM;
        break;
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        matches = target.iType == W8_TARGET_KIND_CHARACTER_INDIRECT && target.iChar != -1;
        break;
    default:
        return true;
    }
    return matches && targetPresent(target);
}

bool Game::actionTargetsEnemies(unsigned int slot, const W8Action& action) const
{
    switch (action.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
    case W8_ACTION_BREATHE:
    case W8_ACTION_TURN_UNDEAD:
        return true;
    case W8_ACTION_CAST_SPELL:
        return SpellTargetsEnemies(data, action.detail);
    case W8_ACTION_USE_ITEM: {
        const auto* item = inventory.itemAt(action.detail_block.item_use.item);
        return item &&
               CanCharacterActivateItem(data, &party.characters[slot], item, status.game_started) &&
               data.items[item->iItemNo].spell_id != 0 &&
               SpellTargetsEnemies(data, data.items[item->iItemNo].spell_id);
    }
    default:
        return false;
    }
}

void Game::aimStoredAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target)
{
    std::vector<Event> outcomes;
    aimStoredAction(slot, stored, target, outcomes);
    events.publish(std::move(outcomes));
}

void Game::aimStoredAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target,
                           std::vector<Event>& outcomes)
{
    const bool committed = stored == W8_TARGETING_CONTEXT_IN_COMBAT;
    const bool changed = party.aimAction(slot, stored, target, committed && combat.has_value());
    if (!committed) return;
    if (changed && combat) combat->target_picked = false;
    if (changed || target.iType != W8_TARGET_KIND_NONE)
        outcomes.emplace_back(ActionAimChanged{slot});
}

void Game::invalidateAimsAt(const W8CombatSlot& target, TargetChange change)
{
    std::vector<Event> outcomes;
    invalidateAimsAt(target, change, outcomes);
    events.publish(std::move(outcomes));
}

void Game::invalidateAimsAt(const W8CombatSlot& target, TargetChange change,
                            std::vector<Event>& outcomes)
{
    const auto invalid = [&](bool source_hostile, bool targets_enemies) {
        if (change == TargetChange::removed) return true;
        const auto* population = level.primary ? level.primary->population.get() : nullptr;
        bool target_hostile;
        switch (target.iType) {
        case W8_TARGET_KIND_CHARACTER:
            target_hostile = party.characters[target.iChar].uiCondition[W8_CONDITION_TURNCOAT] != 0;
            break;
        case W8_TARGET_KIND_MONSTER: {
            const auto* monster = population ? population->findMonster(target.iMonsterID) : nullptr;
            if (!monster) return true;
            target_hostile = monster->ubDisposition == W8_DISPOSITION_HOSTILE;
            break;
        }
        case W8_TARGET_KIND_GROUP: {
            const auto* group = population ? population->findGroup(target.iGroupID) : nullptr;
            if (!group) return true;
            target_hostile = group->ubDisposition == W8_DISPOSITION_HOSTILE;
            break;
        }
        default:
            return false;
        }
        return targets_enemies == (source_hostile == target_hostile);
    };
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.participates(slot)) continue;
        auto& row = party.rows[slot];
        if (combat && row.committed.target == target &&
            invalid(party.characters[slot].uiCondition[W8_CONDITION_TURNCOAT] != 0,
                    actionTargetsEnemies(slot, row.committed))) {
            if (row.committed.kind == W8_ACTION_PROTECT)
                resetCombatChoice(slot, outcomes);
            else
                repickActionTarget(slot, W8_TARGETING_CONTEXT_IN_COMBAT, false, outcomes);
        }
        if (row.executing.target == target &&
            invalid(party.characters[slot].uiCondition[W8_CONDITION_TURNCOAT] != 0,
                    actionTargetsEnemies(slot, row.executing.action())))
            repickActionTarget(slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, false, outcomes);
    }
    if (!level.primary || !level.primary->population) return;
    for (const auto& monster : level.primary->population->monster_roster.live()) {
        if (monster->Target == target &&
            invalid(
                monster->ubDisposition == W8_DISPOSITION_HOSTILE,
                MonsterActionTargetsEnemies(data, monster->action.kind, monster->action.detail)))
            monster->Target = {W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    }
}

namespace {
int ActionSpell(const Game& game, const W8Action& action)
{
    if (action.kind == W8_ACTION_BREATHE) return 0x77;
    if (action.kind == W8_ACTION_CAST_SPELL) return action.detail;
    if (action.kind != W8_ACTION_USE_ITEM) return 0;
    const auto* item = game.inventory.itemAt(action.detail_block.item_use.item);
    if (!item) return 0;
    assert(item->iItemNo != -1);
    return game.data.items[item->iItemNo].spell_id;
}

}

int Game::monsterQuadrant(const W8MonsterInfo& monster) const
{
    const auto eye = level.loaded().party.eye();
    const auto position = monster.entity->nav.GetPosition();
    int bearing = static_cast<int>(NormalizeAngle(GetHeadingAngle(&eye, &position)) * 57.295776f);
    bearing -= status.party_facing;
    if (bearing < 0) bearing += 360;
    return ((bearing + 45) % 360) / 90;
}

W8RangeCategory Game::actionRange(unsigned int slot, unsigned int hand, const W8Action& action,
                                  std::optional<W8RangeCategory> attack_range) const
{
    switch (action.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        return attack_range ? *attack_range
                            : GetCharAttackRange(data, &party.characters[slot], hand);
    case W8_ACTION_CAST_SPELL:
    case W8_ACTION_USE_ITEM: {
        const int spell = ActionSpell(*this, action);
        return spell == 0 ? W8_RANGE_NONE : data.spells[spell].range_category;
    }
    case W8_ACTION_PROTECT:
        return W8_RANGE_TOUCH;
    case W8_ACTION_BREATHE:
        return W8_RANGE_LONG;
    default:
        return W8_RANGE_NONE;
    }
}

W8TargetAssessment Game::assessActionReach(unsigned int slot, unsigned int hand,
                                           const W8Action& action,
                                           std::optional<W8RangeCategory> attack_range,
                                           bool sighting) const
{
    const auto& target = action.target;
    const auto* world = level.primary.get();
    const auto* population = world ? world->population.get() : nullptr;
    switch (target.iType) {
    case W8_TARGET_KIND_MONSTER: {
        const auto* monster = population ? population->findMonster(target.iMonsterID) : nullptr;
        if (!monster || monster->party_threat.sight_state != W8_SIGHT_SEEN || !monster->entity)
            return {W8TargetFailure::Unavailable};
        auto range = W8_RANGE_TOUCH;
        bool spell_sight = true;
        if (!sighting) {
            range = actionRange(slot, hand, action, attack_range);
            if (range == W8_RANGE_NONE) return {W8TargetFailure::Unavailable};
            spell_sight = action.kind == W8_ACTION_TURN_UNDEAD ||
                          (action.kind > W8_ACTION_PROTECT && action.kind < W8_ACTION_EQUIP);
            if (combat && range >= W8_RANGE_TOUCH && range < W8_RANGE_LONG) {
                range = ShortenFormationReach(
                    range, BlockingFormationRows(party, slot, monsterQuadrant(*monster)));
                if (range == W8_RANGE_NONE) return {W8TargetFailure::OutOfRange};
            }
        }
        if (!monster->party_threat.los_flags[spell_sight]) return {W8TargetFailure::SightBlocked};
        return CalcRangeDistance(range) < monster->entity->GetDistanceToPlayer()
                   ? W8TargetAssessment{W8TargetFailure::OutOfRange}
                   : W8TargetAssessment{};
    }
    case W8_TARGET_KIND_CHARACTER: {
        if (static_cast<int>(slot) == target.iChar) return {};
        const auto range = actionRange(slot, hand, action, attack_range);
        if (range == W8_RANGE_NONE) return {W8TargetFailure::Unavailable};
        return range == W8_RANGE_TOUCH && FormationScreens(party, slot, target.iChar)
                   ? W8TargetAssessment{W8TargetFailure::OutOfRange}
                   : W8TargetAssessment{};
    }
    case W8_TARGET_KIND_GROUP: {
        const auto* group = population ? population->findGroup(target.iGroupID) : nullptr;
        if (!group) return {W8TargetFailure::Unavailable};
        const float reach = CalcRangeDistance(actionRange(slot, 0, action));
        for (int id : group->MemberIds()) {
            const auto* member = population->findEntity(id);
            if (member && member->GetDistanceToPlayer() <= reach) return {};
        }
        return {W8TargetFailure::OutOfRange};
    }
    case W8_TARGET_KIND_PLACE: {
        if (!world) return {W8TargetFailure::Unavailable};
        const int spell = ActionSpell(*this, action);
        assert(spell != 0);
        const auto& definition = data.spells[spell];
        auto eye = world->party.eye();
        const auto trace_eye = eye;
        auto point = target.point;
        const bool trace = definition.target_type != W8_TARGET_TYPE_CONE;
        float reach = 12500.0f;
        if (trace) {
            assert(definition.target_type == W8_TARGET_TYPE_RADIUS ||
                   definition.target_type == W8_TARGET_TYPE_POINT);
            eye.y -= g_default_world_height;
            point.y -= 1000.0f;
            reach = CalcRangeDistance(definition.range_category);
        }
        const float dx = point.x - eye.x;
        const float dz = point.z - eye.z;
        if (reach + 1.0f < sqrtf(dx * dx + (point.y - eye.y) * (point.y - eye.y) + dz * dz) -
                               world->party.radius())
            return {W8TargetFailure::OutOfRange};
        if (trace &&
            (!world->compiledIndex() || world->compiledIndex()->traceSight(trace_eye, point)))
            return {W8TargetFailure::SightBlocked};
        return {};
    }
    default:
        return {};
    }
}

W8TargetNeed Game::targetNeed(const W8Action& selected, bool needs_item_target) const
{
    switch (selected.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        return W8_TARGET_NEED_ENEMY;
    case W8_ACTION_CAST_SPELL:
        return SpellTargetNeed(data, selected.detail, false, needs_item_target);
    case W8_ACTION_BREATHE:
        return W8_TARGET_NEED_CONE;
    case W8_ACTION_PROTECT:
        return W8_TARGET_NEED_ALLY;
    case W8_ACTION_USE_ITEM:
        return ItemTargetNeed(data, inventory.itemAt(selected.detail_block.item_use.item),
                              needs_item_target);
    default:
        break;
    }
    return W8_TARGET_NEED_NONE;
}

namespace {
std::optional<int> BestMonsterTarget(const Game& game, unsigned int slot, const W8Action& action,
                                     std::optional<W8RangeCategory> attack_range = {})
{
    if (!game.level.primary || !game.level.primary->population) return {};
    using Priority = std::tuple<W8Condition, bool, float>;
    std::optional<std::pair<Priority, int>> best;
    for (const auto& monster : game.level.primary->population->monster_roster.live()) {
        if (!monster->fActive || !monster->pCombat || monster->hp_current == 0 ||
            game.party.dispositionToward(slot, monster->ubDisposition) != W8_DISPOSITION_HOSTILE ||
            !game.assessActionReach(
                slot, 2, action.aimedAt(W8CombatSlot::monster(monster->location_id)), attack_range))
            continue;
        bool in_reach = false;
        if (GetBestMonsterAttackRange(&game.data.monsters[monster->monster_species], true) !=
            W8_RANGE_NONE) {
            const int quadrant = game.monsterQuadrant(*monster);
            in_reach = quadrant == 2 || (!FormationRowOccupied(game.party, quadrant) &&
                                         FormationRowOccupied(game.party, 4));
        }
        assert(monster->uiHPMax != 0);
        const Priority priority{monster->highest_condition, !in_reach,
                                monster->entity->GetDistanceToPlayer()};
        if (!best || priority < best->first) best = std::pair{priority, monster->location_id};
    }
    return best ? std::optional{best->second} : std::nullopt;
}
}

bool Game::targetInPlay(unsigned int slot, unsigned int hand, const W8Action& action) const
{
    return targetMatches(action.target, targetNeed(action)) &&
           assessActionReach(slot, hand, action);
}

std::optional<TargetChoice> Game::chooseTarget(unsigned int slot, const W8Action& action,
                                               bool allow_weapon_swap) const
{
    const auto kind = action.kind;
    if (!combat ||
        (kind != W8_ACTION_ATTACK && kind != W8_ACTION_BERSERK && kind != W8_ACTION_CAST_SPELL &&
         kind != W8_ACTION_USE_ITEM) ||
        targetNeed(action) != W8_TARGET_NEED_ENEMY)
        return {};
    if (const auto target = BestMonsterTarget(*this, slot, action))
        return TargetChoice{W8CombatSlot::monster(*target), std::nullopt};
    if (!allow_weapon_swap || (kind != W8_ACTION_ATTACK && kind != W8_ACTION_BERSERK)) return {};
    return chooseFallbackTarget(slot, action);
}

std::optional<TargetChoice> Game::chooseFallbackTarget(unsigned int slot,
                                                       const W8Action& action) const
{
    const auto& row = party.rows[slot];
    const auto& character = party.characters[slot];
    const auto bound = [&](W8EquipSlot equip_slot) {
        const auto& item = character.EquippedItem[equip_slot];
        return item.iItemNo != -1 && data.items[item.iItemNo].binds_on_equip && item.bound;
    };
    if (!g_settings.autoswap_weapons || row.remembered.weapon_sets_locked ||
        (combat && combat->execution_active && combat->characters[slot].dead &&
         combat->characters[slot].phase != 0) ||
        bound(W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON) ||
        bound(W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON))
        return {};
    const auto equipment = PlanWeaponSetSwap(data, character);
    if (!equipment) return {};
    const auto& primary =
        character.EquippedItem[equipment->primary ? W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON
                                                  : W8_EQUIP_SLOT_PRIMARY_WEAPON];
    const auto& secondary =
        character.EquippedItem[equipment->secondary ? W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON
                                                    : W8_EQUIP_SLOT_SECONDARY_WEAPON];
    const auto hands = EvaluateWeaponHands(data, primary, secondary);
    W8RangeCategory range = W8_RANGE_NONE;
    for (unsigned int hand = 0; hand < 2; ++hand) {
        if (!hands[hand].in_play) continue;
        const auto& item = hand == 0 ? primary : secondary;
        const auto reach =
            hands[hand].uiHolds == 1 || hands[hand].uiHolds == 3
                ? static_cast<W8RangeCategory>(data.items[item.iItemNo].range_category)
                : W8_RANGE_TOUCH;
        range = std::max(range, reach);
    }
    auto target = BestMonsterTarget(*this, slot, action, range);
    if (!target) return {};
    if (action.target.iType == W8_TARGET_KIND_MONSTER && assessActionReach(slot, 2, action, range))
        target = action.target.iMonsterID;
    return TargetChoice{W8CombatSlot::monster(*target), equipment};
}

void Game::commitWeaponChoice(unsigned int slot, W8WeaponSwap equipment)
{
    std::vector<Event> outcomes;
    commitWeaponChoice(slot, equipment, outcomes);
    events.publish(std::move(outcomes));
}

void Game::commitWeaponChoice(unsigned int slot, W8WeaponSwap equipment,
                              std::vector<Event>& outcomes)
{
    party.rows[slot].weapon_swap_pending ^= 1;
    inventory.commitWeaponSwap(slot, equipment, outcomes);
    outcomes.emplace_back(CharacterNotice{slot, data.strings[0x26d]});
}

bool Game::repickActionTarget(unsigned int slot, W8TargetingContext stored, bool breathe)
{
    std::vector<Event> outcomes;
    const auto result = repickActionTarget(slot, stored, breathe, outcomes);
    events.publish(std::move(outcomes));
    return result;
}

bool Game::repickActionTarget(unsigned int slot, W8TargetingContext stored, bool breathe,
                              std::vector<Event>& outcomes)
{
    if (!party.participates(slot)) return false;
    const auto action = party.action(slot, stored);
    if (combat && stored == W8_TARGETING_CONTEXT_OUT_OF_COMBAT)
        ++combat->characters[slot].pending_action_repick_count;
    const auto choice = chooseTarget(slot, action);
    const auto target =
        choice ? choice->target : W8CombatSlot{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    aimStoredAction(slot, stored, target, outcomes);
    if (choice) {
        if (breathe)
            speech.Queue(&party.characters[slot], g_special_event2, 0, kCharacterEventFullVolume,
                         outcomes);
        if (stored == W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {
            if (g_settings.verbose_combat_messages)
                outcomes.emplace_back(CharacterNotice{slot, data.strings[0x26b]});
            if (!targetInPlay(slot, 2, party.action(slot, W8_TARGETING_CONTEXT_IN_COMBAT)))
                aimStoredAction(slot, W8_TARGETING_CONTEXT_IN_COMBAT, target, outcomes);
        }
    }
    if (combat && stored == W8_TARGETING_CONTEXT_IN_COMBAT && target.iType != action.target.iType)
        outcomes.emplace_back(PartySlotChanged{slot});
    if (choice && choice->equipment) commitWeaponChoice(slot, *choice->equipment, outcomes);
    return choice.has_value();
}

bool Game::canPerformAction(unsigned int party_slot, const W8Action& selected,
                            bool allow_missing_target) const
{
    if (!party.canAct(party_slot)) return false;
    const W8Character* character = &party.characters[party_slot];

    switch (selected.kind) {
    case W8_ACTION_NONE:
        return false;
    case W8_ACTION_ATTACK:
        if (!(GetCharAttackRange(data, &party.characters[party_slot], 2) != W8_RANGE_NONE)) {
            return false;
        }
        break;
    case W8_ACTION_BERSERK:
        if (!CanCharacterBerserk(data, &party.characters[party_slot])) {
            return false;
        }
        break;
    case W8_ACTION_PROTECT:
        if (!party.canAttack(party_slot)) {
            return false;
        }
        break;
    case W8_ACTION_CAST_SPELL:
        if (selected.detail == 0) {
            return false;
        }
        if (!CanCharacterCastSpell(data, character, selected.detail)) {
            return false;
        }
        break;
    case W8_ACTION_USE_ITEM:
        if (inventory.itemAt(selected.detail_block.item_use.item) == 0) {
            return false;
        }
        if (!CanCharacterActivateItem(data, character,
                                      inventory.itemAt(selected.detail_block.item_use.item),
                                      status.game_started)) {
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
        if (!targetInPlay(party_slot, 2, selected)) {
            return false;
        }
    }
    return true;
}

bool Game::canReactWith(unsigned int slot, W8ActionKind action) const
{
    if (!combat) return false;
    const auto& character = party.characters[slot];
    if (character.hp_current == 0 || character.highest_condition >= W8_CONDITION_ASLEEP)
        return false;
    if (combat->characters[slot].dead) return party.rows[slot].executing.kind == action;
    if (party.rows[slot].committed.kind == action) return true;
    return action == W8_ACTION_DEFEND &&
           !canPerformAction(slot, party.action(slot, W8_TARGETING_CONTEXT_IN_COMBAT), false);
}

bool Game::reactDefensively(unsigned int slot, W8ActionKind action)
{
    assert(action == W8_ACTION_DEFEND || action == W8_ACTION_PROTECT);
    if (!canReactWith(slot, action)) return false;
    if (combat->characters[slot].dead) return true;
    const bool fallback = action == W8_ACTION_DEFEND && party.rows[slot].committed.kind != action;
    party.beginDefensiveAction(slot, action);
    combat->beginReaction(slot, fallback);
    events.push(PartySlotChanged{slot});
    events.push(CombatProgressChanged{});
    return true;
}

void Game::pointCameraAtMonster(const W8MonsterInfo& info, bool force, bool animate)
{
    const bool track =
        force || (g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_SELECTED_CHARACTER &&
                  info.Target.iType == W8_TARGET_KIND_CHARACTER &&
                  info.Target.iChar == status.selected_character);
    if (!track && g_settings.camera_rotation_mode != W8_CAMERA_ROTATION_ALL_TARGETS) return;
    if (!info.entity || !info.entity->IsRenderable(true)) return;
    auto& monster = *info.entity;
    auto position = monster.nav.movement.position;
    position.y +=
        monster.nav.movement.height_offset - monster.nav.movement.secondary_height_offset <
                    1000.0f ||
                monster.GetDistanceToPlayer() > 2000.0f
            ? monster.nav.movement.secondary_height_offset
            : monster.nav.movement.height_offset;
    monster.owner_world.party.camera.pointAt(position, track, animate, activity.conversation);
}

void Game::pointCameraAtPosition(const glm::vec3& position, bool force, bool animate)
{
    if (level.primary)
        level.primary->party.camera.pointAt(position, force, animate, activity.conversation);
}

void Game::pointCameraAtCombatTarget(const W8TargetSource* source, const W8CombatSlot* target)
{
    if (g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_DISABLED || !target ||
        !targetPresent(*target))
        return;
    if (g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_SELECTED_CHARACTER &&
        (source->iType != W8_TARGET_SOURCE_CHARACTER ||
         source->iChar != status.selected_character) &&
        (target->iType != W8_TARGET_KIND_CHARACTER || target->iChar != status.selected_character))
        return;
    if (target->iType == W8_TARGET_KIND_PLACE) {
        pointCameraAtPosition(target->point, false, false);
        return;
    }
    const auto* population = level.primary ? level.primary->population.get() : nullptr;
    if (!population) return;
    int monster_id;
    if (target->iType == W8_TARGET_KIND_MONSTER)
        monster_id = target->iMonsterID;
    else if (target->iType == W8_TARGET_KIND_GROUP) {
        const auto* group = population->findGroup(target->iGroupID);
        if (!group) return;
        monster_id = group->leader_location_id;
    } else
        return;
    if (const auto* monster = population->findMonster(monster_id))
        pointCameraAtMonster(*monster, false, true);
}

void Game::setCombatAction(unsigned int slot, W8Action next, ActionOrigin origin, bool cue_ready)
{
    std::vector<Event> outcomes;
    setCombatAction(slot, next, origin, cue_ready, outcomes);
    events.publish(std::move(outcomes));
}

void Game::setCombatAction(unsigned int slot, W8Action next, ActionOrigin origin, bool cue_ready,
                           std::vector<Event>& outcomes)
{
    assert(combat);
    const auto previous = party.commitAction(slot, next);
    combat->characters[slot].action_changed = true;
    const auto& character = party.characters[slot];
    bool revalidate = false;
    if (character.hp_current != 0 && character.highest_condition < W8_CONDITION_TURNCOAT &&
        next.kind != W8_ACTION_NONE) {
        if (origin == ActionOrigin::Player &&
            ((next.kind == W8_ACTION_ATTACK &&
              GetCharAttackRange(data, &character, 2) == W8_RANGE_NONE) ||
             (next.kind == W8_ACTION_BERSERK && !CanCharacterBerserk(data, &character))))
            inventory.readyPrimaryWeapon(slot, outcomes);
        const auto action = party.action(slot, W8_TARGETING_CONTEXT_IN_COMBAT);
        if (!canPerformAction(slot, action, true)) {
            aimStoredAction(slot, W8_TARGETING_CONTEXT_IN_COMBAT,
                            {W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}}, outcomes);
        } else {
            if (!targetInPlay(slot, 2, action) &&
                repickActionTarget(slot, W8_TARGETING_CONTEXT_IN_COMBAT, cue_ready, outcomes)) {
                const W8TargetSource source{.iType = W8_TARGET_SOURCE_CHARACTER,
                                            .iChar = static_cast<int>(slot)};
                pointCameraAtCombatTarget(&source, &party.rows[slot].committed.target);
            }
            revalidate = origin == ActionOrigin::Player;
        }
    }
    if (!combat) return;
    if (next.kind != previous && combat->execution_active && !combat->characters[slot].dead)
        combat->catchUpCharacter(slot);
    outcomes.emplace_back(PartySlotChanged{slot});
    combat->target_picked = false;
    if (next.kind == W8_ACTION_EQUIP)
        outcomes.emplace_back(CharacterNotice{slot, data.strings[0x225]});
    else if (previous == W8_ACTION_EQUIP && !(combat->actingSlot() == static_cast<int>(slot) &&
                                              party.rows[slot].executing.kind == W8_ACTION_EQUIP))
        outcomes.emplace_back(CharacterNotice{slot, data.strings[0x226]});
    SettleActiveParty(*this, {}, ItemReactions::Normal, outcomes);
    if (revalidate) outcomes.emplace_back(ActionChoiceChanged{slot});
}

void Game::resetCombatChoice(unsigned int slot)
{
    std::vector<Event> outcomes;
    resetCombatChoice(slot, outcomes);
    events.publish(std::move(outcomes));
}

void Game::resetCombatChoice(unsigned int slot, std::vector<Event>& outcomes)
{
    party.rows[slot].committed.target = {W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    outcomes.emplace_back(PartySlotChanged{slot});
    if (slot == static_cast<unsigned int>(status.selected_character))
        outcomes.emplace_back(MonstersChanged{});
    setCombatAction(slot, party.rows[slot].remembered.basic(), ActionOrigin::Automatic, false,
                    outcomes);
}

void Game::reconcileCombatHands(unsigned int party_slot, std::vector<Event>& outcomes)
{
    if (!combat.has_value()) return;
    const W8ActionKind kind = party.rows[party_slot].committed.kind;
    if (kind == W8_ACTION_ATTACK || kind == W8_ACTION_BERSERK) {
        // The new hands keep or re-aim the attack; picking another weapon set is
        // left to the commands that prepare an action, so settling never swaps.
        const auto action = party.action(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT);
        const W8CombatSlot none{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
        if (!canPerformAction(party_slot, action, true)) {
            aimStoredAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT, none, outcomes);
        } else if (!targetInPlay(party_slot, 2, action)) {
            const auto choice = chooseTarget(party_slot, action, false);
            aimStoredAction(party_slot, W8_TARGETING_CONTEXT_IN_COMBAT,
                            choice ? choice->target : none, outcomes);
        }
    }
    combat->characters[party_slot].alternate_hand ^= 1;
    if (party_slot == static_cast<unsigned int>(status.selected_character))
        outcomes.emplace_back(CombatPanelChanged{});
}

void Game::restoreCombatEquipment(bool automatic)
{
    for (int party_slot = 0; party_slot < 8; ++party_slot) {
        W8PartySlotRow* row = &party.rows[party_slot];
        W8Character* character = &party.characters[party_slot];
        if (party.members[party_slot].has_value() &&
            (character->hp_current != 0 || character->highest_condition < W8_CONDITION_TURNCOAT) &&
            row->weapon_swap_pending && automatic &&
            !party.rows[party_slot].remembered.weapon_sets_locked) {
            inventory.swapWeaponSets(party_slot, false);
            row->weapon_swap_pending = false;
        }
    }
}
