#include "wiz8/local_code/ConditionsAndEnchantments.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <span>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "local_code/Factions.h"
#include "local_code/FormationAndFacing.h"
#include "monster_spell_icons.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"

/* Enchantment-slot names, indexed by W8EnchantmentSlot. */
unsigned short g_enchantment_notices[16] = {
    0x398, 0x399, 0x39a, 0x39b, 0x39c, 0x39d, 0x39e, 0x39f,
    0x273, 0x670, 0x671, 0x672, 0x673, 0x674, 0x675, 0x676,
};

/* Notices posted when a condition interrupts a character's action, indexed
   by GetConditionInterrupt's result. */
unsigned short g_condition_interrupt_notices[12] = {
    0x3a0, 0x3a1, 0x3a2, 0x3a3, 0x3a4, 0x3a5, 0x3a6, 0x3a7, 0x3a8, 0x3a9, 0x3aa, 0x3ab,
};

bool GetConditionRecordFlag(int party_slot, W8CharacterDependence dependence)
{
    return CurrentGame().party.characters[party_slot].conditions[dependence].active;
}

void ReleaseMonsterConditionBindings(W8MonsterInfo* monster_info)
{
    for (unsigned int slot_kind = 0; slot_kind < W8_DEPENDENCE_COUNT; ++slot_kind) {
        bool cleared = false;
        if ((monster_info->condition_binding_mask & (1 << slot_kind)) != 0) {
            W8Condition condition;
            switch (slot_kind) {
            case W8_DEPENDENCE_SUMMON:
                condition = W8_CONDITION_NONE;
                break;
            case W8_DEPENDENCE_SWALLOWED:
                condition = W8_CONDITION_MISSING;
                break;
            }
            for (unsigned int party_slot = 0; party_slot < 8; ++party_slot) {
                W8Character* character = &CurrentGame().party.characters[party_slot];
                W8CharacterConditionRecord* record = &character->conditions[slot_kind];
                if ((condition == W8_CONDITION_NONE || character->uiCondition[condition] != 0) &&
                    record->level_acquired == GetLoadedLevelID() &&
                    record->source_monster == monster_info->location_id) {
                    cleared = true;
                    record->active = false;
                    record->level_acquired = 0;
                    record->source_monster = 0;
                    if (condition != W8_CONDITION_NONE && character->fInParty) {
                        LiftCondition(CurrentGame(), party_slot, condition, true);
                    }
                }
            }
            if (!cleared && slot_kind == 0) {
                for (unsigned int index = 0;
                     index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
                    W8MonsterInfo* bound = GetMonsterListEntry(index);
                    if (bound->elemental_summon == monster_info->location_id) {
                        bound->elemental_summon = -1;
                        break;
                    }
                }
            }
        }
    }
}

/*
 * Original translation unit: Local Code\Conditions & Enchantments.cpp.
 *
 * Characters and monsters carry the same two arrays: twenty condition
 * durations and eight enchantment slots. The bodies here move conditions
 * between the two, lift them one at a time or all at once, and keep the
 * highest enchantment slot in use up to date as slots empty.
 */

/* Keep the live quantity in the byte selected by the database record. Stack
   items use stack_count; charge and use-count items use uses_or_charges. */
void NormalizeItemQuantityKind(W8ItemInstance* item)
{
    unsigned char quantity_kind;

    if (item->iItemNo == -1) {
        return;
    }

    quantity_kind = Data().items[item->iItemNo].quantity_kind;
    if (quantity_kind != W8_ITEM_QUANTITY_STACK) {
        if (quantity_kind <= W8_ITEM_QUANTITY_STACK || quantity_kind > W8_ITEM_QUANTITY_SHOTS ||
            item->stack_count <= 0) {
            return;
        }
        if (item->uses_or_charges > 0) {
            item->stack_count = 0;
            return;
        }
        item->uses_or_charges = item->stack_count;
        item->stack_count = 0;
        return;
    }

    if (item->uses_or_charges > 0) {
        if (item->stack_count > 0) {
            item->uses_or_charges = 0;
            return;
        }
        item->stack_count = item->uses_or_charges;
        item->uses_or_charges = 0;
        return;
    }
    if (item->stack_count == 0) {
        item->stack_count = 1;
    }
}

/* The load tail: drop equipment the loaded character can no longer wear, then
   normalize the quantity byte on every worn and packed item and on the shared
   party pool. */
void SanitizeLoadedItems(Game& game)
{
    SettleActiveParty(game);
    auto& party = game.party;
    const auto normalize = [](std::span<W8ItemInstance> items) {
        for (auto& item : items)
            if (item.iItemNo != -1) NormalizeItemQuantityKind(&item);
    };
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.members[slot].has_value()) continue;
        auto& character = party.characters[slot];
        normalize(character.EquippedItem);
        normalize(character.backpack);
    }
    normalize(std::span(party.item_pool).first(party.item_count));
}

/* Condition immunity sets by monster kind, from the retail table at
   0x006171A8. */
static constexpr std::array<W8ConditionImmunity, 3> kConditionImmunities = {{
    {20, true, {W8_CONDITION_DISEASED,  W8_CONDITION_POISONED,  W8_CONDITION_UNCONSCIOUS,
                W8_CONDITION_IRRITATED, W8_CONDITION_AFRAID,    W8_CONDITION_INSANE,
                W8_CONDITION_ASLEEP,    W8_CONDITION_NAUSEATED, W8_CONDITION_NONE,
                W8_CONDITION_NONE,      W8_CONDITION_NONE,      W8_CONDITION_NONE,
                W8_CONDITION_NONE,      W8_CONDITION_NONE,      W8_CONDITION_NONE,
                W8_CONDITION_NONE,      W8_CONDITION_NONE,      W8_CONDITION_NONE,
                W8_CONDITION_NONE,      W8_CONDITION_NONE}},
    {22,
     false,
     {W8_CONDITION_DISEASED, W8_CONDITION_POISONED, W8_CONDITION_NAUSEATED, W8_CONDITION_IRRITATED,
      W8_CONDITION_AFRAID,   W8_CONDITION_ASLEEP,   W8_CONDITION_NONE,      W8_CONDITION_NONE,
      W8_CONDITION_NONE,     W8_CONDITION_NONE,     W8_CONDITION_NONE,      W8_CONDITION_NONE,
      W8_CONDITION_NONE,     W8_CONDITION_NONE,     W8_CONDITION_NONE,      W8_CONDITION_NONE,
      W8_CONDITION_NONE,     W8_CONDITION_NONE,     W8_CONDITION_NONE,      W8_CONDITION_NONE}},
    {17,
     true,
     {W8_CONDITION_INSANE, W8_CONDITION_AFRAID, W8_CONDITION_NAUSEATED, W8_CONDITION_NONE,
      W8_CONDITION_NONE,   W8_CONDITION_NONE,   W8_CONDITION_NONE,      W8_CONDITION_NONE,
      W8_CONDITION_NONE,   W8_CONDITION_NONE,   W8_CONDITION_NONE,      W8_CONDITION_NONE,
      W8_CONDITION_NONE,   W8_CONDITION_NONE,   W8_CONDITION_NONE,      W8_CONDITION_NONE,
      W8_CONDITION_NONE,   W8_CONDITION_NONE,   W8_CONDITION_NONE,      W8_CONDITION_NONE}},
}};

bool W8ConditionImmunity::Contains(W8Condition condition) const
{
    return std::ranges::contains(conditions, condition);
}

const W8ConditionImmunity* FindConditionImmunity(unsigned char kind)
{
    const auto found = std::ranges::find(kConditionImmunities, kind, &W8ConditionImmunity::kind);
    return found == kConditionImmunities.end() ? nullptr : &*found;
}

/* Setting a monster's condition runs the sameCountdown rescan, group recount
   and motion update as clearing it, plus immunity, argument and combat-hate
   handling on the way in. */
static void RefreshMonsterConditionState(W8MonsterInfo* monster_info)
{
    int slot = 0x13;
    while (monster_info->uiCondition[slot] == 0) {
        if (slot == 0) {
            break;
        }
        --slot;
    }
    monster_info->highest_condition = static_cast<W8Condition>(slot);
    CurrentGame().events.push(MonstersChanged{});
    if (monster_info->fActive) {
        MonsterInfoSetMotionless(monster_info,
                                 monster_info->highest_condition < W8_CONDITION_WEBBED ? 0 : 1);
    }
}

void SetMonsterCondition(int location_id, W8Condition condition, int duration, int poison_strength,
                         W8TargetSource* target, bool announce)
{
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;
    int old_duration;
    bool handled;

    assert(poison_strength == 0 || condition == W8_CONDITION_POISONED);
    if (condition == W8_CONDITION_POISONED && poison_strength == 0) {
        return;
    }
    switch (condition) {
    case W8_CONDITION_DISEASED:
    case W8_CONDITION_INFATUATED:
    case W8_CONDITION_DEAD:
    case W8_CONDITION_MISSING:
        duration = W8_CONDITION_INDEFINITE;
        break;
    default:
        if (duration == 0) {
            return;
        }
        break;
    }
    monster_info = FindMonster(location_id);
    record = GetMonsterDataForInfo(monster_info);
    if (monster_info->hp_current == 0) {
        return;
    }
    if (const auto* immunity = FindConditionImmunity(record->kind);
        immunity && immunity->Contains(condition)) {
        return;
    }
    old_duration = monster_info->uiCondition[condition];
    if (old_duration < duration) {
        monster_info->uiCondition[condition] = duration;
        if (old_duration == 0) {
            if (condition == W8_CONDITION_HEXED || condition == W8_CONDITION_BLIND) {
                RefreshMonsterSight(monster_info);
            } else if (condition == W8_CONDITION_TURNCOAT) {
                if (monster_info->ubDisposition == W8_DISPOSITION_NEUTRAL) {
                    monster_info->uiCondition[W8_CONDITION_TURNCOAT] = 0;
                    return;
                }
                SetMonsterHostility(monster_info,
                                    monster_info->ubDisposition == W8_DISPOSITION_HOSTILE
                                        ? W8_DISPOSITION_FRIENDLY
                                        : W8_DISPOSITION_HOSTILE);
            }
        }
        RefreshMonsterConditionState(monster_info);
        /* The retail's mangled signature keeps condition an int, but 0x00523D8A
           tests it and 0x00523D8F bounds it unsigned, which is the same cast the
           three later uses of condition in this function already carry. */
        unsigned int condition_index = static_cast<unsigned int>(condition);
        if (old_duration == 0 && condition_index != 0 && condition_index <= 0x12) {
            SetMonsterSpellIcon(*monster_info->entity,
                                static_cast<W8MonsterSpellIconId>(condition - 1), true);
        }
        if (monster_info->pCombat && TargetSourceIsCharacter(target, 0) && target->iChar != -1) {
            int* hate = &monster_info->pCombat->character_hate[target->iChar];
            record = GetMonsterDataForInfo(monster_info);
            *hate += (record->effective_level * static_cast<unsigned int>(condition)) / 3;
        }
        handled = true;
    } else {
        handled = false;
    }
    if (TargetSourceIsCharacter(target, 0) || TargetSourceIsMonster(target, 0)) {
        if (!target->fBackfire && !target->fReflection && !target->target_diverted) {
            monster_info->condition_target = *target;
        }
    }
    if (static_cast<unsigned int>(poison_strength) >
        static_cast<unsigned int>(monster_info->condition_argument)) {
        monster_info->condition_argument = poison_strength;
        handled = true;
    }
    RebuildMonsterDerivedStats(location_id);
    if (!handled) {
        return;
    }
    if (static_cast<unsigned int>(condition) >= 0xD) {
        ResetCombatSlot(&monster_info->Target);
    }
    if (static_cast<unsigned int>(condition) >= 0x12) {
        MonsterStartsDying(monster_info, announce);
        return;
    }
    if (announce &&
        (CurrentGame().combat.has_value() || monster_info->party_threat.visible_to_player)) {
        auto name = GetMonsterName(monster_info, 0, 0);
        CurrentGame().events.push(TextNotice{
            W8_FONT_PALETTE_RUST,
            fmt::format("{} {}!", name, Data().strings[kConditionNotices[condition].singular])});
    }
    if (monster_info->entity->IsCycleInterruptable(monster_info->entity->visual.pending_cycle)) {
        StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_GET_HIT, W8_ANIMATION_PLAY_ONCE);
    }
}
/* Clearing a monster's condition also re-derives its highest set condition
   index, recounts its group, and possibly stops it moving. */
void ClearMonsterCondition(int location_id, W8Condition condition)
{
    W8MonsterInfo* monster_info;
    W8MonsterGroup* monster_group;

    monster_info = FindMonster(location_id);
    if (monster_info->hp_current != 0) {
        assert(monster_info->uiCondition[condition] != 0);
        if (condition == W8_CONDITION_TURNCOAT &&
            monster_info->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
            monster_group = FindMonsterGroup(monster_info->monster_group_id);
            SetMonsterHostility(monster_info, monster_group->ubDisposition);
        }
        if (CurrentGame().combat.has_value() || monster_info->party_threat.visible_to_player) {
            CurrentGame().events.push(
                TextNotice{W8_FONT_PALETTE_RUST,
                           fmt::sprintf(Data().strings[0x244], GetMonsterName(monster_info, 0, 0),
                                        Data().strings[kConditionNotices[condition].name])});
        }
        monster_info->uiCondition[condition] = 0;
        RefreshMonsterConditionState(monster_info);
        if (condition != W8_CONDITION_NONE && condition < W8_CONDITION_MISSING) {
            SetMonsterSpellIcon(*monster_info->entity,
                                static_cast<W8MonsterSpellIconId>(condition - 1), false);
        }
        switch (condition) {
        case W8_CONDITION_HEXED:
        case W8_CONDITION_BLIND:
            RefreshMonsterSight(monster_info);
            RebuildMonsterDerivedStats(location_id);
            return;
        case W8_CONDITION_POISONED:
            monster_info->condition_argument = 0;
            RebuildMonsterDerivedStats(location_id);
            return;
        case W8_CONDITION_AFRAID:
            if (monster_info->pCombat) {
                monster_info->pCombat->advancing = false;
            }
            break;
        default:
            break;
        }
        RebuildMonsterDerivedStats(location_id);
    }
}

/* Age one live monster condition. Poison loses its carried strength in the
   same proportion as its remaining duration, with a probabilistic one-point
   correction when integer division rounds the loss to zero. */
void TickMonsterCondition(int location_id, W8Condition condition, unsigned int minutes)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);

    if (monster_info->hp_current == 0) {
        return;
    }
    assert(monster_info->uiCondition[condition] != 0);
    if (monster_info->uiCondition[condition] <= minutes) {
        ClearMonsterCondition(location_id, condition);
        return;
    }
    if (condition == W8_CONDITION_POISONED) {
        int strength = monster_info->condition_argument;
        unsigned int lost = strength * minutes / monster_info->uiCondition[condition];

        if (lost == 0 && CurrentGame().rng.below(100) <
                             strength * minutes * 100 / monster_info->uiCondition[condition]) {
            lost = 1;
        }
        monster_info->condition_argument = strength - lost;
    }
    monster_info->uiCondition[condition] -= minutes;
}

/* Copy every condition a character is under onto something else in the world.
   Condition seven carries an argument alongside its duration, so it is the one
   entry that is not just a duration. */
void CopyCharacterConditionsToTarget(const W8Character& character, const int* target)
{
    W8TargetSource target_block;
    unsigned int condition;
    int duration;
    int argument;

    ResetTargetSource(&target_block);
    for (condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
        duration = character.uiCondition[condition];
        if (duration != 0) {
            if (condition == W8_CONDITION_POISONED) {
                argument = character.condition_argument;
                duration = character.uiCondition[W8_CONDITION_POISONED];
            } else {
                argument = 0;
            }
            SetMonsterCondition(*target, static_cast<W8Condition>(condition), duration, argument,
                                &target_block, false);
        }
    }
}

/* The same copy the other way round, from a monster onto one character. Every
   condition carries the monster's argument here, not only the seventh. */
void CopyMonsterConditionsToCharacter(W8Character* character, const W8MonsterInfo* monster_info)
{
    unsigned int condition;
    int duration;
    int argument;

    for (condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
        duration = monster_info->uiCondition[condition];
        if (duration != 0) {
            argument = monster_info->condition_argument;
            if (condition == W8_CONDITION_POISONED) {
                duration = monster_info->uiCondition[W8_CONDITION_POISONED];
            }
            InflictCondition(CurrentGame(), CharacterPointerToPartySlot(character),
                             {.condition = static_cast<W8Condition>(condition),
                              .duration = duration,
                              .poison_strength = argument,
                              .announce = false});
        }
    }
}

/* The monster-side counterpart of the party rules' Enchant: a bigger
   argument replaces the stored one, slot two rolls its bonus dice, a freshly
   applied slot adds the spell icon, and the stats rebuild and special-slot
   sight refresh follow. */
void ApplyMonsterCondition(int location_id, W8EnchantmentSlot slot, int argument,
                           unsigned int duration, unsigned int percent)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);
    W8Enchantment* enchantment = &monster_info->enchantments[slot];
    if (enchantment->power <= static_cast<unsigned int>(argument)) {
        unsigned int previous = enchantment->turns;
        enchantment->power = argument;
        enchantment->percent = static_cast<unsigned short>(percent);
        enchantment->turns = duration;
        if (slot == W8_ENCHANTMENT_GUARDIAN_ANGEL) {
            int roll = RollDice(&Data().spells[W8_SPELL_GUARDIAN_ANGEL].effect_dice);
            enchantment->magnitude =
                static_cast<short>(
                    (static_cast<unsigned int>(static_cast<unsigned short>(roll * argument)) *
                     percent) /
                    100) +
                static_cast<short>(roll * argument);
        }
        if (previous == 0) {
            SetMonsterSpellIcon(*monster_info->entity,
                                static_cast<W8MonsterSpellIconId>(slot + 0x10), true);
        }
        RebuildMonsterDerivedStats(location_id);
        if (slot == W8_ENCHANTMENT_SUPERMAN) {
            RefreshMonsterSight(monster_info);
        }
    }
}

/* Empty one of a monster's enchantment slots and tell the live engine object
   that the matching effect is over. */
void ClearMonsterEnchantmentSlot(int location_id, W8EnchantmentSlot slot)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);

    monster_info->enchantments[slot] = {};
    SetMonsterSpellIcon(*monster_info->entity, static_cast<W8MonsterSpellIconId>(slot + 0x10),
                        false);
    RebuildMonsterDerivedStats(location_id);
    if (slot == W8_ENCHANTMENT_SUPERMAN) {
        RefreshMonsterSight(monster_info);
    }
}

/* Run one of a monster's enchantment slots down by the given number of turns,
   emptying it when nothing is left. The look-up is repeated rather than
   reused, which is what the two separate index calls show. */
void TickMonsterEnchantmentSlot(int location_id, W8EnchantmentSlot slot, unsigned int turns)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);
    unsigned int remaining = monster_info->enchantments[slot].turns;

    if (turns < remaining) {
        monster_info->enchantments[slot].turns = remaining - turns;
        return;
    }

    ClearMonsterEnchantmentSlot(location_id, slot);
}

/* Lift one condition from everybody in the world - the party first, then every
   live monster. The monster count is re-read every iteration because lifting a
   condition can remove one. */
void RemoveConditionFromEveryone(W8Condition condition)
{
    unsigned int monster_index;
    W8MonsterInfo* monster_info;

    LiftConditionFromParty(CurrentGame(), condition);

    for (monster_index = 0; monster_index < CurrentLevelPopulation().monster_roster.live().size();
         ++monster_index) {
        monster_info = GetMonsterListEntry(monster_index);
        if (monster_info->uiCondition[condition] != 0) {
            ClearMonsterCondition(monster_info->location_id, condition);
        }
    }
}

/* Strip all eight enchantment slots from every occupied party member and every
   live monster, rescanning each character's top slot and flagging the special
   slot's refresh. The monster count is re-read every iteration because
   clearing one can remove a monster from the list. */
void RemoveAllEnchantments(void)
{
    for (unsigned int enchantment = 0; enchantment < 8; ++enchantment) {
        for (int party_slot = 0; party_slot < 8; ++party_slot) {
            W8Character* character = &CurrentGame().party.characters[party_slot];

            if (CurrentGame().party.members[party_slot].has_value() &&
                character->enchantments[enchantment].turns != 0) {
                Disenchant(CurrentGame(), party_slot, static_cast<W8EnchantmentSlot>(enchantment));
            }
        }
        for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
             ++index) {
            W8MonsterInfo* monster_info = GetMonsterListEntry(index);

            if (monster_info->enchantments[enchantment].turns != 0) {
                ClearMonsterEnchantmentSlot(monster_info->location_id,
                                            static_cast<W8EnchantmentSlot>(enchantment));
            }
        }
    }
}

/* Bind a monster to one of a character's two dependence slots: the monster
   remembers it is bound, a slot-one binding retires the monster's group from
   the encounter budget, and the character's condition record keeps the level
   it happened on, the monster's id and the live flag before the derived
   stats rebuild. */
void BindMonsterToCharacterDependence(unsigned int party_slot, unsigned int dependence_slot,
                                      int monster_id)
{
    W8MonsterInfo* monster_info;

    assert(party_slot < 8);
    assert(dependence_slot < W8_DEPENDENCE_COUNT);
    assert(monster_id != -1);

    monster_info = FindMonster(monster_id);
    monster_info->condition_binding_mask =
        static_cast<unsigned char>(monster_info->condition_binding_mask | (1 << dependence_slot));
    if (dependence_slot == W8_DEPENDENCE_SWALLOWED) {
        RetireMonsterGroupAndAllies(FindMonsterGroup(monster_info->monster_group_id));
    }

    CurrentGame().party.characters[party_slot].conditions[dependence_slot].level_acquired =
        GetLoadedLevelID();
    CurrentGame().party.characters[party_slot].conditions[dependence_slot].source_monster =
        monster_id;
    CurrentGame().party.characters[party_slot].conditions[dependence_slot].active = true;
    RefreshConditionModifiers(CurrentGame(), party_slot);
}
