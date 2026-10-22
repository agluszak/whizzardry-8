#pragma once

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/records.h"
#include "wiz8/rules/targeting.h"

class Monster;
struct W8Character;
struct W8CombatSlot;
struct W8Dice;
struct W8MonsterInfo;
struct W8SpellEffectResult;

int CalculateMonsterFatigueBand(int current, int maximum);
void FatigueMonster(W8MonsterInfo* monster_info, unsigned int amount,
                    W8SpellEffectResult* report_to);
extern char g_poison_suffix[];
int SumCharacterSpellPoints(const W8Character* character);
int SumCharacterSpellPointsLeft(const W8Character* character);
int GetCharacterRealmSpellPoints(const W8Character* character, W8SpellRealm realm);

class Game;

/* One game's health, stamina and spell point rules: damage, healing, fatigue and their
   notices, for the party and the monsters of the game's level. The game owns it. */
class Vitality {
public:
    explicit Vitality(Game& game) : game_(game) {}
    Vitality(const Vitality&) = delete;
    Vitality& operator=(const Vitality&) = delete;
    unsigned int findPartySlotWithLowestHitPoints();
    unsigned int findPartySlotWithLowestSpellPoints();
    void healCharacter(int party_slot, int amount, bool announce);
    void restoreCharacterStamina(int party_slot, int amount, bool announce);
    void drainCharacterSpellPoints(int party_slot, unsigned int amount, bool announce);
    void restoreCharacterSpellPointsEvenly(int party_slot, int amount);
    void restoreCharacterRealmSpellPoints(int party_slot, W8SpellRealm realm, int amount);
    void fatigueCharacter(int party_slot, int amount, bool scale_by_load,
                          W8SpellEffectResult* report_to);

    /* Run one queued fatigue op. The op is a combat slot: a
       character target fatigues that party slot, a monster target resolves the
       monster from its location id. The third argument is unused. */
    void applyQueuedFatigue(W8CombatSlot* op, unsigned int amount, int);
    unsigned int characterActionFatigueCost(int party_slot, W8ActionKind action_kind);
    void damageCharacter(int party_slot, unsigned int damage, bool announce);
    void drainCharacterRealmSpellPoints(int party_slot, W8SpellRealm realm, unsigned int amount,
                                        bool announce);
    void drainPartySpellPoints(int amount, bool announce);
    int spellCastFatigueCost(int spell_id, int result);
    void spendCharacterSpellPoints(int party_slot, W8SpellRealm realm, int amount);
    int monsterActionFatigueCost(const W8MonsterInfo* monster_info);
    void healMonster(W8MonsterInfo* monster_info, unsigned int amount, bool announce);
    void restoreMonsterStamina(W8MonsterInfo* monster_info, int amount, bool announce);

    /* The monster-side effect application pass the aging producer
       drives for both sign directions. The result block, when given, collects
       the damage dealt; the applied amount is also returned. `quiet` marks
       non-provoking damage such as a poison tick: it picks the notice strings,
       skips the condition-target bookkeeping, and silences the struck reaction. */
    unsigned int applyDamageToMonster(W8MonsterInfo* monster_info, unsigned int amount,
                                      struct W8TargetSource* source, bool quiet,
                                      unsigned char in_combat, signed char a,
                                      W8SpellEffectResult* result_stats, bool c);

    /* The character-side counterpart - damage absorbed by the
       slot-2 enchantment first, two thirds of the rest fatigue the character, the
       remainder comes off hit points and can kill. The sixth parameter receives a
       kill-counting result block.
       `quiet` marks non-provoking damage such as a poison tick: it picks the
       shield/notice strings and the poison suffix, and keeps a sleeping character
       asleep. `announce` gates the damage notice, `short_notice` selects the terse
       ShowNotice form over the verbose PostCharacterNotice one, and `detailed`
       includes the character name in the message. */
    unsigned int applyDamageToCharacter(int party_slot, unsigned int amount, bool quiet,
                                        bool announce, bool short_notice,
                                        W8SpellEffectResult* result_stats, bool detailed);

    /* How a monster answers being struck - the struck cycle, a
       possible condition knock-on, and the hostility check toward the attacker. */
    void monsterReactsToBeingStruck(W8MonsterInfo* monster_info, W8TargetSource* attacker,
                                    bool quiet);
    void applyRolledHealthChangeToParty(const W8Dice* dice, W8SpellEffectResult* result,
                                        int announce);

    /* Roll the dice once for every live monster within the radius of
       a point and apply each roll as damage. */
    void damageMonstersInRadius(const glm::vec3& center, float radius, const W8Dice* dice,
                                struct W8TargetSource* source, W8SpellEffectResult* result);
    void restorePartyStaminaByDice(unsigned char count, unsigned char sides, short base);
    void healPartyByDice(unsigned char count, unsigned char sides, short base);
    void restorePartySpellPoints(int amount);

private:
    Game& game_;
};
