#include <string>

#include "wiz8/audio_resources.h"
#ifndef WIZ8_LAYOUTS_COMBAT_STATE_H
#define WIZ8_LAYOUTS_COMBAT_STATE_H

#include <algorithm>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include "wiz8/character_skills.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/targeting.h"

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

/* The combat state, allocated when combat starts. */
struct W8CombatState {
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
    /* Historical offset 0x7a8: continuous-combat UI pacing; the confirm button resets it while
       TimeUntilDeadline reports it still running. */
    uint64_t combat_ui_timer;
    /* Historical offset 0x7ac: the pacing clock the scheduler arms as a w8_get_ticks deadline
       before the scheduled actor's action may execute. */
    uint64_t action_clock;
    wiz8::sound::Playback hit_sound;
    bool hit_sound_active;
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
    /* Historical offset 0x91c: countdown used to pace synthetic movement progress when
       continuous combat is enabled and the world did not advance this frame. */
    uint64_t party_movement_clock;
    /* How much of the party's move is left, in percent. It falls as the party walks, and
       when the move ends it decides which members still act this round. */
    int party_move_percent = 0;
    /* The player aimed a member at a target this round, so the selection stays on that
       member instead of auto-advancing. Cleared when an action changes and as the round
       executes. */
    bool target_picked = false;
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
    /* Historical offset 0xa61: remembered search-mode state; the combat teardown toggles search
       mode back on when it reads nonzero. */
    bool search_mode_saved;
    bool combat_ready;

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

private:
    CombatTurn turn_ = TurnIdle{};
};

// Engine participation starts before the gameplay mode and ends after restoration.
// The state remains available throughout restoration.
class W8CombatLifecycle {
public:
    bool Start(int surprise);
    void End(bool forced_cleanup);
    bool active() const { return phase_ == Phase::Active; }
    bool engineActive() const { return phase_ != Phase::Idle; }
    W8CombatState* state() const { return state_.get(); }

private:
    friend struct ScopedCombat;
    enum class Phase { Idle, Entering, Active, Restoring };
    Phase phase_ = Phase::Idle;
    std::unique_ptr<W8CombatState> state_;
};

#endif
