#include <array>
#include <cassert>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/world.h"

namespace {
// Silence permits alchemy only for the three monster kinds that retain it.
bool MonsterSpellBlocked(const GameData& data, const W8MonsterInfo& monster, int spell_id)
{
    if (monster.uiCondition[W8_CONDITION_SILENCED] == 0) return false;
    if (!data.spells[spell_id].alchemy_spell) return true;
    const auto kind = data.monsters[monster.monster_species].kind;
    return kind != 4 && kind != 5 && kind != 13;
}
} // namespace

/* Everything that has to hold before a monster may start casting: the spell
   has to be one monsters can cast at all, the monster has to be in a state to
   cast it, it has to have somewhere to aim, it has to be able to act, and the
   two combat gates have to agree. */
bool Game::prepareMonsterSpell(W8MonsterInfo* monster_info, int spell_id)
{
    W8CombatSlot* combat_slot;

    if (!resolveMonsterSpellAim(monster_info, spell_id)) return false;
    assert(data.spells[spell_id].monster_castable != 0);

    if (MonsterSpellBlocked(data, *monster_info, spell_id)) return false;

    const auto target_type = data.spells[spell_id].target_type;
    if (target_type == W8_TARGET_TYPE_CASTER) {
        monster_info->Target = W8CombatSlot::monster(monster_info->location_id);
    } else {
        W8TargetNeed need = W8_TARGET_NEED_NONE;
        switch (target_type) {
        case W8_TARGET_TYPE_ALLY:
            need = spell_id == W8_SPELL_RESURRECTION ? W8_TARGET_NEED_CHARACTER_INDIRECT
                                                     : W8_TARGET_NEED_ALLY;
            break;
        case W8_TARGET_TYPE_ENEMY:
            need = W8_TARGET_NEED_ENEMY;
            break;
        case W8_TARGET_TYPE_ENEMY_GROUP:
            need = W8_TARGET_NEED_GROUP;
            break;
        default:
            break;
        }
        if (!targetMatches(monster_info->Target, need)) return false;
    }

    combat_slot = &monster_info->Target;
    if (!monsterActionReachesTarget(monster_info, 0, combat_slot, monster_info->action)) {
        ResetCombatSlot(&monster_info->Target);
        return false;
    }
    if (!monster_info->entity->owner_world
             .castingBlockMessagesAt(monster_info->entity->nav.GetPosition())
             .empty()) {
        return false;
    }
    if (!monsterAcceptsSpell(monster_info, spell_id, combat_slot, monster_info->action)) {
        return false;
    }
    return !monsterSpellHitsNeutral(monster_info, spell_id, combat_slot, monster_info->action);
}

/* Whether the monster may cast the spell now: the record allows monsters to
   cast it, nothing is blocking the cast, the special cases for spell 0x3c
   and fire spells under camera sway pass, and `needs_target` also demands a
   target to aim at. */
bool Game::canMonsterCastSpell(W8MonsterInfo* monster_info, int spell_id, bool needs_target)
{
    if (spell_id == W8_SPELL_NONE) {
        return false;
    }
    if (data.spells[spell_id].monster_castable == 0) {
        return false;
    }
    if (MonsterSpellBlocked(data, *monster_info, spell_id)) {
        return false;
    }
    if (!monster_info->entity->owner_world
             .castingBlockMessagesAt(monster_info->entity->nav.GetPosition())
             .empty()) {
        return false;
    }
    if (spell_id == W8_SPELL_SUMMON_ELEMENTAL && monster_info->elemental_summon != -1) {
        return false;
    }
    if (data.spells[spell_id].realm == W8_SPELL_REALM_FIRE && cameraSwayActive()) {
        return false;
    }
    if (needs_target) {
        const auto targets = monsterSpellTargets(monster_info, spell_id);
        if (targets.empty()) {
            return false;
        }
    }
    return true;
}

/* Pick where the spell lands: collect every slot the spell may be cast at
   and take one at random into the monster's stored target. */
bool Game::aimMonsterSpell(W8MonsterInfo* monster_info, int spell_id)
{
    const auto targets = monsterSpellTargets(monster_info, spell_id);
    if (targets.empty()) {
        return false;
    }
    monster_info->Target = targets[rng.below(static_cast<int>(targets.size()))];
    return true;
}

/* Whether `combat_slot` accepts `spell_id` from this caster. Target type
   eight needs no checking at all; a character or monster slot is checked
   against its own record, and anything else is resolved into its markers
   and accepted when at least half of them take the spell. The per-spell
   switch then vetoes targets the spell would not help or cannot affect. */
bool Game::monsterAcceptsSpell(W8MonsterInfo* monster_info, int spell_id, W8CombatSlot* combat_slot,
                               MonsterAction action)
{
    W8MonsterInfo* target = 0;
    W8Character* character = 0;
    const W8MonsterRecord* record = 0;
    int hp = 0;
    int hp_max = 0;
    int stat = 0;
    int stat_max = 0;
    unsigned int* condition_turns = 0;
    W8Enchantment* enchantments = 0;
    /* 0x00532EE3 and 0x00532F38 test the marker count signed and 0x00532F89
       compares the index signed. */
    int index;
    unsigned int duration;

    if (data.spells[spell_id].target_type == W8_TARGET_TYPE_POINT) {
        return true;
    }
    if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
        character = &party.characters[combat_slot->iChar];
        hp_max = character->uiHPMax;
        hp = character->hp_current;
        stat = character->stamina;
        stat_max = character->uiStaminaMax;
        condition_turns = character->uiCondition;
        enchantments = character->enchantments;
    } else {
        if (combat_slot->iType != W8_TARGET_KIND_MONSTER) {
            std::vector<int> monster_markers;
            std::vector<int> party_markers;
            W8TargetSource source;
            W8CombatSlot probe;
            unsigned int valid = 0;
            int total = 0;
            unsigned int power_level;

            SetTargetSourceToMonster(monster_info, &source);
            power_level = ChooseMonsterSpellPowerLevel(data, rng, *monster_info, spell_id);
            appendSpellTargets(spell_id, power_level, &source, combat_slot, monster_markers,
                               party_markers, W8AreaSightRule::RequireLineOfSight, action);
            for (index = 0; index < static_cast<int>(party_markers.size()); ++index) {
                ++total;
                ResetCombatSlot(&probe);
                probe.iType = W8_TARGET_KIND_CHARACTER;
                probe.iChar = party_markers[index];
                if (monsterAcceptsSpell(monster_info, spell_id, &probe, action)) {
                    ++valid;
                }
            }
            for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
                ++total;
                ResetCombatSlot(&probe);
                probe.iType = W8_TARGET_KIND_MONSTER;
                probe.iMonsterID = monster_markers[index];
                if (monsterAcceptsSpell(monster_info, spell_id, &probe, action)) {
                    ++valid;
                }
            }
            return valid != 0 && ((total + 1U) >> 1) <= valid;
        }
        target = level.loaded().population->findMonster(combat_slot->iMonsterID);
        record = &data.monsters[target->monster_species];
        stat_max = target->stamina_max;
        stat = target->stamina;
        hp = target->hp_current;
        hp_max = target->uiHPMax;
        condition_turns = target->uiCondition;
        enchantments = target->enchantments;
        if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
            target->ubDisposition == W8_DISPOSITION_FRIENDLY && monster_info->pCombat &&
            monster_info->pCombat->reconsider_action) {
            return false;
        }
    }
    switch (spell_id) {
    case W8_SPELL_BLESS:
    case W8_SPELL_ELEMENT_SHIELD:
    case W8_SPELL_SOUL_SHIELD: {
        if (!combat) return true;
        const auto slot = EffectSlotForSpell(g_defense_effect_slot_spells, spell_id);
        if (combat_slot->iType == W8_TARGET_KIND_CHARACTER)
            return combat->effect_slots0[slot].duration == 0;
        return !target->pCombat || target->pCombat->combat_effects_2[slot].duration == 0;
    }
    case W8_SPELL_HEAL_WOUNDS:
    case W8_SPELL_HEAL_ALL:
        if (hp == hp_max) {
            return false;
        }
        break;
    case W8_SPELL_ITCHING_SKIN:
        if (condition_turns[3] != 0) {
            return false;
        }
        break;
    case W8_SPELL_PARALYZE:
    case W8_SPELL_FREEZE_FLESH:
    case W8_SPELL_FREEZE_ALL:
        if (condition_turns[0x10] != 0) {
            return false;
        }
        break;
    case W8_SPELL_SLEEP:
        if (condition_turns[W8_CONDITION_ASLEEP] != 0) {
            return false;
        }
        break;
    case W8_SPELL_STAMINA:
    case W8_SPELL_REST_ALL:
        if (stat == stat_max) {
            return false;
        }
        break;
    case W8_SPELL_TERROR:
        if (condition_turns[6] != 0) {
            return false;
        }
        break;
    case W8_SPELL_BLINDING_FLASH:
        if (condition_turns[0xc] != 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_LESSER_COND:
        if (condition_turns[3] == 0 && condition_turns[4] == 0 && condition_turns[6] == 0 &&
            condition_turns[W8_CONDITION_ASLEEP] == 0 && condition_turns[0xc] == 0) {
            return false;
        }
        break;
    case W8_SPELL_ENCHANTED_BLADE:
    case W8_SPELL_MISSILE_SHIELD:
    case W8_SPELL_ARMORPLATE:
    case W8_SPELL_MAGIC_SCREEN: {
        const auto slot = EffectSlotForSpell(g_being_effect_slot_spells, spell_id);
        duration = combat_slot->iType == W8_TARGET_KIND_CHARACTER
                       ? status.effect_slots[slot].duration
                       : target->effect_slots[slot].duration;
        if (duration != 0) return false;
        if (spell_id == W8_SPELL_ENCHANTED_BLADE && combat_slot->iType == W8_TARGET_KIND_MONSTER &&
            0x3b < record->spell_chance) {
            return false;
        }
        break;
    }
    case W8_SPELL_GUARDIAN_ANGEL:
        if (enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_RAZOR_CLOAK:
        if (enchantments[W8_ENCHANTMENT_RAZOR_CLOAK].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_SLOW:
        if (condition_turns[W8_CONDITION_SLOWED] != 0) {
            return false;
        }
        break;
    case W8_SPELL_WEB:
        if (condition_turns[0xe] != 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_PARALYSIS:
        if (condition_turns[0x10] == 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_POISON:
        if (condition_turns[W8_CONDITION_POISONED] == 0) {
            return false;
        }
        break;
    case W8_SPELL_SILENCE:
        if (condition_turns[W8_CONDITION_SILENCED] != 0) {
            return false;
        }
        if (combat_slot->iType != W8_TARGET_KIND_CHARACTER) {
            if (combat_slot->iType == W8_TARGET_KIND_MONSTER) {
                if (3 < record->kind && (record->kind < 6 || record->kind == 0xd)) {
                    return false;
                }
                if (record->spell_chance != 0) {
                    if (target->entity->IsCycleSupported(W8_MONSTER_CYCLE_SPELL)) {
                        for (index = 0; index < 10; ++index) {
                            if (canMonsterCastSpell(target, record->spells[index], false)) {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }
        if (character->skills[W8_SKILL_MUSIC].level != 0) {
            return true;
        }
        for (index = 0; index < 0x72; ++index) {
            if (character->spell_learned[index] == 1) {
                break;
            }
        }
        if (index > 0x71) {
            return false;
        }
        if (data.spells[spell_id].alchemy_spell != 0 &&
            character->skills[W8_SKILL_SPELLBOOK_ALCHEMY].level != 0) {
            return false;
        }
        break;
    case W8_SPELL_ACID_BOMB:
    case W8_SPELL_ARMORMELT:
    case W8_SPELL_TOXIC_CLOUD:
    case W8_SPELL_DRAINING_CLOUD:
    case W8_SPELL_FIRESTORM:
    case W8_SPELL_DEATH_CLOUD: {
        if (!combat) return true;
        const auto slot = EffectSlotForSpell(g_combat_effect_slot_spells, spell_id);
        if (combat_slot->iType == W8_TARGET_KIND_CHARACTER)
            return combat->effect_slots[slot].duration == 0;
        return !target->pCombat || target->pCombat->combat_effects[slot].duration == 0;
    }
    case W8_SPELL_EYE_FOR_AN_EYE:
        if (enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_HASTE:
        if (enchantments[W8_ENCHANTMENT_HASTE].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_SUPERMAN:
        if (enchantments[W8_ENCHANTMENT_SUPERMAN].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_BODY_OF_STONE:
        if (enchantments[W8_ENCHANTMENT_BODY_OF_STONE].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_HEX:
        if (condition_turns[9] != 0) {
            return false;
        }
        break;
    case W8_SPELL_PURIFY_AIR:
        if (!combat.has_value()) {
            return false;
        }
        for (index = 0; index < 9; ++index) {
            if (g_combat_effect_slot_spells[index] != 0x31) {
                if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
                    duration = combat->effect_slots[index].duration;
                } else {
                    if (!target->pCombat) {
                        continue;
                    }
                    duration = target->pCombat->combat_effects[index].duration;
                }
                if (duration != 0) {
                    return true;
                }
            }
        }
        return false;
    case W8_SPELL_SANE_MIND:
        if (condition_turns[0xb] == 0 && condition_turns[W8_CONDITION_TURNCOAT] == 0) {
            return false;
        }
        break;
    case W8_SPELL_BANISH:
        if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
            return false;
        }
        if (record->kind != 0x14 && record->kind != 0x15 && record->kind != 0x1c &&
            target->summoned == W8_MONSTER_SUMMON_NONE) {
            return false;
        }
        break;
    case W8_SPELL_PANDEMONIUM:
        if (condition_turns[6] == 0) {
            return true;
        }
        [[fallthrough]];
    case W8_SPELL_INSANITY:
        if (condition_turns[0xb] != 0) {
            return false;
        }
        break;
    case W8_SPELL_TURNCOAT:
        if (condition_turns[W8_CONDITION_TURNCOAT] != 0) {
            return false;
        }
        break;
    default:
        break;
    }
    return true;
}

/* Whether the spell's area effect would catch a disposition-neutral monster;
   a true return vetoes the cast, since the AI does not turn neutrals hostile
   by accident. */
bool Game::monsterSpellHitsNeutral(W8MonsterInfo* monster_info, int spell_id,
                                   W8CombatSlot* combat_slot, MonsterAction action)
{
    std::vector<int> monster_markers;
    std::vector<int> party_markers;
    W8TargetSource source;
    /* 0x005331B5 tests the marker count signed. */
    int index;
    W8MonsterInfo* target;
    unsigned int power_level;

    if (!SpellTargetsEnemies(data, spell_id)) {
        return false;
    }
    SetTargetSourceToMonster(monster_info, &source);
    power_level = ChooseMonsterSpellPowerLevel(data, rng, *monster_info, spell_id);
    appendSpellTargets(spell_id, power_level, &source, combat_slot, monster_markers, party_markers,
                       W8AreaSightRule::RequireLineOfSight, action);
    for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
        target = level.loaded().population->findMonster(monster_markers[index]);
        if (target->ubDisposition == W8_DISPOSITION_NEUTRAL) {
            return true;
        }
    }
    return false;
}

/* Which of the record's ten spells the monster casts: each usable slot adds
   its weight from the fixed table, then a roll under the total walks them
   down. The assertion names the original local, uiCastableSpellCnt. */
int Game::chooseMonsterSpell(W8MonsterInfo* monster_info)
{
    constexpr std::array slot_weights{5, 5, 5, 10, 10, 10, 10, 15, 15, 15};
    std::vector<std::pair<int, int>> choices;
    int total = 0;
    const auto& record = data.monsters[monster_info->monster_species];
    for (unsigned int slot = 0; slot < slot_weights.size(); ++slot) {
        const int spell = record.spells[slot];
        if (canMonsterCastSpell(monster_info, spell, true)) {
            choices.emplace_back(spell, slot_weights[slot]);
            total += slot_weights[slot];
        }
    }
    assert(!choices.empty());
    int roll = rng.below(total);
    for (const auto [spell, weight] : choices) {
        if (roll < weight) return spell;
        roll -= weight;
    }
    std::unreachable();
}

/* Return the combat slots `spell_id` may be cast at by this
   monster. The proposed action travels through reach and area evaluation as
   a value; it never overwrites the committed action, even if evaluation throws.
   Compatibility power/target draws still occur at every original probe. */
std::vector<W8CombatSlot> Game::monsterSpellTargets(W8MonsterInfo* monster_info, int spell_id)
{
    std::vector<W8CombatSlot> targets;
    W8SpellTargetType target_type;
    W8CombatSlot slot;
    W8MonsterGroup* monster_group;
    W8MonsterInfo* member;
    unsigned int index;

    const bool special = spell_id == W8_SPELL_SPECIAL_ATTACK_CONE;
    const auto sight_kind = special ? W8VisibilityCondition::ThirdLineOfSightFlag
                                    : W8VisibilityCondition::SpellOriginLineOfSight;
    const MonsterAction action{special ? W8_MONSTER_ACTION_SPECIAL_ATTACK : W8_MONSTER_ACTION_SPELL,
                               special ? monster_info->action.detail : spell_id};
    target_type = data.spells[spell_id].target_type;
    switch (target_type) {
    case W8_TARGET_TYPE_CASTER:
        ResetCombatSlot(&slot);
        slot.iMonsterID = monster_info->location_id;
        slot.iType = W8_TARGET_KIND_MONSTER;
        if (!monsterAcceptsSpell(monster_info, spell_id, &slot, action)) {
            break;
        }
        targets.push_back(slot);
        break;
    case W8_TARGET_TYPE_ALLY:
    case W8_TARGET_TYPE_ENEMY: {
        const bool hostile = target_type == W8_TARGET_TYPE_ENEMY;
        const auto disposition = hostile ? W8_DISPOSITION_HOSTILE : W8_DISPOSITION_FRIENDLY;
        for (const auto& owner : level.loaded().population->monster_roster.live()) {
            auto* member = owner.get();
            if ((!hostile || member != monster_info) && member->fActive &&
                member->hp_current != 0 && member->highest_condition < W8_CONDITION_DEAD &&
                member->pCombat && MonsterHostility(data, *monster_info, *member) == disposition &&
                monster_info->reaches(*member, data, 0, action)) {
                auto aimed = W8CombatSlot::monster(member->location_id);
                if (monsterAcceptsSpell(monster_info, spell_id, &aimed, action))
                    targets.push_back(aimed);
            }
        }
        if (IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, sight_kind)) {
            for (int index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
                if (party.participates(index) &&
                    party.dispositionToward(index, monster_info->ubDisposition) == disposition &&
                    monsterReachesPartyMember(*monster_info, 0, index, action)) {
                    auto aimed = W8CombatSlot::character(index);
                    if (monsterAcceptsSpell(monster_info, spell_id, &aimed, action))
                        targets.push_back(aimed);
                }
            }
        }
        break;
    }
    case W8_TARGET_TYPE_ENEMY_GROUP:
        for (index = 0; index < (*level.loaded().population).monster_groups.size(); ++index) {
            monster_group = (*level.loaded().population).monster_groups[index].get();
            if (monster_group->group_id != monster_info->monster_group_id &&
                monster_group->members_active && monster_group->fInCombat &&
                !level.loaded().population->allMembersDying(*monster_group) &&
                groupSpellTargetsAccept(monster_info, spell_id, monster_group, action)) {
                ResetCombatSlot(&slot);
                slot.iType = W8_TARGET_KIND_GROUP;
                slot.iGroupID = monster_group->group_id;
                if (monsterActionReachesTarget(monster_info, 0, &slot, action)) {
                    targets.push_back(slot);
                }
            }
        }
        if (!partySpellTargetsAccept(monster_info, spell_id, action)) {
            break;
        }
        ResetCombatSlot(&slot);
        slot.iType = W8_TARGET_KIND_PARTY;
        if (!monsterActionReachesTarget(monster_info, 0, &slot, action)) {
            break;
        }
        targets.push_back(slot);
        break;
    case W8_TARGET_TYPE_CONE:
    case W8_TARGET_TYPE_RADIUS:
        for (index = 0; index < (*level.loaded().population).monster_roster.live().size();
             ++index) {
            member = level.loaded().population->monster_roster.live()[index].get();
            if (member != monster_info && member->fActive && member->hp_current != 0 &&
                member->highest_condition < W8_CONDITION_DEAD && member->pCombat &&
                MonsterHostility(data, *monster_info, *member) == W8_DISPOSITION_HOSTILE &&
                monster_info->reaches(*member, data, 0, action)) {
                ResetCombatSlot(&slot);
                slot.iType = W8_TARGET_KIND_MONSTER;
                slot.iMonsterID = member->location_id;
                if (const auto point = monster_info->entity->owner_world.targetPoint(
                        slot, target_type == W8_TARGET_TYPE_RADIUS))
                    slot.point = *point;
                slot.iType = W8_TARGET_KIND_PLACE;
                if (monsterAcceptsSpell(monster_info, spell_id, &slot, action) &&
                    !monsterSpellHitsNeutral(monster_info, spell_id, &slot, action)) {
                    targets.push_back(slot);
                }
            }
        }
        if (IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, sight_kind)) {
            ResetCombatSlot(&slot);
            const auto& world_party = monster_info->entity->owner_world.party;
            slot.point = target_type == W8_TARGET_TYPE_RADIUS
                             ? world_party.position() + glm::vec3{0, 1000, 0}
                             : world_party.eye();
            slot.iType = W8_TARGET_KIND_PLACE;
            if (monsterAcceptsSpell(monster_info, spell_id, &slot, action) &&
                !monsterSpellHitsNeutral(monster_info, spell_id, &slot, action)) {
                for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
                    if (party.participates(index) &&
                        party.dispositionToward(index, (monster_info)->ubDisposition) ==
                            W8_DISPOSITION_HOSTILE &&
                        monsterReachesPartyMember(*monster_info, 0, index, action)) {
                        targets.push_back(slot);
                    }
                }
            }
        }
        break;
    case W8_TARGET_TYPE_ALL_ENEMIES:
        ResetCombatSlot(&slot);
        slot.iType = W8_TARGET_KIND_ALL_ENEMIES;
        if (!monsterAcceptsSpell(monster_info, spell_id, &slot, action) ||
            monsterSpellHitsNeutral(monster_info, spell_id, &slot, action)) {
            break;
        }
        targets.push_back(slot);
        break;
    case W8_TARGET_TYPE_POINT:
        ResetCombatSlot(&slot);
        slot.point = monster_info->entity->nav.GetPosition();
        targets.push_back(slot);
        break;
    default:
        ResetCombatSlot(&slot);
        if (!monsterAcceptsSpell(monster_info, spell_id, &slot, action)) {
            break;
        }
        targets.push_back(slot);
        break;
    }
    return targets;
}

/* Whether a monster can aim the spell it wants to cast. The two area target
   types aim at the world; anything else either needs no aim at all or has to
   pass the slot check. */
bool Game::resolveMonsterSpellAim(W8MonsterInfo* monster_info, int spell_id)
{
    W8SpellTargetType target_type = data.spells[spell_id].target_type;

    if (target_type > W8_TARGET_TYPE_ENEMY_GROUP && target_type < W8_TARGET_TYPE_ALL_ENEMIES) {
        return aimMonsterSpell(monster_info, spell_id);
    }
    if (data.spells[spell_id].needs_aim != 0) {
        const auto point =
            monster_info->entity->owner_world.targetPoint(monster_info->Target, false);
        if (!point) return false;
        monster_info->Target.point = *point;
        return true;
    }
    return true;
}

/* Whether at least half of the group's live in-combat members the caster is
   hostile to accept the spell; the probe is the same target acceptance check
   the cast itself would face. */
bool Game::groupSpellTargetsAccept(W8MonsterInfo* monster_info, int spell_id,
                                   W8MonsterGroup* target_group, MonsterAction action)
{
    unsigned int valid = 0;
    int eligible = 0;
    unsigned int index;
    W8MonsterInfo* member;
    W8CombatSlot slot;

    assert(target_group != 0);
    for (index = 0; index < target_group->MemberIds().size(); ++index) {
        int location_id = target_group->MemberLocationId(index);
        member = level.loaded().population->findMonster(location_id);
        if (member->fActive && member->hp_current != 0 &&
            member->highest_condition < W8_CONDITION_DEAD && member->pCombat &&
            MonsterHostility(data, *monster_info, *member) == W8_DISPOSITION_HOSTILE) {
            ++eligible;
            ResetCombatSlot(&slot);
            slot.iType = W8_TARGET_KIND_MONSTER;
            slot.iMonsterID = location_id;
            if (monsterAcceptsSpell(monster_info, spell_id, &slot, action)) {
                ++valid;
            }
        }
    }
    return valid != 0 && ((eligible + 1U) >> 1) <= valid;
}

/* Whether at least half of the party members the caster is hostile to accept
   the spell. */
bool Game::partySpellTargetsAccept(W8MonsterInfo* monster_info, int spell_id, MonsterAction action)
{
    unsigned int valid = 0;
    int eligible = 0;
    unsigned int index;
    W8CombatSlot slot;

    for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
        if (party.participates(index) &&
            party.dispositionToward(index, (monster_info)->ubDisposition) ==
                W8_DISPOSITION_HOSTILE) {
            ++eligible;
            ResetCombatSlot(&slot);
            slot.iType = W8_TARGET_KIND_CHARACTER;
            slot.iChar = index;
            if (monsterAcceptsSpell(monster_info, spell_id, &slot, action)) {
                ++valid;
            }
        }
    }
    return valid != 0 && ((eligible + 1U) >> 1) <= valid;
}
