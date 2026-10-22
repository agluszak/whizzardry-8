#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/skills.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
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
#include "wiz8/rules/party_conditions.h"
#include "wiz8/world/environment.h"

/* Clear the party modifier block, fold the party's effect slots, the combat
   effect run and each character's own equipment and condition blocks into
   their derived modifiers, then drive the sky node from the stored light
   byte. */
void RebuildPartyEffectBlock(void)
{
    W8GameplayModifierBlock modifiers{};
    ApplyPartyEffectSlots(CurrentGame().status.effect_slots.data(), &modifiers);
    if (CurrentGame().combat.has_value()) {
        unsigned char value = modifiers.armor_matchup;

        for (auto& effect_slot : CurrentGame().combat->effect_slots) {
            W8EffectSlot* slot = &effect_slot;
            if (slot->active && slot->effect_id == 0x31) {
                value -= slot->amount;
                modifiers.armor_matchup = value;
            }
        }
        ApplyCombatEffectSlots(CurrentGame().combat->effect_slots0, &modifiers);
    }
    int active = 0;
    unsigned int slot_byte = 0;
    while (slot_byte <= 0x82f) {
        W8Character* character = &CurrentGame().party.characters[active];
        if (CurrentGame().party.members[active].has_value() && character->hp_current != 0 &&
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
    World* world = CurrentGame().level.primary.get();
    if (!world) return;
    Light* light = world->camera_light;
    if (!light) return;
    light->visible = modifiers.light != 0;
    if (modifiers.light != 0) light->intensity = modifiers.light + kEnvironmentNearScale;
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
