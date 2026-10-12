#ifndef WIZ8_LOCAL_CODE_MAGIC_H
#define WIZ8_LOCAL_CODE_MAGIC_H

#include <cstdint>
#include <vector>

#include "wiz8/character_skills.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/character.h"
#include "surrender/srMath.h"
#include "spell_ids.h"

struct W8ItemInstance;
struct W8MonsterInfo;
class W8Missile;

void TickSpellEffects(void);

extern std::int32_t g_learn_sound;

/* Whether the spell may be cast in the current situation. */
bool SpellUsableNow(std::int32_t spell_id, bool allow_out_of_combat);

W8TargetNeed GetTargetNeededForSpellFriendly(std::int32_t spell_id, bool normalize,
                                             W8TargetingContext context);
W8TargetNeed GetTargetNeededForSpellHostile(std::int32_t spell_id);
/* Whether the monster's spellcasting-blocked condition stops it
   casting this spell - everything except alchemy, and alchemy only for the
   monster kinds that keep it. */
bool IsSpellBlockedForMonster(W8MonsterInfo* monster_info, std::int32_t spell_id);
/* Everything that has to hold before a monster may start
   casting. The power level is unused. */
bool MonsterOKToCastSpell(W8MonsterInfo* monster_info, std::int32_t spell_id, std::int32_t power_level);
std::uint32_t MonsterCastsSpell(W8MonsterInfo* monster_info, std::int32_t spell_id, std::uint32_t power_level);
/* The power level the monster can afford for the spell, out of
   its database base plus its runtime bonus. */
std::uint32_t ChooseMonsterSpellPowerLevel(W8MonsterInfo* monster_info, W8MonsterRecord* record,
                                          std::int32_t spell_id);
/* One physical callable for queued and repeated casts; returns
   0/1/2 and writes the per-step point cost. */
std::int32_t ExecuteCharacterSpellCast(std::int32_t party_slot, std::int32_t spell_id, std::uint32_t power_level,
                              std::int32_t* out_points, bool continue_cast);
/* A backfiring spell swaps roles - the intended target becomes
   the source and the original source the target. */
void RedirectBackfiredSpellTarget(W8TargetSource* source, W8CombatSlot* target);
/* Pick one random in-combat participant other than the source
   and write it as the backfired spell's new target. */
std::int32_t PickBackfireTarget(std::int32_t spell_id, W8TargetSource* source, W8CombatSlot* target);
/* Scatter a backfired point target to a random reachable spot
   inside the spell's range around its source. */
void ScatterSpellPointTarget(std::int32_t spell_id, W8TargetSource* source, W8CombatSlot* target);
/* The once-per-cast backfire roll for single-target spells -
   condition 0x0c gates it, a trait save can avoid it. */
void CheckSpellBackfire(std::int32_t spell_id, W8TargetSource* source, W8CombatSlot* target);
void SetPartySlotSpell(std::int32_t party_slot, std::int32_t spell_id, std::int32_t power_level, const W8CombatSlot* target);
std::int32_t PointCastSpell(srVector3T<float> position, std::int32_t spell_id, std::uint32_t power_level);

bool CanCastFromItem(const W8Character* caster, const W8ItemInstance* item);

bool CanCharacterLearnSpell(W8Character* character, std::int32_t spell_id);
void LearnSpell(W8Character* character, std::int32_t spell_id, bool announce);
/* Learns the spell a spell-source item holds and empties the
   item. */
void LearnSpellFromItem(W8Character* character, W8ItemInstance* item);

/* Zeroes the six per-realm learned-spell counters and recounts them from the
   spell_learned array. */
void RecountLearnedSpellsByRealm(W8Character* character);
/* Learned, and the remaining points in the spell's realm cover its cost.
   CharacterHasCastableSpell expands this body for every spell. */
// FUNCTION: WIZ8 0x004f9750
inline bool CanCharacterCastSpell(W8Character* character, std::int32_t spell_id)
{
    if (spell_id != W8_SPELL_NONE && character->spell_learned[spell_id] == 1 &&
        g_spell_records[spell_id].spell_point_cost <=
            character->iSPLeft[g_spell_records[spell_id].realm]) {
        return true;
    }
    return false;
}
std::uint32_t GetSpellCastingSkillLevel(const W8Character* character, W8Skill spellbook_skill,
                                       W8SpellRealm realm);
W8Skill GetBestSpellbookSkillForSpell(W8Character* character, std::int32_t spell_id, bool pricing,
                                      bool prefer_unlocked, std::uint32_t power_level);

std::int32_t CastSpellFromSource(std::int32_t spell_id, W8TargetSource* source, W8CombatSlot* target,
                        std::uint32_t power_level, std::int32_t power_cast_bonus, std::uint32_t failure_chance,
                        bool recast, std::int32_t* out_power, std::int32_t cast_kind, std::vector<std::int32_t>* party_targets,
                        std::vector<std::int32_t>* monster_targets);
/* Assert and route the source/target pair a cast is about to
   use; some target kinds have their own placement pass. */
void PrepareSpellTarget(std::int32_t spell_id, W8TargetSource* source, W8CombatSlot* target);
/* Resolve valid spell targets for one cast and append location ids to the
   caller's vectors. The party and monster vectors are separate outputs; the
   final flag enables radius highlighting rather than plain line-of-sight
   collection. */
void PopulateSpellTargetMarkers(std::int32_t spell_id, std::int32_t power_level, W8TargetSource* source,
                                W8CombatSlot* target, std::vector<std::int32_t>& monster_markers,
                                std::vector<std::int32_t>& party_markers, std::int32_t highlighting);
/* Drop every marker that no longer names a live, targetable
   monster; a few spell ids prune on extra monster-record rules. */
void PruneSpellTargetMarkers(std::int32_t spell_id, std::vector<std::int32_t>& monster_markers);
std::int32_t GetProfessionCasterLevel(const W8Character* character, W8Profession profession_id);
/* The highest power level this slot can afford to cast the spell
   at for its current target; zero when none is castable. */
std::uint32_t ChooseSpellPowerLevelForTarget(std::int32_t party_slot, std::int32_t spell_id, std::int32_t identify_context);
extern std::uint8_t g_profession_spellbooks[W8_PROFESSION_COUNT];
/* The spell a missile type carries, or W8_SPELL_NONE. */
std::int32_t MissileSpellId(std::int32_t missile_type);
bool PartyHasCondition(std::int32_t effect_id);
bool CombatHasCondition(std::int32_t effect_id);
std::int32_t GetSpellDifficulty(std::uint32_t caster_figure, std::int32_t spell_id, std::int32_t bonus);

/* Whether a spellcasting-blocked condition stops this character
   casting this spell. */
bool IsSpellBlockedForCharacter(const W8Character* character, std::int32_t spell_id);
/* Records the character's chosen spell and power level for the
   pending action. */
void SetCharacterSpell(const W8Character* character, std::int32_t spell_id, std::int32_t power_level);
/* The power level the party slot's chosen spell can actually be
   cast at; zero means the cast cannot happen at all. */
std::int32_t GetAffordableSpellPowerLevel(std::int32_t party_slot);
bool CanPartySlotCastRecordedSpell(std::int32_t party_slot);
bool CanPartySlotUseRecordedItem(std::int32_t party_slot);
/* Queue the slot's recorded spell cast; zero keeps the recorded power. */
void StartCharacterSpellCast(std::int32_t party_slot, std::int32_t power_level);
void StartCharacterItemUse(std::int32_t party_slot);
/* One-line forwarder narrowing CanCharReBreathe to a flag. */
bool CanPartySlotReBreathe(std::int32_t party_slot);
void StartCharacterBreathAttack(std::int32_t party_slot);
/* Sprite bands: automatic power, then failure chance >40, <=40, <=15, <=5 and zero. */
enum W8SpellCastRating {
    W8_CAST_RATING_AUTOMATIC = 0,
    W8_CAST_RATING_HIGH_RISK = 1,
    W8_CAST_RATING_MODERATE_RISK = 2,
    W8_CAST_RATING_LOW_RISK = 3,
    W8_CAST_RATING_MINIMAL_RISK = 4,
    W8_CAST_RATING_NO_FAILURE = 5
};

/* The spell screen's one-to-five safety rating for one cast at a
   power level; zero for the as-affordable request. */
W8SpellCastRating GetSpellCastRating(W8Character* character, std::int32_t spell_id,
                                     std::uint32_t power_level);
/* The same chance for a bare skill figure rather than a caster,
   which is what an item-use attempt has. */
std::uint32_t GetSpellFailureChance(std::uint32_t skill, std::int32_t spell_id, std::int32_t factor);

/* The combat-pace scale an item-use attempt applies to its own difficulty. */
void ScaleByCombatPace(std::int32_t party_slot, std::uint32_t* value);

/* Validate the available target set and cursor state for a spell or item cast. */
bool ValidateSpellTarget(std::int32_t party_slot, std::int32_t spell_id, std::uint32_t power, bool item_cast,
                         bool skip_world_cursor);
bool SpellAffectedTarget(W8Character* character, std::int32_t spell_id, W8CombatSlot* aim,
                         std::uint32_t power);
void TrackItemSpellSource(W8Character* character, std::int32_t spell_id);
bool IsTeleportCastMissingAnchor(W8Character* character, std::int32_t spell_id);
extern std::uint16_t g_realm_message_offsets[W8_SPELL_REALM_COUNT];

void DetachMissileReferences(W8Missile* missile);
bool AllSpellEffectsStillRunning(void);

#endif
