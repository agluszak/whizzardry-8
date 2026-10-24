#include <cassert>
#include <cmath>

#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_roster.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
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
    Events::Operation outcomes{events};
    const bool committed = stored == W8_TARGETING_CONTEXT_IN_COMBAT;
    const bool changed = party.aimAction(slot, stored, target, committed && combat.has_value());
    if (!committed) return;
    if (changed && combat) combat->target_picked = false;
    if (changed || target.iType != W8_TARGET_KIND_NONE) events.push(ActionAimChanged{slot});
}

void Game::invalidateAimsAt(const W8CombatSlot& target, TargetChange change)
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
                DropCharacterFromRound(slot);
            else
                RepickActionTarget(slot, W8_TARGETING_CONTEXT_IN_COMBAT, 0);
        }
        if (row.executing.target == target &&
            invalid(party.characters[slot].uiCondition[W8_CONDITION_TURNCOAT] != 0,
                    actionTargetsEnemies(slot, row.executing.action())))
            RepickActionTarget(slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT, 0);
    }
    if (!level.primary || !level.primary->population) return;
    for (const auto& monster : level.primary->population->monster_roster.live()) {
        if (monster->Target == target &&
            invalid(
                monster->ubDisposition == W8_DISPOSITION_HOSTILE,
                MonsterActionTargetsEnemies(data, monster->action_kind, monster->action_detail)))
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

int MonsterQuadrant(const Game& game, const W8MonsterInfo& monster)
{
    const auto eye = game.level.loaded().party.eye();
    const auto position = monster.entity->nav.GetPosition();
    int bearing = static_cast<int>(NormalizeAngle(GetHeadingAngle(&eye, &position)) * 57.295776f);
    bearing -= game.status.party_facing;
    if (bearing < 0) bearing += 360;
    return ((bearing + 45) % 360) / 90;
}
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
                    range, BlockingFormationRows(party, slot, MonsterQuadrant(*this, *monster)));
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
