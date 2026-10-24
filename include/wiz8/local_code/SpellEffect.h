#ifndef WIZ8_LOCAL_CODE_SPELL_EFFECT_H
#define WIZ8_LOCAL_CODE_SPELL_EFFECT_H

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <stddef.h>

#include "wiz8/data/attack_modes.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"

class SpellVisual;
class Missile;

enum W8SpellDamageReportKind : unsigned char {
    W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH = 1,
    W8_SPELL_DAMAGE_REPORT_MONSTER_DEATH = 3
};

/* One pending death report. Character reports identify a party slot, while
   monster reports carry their own inline text. The message pass owns each
   report until posting. */
struct W8SpellDamageReport {
    W8SpellDamageReportKind kind;
    int value;
    std::string text;
};

/* One spell effect definition, 0x30 bytes. A missile carries its own copy at
   0x1fc. The radius at 0x00 bounds an area effect (0 for a single target),
   the dice at 0x04 are rolled for the effect's size, the three values at
   0x20 through 0x2c combine into its duration, and the percentage at 0x24
   scales both. */
struct W8SpellEffectDefinition {
    float radius; /* an area effect reaches this far; 0 is single-target */
    W8Dice magnitude;
    /* The percentage chance of each condition the effect can inflict,
       rolled by ApplyEffectConditions. */
    unsigned char condition_chances[W8_ATTACK_EFFECT_COUNT];
    int power_level;
    /* Flat base added to the effect dice (SetDice's `base`); sourced
       from the attack's missile_magnitude, the item's missile_magnitude, or
       the missile table's magnitude_base. */
    int magnitude_base;
    int duration_scale;
    unsigned int percent;
    int duration_base;
    int duration_per_power;
};

/* What one missile or queued effect accumulates while it resolves: the total
   amount, the number of hits, one count per condition, and the report records
   handed to the message pass. Both the missile and the effect embed this at
   their own offset. */
struct W8SpellEffectResult {
    unsigned int amount;
    unsigned int count;
    unsigned int condition_counts[W8_CONDITION_COUNT];
    std::vector<std::unique_ptr<W8SpellDamageReport>> reports;
    /* The per-kind totals the notice pass folds into its messages;
       [3] and [4] are the running damage totals the character and monster
       damage paths add to. */
    unsigned int notice_values[6];
    /* The report came from a non-verbose resolution pass and still
       needs to be folded into the shared attack report. */
    bool deferred;
    /* Raised when the swing missed entirely, which is what lets the
       notice pass distinguish "missed" from "no effect". */
    bool missed;
    /* The target the attack actually struck - the accidental-fire path
       replaces it with the rerolled victim. */
    W8CombatSlot target;
};

struct W8SpellEffectEntry {
    void AddVisual(SpellVisual* visual);

    W8SpellId kind = W8_SPELL_NONE;
    /* Only Hypnotic Lure persists for multiple turns. */
    int turns_remaining = 0;
    /* The cast's original source. */
    W8TargetSource OrigSource;
    /* The cast's original target slot, copied beside OrigSource before any
       backfire retargeting; the save chunk stores it after OrigSource. */
    W8CombatSlot OrigTarget;
    /* Magic Effects.cpp asserts this as pQueue->Source: the working source the
       hostility walk and effect bodies hand to CollectHostileMonsters / damage. */
    W8TargetSource Source{};
    W8CombatSlot target{};
    /* CastSpellFromSource copies the complete definition into the queue slot;
       effect handlers interpret its existing fields for their own kind. */
    W8SpellEffectDefinition definition{};
    /* Two integer lists this body walks: the monster location ids
       CollectHostileMonsters gathers at 0x0e0, and a second index list at
       0x0f0 used both as party-slot indices and as monster-manager entry
       indices depending on the effect path. */
    std::vector<int> monster_ids;
    std::vector<int> target_indices;
    // Borrowed from the same world that owns this continuing cast.
    std::vector<SpellVisual*> spell_visuals;
    std::vector<Missile*> missiles;
    /* When the effect ends without having applied, the tick re-casts
       the spell from the stored source. CastSpellFromSource's `c` argument. */
    bool recast = false;
    /* Missiles carrying the effect are still in flight; the tick
       releases them and spawns the impact visual before resolving. */
    bool missiles_pending = false;
    /* The non-missile path has already run ProcessSpellEffectTargets;
       skips re-resolution and gates the post-resolution bookkeeping. */
    bool targets_resolved = false;
    /* Set once this effect's result has been reported. */
    bool reported = false;
    /* Set by a handler that actually landed its effect; the result
       report picks its message from this flag. */
    bool applied = false;
    W8SpellEffectResult result{};
};

void ReportSpellResult(W8SpellEffectEntry* effect);
void SpawnLureEffects(W8SpellEffectEntry* owner, int argument, const W8CombatSlot* target);

#endif
