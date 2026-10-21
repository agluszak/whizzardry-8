#pragma once

#include <bitset>
#include <optional>

#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"

/* A party member's conditions and enchantments, death and the party's settlement, over
   explicit state. The rules change the party, draw from the context's stream and push
   notices and display changes to its events; what they need from the game around the
   party, and what they owe it, goes through a `PartyHost`, called at the point the rules
   reach it so its consequences keep their order among the rules' own changes and draws.
   Every operation settles the party once it has changed what statistics depend on. */

struct RuleContext;
struct W8Character;
struct W8CharacterConditionRecord;
struct W8Enchantment;
struct W8GameplayModifierBlock;

class PartyHost {
public:
    virtual ~PartyHost() = default;

    // Settlement's inputs at the moment the party settles (combat actions, NPC story).
    virtual PartyStatRules statRules() = 0;
    // The world is suspended, as in a scripted scene: no condition takes hold.
    virtual bool worldSuspended() = 0;
    // The loaded level's band (`GetLevelBand`), which decides whether a hex can lift.
    virtual int levelBand() = 0;
    /* The level of the monster that swallowed a member, when it is on the loaded level;
       otherwise a swallowed member regenerates at the default penalty. */
    virtual std::optional<int> swallowerLevel(const W8CharacterConditionRecord& record) = 0;
    /* A cheat-death revival in a fight restores a share of the maximum the first time;
       true when that share is still owed, which it no longer is afterwards. */
    virtual bool spendFirstCombatRevival(unsigned int slot) = 0;

    // Take off what member `slot` can no longer use; true when its hands changed.
    virtual bool unequipUnusable(unsigned int slot) = 0;
    // Combat choices follow a change of the weapons in member `slot`'s hands.
    virtual void handsChanged(unsigned int slot) = 0;
    // Member `slot` became overloaded.
    virtual void overloaded(unsigned int slot) = 0;
    // `condition` took hold of member `slot`, who did not have it.
    virtual void conditionStarted(unsigned int slot, W8Condition condition) = 0;
    // `condition` was lifted from member `slot`.
    virtual void conditionLifted(unsigned int slot, W8Condition condition) = 0;
    // Member `slot`'s highest condition was recomputed (formation seats, portrait pose).
    virtual void standingChanged(unsigned int slot) = 0;
    // An enchantment slot of member `slot` filled or emptied.
    virtual void enchantmentChanged(unsigned int slot, W8EnchantmentSlot enchantment) = 0;
    // Member `slot` died: its fight, its NPC and the party's reaction.
    virtual void died(unsigned int slot) = 0;
    // Member `slot` is dead or missing, so the party stops aiming at it.
    virtual void incapacitated(unsigned int slot) = 0;
    // Member `slot` reacts to its new highest condition.
    virtual void conditionReaction(unsigned int slot) = 0;
    // Member `slot` reacts to `condition` being lifted.
    virtual void recoveryReaction(unsigned int slot, W8Condition condition) = 0;
};

/* Bring the party's equipment, statistics and shared load in line with its state:
   unusable equipment comes off, pools follow changed ceilings, combat choices follow
   changed weapon hands, and a member whose hit points run out dies. */
void SettleParty(RuleContext& rules, PartyHost& host,
                 std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});

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
bool InflictCondition(RuleContext& rules, PartyHost& host, unsigned int slot,
                      const ConditionInfliction& infliction);
/* Lift a condition from member `slot`, with what that condition alone maintained. An
   infatuation outside its levels keeps a hex, and the madness ring's bearer keeps their
   insanity without its counter-charm. */
void LiftCondition(RuleContext& rules, PartyHost& host, unsigned int slot, W8Condition condition,
                   bool announce);
/* Run one of member `slot`'s conditions down by `minutes`, lifting it when they run out;
   poison also loses strength in proportion. */
void AgeCondition(RuleContext& rules, PartyHost& host, unsigned int slot, W8Condition condition,
                  unsigned int minutes);
// Lift `condition` from every member who has it.
void LiftConditionFromParty(RuleContext& rules, PartyHost& host, W8Condition condition);
// Lift every clearable condition from the party, except infatuation.
void LiftAllConditionsFromParty(RuleContext& rules, PartyHost& host);
/* The cheat-death trait revives member `slot` instead of letting it die: unconscious for
   a while, with hit points rolled back up. */
void ReviveByCheatingDeath(RuleContext& rules, PartyHost& host, unsigned int slot);
// Cheat death is what death becomes for this member now.
bool CheatsDeath(const W8Character& character);

/* Put an enchantment on member `slot` unless a stronger one is in place; Guardian Angel
   rolls the damage it absorbs. */
void Enchant(RuleContext& rules, PartyHost& host, unsigned int slot, W8EnchantmentSlot enchantment,
             int power, unsigned int turns, unsigned int percent);
void Disenchant(RuleContext& rules, PartyHost& host, unsigned int slot,
                W8EnchantmentSlot enchantment);
// Run an enchantment down by `turns`, emptying it when none are left.
void AgeEnchantment(RuleContext& rules, PartyHost& host, unsigned int slot,
                    W8EnchantmentSlot enchantment, unsigned int turns);

/* Rebuild member `slot`'s condition block from its conditions, enchantments, a swallowing
   monster and a bound NPC. The caller settles the party afterwards. */
void RebuildConditionModifiers(RuleContext& rules, PartyHost& host, unsigned int slot);
/* Fold running conditions into a modifier block. `character` is null for a monster; a
   character's traits soften blindness. A swallowed member's regeneration is not here
   (`RebuildConditionModifiers`). */
void ApplyConditionModifiers(const W8Character* character, const unsigned int* condition_turns,
                             int condition_argument, W8GameplayModifierBlock* target);
void ApplyEnchantmentModifiers(const W8Enchantment* enchantments, W8GameplayModifierBlock* target);
