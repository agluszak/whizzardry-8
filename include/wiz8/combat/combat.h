#pragma once

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "wiz8/audio_resources.h"
#include "wiz8/character_skills.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/targeting.h"

class Game;
struct W8Character;

struct W8MonsterInfo;

struct W8SpellDamageReport;
class Missile;
class SpellVisual;

enum class W8CombatEquipPhase : unsigned char { action_selection, resolving };

/* Who acts in a combat turn: a party slot, or a monster by its location id. A
   monster is resolved through FindMonster when its turn is used. */
struct PartyActor {
    int slot;
    bool operator==(const PartyActor&) const = default;
};
struct MonsterActor {
    int location_id;
    bool operator==(const MonsterActor&) const = default;
};
using CombatActor = std::variant<PartyActor, MonsterActor>;

/* The turn in progress. A scheduled actor waits on the action clock; a
   resolving one is stepped each frame until its action reports finishing; a
   finishing one is settled (its next phase) and the turn becomes idle. */
struct TurnIdle {};
struct TurnScheduled {
    CombatActor actor;
};
struct TurnResolving {
    CombatActor actor;
};
struct TurnFinishing {
    CombatActor actor;
};
using CombatTurn = std::variant<TurnIdle, TurnScheduled, TurnResolving, TurnFinishing>;

/* What one step of a resolving action reports. */
enum class ActionStep : unsigned char { resolving, finishing };

/* What the turn waits on before it may advance, checked in this order. */
enum class CombatWait : unsigned char {
    none,
    camera,       // the party camera is still turning
    monster,      // a monster's cycle, animation or turn toward its target is unfinished
    hit_sound,    // the latest hit sound has not completed
    spell_effect, // a spell effect has run out and is being retired
    missile,      // an engaged or spell missile is still in flight
    npc_script,   // a conscious NPC party slot's combat script is still queued
};

/* OnCollision produces HIT or DEFLECTED; attack setup also seeds the result
   when collision is bypassed. The shared combat-state field remains one byte. */
enum W8MissileHitResult : signed char {
    W8_MISSILE_HIT_NONE = 0,
    W8_MISSILE_HIT = 1,
    W8_MISSILE_HIT_DEFLECTED = 2
};

/* Per-hand best-outcome tracking inside a W8CombatCharacterRow, 0x10 bytes.
   The score is only overwritten when a swing resolves better than the stored
   one, at which point the hand's skills and the character's dual-wield flag
   are refreshed. */
struct W8CombatHandRecord {
    int score;
    W8Skill weapon_skill;
    W8Skill combat_skill;
    int dual_wielding;
};

/* One combat participant's row, one per character. */
enum W8CombatPortraitStatus : signed char {
    W8_COMBAT_PORTRAIT_EMPTY = -1,
    W8_COMBAT_PORTRAIT_READY = 0,
    W8_COMBAT_PORTRAIT_CANNOT_SWITCH = 1,
    W8_COMBAT_PORTRAIT_INELIGIBLE = 2,
    W8_COMBAT_PORTRAIT_ACTING = 3
};

struct W8CombatCharacterRow {
    unsigned int phase;
    bool dead;
    /* Historical offset 0x38: the two hand values GetCharacterTurnValue reuses once this row's
       turn is already set up. */
    int saved_attack_value[2];
    /* Historical offset 0x40: the same two per-hand reach values PrepareCharacterAttacks writes
       into saved_attack_value. */
    int hand_attack_values[2];
    /* Historical offset 0x48: per-hand best-outcome tracking; the score is only overwritten
       when a swing resolves better than the stored one, at which point the
       hand's skills and the character's dual-wield flag are refreshed. */
    W8CombatHandRecord hand_records[2];
    unsigned int uiSwingsRemaining;
    int current_hand;
    W8EquipSlot current_equip_slot;
    /* Historical offset 0x74: the paired weapon slot GetPairedEquipSlot answered for
       current_equip_slot, -1 when nothing is paired with it. */
    W8EquipSlot paired_equip_slot;
    /* Historical offset 0x78/0x7c: the item record indexes of the weapon in the attacking hand
       and of the paired weapon (the primary's own when nothing is paired). */
    int weapon_item_id;
    int paired_item_id;
    /* Historical offset 0x80: berserk latch - interrupt case 8 sets it; while set the slot
       retargets onto friends and skips the enemy-hostility bookkeeping.
       Same interrupt sets berserk on monsters. */
    bool berserk;
    /* Historical offset 0x81: toggled when the slot swaps to its alternate hand in PC Item;
       while set the pending hand-attack values are rebuilt. */
    bool alternate_hand;
    unsigned char extra_swings[2];
    /* Historical offset 0x84/0x88: the slot's combat-portrait catalog image and the alternate the
       combat portrait strip draws while the slot is the hovered combat slot
       (combat_action_hover_party_slot); -1 draws nothing. */
    int portrait_image;
    int portrait_image_alternate;
    /* Historical offset 0x8c: the slot's combat-strip status recomputed each combat-mode frame:
       -1 slot empty or out of the fight, 0 ready, 1 cannot switch to combat,
       2 dead or ineligible, 3 the acting combatant (portrait pulses). */
    W8CombatPortraitStatus combat_status;
    /* Historical offset 0x90: how many times the character already rolled to notice an attacker
       this round; the first attempt always succeeds and each later one is 25
       points harder on the senses check. */
    int spot_attempts;
    /* Historical offset 0x94: incremented when an out-of-combat action is repicked during
       combat. */
    unsigned int pending_action_repick_count;
    /* Historical offset 0x98/0x99: per-slot once-per-combat action-use flags read by
       CanPartySlotPray and CanPartySlotTurnUndead. */
    bool pray_used;
    bool turn_undead_used;
    /* Historical offset 0x9c: the combat clock value when CatchUpCombatActor last advanced this
       row's phase; the spell-scaling paths read it as the character's combat
       pace. */
    unsigned int phase_clock_stamp;
    /* Historical offset 0xa0: the round's interception count, checked against the guarding
       hand's attack count before another intercept is allowed and bumped on
       each successful one. */
    unsigned int interception_count;
    /* Historical offset 0xa4: the queued action just switched to DEFEND; case 4 of the
       action dispatch routes to the defend notice while set. */
    bool defend_switched;
    /* Historical offset 0xa5: the attack's sound/roll state; set once MakePCAttackSound has
       played so a resumed swing does not replay it, cleared when the row's
       attack finishes. */
    bool attack_sound_played;
    bool cheat_death_used;
    /* Historical offset 0xa7: SetCharacterCombatAction raises it when the slot's queued action
       changes while an action runs; committing the pending block clears it. */
    bool action_changed;
    /* Historical offset 0xa8: one byte per skill id recording defensive use this round. Attack
       init raises Shield (0x06), Locks & Traps (0x0b) and Reflextion (0x26)
       when the target has the skill trained; the round-end pass awards
       practice credit to exactly those three entries and clears them. */
    unsigned char skill_use_flags[0x29];
};

/* One fight, owned by its game (`Game::combat`) from the moment it starts until it
   ends. Nothing outlives it: the next fight is a new value. */
struct Combat {
    /* Historical offset 0x000: raised by BeginCombatExecution and cleared at the round
       boundary. Gates actor scheduling; not the end-of-combat flag. */
    unsigned char execution_active;
    /* Historical offset 0x001: set when combat begins and when continuous combat resumes;
       cleared at the round boundary while continuous_combat is off. Gates
       party movement and the combat-sensitive UI panels. */
    bool round_active;
    /* Historical offset 0x004: current round number - incremented at each round boundary,
       shown in the round notices and gating the round-one specials. */
    unsigned int round_count;
    unsigned int round_counter;
    /* Historical offset 0x00c: the combat outcome the end-of-combat pass reports - zero while no
       result is recorded, otherwise the kill count formatted next to
       "kill"/"kills". */
    int combat_result;
    /* Historical offset 0x010: experience from kills, divided among the active party members
       at combat end. */
    int experience_pool;
    /* Historical offset 0x014: flat bonus experience accumulated by hostility events, added to
       the pool at award time. */
    int experience_bonus;
    W8CombatCharacterRow characters[8];
    /* Historical offset 0x6b8: the attack announcement the monster-attack message builder
       sprintf's into and ShowNotice displays; 0x78 wide chars. */
    std::string attack_message;
    /* The step runs these countdowns down by its elapsed time (`passTime`); one at zero
       is due. `continuous_start_delay` holds continuous combat back after the fight
       starts; `action_delay` paces the scheduled actor, whose action starts when it is
       due; `party_movement_delay` paces the synthetic progress of a continuous-combat
       move while the world stands still. */
    std::chrono::milliseconds continuous_start_delay{};
    std::chrono::milliseconds action_delay{};
    std::chrono::milliseconds party_movement_delay{};
    W8EffectSlot effect_slots[9];
    W8EffectSlot effect_slots0[6];
    Missile* engaged_missile;
    /* Historical offset 0x8c4: staged hit result of the in-flight missile (0 = pending, 1 = hit, 2 = deflected) */
    W8MissileHitResult missile_hit_result;
    W8CombatSlot TargetHit;
    int pending_death_count;
    W8PartyAction uiNextPartyAction;
    W8PartyAction uiCurrentPartyAction;
    unsigned int uiPartyActionPhase;
    W8PartyActionStatus uiCurrentPartyActionStatus;
    /* How much of the party's move is left, in percent. It falls as the party walks, and
       when the move ends it decides which members still act this round. */
    int party_move_percent = 0;
    W8PartyFormationState saved_formation;
    /* Historical offset 0x9a4: the running attack's target did not see it coming - the monster
       stands behind the character or the target monster is looking away. The
       announcement appends the caught-unaware text. */
    unsigned char unaware;
    /* Historical offset 0x9a5: the running attack is a kind-3 monster's natural attack inside
       short range, so the announcement skips the weapon-name string and uses
       the natural-attack verbs instead. */
    unsigned char natural_attack;
    /* Historical offset 0x9a6: the running attack's outcome record, cleared to a fresh 0xa2-byte
       block before each attack resolves. The swing and hit tallies feed the
       "hit once"/"hit %d of %d" notices, one flag per condition records what
       the swings inflicted (index 0x12 doubles as the killed gate), the report
       list is a W8SpellDamageReport* vector drained front-first, and the six
       notice values carry the amounts the notices print - [3] and [4]
       are the running damage totals the character/monster damage paths add to. */
    W8SpellEffectResult attack_report;
    /* Historical offset 0xa48: the once-per-combat difficulty evaluation has run; the update
       tick calls the evaluator on the first frame it sees this clear. */
    unsigned char combat_evaluated;
    /* Historical offset 0xa4c: the in-flight breath visual for a character's special-attack
       action. The executor refuses while its `finished` flag is clear and
       hands the previous one to the world updater through `auto_release`. */
    SpellVisual* breath_visual;
    /* Historical offset 0xa50: combat is in the equip/resolve phase the review screens key
       their captions and item actions on. */
    W8CombatEquipPhase equip_phase = W8CombatEquipPhase::action_selection;
    /* Historical offset 0xa51: how many party slots still have a pending equip action. */
    unsigned char equip_pending;
    /* Historical offset 0xa52/0xa53: the side is surprised and cannot act this round - +0xa52
       gates the party slots and the neutral/friendly monsters, +0xa53 the
       hostile ones; the surprise roll at combat start sets them, mutual
       surprise cancels both, and the round-end pass clears them. */
    bool party_surprised;
    bool monsters_surprised;
    /* Historical offset 0xa54: hostile monsters are engaged in this combat; latched at start
       from current roster threats and re-raised by hostility events. */
    bool enemies_engaged;
    /* Historical offset 0xa55: no aggressive action has resolved this round; the round-end
       pass counts it toward unengaged_rounds. */
    unsigned char passive_round;
    /* Historical offset 0xa56: consecutive rounds that ended with an engaged flag still set but
       no monster group able to engage; the combat-over check reads it. */
    unsigned char unengaged_rounds;
    bool notice_scroll_pending;
    /* Historical offset 0xa58: queued refusal script for NPC party slots 0 and 1, set after
       queuing events 0x3a and 0x36; event 0x36 clears it. */
    bool npc_combat_script_pending[2];
    /* Historical offset 0xa5c: the combat updates elapsed; the engagement sweep waits for the
       third before it touches group states. */
    unsigned int combat_update_count;
    /* Historical offset 0xa60: the scheduler's pacing latch - armed by
       BeginCombatExecution, it suppresses a second action delay for
       a monster's turn and caps the armed delay at 800 ms. */
    bool pacing_latch;
    bool combat_ready;

    /* Run the countdowns down by the time the step passed. */
    void passTime(std::chrono::milliseconds elapsed)
    {
        const auto run_down = [elapsed](std::chrono::milliseconds& countdown) {
            countdown = std::max(countdown - elapsed, std::chrono::milliseconds::zero());
        };
        run_down(continuous_start_delay);
        run_down(action_delay);
        run_down(party_movement_delay);
    }

    const CombatTurn& turn() const { return turn_; }
    /* The actor holding the turn, null while it is idle. */
    const CombatActor* actor() const
    {
        return std::visit(
            []<typename Phase>(const Phase& phase) -> const CombatActor* {
                if constexpr (std::is_same_v<Phase, TurnIdle>) {
                    return nullptr;
                } else {
                    return &phase.actor;
                }
            },
            turn_);
    }
    bool holdsTurn(const CombatActor& actor) const
    {
        const CombatActor* current = this->actor();
        return current && *current == actor;
    }
    bool resolvingTurnOf(const CombatActor& actor) const
    {
        const auto* resolving = std::get_if<TurnResolving>(&turn_);
        return resolving && resolving->actor == actor;
    }
    std::optional<int> actingSlot() const
    {
        const CombatActor* current = actor();
        const auto* party = current ? std::get_if<PartyActor>(current) : nullptr;
        return party ? std::optional{party->slot} : std::nullopt;
    }
    /* The acting monster's location id. */
    std::optional<int> actingMonster() const
    {
        const CombatActor* current = actor();
        const auto* monster = current ? std::get_if<MonsterActor>(current) : nullptr;
        return monster ? std::optional{monster->location_id} : std::nullopt;
    }
    void schedule(CombatActor actor) { turn_ = TurnScheduled{actor}; }
    /* Move the actor's turn to the step's phase. A turn the actor no longer
       holds - ended while its action ran - is left as it is. */
    void setStepOf(CombatActor actor, ActionStep step)
    {
        if (!holdsTurn(actor)) {
            return;
        }
        if (step == ActionStep::resolving) {
            turn_ = TurnResolving{actor};
        } else {
            turn_ = TurnFinishing{actor};
        }
    }
    void endTurnOf(CombatActor actor)
    {
        if (holdsTurn(actor)) {
            turn_ = TurnIdle{};
        }
    }

    unsigned int phaseAtStep(unsigned int phase, unsigned int base) const;
    void catchUpCharacter(unsigned int slot);
    void beginReaction(unsigned int slot, bool fallback_defend)
    {
        characters[slot].dead = true;
        if (fallback_defend) characters[slot].defend_switched = true;
    }

    // Retire participation and scheduling together, without owning the party character.
    void retirePartyMember(unsigned int slot)
    {
        ++pending_death_count;
        endTurnOf(PartyActor{static_cast<int>(slot)});
        characters[slot].phase = 0;
        characters[slot].dead = true;
        characters[slot].action_changed = true;
    }

    /* One step of the fight (the game's step calls it while the world runs and nothing
       holds the turn): wait for what the turn waits on, start the round when continuous
       combat or a surprise allows, and move the turn through its phases. The fight may
       end inside it. */
    void step(Game& game);
    /* The first thing the turn waits on. */
    CombatWait waitingOn(const Game& game) const;
    // Failed playback leaves the previous successful hit sound in charge of the wait.
    void waitForHitSound(wiz8::sound::Playback sound);
    /* Pick the next party slot or monster whose action phase arrived, start
       party-movement phases, arm the action delay, and roll the round counter forward
       until someone can act or the round ends. */
    void scheduleNextActor(Game& game);

    /* Run a monster's committed action: the monster-side counterpart of
       ExecuteCharacterAction. */
    void executeMonsterAction(Game& game, W8MonsterInfo& monster);
    /* Step the acting monster's resolving action and move its turn to what it reports. */
    void stepMonsterAction(Game& game, W8MonsterInfo& monster);
    /* Start a party slot's breath: face the target and build the breath visual. */
    bool startCharacterBreath(Game& game, int party_slot);
    /* Release a party slot's breath once its visual has played: resolve it against the
       target markers and every hostile monster. */
    ActionStep releaseCharacterBreath(Game& game, int party_slot);
    /* Pick the hand a party slot's attack lands with this round and derive its next
       action phase from the stored attack values. */
    void settleCharacterPhase(Game& game, int party_slot);

private:
    void beginScheduledAction(Game& game, const CombatActor& actor);
    void stepResolvingAction(Game& game, const CombatActor& actor);
    /* Settle a finished action: a party slot falls back to its remembered combat choice
       and takes its next phase; a monster pays the fatigue and may queue another attack
       this round. The turn becomes idle. */
    void finishAction(Game& game, const CombatActor& actor);

    // Keep the latest sound identity; completion belongs to the audio service.
    wiz8::sound::Playback hit_sound_;
    CombatTurn turn_ = TurnIdle{};
};
