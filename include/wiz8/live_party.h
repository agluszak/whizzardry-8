#pragma once

#include <bitset>

#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party_slot.h"

class Game;
struct W8Character;

/* The game's party commands: a member's conditions, enchantments and death, and the
   party's settlement. Each applies the calculations in `rules/party_conditions.h` and,
   in order among them, their consequences for the game's fight, story, world, speech and
   interface. Every change to what a member's statistics depend on settles the party. */

/* Bring the party's equipment, statistics and shared load in line with its state:
   unusable equipment comes off, pools follow changed ceilings, a member whose hit points
   run out dies, and combat choices follow changed weapon hands. */
void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});

struct ConditionInfliction {
    W8Condition condition = W8_CONDITION_NONE;
    // Turns; diseases, infatuation, death and disappearance last indefinitely.
    int duration = 0;
    // Poison's strength; a poisoning without one does nothing.
    int poison_strength = 0;
    // Announce a disappearance with the alternative notice.
    bool alternate_missing_notice = false;
    bool announce = true;
};
/* Inflict a condition on member `slot`. Traits, the world's suspension and poison without
   strength refuse it (some with a notice), a cheat-death trait turns death into a
   revival, and a condition the member already has for longer is not shortened. True when
   it took hold or lengthened. */
bool InflictCondition(Game& game, unsigned int slot, const ConditionInfliction& infliction);
/* Lift a condition from member `slot`, with what that condition alone maintained. An
   infatuation outside its levels keeps a hex, and the madness ring's bearer keeps their
   insanity without its counter-charm. */
void LiftCondition(Game& game, unsigned int slot, W8Condition condition, bool announce);
/* Run one of member `slot`'s conditions down by `minutes`, lifting it when they run out;
   poison also loses strength in proportion. */
void AgeCondition(Game& game, unsigned int slot, W8Condition condition, unsigned int minutes);
// Lift `condition` from every member who has it.
void LiftConditionFromParty(Game& game, W8Condition condition);
// Lift every clearable condition from the party, except infatuation.
void LiftAllConditionsFromParty(Game& game);
/* The cheat-death trait revives member `slot` instead of letting it die: unconscious for
   a while, with hit points rolled back up. */
void ReviveByCheatingDeath(Game& game, unsigned int slot);

// Put an enchantment on member `slot` unless a stronger one is in place.
void Enchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment, int power,
             unsigned int turns, unsigned int percent);
void Disenchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment);
// Run an enchantment down by `turns`, emptying it when none are left.
void AgeEnchantment(Game& game, unsigned int slot, W8EnchantmentSlot enchantment,
                    unsigned int turns);
/* Member `slot`'s condition block follows a change outside its conditions (a bound NPC,
   a dependence), and the party settles. */
void RefreshConditionModifiers(Game& game, unsigned int slot);

/* A member of the party (its own `fInParty`) settles the party; any other character, such as
   a draft in character creation, is calculated on its own. */
void RecalculateCharacterDerivedStats(Game& game, W8Character& character);
/* The calculation for a draft outside the party from the effective attributes and skill
   levels it holds, which the creation editor edits directly. */
void RecalculateEditedCharacterStats(W8Character& draft);
