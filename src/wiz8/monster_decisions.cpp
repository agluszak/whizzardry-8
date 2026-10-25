#include <array>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/character_skills.h"
#include "wiz8/checked_integer.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/group_attacks.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterAI.h"
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
constexpr int kSummonEffect = 6;

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
        if (!targetMatches(&monster_info->owner_world, monster_info->Target, need)) return false;
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
    if (data.spells[spell_id].realm == W8_SPELL_REALM_FIRE &&
        monster_info->owner_world.party.camera.swayActive()) {
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
            appendSpellTargets(monster_info->owner_world, spell_id, power_level, &source,
                               combat_slot, monster_markers, party_markers,
                               W8AreaSightRule::RequireLineOfSight, action);
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
        target = monster_info->owner_world.population->findMonster(combat_slot->iMonsterID);
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
        if (!partyCombatActive(monster_info->owner_world)) return true;
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
        if (!partyCombatActive(monster_info->owner_world)) return true;
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
        if (!partyCombatActive(monster_info->owner_world)) {
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
    appendSpellTargets(monster_info->owner_world, spell_id, power_level, &source, combat_slot,
                       monster_markers, party_markers, W8AreaSightRule::RequireLineOfSight, action);
    for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
        target = monster_info->owner_world.population->findMonster(monster_markers[index]);
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
        for (const auto& owner : monster_info->owner_world.population->monster_roster.live()) {
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
        for (index = 0; index < (*monster_info->owner_world.population).groups().size(); ++index) {
            monster_group = (*monster_info->owner_world.population).groups()[index].get();
            if (monster_group->group_id != monster_info->monster_group_id &&
                monster_group->members_active && monster_group->fInCombat &&
                !monster_info->owner_world.population->allMembersDying(*monster_group) &&
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
        for (index = 0;
             index < (*monster_info->owner_world.population).monster_roster.live().size();
             ++index) {
            member = monster_info->owner_world.population->monster_roster.live()[index].get();
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
        member = monster_info->owner_world.population->findMonster(location_id);
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

/* Decide what one monster does this round. A monster taken out of the fight
   by its worst condition, or told to give up, stands down and ends its turn.
   Otherwise it rolls to hold back, and if not, a monster that is not yet
   engaged either holds or gives up by its record. An engaged monster with a
   usable ranged attack and the party out of reach closes in (or backs off
   when hurt); otherwise it rolls to flee and to cast, weighs its attacks, and
   only then settles for closing in, backing off or holding. Whatever it
   settles on is checked, and a group that has given up holds instead. */
void Game::updateMonsterDecision(W8MonsterInfo* monster_info)
{
    const W8MonsterRecord* record;
    W8RangeCategory range_category;
    unsigned int chance;
    unsigned char rating;
    unsigned int spell;

    if (monster_info->highest_condition >= W8_CONDITION_ASLEEP) {
        monster_info->action.kind = W8_MONSTER_ACTION_NONE;
        monster_info->pCombat->phase = 0;
        monster_info->pCombat->active = true;
        return;
    }
    record = &data.monsters[monster_info->monster_species];
    if (monster_info->monster_species == 0x224) {
        monster_info->action.kind = W8_MONSTER_ACTION_WAIT;
        return;
    }
    const auto approach = [&] {
        const bool retreat =
            monster_info->hp_current / static_cast<float>(monster_info->uiHPMax) <= 0.95f &&
            record->prefer_ranged_actions == 0;
        return retreat ? W8_MONSTER_ACTION_BACK_OFF : W8_MONSTER_ACTION_APPROACH;
    };
    const auto nearest_distance = [&] {
        const auto threat = monster_info->entity->owner_world.nearestThreat(
            data, *monster_info, W8VisibilityCondition::FirstLineOfSightFlag);
        return threat ? threat->distance : 999999.0f;
    };
    const auto hp_percent = monster_info->hp_current * 100 / monster_info->uiHPMax;
    assert(monster_info->pCombat);
    chance = 0;
    if ((record->flags & W8_MONSTER_FLAG_NPC) != 0 &&
        monster_info->ubDisposition != W8_DISPOSITION_HOSTILE && hp_percent <= 33) {
        chance = 100;
    } else if (monster_info->pCombat->advancing) {
        if (CalcRangeDistance(W8_RANGE_SHORT) < nearest_distance())
            chance = 100;
        else
            monster_info->pCombat->advancing = false;
    } else {
        chance = record->advance_chance == 0 && record->combat_morale == 0 ? 100
                                                                           : record->advance_chance;
    }
    const auto decide = [&] {
        if (rng.below(100) < chance) {
            monster_info->action.kind = W8_MONSTER_ACTION_ADVANCE;
            return;
        }
        if (monster_info->ubDisposition == W8_DISPOSITION_NEUTRAL) {
            if (record->camouflage1 != 0) {
                monster_info->action.kind = W8_MONSTER_ACTION_NONE;
                monster_info->pCombat->phase = 0;
                monster_info->pCombat->active = true;
            } else {
                monster_info->action.kind = W8_MONSTER_ACTION_RETURN_TO_START;
            }
            return;
        }
        if (record->prefer_ranged_actions == 0 &&
            (range_category = GetBestMonsterAttackRange(record, true)) != W8_RANGE_NONE &&
            (monster_info->uiCondition[W8_CONDITION_BLIND] == 0 || record->kind == 0xc) &&
            nearest_distance() > CalcRangeDistance(range_category)) {
            monster_info->action.kind = approach();
        } else {
            rating = rateMonsterBestAttack(*monster_info, false);
            chance = record->flee_chance;
            if (chance != 0) {
                if (rating != 0) {
                    chance = 100;
                }
                if (canMonsterUseSpecial(monster_info, false) && rng.below(100) < chance) {
                    monster_info->action.kind = W8_MONSTER_ACTION_SPECIAL_ATTACK;
                    [[maybe_unused]] const bool aimed = aimMonsterSpecial(monster_info);
                    assert(aimed);
                    return;
                }
            }
            chance = record->spell_chance;
            if (chance != 0) {
                if (rating != 0) {
                    chance = 100;
                }
                if (record->spell_chance != 0) {
                    if (monster_info->entity->IsCycleSupported(W8_MONSTER_CYCLE_SPELL)) {
                        for (spell = 0; spell < 10; ++spell) {
                            if (canMonsterCastSpell(monster_info, record->spells[spell], true)) {
                                if (rng.below(100) < chance) {
                                    monster_info->action.kind = W8_MONSTER_ACTION_SPELL;
                                    monster_info->action.detail = chooseMonsterSpell(monster_info);
                                    [[maybe_unused]] const bool aimed =
                                        aimMonsterSpell(monster_info, monster_info->action.detail);
                                    assert(aimed);
                                    return;
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (rating != 0) {
                if (rating == W8_MONSTER_ATTACK_OUT_OF_REACH) {
                    if (monster_info->uiCondition[W8_CONDITION_BLIND] != 0 && record->kind != 0xc) {
                        monster_info->action.kind = W8_MONSTER_ACTION_RETURN_TO_START;
                    } else {
                        monster_info->action.kind = approach();
                    }
                } else {
                    monster_info->action.kind = W8_MONSTER_ACTION_RETURN_TO_START;
                }
            } else if (!chooseMonsterAction(*monster_info, false, false, true)) {
                monster_info->action.kind = W8_MONSTER_ACTION_WAIT;
            }
        }
    };
    decide();
    if (!monsterActionUsable(monster_info) &&
        monster_info->owner_world.population->findGroup(monster_info->monster_group_id)
            ->engagement) {
        monster_info->action.kind = W8_MONSTER_ACTION_RETURN_TO_START;
    }
}

/* Whether the monster can flee at all: it has a flee chance, a special-attack
   row, the special-attack cycle the flee uses, enough stamina left, and - for
   non-summoning attacks - somewhere to run. Summoning rows flee at the party
   instead and may not be offered as a special action. */
bool Game::canMonsterUseSpecial(W8MonsterInfo* monster_info, bool exclude_special)
{
    const auto* record = &data.monsters[monster_info->monster_species];
    if (record->flee_chance == 0) {
        return false;
    }
    if (record->special_attack_kind == 0) {
        return false;
    }
    if (!monster_info->entity->IsCycleSupported(0x12)) {
        return false;
    }
    if (partyCombatActive(monster_info->owner_world) &&
        monster_info->pCombat->special_cooldown != 0) {
        return false;
    }
    if (static_cast<unsigned int>(monster_info->stamina) <
        static_cast<unsigned int>(monster_info->stamina_max) / 10) {
        return false;
    }
    if (monster_info->uiCondition[W8_CONDITION_SILENCED] != 0 &&
        MonsterSpecialAttackHonorsCastingBlock(record->special_attack_kind)) {
        return false;
    }
    if (g_special_attack_table[record->special_attack_kind][0] == kSummonEffect) {
        if (CalcRangeDistance(W8_RANGE_LONG) < monster_info->entity->GetDistanceToPlayer()) {
            return false;
        }
        if (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY) {
            return false;
        }
        if (exclude_special) {
            return false;
        }
    } else {
        const auto targets = monsterSpellTargets(monster_info, W8_SPELL_SPECIAL_ATTACK_CONE);
        if (targets.empty()) {
            return false;
        }
    }
    return true;
}

/* Aim a monster that wants to get away. A summoning special-attack row aims
   at the monster's own position. */
bool Game::aimMonsterSpecial(W8MonsterInfo* monster_info)
{
    const auto* record = &data.monsters[monster_info->monster_species];
    glm::vec3 position;

    if (g_special_attack_table[record->special_attack_kind][0] == kSummonEffect) {
        position = monster_info->entity->nav.GetPosition();
        ResetCombatSlot(&monster_info->Target);
        monster_info->Target.iType = W8_TARGET_KIND_PLACE;
        monster_info->Target.point = position;
        return true;
    }
    return aimMonsterSpell(monster_info, W8_SPELL_SPECIAL_ATTACK_CONE);
}

/* Whether the action a monster has settled on can actually be carried out. An
   attack needs a character to swing at; a spell needs to be castable and needs
   a target of a kind it accepts; fleeing is refused outright for the summoning
   special-attack rows that have nowhere to flee to. */
bool Game::monsterActionUsable(W8MonsterInfo* monster_info)
{
    int spell_id;

    switch (monster_info->action.kind) {
    case W8_MONSTER_ACTION_ATTACK:
        return monster_info->Target.iType == W8_TARGET_KIND_CHARACTER;
    case W8_MONSTER_ACTION_SPELL:
        spell_id = monster_info->action.detail;
        if (!SpellTargetsEnemies(data, spell_id)) {
            return false;
        }
        switch (monster_info->Target.iType) {
        case W8_TARGET_KIND_CHARACTER:
        case W8_TARGET_KIND_PARTY:
            return true;
        case W8_TARGET_KIND_ALL_ENEMIES:
        case W8_TARGET_KIND_PLACE:
            break;
        default:
            return false;
        }
        break;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        if (g_special_attack_table[data.monsters[monster_info->monster_species].special_attack_kind]
                                  [0] == kSummonEffect) {
            return false;
        }
        spell_id = W8_SPELL_SPECIAL_ATTACK_CONE;
        break;
    default:
        return false;
    }
    std::vector<int> monster_markers, party_markers;
    W8TargetSource source;
    SetTargetSourceToMonster(monster_info, &source);
    const auto power = ChooseMonsterSpellPowerLevel(data, rng, *monster_info, spell_id);
    appendSpellTargets(monster_info->owner_world, spell_id, power, &source, &monster_info->Target,
                       monster_markers, party_markers, W8AreaSightRule::RequireLineOfSight);
    return !party_markers.empty();
}

namespace {
bool CanProtect(const W8MonsterInfo& monster, const W8MonsterRecord& record)
{
    return monster.fActive && monster.pCombat && monster.hp_current != 0 &&
           monster.highest_condition < W8_CONDITION_BLIND &&
           (record.flags & W8_MONSTER_FLAG_BODYGUARD) != 0 && record.attacks[0].fHasAttack != 0;
}

struct MonsterActionCandidate {
    MonsterAction action;
    int attack_index;
    W8CombatSlot target;
};

/* Collect the possible actions for this decision without retaining a queue.
   Consider everything the monster may do this
   round. A fresh pick first rolls the cooperative sweep, queueing a kind-8
   entry for every character and other monster the monster has an attack on,
   and returns early when any of them stuck; a behavior-2 record may then add
   the kind-1 wait action. The rest enumerates attack candidates: each usable
   attack contributes one kind-0 entry per set attack-mode bit for every
   eligible character and monster target. `target_locked` keeps only the
   stored target; `attack_locked` keeps only the committed attack and always
   sweeps every target. A character's evasion skill roll keeps attacks off it
   but flags the slot, and each flagged slot may practice that skill at the
   end. */
std::vector<MonsterActionCandidate> BuildMonsterActions(Game& game, W8MonsterInfo* monster_info,
                                                        bool target_locked, bool attack_locked)
{
    const auto& record = game.data.monsters[monster_info->monster_species];
    std::vector<MonsterActionCandidate> actions;
    const auto append = [&](W8MonsterActionKind kind, int detail, int attack, W8CombatSlot target) {
        actions.push_back({{kind, detail}, attack, target});
        // Retail drew an unused tie value for each candidate. Keep its place in the RNG stream.
        game.rng.below(100);
    };
    if (!target_locked && !attack_locked) {
        if (CanProtect(*monster_info, record) &&
            game.rng.below(100) < monster_info->attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE]) {
            monster_info->action.kind = W8_MONSTER_ACTION_PROTECT;
            for (const auto& owner : monster_info->owner_world.population->monster_roster.live()) {
                auto* other = owner.get();
                if (other != monster_info) {
                    ResetCombatSlot(&monster_info->Target);
                    monster_info->Target.iType = W8_TARGET_KIND_MONSTER;
                    monster_info->Target.iMonsterID = other->location_id;
                    if (game.canMonsterProtect(*monster_info, monster_info->Target)) {
                        append(W8_MONSTER_ACTION_PROTECT, 0, 0,
                               W8CombatSlot::monster(other->location_id));
                    }
                }
            }
            for (int index = 0; index < 8; ++index) {
                ResetCombatSlot(&monster_info->Target);
                monster_info->Target.iType = W8_TARGET_KIND_CHARACTER;
                monster_info->Target.iChar = index;
                if (game.canMonsterProtect(*monster_info, monster_info->Target)) {
                    append(W8_MONSTER_ACTION_PROTECT, 0, 0, W8CombatSlot::character(index));
                }
            }
            if (!actions.empty()) {
                return actions;
            }
        }
        if (record.combat_morale == 2 && record.combat_behavior != 3 &&
            game.rng.below(100) < monster_info->attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE]) {
            append(W8_MONSTER_ACTION_WAIT, -1, 0, {W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}});
        }
    }
    monster_info->action.kind = W8_MONSTER_ACTION_ATTACK;
    const auto attack_lo = attack_locked ? monster_info->pCombat->attack_index : 0u;
    const auto attack_hi = attack_locked ? attack_lo + 1 : W8_MAX_MONSTER_ATTACKS;
    const bool restrict_target = target_locked && !attack_locked &&
                                 !monster_info->pCombat->berserk &&
                                 monster_info->attributes[W8_MONSTER_ATTRIBUTE_DEXTERITY] < 75;
    const auto& target = monster_info->Target;
    if (restrict_target &&
        (!game.targetPresent(&monster_info->owner_world, target) ||
         (target.iType != W8_TARGET_KIND_CHARACTER && target.iType != W8_TARGET_KIND_MONSTER)))
        return actions;
    const bool scan_chars = !restrict_target || target.iType == W8_TARGET_KIND_CHARACTER;
    const bool scan_monsters = !restrict_target || target.iType == W8_TARGET_KIND_MONSTER;
    assert(!restrict_target || (scan_chars ? target.iChar != -1 : target.iMonsterID != -1));
    const int char_lo = restrict_target && scan_chars ? target.iChar : 0;
    const int char_hi = scan_chars ? (restrict_target ? char_lo + 1 : W8_PARTY_SLOT_COUNT) : 0;
    const auto* locked_monster =
        restrict_target && scan_monsters
            ? monster_info->owner_world.population->findMonster(target.iMonsterID)
            : nullptr;
    bool avoided[W8_PARTY_SLOT_COUNT]{}, resisted[W8_PARTY_SLOT_COUNT]{};
    for (int index = char_lo; index < char_hi; ++index) {
        avoided[index] = game.rng.below(100) <
                         game.party.characters[index].skills[W8_SKILL_STEALTH].level * 75 / 100;
    }
    const bool friendly_targets = monster_info->pCombat && monster_info->pCombat->berserk;
    const auto disposition_needed =
        friendly_targets ? W8_DISPOSITION_FRIENDLY : W8_DISPOSITION_HOSTILE;
    const auto append_attack = [&](unsigned int attack, W8CombatSlot aimed) {
        for (unsigned int mode = 0; mode < 9; ++mode) {
            if ((record.attacks[attack].attack_modes & (1 << mode)) != 0)
                append(W8_MONSTER_ACTION_ATTACK, mode, attack, aimed);
        }
    };
    for (unsigned int attack = attack_lo; attack < attack_hi; ++attack) {
        if (game.rateMonsterAttack(*monster_info, attack, friendly_targets) != 0) {
            continue;
        }
        if (scan_chars && monster_info->player_visibility.los_flags[RangeCategoryUsesSightCondition(
                              monster_info, static_cast<W8RangeCategory>(
                                                record.attacks[attack].range_category))] != 0) {
            for (int index = char_lo; index < char_hi; ++index) {
                if (game.party.participates(index) &&
                    game.party.dispositionToward(index, (monster_info)->ubDisposition) ==
                        disposition_needed &&
                    game.monsterReachesPartyMember(*monster_info, attack, index,
                                                   monster_info->action)) {
                    if (!avoided[index]) {
                        append_attack(attack, W8CombatSlot::character(index));
                    } else {
                        resisted[index] = true;
                    }
                }
            }
        }
        if (scan_monsters) {
            for (const auto& owner : monster_info->owner_world.population->monster_roster.live()) {
                auto* other = owner.get();
                if (locked_monster && other != locked_monster) continue;
                if (other != monster_info && other->fActive && other->hp_current != 0 &&
                    game.data.monsters[other->monster_species].untargetable == 0 &&
                    other->pCombat &&
                    MonsterHostility(game.data, *monster_info, *other) == disposition_needed &&
                    monster_info->reaches(*other, game.data, attack, monster_info->action)) {
                    append_attack(attack, W8CombatSlot::monster(other->location_id));
                }
            }
        }
    }
    if (scan_chars) {
        for (int index = char_lo; index < char_hi; ++index) {
            if (resisted[index] && game.rng.below(2) == 0) {
                PracticeCharacterSkill(game, &game.party.characters[index], W8_SKILL_STEALTH, 1,
                                       false);
            }
        }
    }
    return actions;
}

} // namespace

/* Build the monster's list of possible actions and take one of them at
   random into its action fields and target. Committing a plain attack also
   commits its attack index and, when asked, restages the round's attack
   counts; a wait action keeps only its detail word. */
bool Game::chooseMonsterAction(W8MonsterInfo& monster, bool target_locked, bool attack_locked,
                               bool set_attack_rate)
{
    const auto& record = data.monsters[monster.monster_species];
    const auto actions = BuildMonsterActions(*this, &monster, target_locked, attack_locked);
    if (actions.empty()) {
        return false;
    }
    const auto& entry = actions[rng.below(wiz8::checked_integer<std::uint32_t>(actions.size()))];
    monster.action.kind = entry.action.kind;
    switch (entry.action.kind) {
    case W8_MONSTER_ACTION_ATTACK:
        monster.pCombat->attack_index = entry.attack_index;
        monster.action.detail = entry.action.detail;
        if (set_attack_rate) {
            monster.pCombat->attacks_per_round0 = record.attacks_per_round;
            monster.pCombat->attacks_per_round = record.attacks_per_round;
        }
        break;
    case W8_MONSTER_ACTION_WAIT:
        monster.action.detail = entry.action.detail;
        break;
    default:
        break;
    }
    monster.Target = entry.target;
    return true;
}
