#ifndef WIZ8_LAYOUTS_TARGETING_H
#define WIZ8_LAYOUTS_TARGETING_H

#include <cstdint>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/item_location.h"

struct W8ItemInstance;
struct W8MonsterInfo;
struct W8MonsterGroup;

/* The source-kind domain. Zero is the empty source, one a character, two a
   monster, and three a source that keeps the original's character or monster
   id while reading as a point - which is what a backfire or a reflection
   produces, and what the two flags distinguish. This is a different domain
   from W8TargetKind even though both name a leading field. */
enum W8TargetSourceKind : std::uint8_t {
    W8_TARGET_SOURCE_NONE = 0,
    W8_TARGET_SOURCE_CHARACTER = 1,
    W8_TARGET_SOURCE_MONSTER = 2,
    W8_TARGET_SOURCE_INDIRECT = 3,
    W8_TARGET_SOURCE_COUNT = 4 /* SOURCE_TYPE_COUNT in PrepareSpellTarget's assert */
};

struct W8TargetSource {
    W8TargetSourceKind iType;
    int iChar = -1;
    int iMonsterID = -1;
    /* The world point for a source that is a place rather than somebody. */
    glm::vec3 point;
    /* System-driven cast (retaliation/AI/trap): skips the backfire check and
       counts as already reported. */
    bool auto_cast;
    bool name_known;
    /* The cast's aim was already resolved; MonsterCanAimSpell retargeting is
       skipped. */
    bool aim_resolved;
    bool fReflection;
    bool fBackfire;
    /* The cast's source was resolved to a point, not a creature; missile and
       spell paths then skip the monster's spell vertex. */
    bool point_source;
    /* The attack's target ended up different from the one the slot row aimed
       at - a fumble reroll or a guardian interception replaced it. */
    bool target_diverted;
    /* The precomputed cast difficulty item/tracked sources carry; the spell
       engine reads it back when name_known skips recomputation. */
    unsigned char spell_difficulty;
    unsigned char unknown_20;
    /* Set on sources the item-spell path builds. */
    bool item_cast;
    unsigned char unknown_22[0x12];
};

/* The target-kind domain a combat slot's leading field takes. The kinds that
   name something put it in their own field, which is what pairs each kind with
   the field the aiming wrappers fill in. Kind five selects all enemies;
   kind eight's complete consumer role remains unresolved. This is a different domain from
   W8TargetSourceKind even though both name a leading field. */
enum W8TargetKind : std::uint8_t {
    W8_TARGET_KIND_NONE = 0,
    W8_TARGET_KIND_CHARACTER = 1,
    W8_TARGET_KIND_PARTY = 2,
    W8_TARGET_KIND_MONSTER = 3,
    W8_TARGET_KIND_GROUP = 4,
    W8_TARGET_KIND_ALL_ENEMIES = 5,
    W8_TARGET_KIND_PLACE = 6,
    W8_TARGET_KIND_ITEM = 7,
    W8_TARGET_KIND_EIGHT = 8,
    W8_TARGET_KIND_CHARACTER_INDIRECT = 9,
    W8_TARGET_KIND_COUNT = 10 /* TARGET_TYPE_COUNT in PrepareSpellTarget's assert */
};

/* What a spell or action asks the interface to select, independently of the
   recorded target's W8TargetKind. INDIRECT uses the character-indirect target for spell 0x58; UNSET is the
   temporary value used before a combat action supplies its requirement. */
enum W8TargetNeed : std::int8_t {
    W8_TARGET_NEED_UNSET = -1,
    W8_TARGET_NEED_NONE = 0,
    W8_TARGET_NEED_ALLY = 1,
    W8_TARGET_NEED_ENEMY = 2,
    W8_TARGET_NEED_PLACE = 3,
    W8_TARGET_NEED_CONE = 4,
    W8_TARGET_NEED_GROUP = 5,
    W8_TARGET_NEED_ITEM = 6,
    W8_TARGET_NEED_CHARACTER_INDIRECT = 7,
    W8_TARGET_NEED_CASTER = 8
};

/* The shorter form a combatant carries inline, with one more field reset to
   -1 and no room for the tail. Its disk form is wiz8::retail::CombatTarget. */
struct W8CombatSlot {
    W8TargetKind iType;
    int iChar;      /* -1 when empty */
    int iMonsterID; /* -1 when empty */
    int iGroupID;   /* -1 when empty */
    glm::vec3 point;
    /* The item aimed at, for the one kind that aims at one. */
    W8ItemLocation item;

    bool operator==(const W8CombatSlot&) const = default;
};
/* Party movement is distinct from an individual character's action. */
enum W8PartyAction : std::uint8_t {
    W8_PARTY_ACTION_NONE = 0,
    W8_PARTY_ACTION_WALK = 1,
    W8_PARTY_ACTION_RUN = 2
};

/* Movement starts at the assigned initiative phase, runs its movement budget,
   ends the movement phase, then releases the remaining turns in the free phase. */
enum W8PartyActionStatus : std::uint8_t {
    W8_PARTY_ACTION_NOT_STARTED = 0,
    W8_PARTY_ACTION_IN_PROGRESS = 1,
    W8_PARTY_ACTION_PHASE_ENDED = 2,
    W8_PARTY_ACTION_FINISHED = 3
};

/* Action-specific inputs, independent of their two-word retail encoding.
   Item relationships are locations resolved against the operation's party. */
struct W8ActionDetailBlock {
    struct {
        int power_level;
        int unused;
    } spell{};
    struct {
        int kind;
        W8ItemLocation item;
    } item_use{};
};

/* The combat actions a party slot row's action and the level block's
   selection_kind carry; SelectCombatAction picks one and ChooseAction applies
   it. The names are the submenu help captions (string ids 0x52-0x5f): a menu-0
   entry's caption is the action its row selects. 10 and 11 are the two party
   move kinds, which ApplyPartyCombatAction routes away from the action
   record. */
enum W8ActionKind {
    W8_ACTION_NONE = -1,
    W8_ACTION_ATTACK = 0,
    W8_ACTION_BERSERK = 1,
    W8_ACTION_BREATHE = 2,
    W8_ACTION_TURN_UNDEAD = 3,
    W8_ACTION_DEFEND = 4,
    W8_ACTION_PROTECT = 5,
    W8_ACTION_PRAY = 6,
    W8_ACTION_CAST_SPELL = 7,
    W8_ACTION_USE_ITEM = 8,
    W8_ACTION_EQUIP = 9,
    W8_ACTION_WALK = 10,
    W8_ACTION_RUN = 11
};

// An action snapshot owns its contents. Assessment cannot edit the controller's
// pending, committed or remembered choice through this value.
struct W8Action {
    W8ActionKind kind = W8_ACTION_NONE;
    int detail = -1;
    W8CombatSlot target{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    W8ActionDetailBlock detail_block{.item_use = {-1, {}}};
};

enum class W8AreaSightRule : std::uint8_t { RequireLineOfSight, GeometryOnly };

enum class W8TargetFailure : std::uint8_t { None, Unavailable, OutOfRange, SightBlocked };
struct W8TargetAssessment {
    W8TargetFailure failure = W8TargetFailure::None;
    operator bool() const { return failure == W8TargetFailure::None; }
};

// Action storage choices used at operation boundaries; algorithms receive the selection.
enum W8TargetingContext : std::uint8_t {
    W8_TARGETING_CONTEXT_OUT_OF_COMBAT = 0,
    W8_TARGETING_CONTEXT_IN_COMBAT = 1,
    W8_TARGETING_CONTEXT_SHARED = 2,
    W8_TARGETING_CONTEXT_SPELL = 3,
    W8_TARGETING_CONTEXT_ITEM = 4,
    W8_TARGETING_CONTEXT_BREATH = 5,
    W8_TARGETING_CONTEXT_DIALOGUE = 7,
    W8_TARGETING_CONTEXT_ATTACK = 8 /* fixed basic attack, without a target/detail block */
};
#endif
