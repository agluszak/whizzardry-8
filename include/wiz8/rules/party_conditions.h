#pragma once

#include <optional>

#include "wiz8/rules/conditions.h"

/* The calculations behind a party member's conditions, enchantments and death, over
   explicit inputs. The game's party commands (`live_party.h`) apply them in order with
   the consequences they have for the game around the party. */

class Rng;
struct GameData;
struct W8Character;
struct W8Enchantment;
struct W8GameplayModifierBlock;
struct W8Party;

// Diseases, infatuation, death and disappearance last indefinitely; others as asked.
int ConditionDuration(W8Condition condition, int duration);

enum class ConditionResistance { None, Silent, Fearless, MentallyImmune };
// A trait that refuses `condition` for `duration` turns, and whether it says why.
ConditionResistance ResistCondition(const W8Character& character, W8Condition condition,
                                    int duration);

// An infatuation keeps its hex outside the levels of band `level_band`.
bool InfatuationHoldsHex(const W8Character& character, int level_band);
/* The madness ring keeps member `slot` insane: carried by them, or, from the pool, by
   whoever resists least, unless they wear its counter-charm. */
bool MadnessRingHoldsInsanity(const W8Party& party, unsigned int slot);

// The highest condition with turns left, scanning down from the top.
W8Condition HighestCondition(const W8Character& character);
// The highest enchantment slot still in use.
W8EnchantmentSlot TopEnchantment(const W8Character& character);

// Poison's strength after `minutes` of its `remaining` turns, bled pro rata.
int AgedPoisonStrength(Rng& rng, int strength, unsigned int remaining, unsigned int minutes);

// Cheat death is what death becomes for this member now.
bool CheatsDeath(const W8Character& character);
// How long a cheat-death revival leaves the member unconscious.
int CheatDeathUnconsciousTurns(const W8Character& character);
/* The hit points a cheat-death revival rolls back up: from a scaled share of the maximum
   the first time in a fight, from a scaled share of the current pool every other time. */
unsigned int CheatDeathHitPoints(Rng& rng, const W8Character& character, bool first_in_fight);

/* Put an enchantment on the member unless a stronger one is in place; Guardian Angel
   rolls the damage it absorbs. True when it changed. */
bool StrengthenEnchantment(Rng& rng, const GameData& data, W8Character& character,
                           W8EnchantmentSlot enchantment, int power, unsigned int turns,
                           unsigned int percent);

// The member is missing inside a monster that swallowed them.
bool Swallowed(const W8Character& character);
/* The member's condition block from its conditions, enchantments, a bound NPC and, when
   swallowed, the swallower's level (the default penalty when it is not on the level). */
W8GameplayModifierBlock ConditionModifiers(const W8Character& character, bool npc_bound,
                                           std::optional<int> swallower_level);
/* Fold running conditions into a modifier block. `character` is null for a monster; a
   character's traits soften blindness. A swallowed member's regeneration is not here
   (`ConditionModifiers`). */
void ApplyConditionModifiers(const W8Character* character, const unsigned int* condition_turns,
                             int condition_argument, W8GameplayModifierBlock* target);
void ApplyEnchantmentModifiers(const W8Enchantment* enchantments, W8GameplayModifierBlock* target);
