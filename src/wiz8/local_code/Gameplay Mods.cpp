#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "wiz8/combat/combat.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_conditions.h"
#include "wiz8/world/world.h"

void Game::refreshPartyEffects()
{
    party.refreshModifiers(status.effect_slots,
                           combat ? std::span<const W8EffectSlot>{combat->effect_slots}
                                  : std::span<const W8EffectSlot>{},
                           combat ? std::span<const W8EffectSlot>{combat->effect_slots0}
                                  : std::span<const W8EffectSlot>{});
    const auto light = party.modifiers.light;
    SettleActiveParty(*this);
    if (level.primary) level.primary->setPartyLight(light);
}

/* The monster-side rebuild: refill the monster's modifier block from its
   condition durations, enchantments and effect slots, then its armor byte's
   combat adjustment and the combat effect run, and finally the derived
   attributes and regeneration rates. */
void RebuildMonsterDerivedStats(int location_id)
{
    auto& monster = *FindMonster(location_id);
    RebuildMonsterDerivedStats(monster, *GetMonsterDataForInfo(&monster));
}

void RebuildMonsterDerivedStats(W8MonsterInfo& monster, const W8MonsterRecord& record)
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
    constexpr std::array attributes{W8_ATTRIBUTE_STRENGTH, W8_ATTRIBUTE_INTELLIGENCE,
                                    W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_SPEED,
                                    W8_ATTRIBUTE_SENSES};
    static_assert(attributes.size() == W8_MONSTER_ATTR_COUNT);
    for (std::size_t index = 0; index < attributes.size(); ++index) {
        const int value =
            record.attribute_values[index] + modifiers->attribute_adjustments[attributes[index]];
        monster.attributes[index] = static_cast<unsigned char>(std::clamp(value, 1, 125));
    }
    RebuildMonsterRegenRates(monster_info);
}
