#ifndef WIZ8_LOCAL_CODE_MAGIC_H
#define WIZ8_LOCAL_CODE_MAGIC_H

#include <cstdint>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/character_skills.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"

enum class W8SpellCastKind : unsigned char {
    Standard = 0,
    ItemUse = 1,
    MusicItemUse = 2,
    EngineeringItemUse = 3,
    ThrowingItemUse = 4
};

struct W8ItemInstance;
struct W8MonsterInfo;
class Missile;
struct W8SpellEffectEntry;

extern int g_learn_sound;

W8TargetNeed GetTargetNeededForSpellFriendly(int spell_id, bool normalize, bool needs_item_target);
W8TargetNeed GetTargetNeededForSpellHostile(int spell_id);

/* Learned, and the remaining points in the spell's realm cover its cost.
   CharacterHasCastableSpell expands this body for every spell. */
inline bool CanCharacterCastSpell(W8Character* character, int spell_id)
{
    if (spell_id != W8_SPELL_NONE && character->spell_learned[spell_id] == 1 &&
        Data().spells[spell_id].spell_point_cost <=
            character->iSPLeft[Data().spells[spell_id].realm]) {
        return true;
    }
    return false;
}
unsigned int GetSpellCastingSkillLevel(const W8Character* character, W8Skill spellbook_skill,
                                       W8SpellRealm realm);

int GetProfessionCasterLevel(const W8Character* character, W8Profession profession_id);
extern unsigned char g_profession_spellbooks[W8_PROFESSION_COUNT];
/* The spell a missile type carries, or W8_SPELL_NONE. */
int MissileSpellId(int missile_type);

/* One-line forwarder narrowing CanCharReBreathe to a flag. */
bool CanPartySlotReBreathe(int party_slot);
/* Sprite bands: automatic power, then failure chance >40, <=40, <=15, <=5 and zero. */
enum W8SpellCastRating {
    W8_CAST_RATING_AUTOMATIC = 0,
    W8_CAST_RATING_HIGH_RISK = 1,
    W8_CAST_RATING_MODERATE_RISK = 2,
    W8_CAST_RATING_LOW_RISK = 3,
    W8_CAST_RATING_MINIMAL_RISK = 4,
    W8_CAST_RATING_NO_FAILURE = 5
};

bool IsTeleportCastMissingAnchor(W8Character* character, int spell_id);
extern unsigned short g_realm_message_offsets[W8_SPELL_REALM_COUNT];

class Game;

/* One game's spell casting: choosing and validating spells and targets, casting them
   from characters, monsters and items, and running the effects still taking effect.
   The game owns it. */
class Spellcasting {
public:
    explicit Spellcasting(Game& game) : game_(game) {}
    Spellcasting(const Spellcasting&) = delete;
    Spellcasting& operator=(const Spellcasting&) = delete;
    void tickSpellEffects();

    /* Whether the spell may be cast in the current situation. */
    bool spellUsableNow(int spell_id, bool allow_out_of_combat);

    /* Whether the monster's spellcasting-blocked condition stops it
       casting this spell - everything except alchemy, and alchemy only for the
       monster kinds that keep it. */
    bool isSpellBlockedForMonster(W8MonsterInfo* monster_info, int spell_id);

    /* Everything that has to hold before a monster may start
       casting. The power level is unused. */
    bool monsterOKToCastSpell(W8MonsterInfo* monster_info, int spell_id, int power_level);
    unsigned int monsterCastsSpell(W8MonsterInfo* monster_info, int spell_id,
                                   unsigned int power_level);

    /* The power level the monster can afford for the spell, out of
       its database base plus its runtime bonus. */
    unsigned int chooseMonsterSpellPowerLevel(W8MonsterInfo* monster_info,
                                              const W8MonsterRecord* record, int spell_id);

    /* One physical callable for queued and repeated casts; returns
       0/1/2 and writes the per-step point cost. */
    int executeCharacterSpellCast(int party_slot, int spell_id, unsigned int power_level,
                                  int* out_points, bool continue_cast);

    /* A backfiring spell swaps roles - the intended target becomes
       the source and the original source the target. */
    void redirectBackfiredSpellTarget(W8TargetSource* source, W8CombatSlot* target);

    /* Pick one random in-combat participant other than the source
       and write it as the backfired spell's new target. */
    int pickBackfireTarget(int spell_id, W8TargetSource* source, W8CombatSlot* target);

    /* Scatter a backfired point target to a random reachable spot
       inside the spell's range around its source. */
    void scatterSpellPointTarget(int spell_id, W8TargetSource* source, W8CombatSlot* target);

    /* The once-per-cast backfire roll for single-target spells -
       condition 0x0c gates it, a trait save can avoid it. */
    void checkSpellBackfire(int spell_id, W8TargetSource* source, W8CombatSlot* target);
    void setPartySlotSpell(int party_slot, int spell_id, int power_level,
                           const W8CombatSlot* target);
    int pointCastSpell(glm::vec3 position, int spell_id, unsigned int power_level);
    bool canCastFromItem(const W8Character* caster, const W8ItemInstance* item);
    bool canCharacterLearnSpell(W8Character* character, int spell_id);
    void learnSpell(W8Character* character, int spell_id, bool announce);

    /* Learns the spell a spell-source item holds and empties the
       item. */
    void learnSpellFromItem(W8Character* character, W8ItemLocation location);
    W8Skill getBestSpellbookSkillForSpell(W8Character* character, int spell_id, bool pricing,
                                          bool prefer_unlocked, unsigned int power_level);
    int castSpellFromSource(int spell_id, W8TargetSource* source, W8CombatSlot* target,
                            unsigned int power_level, int power_cast_bonus,
                            unsigned int failure_chance, bool recast, int* out_power,
                            W8SpellCastKind cast_kind, std::vector<int>* party_targets,
                            std::vector<int>* monster_targets);

    /* Assert and route the source/target pair a cast is about to
       use; some target kinds have their own placement pass. */
    void prepareSpellTarget(int spell_id, W8TargetSource* source, W8CombatSlot* target);

    /* Resolve valid spell targets for one cast and append location ids to the
       caller's vectors. The party and monster vectors are separate outputs; the
       sight rule explicitly chooses geometry-only preview or line-of-sight
       eligibility. The query never changes presentation. */
    void populateSpellTargetMarkers(int spell_id, int power_level, W8TargetSource* source,
                                    W8CombatSlot* target, std::vector<int>& monster_markers,
                                    std::vector<int>& party_markers, W8AreaSightRule sight_rule);

    /* Drop every marker that no longer names a live, targetable
       monster; a few spell ids prune on extra monster-record rules. */
    void pruneSpellTargetMarkers(int spell_id, std::vector<int>& monster_markers);
    bool partyHasCondition(int effect_id) const;
    bool combatHasCondition(int effect_id) const;
    int getSpellDifficulty(unsigned int caster_figure, int spell_id, int bonus);

    /* Whether a spellcasting-blocked condition stops this character
       casting this spell. */
    bool isSpellBlockedForCharacter(const W8Character* character, int spell_id);

    /* Records the character's chosen spell and power level for the
       pending action. */
    void setCharacterSpell(const W8Character* character, const W8Action& spell);

    /* The power level the party slot's chosen spell can actually be
       cast at; zero means the cast cannot happen at all. */
    int getAffordableSpellPowerLevel(int party_slot);
    bool canPartySlotCastRecordedSpell(int party_slot);
    bool canPartySlotUseRecordedItem(int party_slot);

    /* Queue the slot's recorded spell cast; zero keeps the recorded power. */
    void startCharacterSpellCast(int party_slot, int power_level);
    void startCharacterItemUse(int party_slot);
    void startCharacterBreathAttack(int party_slot);

    /* The spell screen's one-to-five safety rating for one cast at a
       power level; zero for the as-affordable request. */
    W8SpellCastRating getSpellCastRating(W8Character* character, int spell_id,
                                         unsigned int power_level);

    /* The same chance for a bare skill figure rather than a caster,
       which is what an item-use attempt has. */
    unsigned int getSpellFailureChance(unsigned int skill, int spell_id, int factor);

    /* The combat-pace scale an item-use attempt applies to its own difficulty. */
    void scaleByCombatPace(int party_slot, unsigned int* value);

    /* Validate the available target set and cursor state for a spell or item cast. */
    bool validateSpellTarget(int party_slot, int spell_id, unsigned int power, bool item_cast,
                             bool skip_world_cursor);
    bool spellAffectedTarget(int spell_id, const W8CombatSlot* aim, unsigned int power,
                             std::uint64_t now);
    bool attemptSpellPractice(int spell_id, const W8CombatSlot* aim, unsigned int power,
                              std::uint64_t now);
    void trackItemSpellSource(W8Character* character, int spell_id);
    void detachMissileReferences(Missile* missile);
    bool allSpellEffectsStillRunning() const;
    W8SpellEffectEntry* findMonsterControlSpellEffect() const;

    /* Advance every queued spell effect one frame. */
    void updateSpellEffects();
    void reportSpellResult(W8SpellEffectEntry* effect);
    void spawnLureEffects(W8SpellEffectEntry* owner, int argument, const W8CombatSlot* target);

private:
    std::uint64_t* spellPracticeCooldown(int spell_id);
    std::string spellTargetString(const W8TargetSource* source, const W8CombatSlot* target);
    unsigned int chooseSpellPowerLevelForTarget(W8Character* caster, int spell_id,
                                                const W8CombatSlot& target, int identify_context);
    unsigned int choosePowerLevelToRestore(W8Character* character, int spell_id,
                                           const W8Character* target);
    unsigned int choosePowerLevelForDuration(W8Character* character, int spell_id,
                                             unsigned int turns_needed);
    unsigned int getCastFailureChance(W8Character* character, int spell_id,
                                      unsigned int power_level);
    unsigned char spellbookMaskForSpell(int spell_id);
    void finishSpellEffect(W8SpellEffectEntry* effect);
    void releaseHypnoticLureControl();

    Game& game_;
};

#endif
