#include "wiz8/character_skills.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/utility.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"

/* Clear the party modifier block, fold the party's effect slots, the combat
   effect run and each character's own equipment and condition blocks into
   their derived modifiers, then drive the sky node from the stored light
   byte. */
void RebuildPartyEffectBlock(void)
{
    W8GameplayModifierBlock modifiers{};
    ApplyPartyEffectSlots(CurrentGame().status.effect_slots.data(), &modifiers);
    if (CurrentGame().combat.active()) {
        unsigned char value = modifiers.armor_matchup;

        for (auto& effect_slot : CurrentGame().combat.state()->effect_slots) {
            W8EffectSlot* slot = &effect_slot;
            if (slot->active && slot->effect_id == 0x31) {
                value -= slot->amount;
                modifiers.armor_matchup = value;
            }
        }
        ApplyCombatEffectSlots(CurrentGame().combat.state()->effect_slots0, &modifiers);
    }
    int active = 0;
    unsigned int slot_byte = 0;
    while (slot_byte <= 0x82f) {
        W8Character* character = &CurrentGame().party.characters[active];
        if (CurrentGame().party.rows[active].fOccupied && character->hp_current != 0 &&
            character->highest_condition == W8_CONDITION_NONE &&
            CharacterHasTrait(character, W8_TRAIT_CAMP_RECOVERY_BONUS)) {
            break;
        }
        slot_byte += 0x106;
        ++active;
    }
    if (slot_byte < 0x830) {
        modifiers.boost_health_regen = 1;
        modifiers.boost_stamina_regen = 1;
        modifiers.boost_spell_regen = 1;
    }
    CurrentGame().party.modifiers = modifiers;
    SettleActiveParty(CurrentGame());
    if (modifiers.light == 0) {
        SetSkyNodeVisible(false);
        return;
    }
    SetSkyNodeVisible(true);
    SetCameraLightIntensity(modifiers.light + kEnvironmentNearScale);
}

/* Fold the party's twelve effect slots into the shared modifier block. */
void ApplyPartyEffectSlots(const W8EffectSlot* source, W8GameplayModifierBlock* target)
{
    const W8EffectSlot* slot = source;

    for (int index = 0; index < 12; ++index, ++slot) {
        if (slot->active) {
            unsigned char amount = static_cast<unsigned char>(slot->amount);
            unsigned int percent = slot->percent;
            unsigned char adjusted = amount;

            switch (slot->effect_id) {
            case 0x14:
                adjusted = static_cast<unsigned char>((slot->amount + 1) / 2);
                AdjustByteByPercent(&adjusted, percent);
                target->hit_bonus += adjusted;
                break;
            case 0x20:
                adjusted = static_cast<unsigned char>((slot->amount + 1) / 2);
                AdjustByteByPercent(&adjusted, percent);
                target->armor_matchup += adjusted;
                break;
            case 0x28:
                adjusted = static_cast<unsigned char>(slot->amount * 4 + 7);
                AdjustByteByPercent(&adjusted, percent);
                target->resistance_bonus_all += adjusted;
                break;
            case 0x11:
                target->detect_secrets = 1;
                break;
            case 8:
                AdjustByteByPercent(&adjusted, percent);
                target->light = adjusted << 1;
                break;
            case 0x21:
                AdjustByteByPercent(&adjusted, percent);
                target->value = adjusted;
                break;
            case 0x1a:
                adjusted = static_cast<unsigned char>((slot->amount + 5) * 5);
                AdjustByteByPercent(&adjusted, percent);
                target->missile_deflect_chance = adjusted;
                break;
            case 0x2d:
                target->sight_override = 1;
                break;
            }
        }
    }
}

/* Fold the six combat effect records at +0x85a into the shared modifier block. */
void ApplyCombatEffectSlots(const W8EffectSlot* source, W8GameplayModifierBlock* target)
{
    const W8EffectSlot* slot = source;

    for (int index = 0; index < 6; ++index, ++slot) {
        if (slot->active) {
            unsigned char adjusted;
            unsigned int percent = slot->percent;

            switch (slot->effect_id) {
            case 2:
                adjusted = 2;
                AdjustByteByPercent(&adjusted, percent);
                target->armor_flat += adjusted;
                target->hit_bonus += adjusted;
                break;
            case 0x35:
                adjusted = static_cast<unsigned char>(slot->amount * 7);
                AdjustByteByPercent(&adjusted, percent);
                target->resistance_bonus[0] += adjusted;
                target->resistance_bonus[1] += adjusted;
                target->resistance_bonus[2] += adjusted;
                target->resistance_bonus[3] += adjusted;
                break;
            case 0x3b:
                adjusted = static_cast<unsigned char>(slot->amount * 10);
                AdjustByteByPercent(&adjusted, percent);
                target->resistance_bonus[5] += adjusted;
                target->resistance_bonus[4] += adjusted;
                break;
            }
        }
    }
}

/* Rebuild one party member's condition/enchantment modifier block from the
   live condition durations, the enchantment slots and the bound-NPC penalty.
   Every condition and enchantment change funnels through it. */
void RebuildConditionModifiers(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    character->condition_modifiers = {};
    ApplyConditionModifiers(character, character->uiCondition, character->condition_argument,
                            &character->condition_modifiers);
    ApplyEnchantmentModifiers(character->enchantments, &character->condition_modifiers);
    ApplyBoundNpcPenalty(character, &character->condition_modifiers);
}

/* The monster-side rebuild: refill the monster's modifier block from its
   condition durations, enchantments and effect slots, then its armor byte's
   combat adjustment and the combat effect run, and finally the derived
   attributes and regeneration rates. */
void RebuildMonsterDerivedStats(int location_id)
{
    RebuildMonsterDerivedStats(*FindMonster(location_id));
}

void RebuildMonsterDerivedStats(W8MonsterInfo& monster)
{
    auto* monster_info = &monster;
    W8GameplayModifierBlock* modifiers = &monster_info->modifiers;

    *modifiers = {};
    ApplyConditionModifiers(0, monster_info->uiCondition, monster_info->condition_argument,
                            modifiers);
    ApplyEnchantmentModifiers(monster_info->enchantments, modifiers);
    ApplyPartyEffectSlots(monster_info->effect_slots, modifiers);
    if (monster_info->pCombat) {
        W8EffectSlot* slot = monster_info->pCombat->combat_effects;
        for (int index = 9; index != 0; --index, ++slot) {
            if (slot->active && slot->effect_id == 0x31) {
                modifiers->armor_matchup -= slot->amount;
            }
        }
        ApplyCombatEffectSlots(monster_info->pCombat->combat_effects_2, modifiers);
    }
    ConvertMonsterAttributes(monster_info);
    RebuildMonsterRegenRates(monster_info);
}

/* Fold the live conditions into the modifier block: each running condition id
   carries its own fixed penalty set, condition seven also adds the carried
   argument (poison strength) to the block's regen channel, and condition 0x13
   scales its hit adjustment off the bound monster when the record still
   resolves on the saved level. */
void ApplyConditionModifiers(W8Character* character, const unsigned int* condition_turns,
                             int condition_argument, W8GameplayModifierBlock* target)
{
    unsigned int i;

    for (unsigned int index = 0; index < W8_CONDITION_COUNT; ++index) {
        if (condition_turns[index] == 0) {
            continue;
        }
        switch (index) {
        case 3:
            target->hit_bonus -= 2;
            target->armor_class_adjustment -= 2;
            break;
        case 4:
            target->hit_bonus -= 5;
            target->armor_class_adjustment -= 4;
            break;
        case 5:
            target->attribute_adjustments[W8_ATTRIBUTE_SPEED] -= 0x32;
            break;
        case 6:
            target->hit_bonus -= 3;
            target->armor_class_adjustment -= 2;
            break;
        case W8_CONDITION_POISONED:
            target->hit_bonus -= 2;
            target->armor_class_adjustment -= 2;
            target->damage_per_minute += condition_argument;
            break;
        case 9:
            target->hit_bonus -= 5;
            for (i = 0; i < 7; ++i) {
                target->attribute_adjustments[i] -= 0x14;
            }
            for (i = 0; i < 0x29; ++i) {
                target->skill_bonus[i] -= 0x14;
            }
            break;
        case 0xb:
            target->attribute_adjustments[W8_ATTRIBUTE_INTELLIGENCE] -= 0x32;
            break;
        case 0xc:
            if (character == 0 || !CharacterHasTrait(character, W8_TRAIT_EFFECTIVE_WHILE_BLIND)) {
                target->attribute_adjustments[W8_ATTRIBUTE_SENSES] -= 0x32;
                target->out_of_formation = 1;
            } else {
                target->attribute_adjustments[W8_ATTRIBUTE_SENSES] +=
                    static_cast<signed char>(ScaleValueByProfessionLevel(
                        character, W8_TRAIT_EFFECTIVE_WHILE_BLIND, 50.0f)) -
                    0x32;
            }
            break;
        case 0xe:
            target->attribute_adjustments[W8_ATTRIBUTE_DEXTERITY] -= 0x32;
            [[fallthrough]];
        case W8_CONDITION_ASLEEP:
        case 0x10:
        case W8_CONDITION_UNCONSCIOUS:
            target->out_of_formation = 1;
            break;
        case 0x13:
            if (GetConditionRecordFlag(CharacterPointerToPartySlot(character),
                                       W8_DEPENDENCE_SWALLOWED)) {
                W8MonsterInfo* bound;
                if (character->conditions[W8_DEPENDENCE_SWALLOWED].level_acquired ==
                        GetLoadedLevelID() &&
                    (bound = FindMonster(
                         character->conditions[W8_DEPENDENCE_SWALLOWED].source_monster)) != 0) {
                    const W8MonsterRecord* monster = GetMonsterDataForInfo(bound);
                    target->health_regen_adjustment += -1 - (monster->effective_level >> 1);
                } else {
                    target->health_regen_adjustment += -5;
                }
            }
            break;
        }
    }
}

/* Fold the enchantment slots into the modifier block: slot five raises one
   attribute by ten per power, slot six raises them all by five per power and
   slot seven raises the damage-reduction and armor bytes by eight and one per
   power, each scaled by the slot's percentage. */
void ApplyEnchantmentModifiers(const W8Enchantment* enchantments, W8GameplayModifierBlock* target)
{
    unsigned char amount;
    unsigned int i;

    for (unsigned int index = 0; index < 8; ++index) {
        const W8Enchantment* slot = &enchantments[index];
        if (slot->turns == 0) {
            continue;
        }
        switch (index) {
        case 5:
            amount = static_cast<unsigned char>(slot->power * 10);
            AdjustByteByPercent(&amount, slot->percent);
            target->attribute_adjustments[W8_ATTRIBUTE_SPEED] += amount;
            break;
        case 6:
            amount = static_cast<unsigned char>(slot->power * 5);
            AdjustByteByPercent(&amount, slot->percent);
            for (i = 0; i < 7; ++i) {
                target->attribute_adjustments[i] += amount;
            }
            break;
        case 7:
            amount = static_cast<unsigned char>(slot->power << 3);
            AdjustByteByPercent(&amount, slot->percent);
            target->damage_reduction_adjustment += amount;
            amount = static_cast<unsigned char>(slot->power);
            AdjustByteByPercent(&amount, slot->percent);
            target->armor_matchup += amount;
            break;
        }
    }
}
