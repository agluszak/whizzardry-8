#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <glm/geometric.hpp>

#include "attack_modes.h"
#include "dice.h"
#include "equipment_slots.h"
#include "gameplay_modifiers.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/item_instance.h"
#include "layouts/npc_state.h"
#include "layouts/targeting.h"
#include "local_code/Factions.h"
#include "local_code/SpellEffect.h"
#include "monster_actions.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/character_stats.h"
#include "wiz8/checked_integer.h"
#include "wiz8/compat/unaligned.h"
#include "wiz8/conditions.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/item_rules.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/picking.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/xstatus.h"

/* Shared combat attribute differential used by pre-hit and hit-location rolls. */
static int SumCharacterCombatAttributes(const W8Character& character)
{
    return character.attributes[W8_ATTRIBUTE_SENSES].effective +
           character.attributes[W8_ATTRIBUTE_SPEED].effective +
           character.attributes[W8_ATTRIBUTE_DEXTERITY].effective +
           character.attributes[W8_ATTRIBUTE_INTELLIGENCE].effective;
}

static int SumMonsterCombatAttributes(const W8MonsterInfo& monster)
{
    return monster.attributes[W8_MONSTER_ATTRIBUTE_SENSES] +
           monster.attributes[W8_MONSTER_ATTRIBUTE_SPEED] +
           monster.attributes[W8_MONSTER_ATTRIBUTE_DEXTERITY] +
           monster.attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE];
}

static int SumTargetCombatAttributes(const W8CombatSlot& target)
{
    if (target.iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster = FindMonster(target.iMonsterID);
        return SumMonsterCombatAttributes(*monster);
    }
    if (target.iType == W8_TARGET_KIND_CHARACTER)
        return SumCharacterCombatAttributes(g_status.party.characters[target.iChar]);
    DebugMessage(
        1, fmt::format("GetTargetAttackAttributes: Invalid Target Type {:d}(char {:d}, ID {:d})",
                       fmt::underlying(target.iType), target.iChar, target.iMonsterID));
    return 0;
}

// Retaliation spends one Razor Cloak charge after damage, even if it was a zero-damage hit.
static void ApplyRazorCloakBacklash(const W8CombatSlot& target, W8MonsterInfo* monster,
                                    auto&& apply_damage)
{
    const bool is_monster = target.iType == W8_TARGET_KIND_MONSTER;
    W8Enchantment* enchantment =
        is_monster
            ? &monster->enchantments[W8_ENCHANTMENT_RAZOR_CLOAK]
            : &g_status.party.characters[target.iChar].enchantments[W8_ENCHANTMENT_RAZOR_CLOAK];
    if (enchantment->turns == 0) return;
    W8TargetSource source;
    if (is_monster)
        SetTargetSourceToMonster(monster, &source);
    else
        SetTargetSourceToCharacter(target.iChar, &source);
    apply_damage(&source, enchantment);
    if (--enchantment->power == 0) {
        if (is_monster)
            ClearMonsterEnchantmentSlot(monster->location_id, W8_ENCHANTMENT_RAZOR_CLOAK);
        else
            ClearCharacterEnchantmentSlot(target.iChar, W8_ENCHANTMENT_RAZOR_CLOAK);
    }
}

void GetCharacterHandDamageDice(const W8Character* character, int hand, W8Dice* dice)
{
    if (character->Hand[hand].uiHolds == HOLDS_NOTHING) {
        *dice = character->Hand[hand].damage_dice;
        return;
    }
    W8EquipSlot slot;
    if (hand == 0) {
        slot = W8_EQUIP_SLOT_PRIMARY_WEAPON;
        if (ItemHasSingledOutGenericName(
                character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo)) {
            W8EquipSlot partner = GetPairedEquipSlot(W8_EQUIP_SLOT_PRIMARY_WEAPON);
            if (partner != W8_EQUIP_SLOT_NONE) slot = partner;
        }
    } else {
        slot = W8_EQUIP_SLOT_SECONDARY_WEAPON;
    }
    *dice = g_item_records[character->EquippedItem[slot].iItemNo].damage_dice;
}

int GetCharacterHandDamageBonus(const W8Character* character, int hand)
{
    return character->Hand[hand].damage_percent + character->bonus.damage_percent;
}

/* Nine attack modes, one bit each, held in the low half of a word. */

/* Clear a forty-eight byte attack block. */
void ClearAttackBlock(W8SpellEffectDefinition* block)
{
    memset(block, 0, 0x30);
}

/* Fold specific melee animation cycles onto ATTACK_CLOSE, and the throw
   and shoot cycles onto ATTACK_RANGED. Other cycles retain their identity. */
W8MonsterCycle NormalizeMonsterCycle(W8MonsterCycle cycle)
{
    switch (cycle) {
    case W8_MONSTER_CYCLE_ATTACK_SWING:
    case W8_MONSTER_CYCLE_ATTACK_THRUST:
    case W8_MONSTER_CYCLE_ATTACK_BASH:
    case W8_MONSTER_CYCLE_ATTACK_MELEE:
    case W8_MONSTER_CYCLE_ATTACK_PUNCH:
    case W8_MONSTER_CYCLE_ATTACK_KICK:
    case W8_MONSTER_CYCLE_ATTACK_LASH:
        return W8_MONSTER_CYCLE_ATTACK_CLOSE;
    case W8_MONSTER_CYCLE_ATTACK_THROW:
    case W8_MONSTER_CYCLE_ATTACK_SHOOT:
        return W8_MONSTER_CYCLE_ATTACK_RANGED;
    default:
        break;
    }
    return cycle;
}

/* Pick one of the attack modes a mask allows, at random. The walk wraps round
   the nine bits, counting only the ones that are set, until it has passed as
   many as the roll asked for - so a mask with one bit always answers that bit
   however the roll came out. */
W8AttackMode ChooseAttackMode(unsigned int attack_modes)
{
    unsigned int wanted;
    unsigned int seen = 0;
    unsigned int mode = 0;

    if (static_cast<short>(attack_modes) == 0) {
        Fail("fsAttackModes != 0");
    }

    wanted = Random(W8_ATTACK_MODE_COUNT);
    while (((attack_modes & 0xffff) & (1 << mode)) == 0 || seen++ < wanted) {
        ++mode;
        if (mode > 8) {
            mode = 0;
        }
    }
    return static_cast<W8AttackMode>(mode);
}

/* Whether one character can swing this round: engaged, in better shape than
   the attack threshold, and with the first hand in play. */
bool CanCharacterAttack(int party_slot)
{
    const W8Character* character = &g_status.party.characters[party_slot];

    if (!IsPartySlotEligible(party_slot)) {
        return false;
    }
    if (character->highest_condition > W8_CONDITION_INSANE) {
        return false;
    }
    return character->Hand[0].in_play;
}

/* Whether a bodyguard can protect another combatant: active, alive, able
   to act and equipped with a natural attack. The AI uses this for action 8. */
bool CanMonsterProtect(W8MonsterInfo* monster_info)
{
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);

    if (!monster_info->fActive || !monster_info->pCombat || monster_info->hp_current == 0 ||
        monster_info->highest_condition >= W8_CONDITION_BLIND ||
        (record->flags & W8_MONSTER_FLAG_BODYGUARD) == 0) {
        return false;
    }
    return record->attacks[0].fHasAttack != 0;
}

/* How much of a hit the target actually takes. The two reductions add, the
   remainder is taken as a percentage rounding to nearest, and nothing goes
   below zero. */
int ApplyDamageReduction(const W8MonsterInfo* monster_info, const W8MonsterRecord* record,
                         int damage)
{
    int reduction = monster_info->modifiers.damage_reduction_adjustment + record->damage_reduction;

    if (reduction != 0) {
        damage = ((100 - reduction) * damage + 50) / 100;
    }
    if (damage < 0) {
        return 0;
    }
    return damage;
}

/* Why one of a monster's attacks cannot be made, or zero when it can. An
   attack the record lacks is simply not usable, and so is one whose data is
   broken, which is reported. Otherwise the attack is out of reach unless it
   reaches anyone, judged with the monster's current action set aside. */
unsigned char RateMonsterAttack(W8MonsterInfo* monster_info, W8MonsterRecord* record,
                                unsigned int attack, int, bool friendly_targets)
{
    W8MonsterActionKind action_kind;
    bool reaches;

    if (attack >= W8_MAX_MONSTER_ATTACKS) {
        Fail("uiAttack < MAX_MONSTER_ATTACKS");
    }
    if (record->attacks[attack].fHasAttack == 0) {
        return W8_MONSTER_ATTACK_NOT_USABLE;
    }
    if (record->attacks[attack].attack_modes == 0) {
        DebugMessage(0, fmt::format("DATA ERROR: {} has 0 attack modes for attack {:d}",
                                    record->name0, attack));
        return W8_MONSTER_ATTACK_NOT_USABLE;
    }
    if (record->attacks_per_round == 0) {
        DebugMessage(0, fmt::format("DATA ERROR: {} has 0 ATTACKS/round", record->name0));
        return W8_MONSTER_ATTACK_NOT_USABLE;
    }
    if (record->swings_per_round == 0) {
        DebugMessage(0, fmt::format("DATA ERROR: {} has 0 SWINGS/round", record->name0));
        return W8_MONSTER_ATTACK_NOT_USABLE;
    }
    action_kind = monster_info->action_kind;
    monster_info->action_kind = W8_MONSTER_ACTION_ATTACK;
    reaches = MonsterAttackReachesAnyone(monster_info, attack, friendly_targets);
    monster_info->action_kind = action_kind;
    return !reaches ? W8_MONSTER_ATTACK_OUT_OF_REACH : W8_MONSTER_ATTACK_USABLE;
}

/* Why a monster cannot attack at all, or zero when some attack is usable. A
   motionless monster is always rated at one; otherwise each of its three
   attacks is rated, a usable one answering at once and the highest reason
   otherwise kept. */
unsigned char RateMonsterBestAttack(W8MonsterInfo* monster_info, W8MonsterRecord* record,
                                    bool friendly_targets)
{
    unsigned char best;
    unsigned char rating;
    unsigned int attack;

    if (monster_info->fMotionless) {
        return 1;
    }
    best = 0;
    for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
        rating = RateMonsterAttack(monster_info, record, attack, 1, friendly_targets);
        if (rating == 0) {
            return 0;
        }
        if (rating > best) {
            best = rating;
        }
    }
    return best;
}

/* HAND_COUNT, named by the assertion that bounds every hand argument here. */
enum { W8_HAND_COUNT = 2 };

/* Whether one of a character's hands can reach the target it is aimed at: the
   hand has to be in play and to have a range category at all. */
bool CanHandReachTarget(int party_slot, unsigned int hand)
{
    if (hand >= W8_HAND_COUNT) {
        Fail("uiHand < HAND_COUNT");
    }
    if (!g_status.party.characters[party_slot].Hand[hand].in_play) {
        return false;
    }
    return GetCharAttackRange(&g_status.party.characters[party_slot], hand) != W8_RANGE_NONE;
}

/* Whether either hand can. */
bool CanAnyHandReachTarget(int party_slot)
{
    unsigned int hand;

    for (hand = 0; hand < W8_HAND_COUNT; ++hand) {
        if (CanHandReachTarget(party_slot, hand)) {
            return true;
        }
    }
    return false;
}

/* What one hand's attack is worth, or nothing when it cannot reach. */
int GetHandAttackValue(int party_slot, unsigned int hand)
{
    if (CanHandReachTarget(party_slot, hand)) {
        return g_status.party.characters[party_slot].Hand[hand].attacks;
    }
    return 0;
}

/* How much of a hit a character keeps. Their own reduction is taken as a
   percentage rounding to nearest, and using it practises the skill it comes
   from - but only for a character who has that skill at all. */
int ApplyCharacterDamageReduction(W8Character* character, int damage)
{
    if (character->damage_reduction != 0) {
        damage = ((100 - character->damage_reduction) * damage + 50) / 100;
    }
    if (damage < 0) {
        damage = 0;
    }
    if (character->skills[W8_SKILL_IRON_SKIN].active) {
        PracticeCharacterSkill(character, W8_SKILL_IRON_SKIN, 1, false);
    }
    return damage;
}

/* Whether a character can berserk: the ability itself, a hand that can reach
   the target, and a primary hand fighting at short range or closer. */
bool CanCharacterBerserk(int party_slot)
{
    W8Character* character = &g_status.party.characters[party_slot];
    unsigned int hand;

    if (!CharacterHasTrait(character, W8_TRAIT_BERSERK)) {
        return false;
    }
    for (hand = 0; hand < W8_HAND_COUNT; ++hand) {
        if (hand >= W8_HAND_COUNT) {
            Fail("uiHand < HAND_COUNT");
        }
        if (g_status.party.characters[party_slot].Hand[hand].in_play &&
            GetCharAttackRange(&g_status.party.characters[party_slot], hand) != W8_RANGE_NONE) {
            return GetCharAttackRange(character, 0) <= W8_RANGE_SHORT;
        }
    }
    return false;
}

/* Whether a character could attack what a combat slot names. A party member
   has to be in play and not screened off by the front rank; a monster has to be
   engaged, alive, targetable and within the character's reach. */
bool CharacterHasAttackOn(int party_slot, W8CombatSlot* target)
{
    W8MonsterInfo* monster_info;
    int target_slot;

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        target_slot = target->iChar;
        if (!CanPartySlotParticipate(target_slot)) {
            return false;
        }
        if (FrontRankScreens(party_slot, target_slot) > 0) {
            return false;
        }
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        if (!monster_info->fActive || monster_info->hp_current == 0 || !monster_info->pCombat) {
            return false;
        }
        if (GetMonsterDataForInfo(monster_info)->untargetable != 0) {
            return false;
        }
        if (!AssessMonsterAim(
                party_slot, 0, monster_info,
                SelectCombatAction(party_slot, !g_combat.state()->characters[party_slot].dead
                                                   ? W8_TARGETING_CONTEXT_IN_COMBAT
                                                   : W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

/* Whether a friendly combatant qualifies for protection: within the
   bodyguard's first-attack range, higher level, and injured or out of
   formation. Preserve retail's use of the bodyguard's formation modifier
   for a monster target. */
bool CanMonsterProtectCombatant(W8MonsterInfo* monster_info, W8CombatSlot* target)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8Character* character;
    W8MonsterInfo* target_info;
    int target_slot;
    unsigned int target_level;
    unsigned int hp_percent;
    unsigned char out_of_formation;

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        target_slot = target->iChar;
        if (!CanPartySlotParticipate(target_slot)) {
            return false;
        }
        if (MonsterVsCharDisposition(target_slot, monster_info) != W8_DISPOSITION_FRIENDLY) {
            return false;
        }
        if (!MonsterAttackReachesCharacter(monster_info, record, 0, target_slot)) {
            return false;
        }
        character = &g_status.party.characters[target_slot];
        hp_percent = character->hp_current * 100 / character->uiHPMax;
        target_level = character->uiExpLevel;
        out_of_formation = character->bonus.out_of_formation;
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        target_info = FindMonster(target->iMonsterID);
        if (!target_info->fActive || target_info->hp_current == 0 || !target_info->pCombat) {
            return false;
        }
        if (GetMonsterDataForInfo(target_info)->untargetable != 0) {
            return false;
        }
        if (MonsterHostility(monster_info, target_info) != W8_DISPOSITION_FRIENDLY) {
            return false;
        }
        if (!MonsterAttackReachesMonster(monster_info, record, 0, target_info)) {
            return false;
        }
        target_level = GetMonsterDataForInfo(target_info)->effective_level;
        hp_percent = target_info->hp_current * 100 / target_info->uiHPMax;
        out_of_formation = monster_info->modifiers.out_of_formation;
    } else {
        return false;
    }
    if (target_level > record->effective_level && (hp_percent < 40 || out_of_formation != 0)) {
        return true;
    }
    return false;
}

/* Protection readiness for the selected target. The action-8 executor
   requires an eligible bodyguard with an attack in range of that target. */
bool CanMonsterProtectTarget(W8MonsterInfo* monster_info)
{
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);

    if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current != 0 &&
        monster_info->highest_condition < W8_CONDITION_BLIND &&
        (record->flags & W8_MONSTER_FLAG_BODYGUARD) != 0 && record->attacks[0].fHasAttack != 0) {
        return CanMonsterProtectCombatant(monster_info, &monster_info->Target);
    }
    return false;
}

/* Attack verbs whose fatigue penalty halves before it lands on the monster's
   attack score - the unarmed and natural strikes. The score roll scans it for
   the attack's verb byte. */
static unsigned char g_low_fatigue_attack_verbs[0x12] = {
    0x07, 0x0d, 0x0e, 0x0f, 0x15, 0x19, 0x21, 0x2b, 0x31,
    0x32, 0x34, 0x35, 0x36, 0x38, 0x3b, 0x55, 0x56, 0x57,
};

static int GetCharacterProtectionScore(W8Character* character)
{
    int armed = 0;
    if (character->Hand[0].in_play) {
        armed = character->Hand[0].hit_bonus * 5 + character->Hand[0].attack_score;
    }
    unsigned int penalty = FatigueArmorPenalty(character->fatigue_band);
    if (character->Hand[0].weapon_skill == W8_SKILL_MODERN_WEAPONS) {
        penalty >>= 1;
    }
    return character->attributes[W8_ATTRIBUTE_SPEED].effective - penalty + armed +
           character->bonus.hit_bonus * 5;
}

static int GetMonsterProtectionScore(W8MonsterInfo* monster)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster);
    int armed = 0;
    if (record->attacks[0].fHasAttack != 0) {
        armed = record->attacks[0].attack_score;
    }
    unsigned int penalty = FatigueArmorPenalty(monster->fatigue_band);
    for (unsigned char g_low_fatigue_attack_verb : g_low_fatigue_attack_verbs) {
        if (record->attacks[0].ubWeaponNameIndex == g_low_fatigue_attack_verb) {
            penalty >>= 1;
            break;
        }
    }
    return monster->attributes[W8_MONSTER_ATTRIBUTE_SPEED] - penalty + armed +
           monster->modifiers.hit_bonus * 5;
}

/* Guardian interception: every party member and combat monster protecting the
   struck target rolls its defense score against the attacker's, and a success
   replaces the target with the guardian. Answers whether the target was
   taken over. */
int ResolveGuardianInterception(W8TargetSource* source, W8CombatSlot* target)
{
    std::vector<W8TargetSource> candidates;
    std::vector<W8TargetSource> interposers;
    W8TargetSource guardian;
    std::string target_name;
    int guardian_score;
    int attacker_score;
    int i;

    for (int slot = 0; slot < 8; ++slot) {
        W8Character* character = &g_status.party.characters[slot];
        W8CombatCharacterRow* row = &g_combat.state()->characters[slot];
        W8PartySlotRow* party_row = &g_status.party.rows[slot];
        if (IsPartySlotEligible(slot) && character->highest_condition < W8_CONDITION_BLIND &&
            character->Hand[0].in_play && TryCharacterAction(slot, W8_ACTION_PROTECT, false) &&
            row->interception_count < character->Hand[0].attacks) {
            W8CombatSlot* guarded =
                !row->dead ? &party_row->committed_action.target : &party_row->target_out_of_combat;
            if ((*target == *guarded) && CharacterHasAttackOn(slot, target)) {
                SetTargetSourceToCharacter(slot, &guardian);
                candidates.push_back(guardian);
                if (!row->dead) {
                    SwitchCharacterTo(slot, W8_ACTION_PROTECT);
                }
            }
        }
    }
    for (unsigned int monster_index = 0;
         monster_index < CurrentLevelPopulation().monster_roster.live().size(); ++monster_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_index);
        W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
        if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_BLIND &&
            (record->flags & W8_MONSTER_FLAG_BODYGUARD) != 0 &&
            record->attacks[0].fHasAttack != 0 &&
            monster_info->action_kind == W8_MONSTER_ACTION_PROTECT &&
            monster_info->pCombat->interception_count < record->attacks_per_round &&
            (*target == monster_info->Target) && CanMonsterProtectCombatant(monster_info, target)) {
            SetTargetSourceToMonster(monster_info, &guardian);
            candidates.push_back(guardian);
        }
    }
    if (candidates.empty()) {
        return 0;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        target_name = g_status.party.characters[target->iChar].name;
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* target_info = FindMonster(target->iMonsterID);
        target_name = GetMonsterName(target_info, nullptr, 0);
    } else {
        Fail("FALSE");
    }
    for (i = 0; i < static_cast<int>(candidates.size()); ++i) {
        W8TargetSource* candidate = &candidates[i];
        if (candidate->iType == W8_TARGET_SOURCE_CHARACTER) {
            W8Character* defender = &g_status.party.characters[candidate->iChar];
            guardian_score = GetCharacterProtectionScore(defender);
        } else if (candidate->iType == W8_TARGET_SOURCE_MONSTER) {
            W8MonsterInfo* defender = FindMonster(candidate->iMonsterID);
            guardian_score = GetMonsterProtectionScore(defender);
        } else {
            Fail("FALSE");
        }
        if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
            W8Character* attacker = &g_status.party.characters[source->iChar];
            attacker_score = GetCharacterProtectionScore(attacker);
        } else if (source->iType == W8_TARGET_SOURCE_MONSTER) {
            W8MonsterInfo* attacker = FindMonster(source->iMonsterID);
            attacker_score = GetMonsterProtectionScore(attacker);
        } else {
            Fail("FALSE");
        }
        if (g_combat.state()->unaware != 0) {
            attacker_score += 10;
        }
        if (g_combat.state()->natural_attack != 0) {
            attacker_score += 10;
        }
        int chance = guardian_score - attacker_score + 50;
        if (chance < 0x60) {
            if (chance < 5) {
                chance = 5;
            }
        } else {
            chance = 0x5f;
        }
        if (Random(100) < static_cast<unsigned int>(chance)) {
            interposers.push_back(*candidate);
        }
    }
    if (static_cast<int>(interposers.size()) == 0) {
        if (g_settings.verbose_combat_messages != 0 && static_cast<int>(candidates.size()) != 0) {
            for (i = 0; i < static_cast<int>(candidates.size()); ++i) {
                W8TargetSource* candidate = &candidates[i];
                g_combat.state()->attack_message =
                    fmt::sprintf(g_localized_strings[0x219], target_name);
                if (candidate->iType == W8_TARGET_SOURCE_CHARACTER) {
                    PostCharacterNotice(candidate->iChar, g_combat.state()->attack_message);
                } else if (candidate->iType == W8_TARGET_SOURCE_MONSTER) {
                    W8MonsterInfo* defender = FindMonster(candidate->iMonsterID);
                    PostMonsterNotice(defender, g_combat.state()->attack_message);
                }
            }
        }
        return 0;
    }
    unsigned int uiSuccessfulCnt = static_cast<int>(interposers.size());
    if (uiSuccessfulCnt <= 0) {
        Fail("uiSuccessfulCnt > 0");
    }
    W8TargetSource* interposer = &interposers[Random(uiSuccessfulCnt)];
    g_combat.state()->attack_message = fmt::sprintf(g_localized_strings[0x21a], target_name);
    if (interposer->iType == W8_TARGET_SOURCE_CHARACTER) {
        int slot = interposer->iChar;
        PostCharacterNotice(slot, g_combat.state()->attack_message);
        ResetCombatSlot(target);
        target->iType = W8_TARGET_KIND_CHARACTER;
        target->iChar = slot;
        ++g_combat.state()->characters[slot].interception_count;
        unsigned int fatigue = g_item_records[g_status.party.characters[slot]
                                                  .EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON]
                                                  .iItemNo]
                                       .weight /
                                   0x28 +
                               1;
        FatigueCharacter(slot, Random(fatigue) + 1 + fatigue, true, nullptr);
    } else if (interposer->iType == W8_TARGET_SOURCE_MONSTER) {
        W8MonsterInfo* defender = FindMonster(interposer->iMonsterID);
        PostMonsterNotice(defender, g_combat.state()->attack_message);
        ResetCombatSlot(target);
        target->iType = W8_TARGET_KIND_MONSTER;
        target->iMonsterID = interposer->iMonsterID;
        ++defender->pCombat->interception_count;
        FatigueMonster(defender, Random(3) + 2, nullptr);
    }
    g_combat.state()->unaware = 0;
    g_combat.state()->natural_attack = 0;
    return 1;
}

/* The character counterpart: the target has to still be in play, the character
   engaged and in shape, the first hand in play, and the attack itself has to
   come off. */
bool CanCharacterAttackItsTarget(int party_slot)
{
    W8Character* character;

    if (!TargetIsInPlay(party_slot, 0,
                        SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
        return false;
    }
    if (!IsPartySlotEligible(party_slot)) {
        return false;
    }
    character = &g_status.party.characters[party_slot];
    if (character->highest_condition >= W8_CONDITION_BLIND || !character->Hand[0].in_play) {
        return false;
    }
    return CharacterHasAttackOn(party_slot, &g_status.party.rows[party_slot].target_out_of_combat);
}

/* What an attack mode is worth to hit with, which depends on whether the
   attacker is a monster or a character - the same nine modes score differently
   for each. Its error text carries the function's own name. */
static int AttackModeMod(int is_character, W8AttackMode attack_mode)
{
    if (is_character == 0) {
        switch (attack_mode) {
        case W8_ATTACK_MODE_SWING:
        case W8_ATTACK_MODE_BASH:
        case W8_ATTACK_MODE_SHOOT:
            return 0;
        case W8_ATTACK_MODE_THRUST:
        case W8_ATTACK_MODE_KICK:
            return -10;
        case W8_ATTACK_MODE_BERSERK:
            return -30;
        case W8_ATTACK_MODE_THROW:
            return -5;
        case W8_ATTACK_MODE_PUNCH:
            return 5;
        case W8_ATTACK_MODE_LASH:
            return 10;
        default:
            Fail("FALSE", "AttackModeMod: ERROR - Invalid attack mode");
            return 0;
        }
    }
    switch (attack_mode) {
    case W8_ATTACK_MODE_SWING:
    case W8_ATTACK_MODE_KICK:
        return 0;
    case W8_ATTACK_MODE_THRUST:
    case W8_ATTACK_MODE_SHOOT:
        return 10;
    case W8_ATTACK_MODE_BASH:
    case W8_ATTACK_MODE_PUNCH:
        return -5;
    case W8_ATTACK_MODE_BERSERK:
        return -30;
    case W8_ATTACK_MODE_THROW:
        return 5;
    case W8_ATTACK_MODE_LASH:
        return -10;
    default:
        Fail("FALSE", "AttackModeMod: ERROR - Invalid attack mode");
        return 0;
    }
}

unsigned short g_pc_hit_location_labels[5][2] = {
    {0x429, 0x42a}, {0x42b, 0x42c}, {0x42d, 0x42e}, {0x42f, 0x430}, {0x431, 0x432},
};

unsigned short g_monster_hit_location_labels[W8_MONSTER_HIT_LOCATIONS][W8_MONSTER_BODY_TYPES] = {
    {0x55c, 0x563, 0x56a, 0x55c, 0x571, 0x572}, {0x55d, 0x564, 0x56b, 0x55d, 0x572, 0x575},
    {0x55e, 0x565, 0x56c, 0x55e, 0x55e, 0x55e}, {0x55f, 0x566, 0x56d, 0x55f, 0x573, 0x576},
    {0x560, 0x567, 0x56e, 0x560, 0x574, 0x577}, {0x561, 0x568, 0x56f, 0x568, 0x560, 0x560},
    {0x562, 0x569, 0x570, 0x562, 0x55c, 0x55d},
};

unsigned short g_attack_flag_name_ids[9][2] = {
    {1311, 1312}, {1313, 1314}, {1315, 1316}, {1317, 1318}, {1319, 1320},
    {1321, 1322}, {1323, 1324}, {1325, 1326}, {1327, 1328},
};

unsigned short g_attack_effect_name_ids[W8_ATTACK_EFFECT_COUNT] = {
    1330, 1331, 1332, 1333, 1334, 1335, 1336, 1337, 1338, 1339, 1340, 1341, 1342, 1343, 1344, 1345,
};

unsigned short g_special_category_name_ids[42] = {
    1400, 1401, 1402, 1403, 1404, 1405, 1406, 1407, 1408, 1409, 1410, 1411, 1412, 1413,
    1414, 1415, 1416, 1417, 1418, 1419, 1420, 1421, 1422, 1423, 1424, 1425, 1426, 1427,
    1428, 1429, 1430, 1431, 1432, 0,    1433, 1434, 1435, 1436, 1437, 1438, 1439, 1440,
};

/* Land a spell missile on whatever it struck. The spell's own impact sound
   plays when it has one. A single-target effect (no radius) rolls its size,
   applies it to the struck monster or character and rolls the effect's
   conditions against the target. An area effect instead reaches the whole
   party when a character was struck, or every active monster within the
   radius of the struck monster's position. */
void ResolveSpellMissileHit(Missile* missile)
{
    W8TargetSource* source = &missile->m_Source;
    W8CombatSlot* target = &missile->combat_slot;
    W8SpellEffectDefinition* definition = &missile->definition;
    W8SpellRuntimeRecord* spell;
    W8CombatSlot struck;
    unsigned int magnitude;
    W8MonsterInfo* monster_info;
    glm::vec3 location;
    bool announce;
    bool verbose;
    unsigned int index;

    TargetSourceIsCharacter(source, 0);
    spell = &g_spell_records[MissileSpellId(missile->missile_table_index)];
    if (spell->sound_name.size() != 0) {
        wiz8::sound::play(fmt::format("Data\\Missiles\\Sounds\\{}.wav", spell->sound_name));
    }

    if (definition->radius > 0.0f) {
        if (target->iType == W8_TARGET_KIND_MONSTER) {
            monster_info = FindMonster(target->iMonsterID);
            DamageMonstersInRadius(monster_info->p3D->nav.GetPosition(), definition->radius,
                                   &definition->magnitude, source, &missile->result);
            announce = g_settings.verbose_combat_messages != 0;
            verbose = g_settings.verbose_combat_messages != 0;
            glm::vec3 center = monster_info->p3D->nav.GetPosition();
            ResetCombatSlot(&struck);
            struck.iType = W8_TARGET_KIND_MONSTER;
            for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                 ++index) {
                monster_info = GetMonsterListEntry(index);
                if (monster_info->fActive) {
                    location = monster_info->p3D->GetRepresentationLocation();
                    glm::vec3 offset = center - location;
                    if (glm::length(offset) <= definition->radius) {
                        struck.iMonsterID = monster_info->location_id;
                        ApplyEffectConditions(source, &struck, definition, announce, verbose, 0);
                    }
                }
            }
        } else {
            ApplyRolledHealthChangeToParty(&definition->magnitude, &missile->result, 1);
            announce = g_settings.verbose_combat_messages != 0;
            verbose = g_settings.verbose_combat_messages != 0;
            ResetCombatSlot(&struck);
            struck.iType = W8_TARGET_KIND_CHARACTER;
            for (index = 0; index < 8; ++index) {
                if (g_status.party.rows[index].fOccupied) {
                    struck.iChar = index;
                    ApplyEffectConditions(source, &struck, definition, announce, verbose, 0);
                }
            }
        }
        return;
    }

    magnitude = RollEffectMagnitude(definition);
    if (magnitude > 0) {
        ApplyEffectAndAnnounce(&magnitude, target, spell->realm, definition->power_level);
        if (magnitude > 0) {
            if (target->iType == W8_TARGET_KIND_MONSTER) {
                monster_info = FindMonster(target->iMonsterID);
                ApplyDamageToMonster(monster_info, magnitude, source, false,
                                     g_settings.verbose_combat_messages, 0, &missile->result,
                                     false);
            } else {
                ApplyDamageToCharacter(target->iChar, magnitude, false,
                                       g_settings.verbose_combat_messages, false, &missile->result,
                                       false);
            }
        }
    }
    ApplyEffectConditions(source, target, definition, g_settings.verbose_combat_messages,
                          g_settings.verbose_combat_messages, &missile->result);
}

static unsigned int RollMonsterHitLocation(W8MonsterRecord* record)
{
    unsigned int total = 0;
    unsigned int roll = Random(100);
    for (unsigned int hit_location = 0;; ++hit_location) {
        if (hit_location >= W8_MONSTER_HIT_LOCATIONS) {
            DebugMessage(0, fmt::format("ERROR: DBS Hit Locations total only {:d}% for monster {}",
                                        total, record->name0));
            return 3;
        }
        total += record->hit_location_chances[hit_location];
        if (roll < total) {
            return hit_location;
        }
    }
}

static unsigned int RollCharacterHitLocation()
{
    unsigned int total = 0;
    unsigned int roll = Random(100);
    for (unsigned int hit_location = 0;; ++hit_location) {
        if (hit_location >= W8_PC_HIT_LOCATIONS) {
            DebugMessage(1, "ERROR: gubLocalACPercent total only %d%%");
            return 1;
        }
        total += gubLocalACPercent[hit_location];
        if (roll < total) {
            return hit_location;
        }
    }
}

/* Land a physical missile on its target. The notice names the target, with
   the retargeted suffix when the shot was turned aside onto it, and colours
   the name by side. A deflected shot only reports the deflection. Otherwise
   the hit location is rolled from the monster body's own chances or the
   party's gubLocalACPercent, named in verbose mode, and the shot must still
   penetrate the armour there: a 50 percent base, plus the attack mode's
   modifier and 5 per point of armour class under 10. A penetrating shot
   rolls the effect size and, when it is not nothing, plays the hit sound,
   applies the damage and rolls the effect's conditions. */
void ResolveMissileHit(Missile* missile, bool deflected)
{
    W8TargetSource* source = &missile->m_Source;
    W8CombatSlot* target = &missile->combat_slot;
    W8MonsterRecord* record;
    std::string text;
    std::string location_name;
    unsigned char target_start;
    unsigned char target_stop;
    char source_color;
    char target_color;
    unsigned int hit_location;
    W8AttackMode attack_mode;
    int chance;
    int penetration_roll;
    unsigned int magnitude;

    if (!IsTargetStillPresent(target)) {
        return;
    }
    const bool target_is_monster = target->iType == W8_TARGET_KIND_MONSTER;
    W8MonsterInfo* monster_info = target_is_monster ? FindMonster(target->iMonsterID) : nullptr;
    TargetSourceIsCharacter(source, 0);
    text = fmt::format("{} ", g_localized_strings[0x1bf]);
    target_start = text.size();
    if (target_is_monster) {
        text += GetMonsterName(monster_info, 0, 0);
    } else {
        text += g_status.party.characters[target->iChar].name;
    }
    if (missile->retargeted) {
        text += " ";
        text += g_localized_strings[0x1c0];
    }
    target_stop = text.size();
    source_color = GetSourceNoticeColor(source);
    target_color = GetTargetNoticeColor(source, target);
    ShowNotice(source_color, text);
    if (target_color != source_color) {
        HighlightTextBoxRange(target_color, target_start, target_stop, -1);
    }
    if (deflected) {
        ShowNotice(source_color, g_localized_strings[0x214]);
        return;
    }

    if (target_is_monster) {
        record = GetMonsterDataForInfo(monster_info);
        hit_location = RollMonsterHitLocation(record);
        if (record->hit_location_chances[hit_location] < 100) {
            location_name =
                g_localized_strings[g_monster_hit_location_labels[hit_location]
                                                                 [record->constitution]];
        } else {
            location_name = "";
        }
    } else {
        hit_location = RollCharacterHitLocation();
        location_name = g_localized_strings[g_pc_hit_location_labels[hit_location][0]];
    }
    if (g_settings.verbose_combat_messages != 0) {
        ShowNotice(source_color, fmt::sprintf(g_localized_strings[0x20c], location_name));
    }

    attack_mode = g_missile_table[missile->missile_table_index].attack_mode;
    chance = AttackModeMod(1, attack_mode) + 50 +
             (10 - TargetArmorClassAtLocation(target, attack_mode, hit_location)) * 5;
    penetration_roll = Random(100) + 1;
    CombatLog(fmt::format("TO PENETRATE: Chance {:d}, Rolled {:d}", chance, penetration_roll));
    if (penetration_roll <= chance) {
        magnitude = RollEffectMagnitude(&missile->definition);
        if (magnitude > 0) {
            MakePCHitSound(missile, target, hit_location, -1);
            if (target_is_monster) {
                ApplyDamageToMonster(monster_info, magnitude, source, false, 1, 1, 0, false);
            } else {
                ApplyDamageToCharacter(target->iChar, magnitude, false, true, true, 0, false);
            }
            ApplyEffectConditions(source, target, &missile->definition, true, false, 0);
        } else if (g_settings.verbose_combat_messages != 0) {
            ShowNotice(source_color, g_localized_strings[0x20e]);
        }
    } else {
        ShowNotice(source_color, g_localized_strings[0x20f]);
        MakePCHitSound(missile, target, hit_location, -1);
    }
}

/* Which hand a character attacks with: the hand carrying the most attacks
   wins, ties go to the bigger damage bonus, and a hand that cannot reach is
   not a candidate. */
int ChooseCharacterAttackHand(int party_slot)
{
    int best_hand = -1;
    unsigned int best_attacks = 0;
    int best_damage_bonus = -999;
    unsigned int hand;

    for (hand = 0; hand < W8_HAND_COUNT; ++hand) {
        if (hand >= W8_HAND_COUNT) {
            Fail(
                "uiHand < HAND_COUNT", // reinterpret-ok: the assert message slot carries the failing hand index
                reinterpret_cast<const char*>(hand));
        }
        if (g_status.party.characters[party_slot].Hand[hand].in_play &&
            GetCharAttackRange(&g_status.party.characters[party_slot], hand) != W8_RANGE_NONE &&
            (best_attacks < g_status.party.characters[party_slot].Hand[hand].attacks ||
             (g_status.party.characters[party_slot].Hand[hand].attacks == best_attacks &&
              g_status.party.characters[party_slot].Hand[hand].damage_bonus > best_damage_bonus))) {
            best_attacks = g_status.party.characters[party_slot].Hand[hand].attacks;
            best_hand = hand;
            best_damage_bonus = g_status.party.characters[party_slot].Hand[hand].damage_bonus;
        }
    }
    return best_hand;
}

/* Pick the attacking hand and record each hand's reach-adjusted attack count
   on the combat row, zero for a hand that cannot reach. A character with no
   usable hand cannot be attacking. */
void PrepareCharacterAttacks(int party_slot)
{
    int best_hand = ChooseCharacterAttackHand(party_slot);
    unsigned int hand;
    int value;

    if (best_hand == -1) {
        DebugMessage(1, fmt::format("ERROR: {} is preparing attacks with when it's not possible!",
                                    g_status.party.characters[party_slot].name));
        return;
    }
    g_combat.state()->characters[party_slot].current_hand = best_hand;
    for (hand = 0; hand < W8_HAND_COUNT; ++hand) {
        if (hand >= W8_HAND_COUNT) {
            Fail(
                "uiHand < HAND_COUNT", // reinterpret-ok: the assert message slot carries the failing hand index
                reinterpret_cast<const char*>(hand));
        }
        if (!g_status.party.characters[party_slot].Hand[hand].in_play ||
            GetCharAttackRange(&g_status.party.characters[party_slot], hand) == W8_RANGE_NONE) {
            value = 0;
        } else {
            value = g_status.party.characters[party_slot].Hand[hand].attacks;
        }
        g_combat.state()->characters[party_slot].saved_attack_value[hand] = value;
        g_combat.state()->characters[party_slot].hand_attack_values[hand] = value;
    }
}

/* The situational armour-class adjustment a target gets against one attack
   mode: the character's conditional components, or the monster's bonus and
   fatigue share, plus the four points a distracted or flanked target loses
   against anything but the special modes. */
int GetTargetArmorClassModifier(W8CombatSlot* target, W8AttackMode attack_mode)
{
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;
    int modifier = 0;
    bool distracted = false;
    unsigned char out_of_formation;

    if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        modifier = monster_info->modifiers.armor_class_adjustment +
                   monster_info->modifiers.armor_flat +
                   static_cast<int>(FatigueArmorPenalty(monster_info->fatigue_band) / 10);
        if (g_combat.active()) {
            if ((g_combat.state()->unaware != 0 || g_combat.state()->natural_attack != 0) &&
                (record->flags & W8_MONSTER_FLAG_VULNERABLE_FROM_BEHIND) != 0) {
                modifier -= 2;
            }
            if (monster_info->action_kind == W8_MONSTER_ACTION_WAIT) {
                modifier += 2;
            }
            distracted = monster_info->action_kind == W8_MONSTER_ACTION_ADVANCE;
        }
        out_of_formation = monster_info->modifiers.out_of_formation;
    } else if (target->iType == W8_TARGET_KIND_CHARACTER) {
        W8Character* character = &g_status.party.characters[target->iChar];
        if (!g_combat.active()) {
            distracted = IsLevelFastMovement();
        } else {
            int component;
            if (g_combat.state()->unaware != 0 || g_combat.state()->natural_attack != 0) {
                component = character->armor_class_components[W8_AC_COMPONENT_SPEED];
                if (component > 0) {
                    modifier = -component;
                }
                component = character->armor_class_components[W8_AC_COMPONENT_SHIELD];
                if (component > 0) {
                    modifier -= component;
                }
            }
            if (GetEffectivePartyAction() == W8_PARTY_ACTION_NONE &&
                TryCharacterAction(target->iChar, W8_ACTION_PRAY, false)) {
                modifier -= 4;
            }
            distracted = GetEffectivePartyAction() == W8_PARTY_ACTION_RUN;
        }
        out_of_formation = character->bonus.out_of_formation;
    } else {
        Fail("FALSE");
        return 0;
    }
    if (distracted && out_of_formation == 0 &&
        (attack_mode != W8_ATTACK_MODE_THROW &&
         (attack_mode <= W8_ATTACK_MODE_KICK || attack_mode > W8_ATTACK_MODE_SHOOT))) {
        modifier -= 4;
    }
    return modifier;
}

/* Which of the hand's attack modes this swing uses: a random one of the set
   mode bits, walked cyclically until the roll's-th set bit. */
W8AttackMode CharChooseHandAttackMode(W8Character* character, int hand)
{
    W8ItemInstance* item;
    unsigned short modes;
    int slot;

    if (character->Hand[hand].uiHolds == HOLDS_NOTHING) {
        modes = character->Hand[hand].attack_flags;
    } else {
        slot = (hand != 0) + 6;
        item = &character->EquippedItem[slot];
        if (item->iItemNo == -1) {
            Fail("pPC->EquippedItem[uiWeaponSlot].iItemNo != -1",
                 fmt::format("CharChooseHandAttackMode: ERROR - {}'s weapon slot {:d} has "
                             "no item in it, but holds {:d}",
                             character->name, slot, character->Hand[hand].uiHolds));
        }
        modes = g_item_records[item->iItemNo].attack_flags;
    }
    if (modes == 0) {
        Fail("fsAttackModes > 0",
             fmt::format("CharChooseHandAttackMode: ERROR - {}'s hand {:d} has no attack modes",
                         character->name, hand));
    }
    return ChooseAttackMode(modes);
}

/* The bare display name of whatever a combat slot names, for the attack
   notices (spell notices use Magic.cpp's " at ..." form). A target that is its
   own source answers the reflexive pronoun instead of the name. */
static std::string CombatTargetName(const W8TargetSource* source, const W8CombatSlot* target)
{
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;
    W8MonsterGroup* monster_group;

    if (target == nullptr) {
        Fail("pTarget != NULL");
    }
    switch (target->iType) {
    case W8_TARGET_KIND_NONE:
    case W8_TARGET_KIND_PLACE:
        return "";
    case W8_TARGET_KIND_CHARACTER:
    case W8_TARGET_KIND_CHARACTER_INDIRECT:
        break;
    case W8_TARGET_KIND_PARTY:
        return g_localized_strings[0x1e3];
    case W8_TARGET_KIND_MONSTER:
        if (target->iMonsterID == -1) {
            Fail("pTarget->iMonsterID != BAD_INDEX");
        }
        monster_info = FindMonster(target->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        if (TargetSourceIsMonster(source, 0) && source->iMonsterID == target->iMonsterID) {
            return std::string(
                g_localized_strings[g_gender_name_message_rows[record->name_group][3]]);
        }
        return std::string(GetMonsterName(monster_info, record, 0));
    case W8_TARGET_KIND_GROUP:
        monster_group = FindMonsterGroup(target->iGroupID);
        return std::string(GetMonsterGroupName(monster_group));
    case W8_TARGET_KIND_ALL_ENEMIES:
        return g_localized_strings[0x1e4];
    case W8_TARGET_KIND_EIGHT:
        return g_localized_strings[0x1e5];
    case W8_TARGET_KIND_ITEM:
        return fmt::sprintf(g_localized_strings[0x1e6],
                            FormatItemDisplayName(g_status.party.itemAt(target->item), false));
    default:
        Fail("FALSE", fmt::format("SpellTargetString: ERROR - Invalid target type {:d}",
                                  fmt::underlying(target->iType)));
        return " - TARGET INVALID";
    }
    if (target->iChar == -1) {
        Fail("pTarget->iChar != BAD_INDEX");
    }
    if (TargetSourceIsCharacter(source, 0) && !source->name_known &&
        source->iChar == target->iChar) {
        return std::string(
            g_localized_strings
                [g_gender_name_message_rows[g_status.party.characters[source->iChar].gender][3]]);
    }
    return std::string(g_status.party.characters[target->iChar].name);
}

/* What the target's armour starts from before the situational modifier: the
   character's total, or ten minus the monster record's evasion - unless the
   monster is out of formation, which costs it a flat five. */
int GetTargetArmorClass(W8CombatSlot* target, W8AttackMode attack_mode)
{
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;
    /* Retail continued with an uninitialized base after the assertion;
       natively it starts at zero. */
    int base = 0;

    if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        base = 10 - record->evasion;
        if (monster_info->modifiers.out_of_formation != 0 && base > -5) {
            return GetTargetArmorClassModifier(target, attack_mode) - 5;
        }
    } else if (target->iType == W8_TARGET_KIND_CHARACTER) {
        base = g_status.party.characters[target->iChar].armor_class_total;
    } else {
        Fail("FALSE");
    }
    return GetTargetArmorClassModifier(target, attack_mode) + base;
}

/* The armour class a hit at one location must beat: the base above plus the
   location and attack-mode terms each target kind carries, with the
   situational modifier applied a second time through the base. */
int TargetArmorClassAtLocation(W8CombatSlot* target, W8AttackMode attack_mode, int hit_location)
{
    W8Character* character;
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;
    int base = GetTargetArmorClass(target, attack_mode);

    if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        return GetTargetArmorClassModifier(target, attack_mode) +
               (monster_info->modifiers.armor_matchup -
                record->armor_class_by_attack_mode[attack_mode] -
                record->armor_class_by_location[hit_location]) +
               record->evasion + base;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        character = &g_status.party.characters[target->iChar];
        return GetTargetArmorClassModifier(target, attack_mode) +
               (character->armor_class_by_location[hit_location] - character->armor_class_total) +
               base;
    }
    Fail("FALSE");
    return GetTargetArmorClassModifier(target, attack_mode) + hit_location + base;
}

/* Whether a swing the attacker landed is blocked outright: a character blocks
   with the off-hand item when their shield component covers the margin; a
   monster needs its record's blocking flag and a margin of ten or less. The
   block posts the notice and plays the material impact sound. */
bool BlockedForSpecialReason(int weapon_class, W8CombatSlot* target, int attack_value,
                             int armor_value, unsigned int palette)
{
    W8MonsterInfo* monster_info;
    W8MonsterRecord* record;
    int material;
    int difference = attack_value - armor_value;

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        W8Character* character = &g_status.party.characters[target->iChar];
        int shield = 0;
        if (g_combat.state()->unaware == 0 && g_combat.state()->natural_attack == 0) {
            shield = character->armor_class_components[W8_AC_COMPONENT_SHIELD] * 5;
        }
        if (difference > shield) {
            return false;
        }
        if (character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo != -1) {
            material =
                g_item_records[character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo]
                    .material;
        } else {
            material = 2;
        }
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        if ((record->flags & W8_MONSTER_FLAG_VULNERABLE_FROM_BEHIND) == 0) {
            return false;
        }
        if (difference > 10) {
            return false;
        }
        material = 2;
    } else {
        Fail("FALSE", "BlockedForSpecialReason: Unknown target type");
        return false;
    }
    if (g_settings.verbose_combat_messages != 0) {
        ShowNotice(palette, g_localized_strings[0x213]);
    }
    PlayCombatSound(GetMaterialImpactSound(weapon_class, material), 1, true, -1);
    return true;
}

/* The most a single attack may take off the current target: a fifth of its
   maximum hit points, at least one. */
unsigned int CapAttackDamageByTargetHealth(unsigned int damage)
{
    W8CombatSlot* target = &g_combat.state()->TargetHit;
    W8MonsterInfo* monster_info;
    unsigned int health;
    unsigned int limit;

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        health = g_status.party.characters[target->iChar].uiHPMax;
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        health = monster_info->uiHPMax;
    } else {
        Fail("FALSE");
        health = 0;
    }
    limit = health / 5;
    if (limit <= 1) {
        limit = 1;
    }
    if (damage > limit) {
        damage = limit;
    }
    return damage;
}

/* Launch the animation cycle for the attack the monster just committed to.
   Action details zero through eight pick cycles nine through seventeen; a
   monster that lacks the cycle falls back to six or seven. A ranged cycle on
   an attack whose record only reaches short range is a data error, corrected
   to long and reported; the reverse is corrected to short the same way. */
void StartMonsterAttackCycle(W8MonsterInfo* monster_info, W8AttackMode action_detail)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    unsigned int attack = monster_info->pCombat->attack_index;
    W8RangeCategory range;
    signed char cycle;

    switch (action_detail) {
    case W8_ATTACK_MODE_THRUST:
        cycle = W8_MONSTER_CYCLE_ATTACK_THRUST;
        break;
    case W8_ATTACK_MODE_SHOOT:
        cycle = W8_MONSTER_CYCLE_ATTACK_SHOOT;
        break;
    case W8_ATTACK_MODE_THROW:
        cycle = W8_MONSTER_CYCLE_ATTACK_THROW;
        break;
    case W8_ATTACK_MODE_SWING:
        cycle = W8_MONSTER_CYCLE_ATTACK_SWING;
        break;
    case W8_ATTACK_MODE_KICK:
        cycle = W8_MONSTER_CYCLE_ATTACK_KICK;
        break;
    case W8_ATTACK_MODE_BASH:
        cycle = W8_MONSTER_CYCLE_ATTACK_BASH;
        break;
    case W8_ATTACK_MODE_PUNCH:
        cycle = W8_MONSTER_CYCLE_ATTACK_PUNCH;
        break;
    case W8_ATTACK_MODE_LASH:
        cycle = W8_MONSTER_CYCLE_ATTACK_LASH;
        break;
    case W8_ATTACK_MODE_BERSERK:
        cycle = W8_MONSTER_CYCLE_ATTACK_MELEE;
        break;
    default:
        Fail("FALSE", fmt::format("StartMonsterAttack: ERROR - Invalid attack mode {:d}",
                                  fmt::underlying(action_detail)));
        return;
    }
    if (!monster_info->p3D->IsCycleSupported(cycle)) {
        switch (cycle) {
        case W8_MONSTER_CYCLE_ATTACK_SWING:
        case W8_MONSTER_CYCLE_ATTACK_THRUST:
        case W8_MONSTER_CYCLE_ATTACK_BASH:
        case W8_MONSTER_CYCLE_ATTACK_MELEE:
        case W8_MONSTER_CYCLE_ATTACK_PUNCH:
        case W8_MONSTER_CYCLE_ATTACK_KICK:
        case W8_MONSTER_CYCLE_ATTACK_LASH:
            cycle = W8_MONSTER_CYCLE_ATTACK_CLOSE;
            break;
        case W8_MONSTER_CYCLE_ATTACK_THROW:
        case W8_MONSTER_CYCLE_ATTACK_SHOOT:
            cycle = W8_MONSTER_CYCLE_ATTACK_RANGED;
            break;
        }
    }
    if (cycle == W8_MONSTER_CYCLE_ATTACK_SHOOT || cycle == W8_MONSTER_CYCLE_ATTACK_THROW ||
        cycle == W8_MONSTER_CYCLE_ATTACK_RANGED) {
        if (record->attacks[attack].range_category <= W8_RANGE_SHORT) {
            record->attacks[attack].range_category = W8_RANGE_EXTREME;
            DebugMessage(
                0, fmt::format("DATA ERROR: {} has mismatched range/attack modes for attack {:d}",
                               record->name0, attack));
        }
    } else if (record->attacks[attack].range_category >= W8_RANGE_LONG) {
        record->attacks[attack].range_category = W8_RANGE_TOUCH;
        DebugMessage(0,
                     fmt::format("DATA ERROR: {} has mismatched range/attack modes for attack {:d}",
                                 record->name0, attack));
    }
    range = GetMonsterActionRangeCategory(monster_info, record, attack);
    if (range >= W8_RANGE_LONG) {
        monster_info->fMissileReleased = false;
    }
    ResetCombatSlot(&g_combat.state()->TargetHit);
    StartMonsterCycle(monster_info, cycle, W8_ANIMATION_PLAY_ONCE);
}

/* Release queued casualty notices and reset the attack result. */
static void DrainCombatDeathReports(W8SpellEffectResult* report)
{
    while (!report->reports.empty()) {
        auto entry = std::move(report->reports.front());
        report->reports.erase(report->reports.begin());
        if (entry != nullptr) {
            if (entry->kind == W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH) {
                PostCharacterNotice(
                    entry->value,
                    fmt::format(
                        "{}!",
                        g_localized_strings[g_condition_notices[W8_CONDITION_DEAD].singular]));
            } else if (entry->kind == W8_SPELL_DAMAGE_REPORT_MONSTER_DEATH) {
                ShowNotice(
                    W8_FONT_PALETTE_RUST,
                    fmt::format(
                        "{} {}!", entry->text,
                        g_localized_strings[g_condition_notices[W8_CONDITION_DEAD].singular]));
            }
        }
    }
    *report = {};
}

/* Both attack result paths announce target conditions in the same way. */
static void AnnounceCombatTargetCondition(const W8SpellEffectResult* report, unsigned int condition,
                                          W8MonsterInfo* monster_info)
{
    const std::string& name = g_localized_strings[g_condition_notices[condition].singular];
    if (report->target.iType == W8_TARGET_KIND_MONSTER) {
        ShowNotice(W8_FONT_PALETTE_RUST,
                   fmt::format("{} {}!", GetMonsterName(monster_info, nullptr, 0), name));
    } else if (report->target.iType == W8_TARGET_KIND_CHARACTER) {
        PostCharacterNotice(report->target.iChar, fmt::format("{}!", name));
    }
}

/* Announce a character's attack on the combat log: the swings line, the
   damage notices, what the target suffered unless it died, and the casualty
   list - then wipe the record clean for the next attack. */
/* The two combat streams use the same damage/condition report; their
   speaker, colour and the monster-only Missing condition remain distinct. */
static void ReportAttackResult(int party_slot, W8MonsterInfo* attacker, W8SpellEffectResult* report)
{
    const bool monster_attacker = attacker != nullptr;
    const unsigned int palette = monster_attacker ? W8_FONT_PALETTE_RUST : W8_FONT_PALETTE_WHITE;
    auto post = [&](std::string_view message) {
        if (monster_attacker)
            PostMonsterNotice(attacker, message);
        else
            PostCharacterNotice(party_slot, message);
    };
    auto postNoticeValue = [&](int index, int text_id) {
        if (report->notice_values[index] > 0)
            post(fmt::sprintf(g_localized_strings[text_id], report->notice_values[index]));
    };

    if (report->condition_counts[W8_CONDITION_DEAD] == 0) {
        if (report->count == 0) {
            ShowNotice(palette, g_localized_strings[report->missed ? 0x20b : 0x209]);
            return;
        }
        if (report->amount == 0) {
            ShowNotice(palette, g_localized_strings[0x20e]);
        } else if (report->count == 1) {
            post(fmt::sprintf(g_localized_strings[0x265], report->amount));
        } else if (report->count > 1) {
            post(fmt::sprintf(g_localized_strings[0x266], report->count, report->amount));
        }
    }
    postNoticeValue(3, 0x267);
    postNoticeValue(4, 0x268);

    if (report->condition_counts[W8_CONDITION_DEAD] == 0) {
        W8MonsterInfo* target_monster = nullptr;
        w8_unaligned_uint* condition_turns;
        if (report->target.iType == W8_TARGET_KIND_MONSTER) {
            target_monster = FindMonster(report->target.iMonsterID);
            condition_turns = target_monster->uiCondition;
        } else {
            condition_turns = g_status.party.characters[report->target.iChar].uiCondition;
        }
        if (condition_turns[W8_CONDITION_DEAD] == 0) {
            for (int index = 0; index < 3; ++index) {
                static constexpr int text_ids[] = {0x1a6, 0x264, 0x261};
                if (report->notice_values[index] > 0)
                    ShowNotice(palette, fmt::sprintf(g_localized_strings[text_ids[index]],
                                                     report->notice_values[index]));
            }
            if (!monster_attacker && report->notice_values[5] > 0) {
                const W8Character& character = g_status.party.characters[party_slot];
                if (character.hp_current == static_cast<unsigned int>(character.uiHPMax))
                    post(g_localized_strings[0x258]);
                else
                    post(fmt::sprintf(g_localized_strings[0x25a], report->notice_values[5]));
            }
            for (unsigned int condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
                if (report->condition_counts[condition] == 0 || condition == W8_CONDITION_DRAINED)
                    continue;
                if (condition == W8_CONDITION_MISSING) {
                    if (monster_attacker && report->target.iType == W8_TARGET_KIND_CHARACTER &&
                        GetConditionRecordFlag(report->target.iChar, W8_DEPENDENCE_SWALLOWED))
                        PostCharacterNotice(report->target.iChar, g_localized_strings[0x1d5]);
                    continue;
                }
                AnnounceCombatTargetCondition(report, condition, target_monster);
            }
        }
    }
    DrainCombatDeathReports(report);
}

void ReportCharacterAttackResult(int party_slot, W8SpellEffectResult* report)
{
    ReportAttackResult(party_slot, nullptr, report);
}

static int GetCombatFumbleChance(const std::vector<W8CombatSlot>& targets, int attack_score)
{
    int chance;
    if (attack_score < 100) {
        chance = static_cast<int>(pow(100 - attack_score, 3.0) * 1e-5 + 0.5);
        const auto count = wiz8::checked_integer<unsigned int>(targets.size());
        if (count == 0) {
            Fail("uiNumTargets > 0");
        }
        chance = count * chance * 10 / 100;
    } else {
        chance = 0;
    }
    chance = std::clamp(chance, 0, 100);
    return chance;
}

static W8CombatSlot ChooseCombatFumbleTarget(const std::vector<W8CombatSlot>& targets)
{
    const auto count = wiz8::checked_integer<unsigned int>(targets.size());
    if (count == 0) {
        Fail("uiChoices > 0");
    }
    return targets[Random(count)];
}

static int RollBackstabExtraDice(int chance)
{
    unsigned int roll = Random(100);
    int extra = 0;
    if (roll + 15 < static_cast<unsigned int>(chance)) {
        extra = 1;
        if (roll + 75 < static_cast<unsigned int>(chance)) {
            extra = 2;
            if (roll + 95 < static_cast<unsigned int>(chance)) {
                extra = 3;
            }
        }
    }
    return extra;
}

static void MarkDefenderSkillUse(int party_slot)
{
    W8Character* defender = &g_status.party.characters[party_slot];
    if (defender->skills[W8_SKILL_STEALTH].active) {
        g_combat.state()->characters[party_slot].skill_use_flags[W8_SKILL_STEALTH] = 1;
    }
    if (defender->skills[W8_SKILL_SHIELD].active && g_combat.state()->unaware == 0 &&
        g_combat.state()->natural_attack == 0 &&
        defender->armor_class_components[W8_AC_COMPONENT_SHIELD] > 0) {
        g_combat.state()->characters[party_slot].skill_use_flags[W8_SKILL_SHIELD] = 1;
    }
    if (defender->skills[W8_SKILL_REFLEXTION].active) {
        g_combat.state()->characters[party_slot].skill_use_flags[W8_SKILL_REFLEXTION] = 1;
    }
}

/* Both character and monster attacks report invalid targets identically. */
static void ReportInvalidAttackTarget(const W8CombatSlot& target, const W8TargetSource& source)
{
    DebugMessage(!g_dev_mode,
                 fmt::format("InvalidAttackTarget: Target Type {:d}(char {:d},ID "
                             "{:d}), Source Type {:d}(char {:d},ID {:d})",
                             fmt::underlying(target.iType), target.iChar, target.iMonsterID,
                             fmt::underlying(source.iType), source.iChar, source.iMonsterID));
}

/* Resolve one queued swing of the monster's attack: rolls the hit chance and
   the fumble redirection, resolves guardian interception, picks the hit
   location, rolls penetration, applies damage and the struck target's
   retaliation enchantment, then decides whether the monster swings again,
   repicks a target or is done. */
W8CombatActionStatus ContinueMonsterAttack(W8MonsterInfo* monster_info, W8MonsterRecord* record)
{
    W8SpellEffectResult* report = &g_combat.state()->attack_report;
    W8SpellEffectResult local_report;
    W8MonsterCombatState* combat = monster_info->pCombat.get();
    unsigned int attack_index = combat->attack_index;
    W8AttackMode action_detail = static_cast<W8AttackMode>(monster_info->action_detail);
    W8MonsterAttack* attack = &record->attacks[attack_index];
    W8MonsterInfo* target_info = nullptr;
    W8MonsterRecord* target_record;
    W8TargetSource source;
    W8TargetSource victim_source;
    W8CombatSlot entry_target;
    W8CombatSlot repick_target;
    W8SpellEffectDefinition effect;
    int event_ids[3];
    std::string location_name;
    bool verbose = g_settings.verbose_combat_messages != 0;
    bool swing_missed = false;
    bool deflected = false;
    bool guaranteed_hit = false;
    bool guaranteed_penetration = false;
    bool fumbled = false;
    bool repicked = false;
    unsigned int queued_fatigue = 1;
    unsigned int hit_location = 0;
    unsigned int damage = 0;
    unsigned int applied = 0;
    unsigned int dice_count = 0;
    int message_id = 0;
    int to_hit = 0;
    int roll = 0;
    W8RangeCategory range;
    std::string_view location_text;

    SetTargetSourceToMonster(monster_info, &source);
    entry_target = monster_info->Target;
    if (entry_target.iType == W8_TARGET_KIND_CHARACTER) {
        MarkDefenderSkillUse(entry_target.iChar);
    }
    range = GetMonsterActionRangeCategory(monster_info, record, attack_index);
    if (range < W8_RANGE_LONG) {
        to_hit = GetMonsterAttackScore(monster_info, attack, action_detail, 0);
        roll = Random(100) + 1;
        guaranteed_hit = roll <= 5;
        swing_missed = roll > 95;
        std::vector<W8CombatSlot> fumble_list;
        BuildMonsterTargetList(monster_info, record, attack_index, fumble_list);
        int redirect_chance = GetMonsterAttackScore(monster_info, attack, action_detail, 1);
        int fumble_chance = GetCombatFumbleChance(fumble_list, redirect_chance);
        fumbled = roll > 100 - fumble_chance;
        CombatLog(fmt::format("TO HIT: Chance {:d}, Rolled {:d} (fumble {:d}%)", to_hit, roll,
                              fumble_chance));
        if (!guaranteed_hit && fumbled) {
            if (!verbose) {
                local_report = {};
                report = &local_report;
            }
            g_combat.state()->TargetHit = ChooseCombatFumbleTarget(fumble_list);
            AnnounceAccidentalStrike(&source, &g_combat.state()->TargetHit);
            guaranteed_hit = true;
            guaranteed_penetration = false;
            swing_missed = false;
            roll = Random(100) + 1;
            g_combat.state()->unaware = 0;
            g_combat.state()->natural_attack = 0;
        } else {
            g_combat.state()->TargetHit = monster_info->Target;
            guaranteed_penetration = guaranteed_hit;
        }
    } else {
        if (!monster_info->fMissileReleased) {
            Fail("pMonsterInfo->fMissileReleased",
                 fmt::format("ContinueMonsterAttack: ERROR - Missile not released, ID "
                             "{:d}, cycle {:d}, pending {:d}",
                             monster_info->location_id,
                             monster_info->p3D->Query(W8_MONSTER_QUERY_CYCLE),
                             monster_info->p3D->visual.pending_cycle));
        }
        if (g_combat.state()->missile_hit_result == W8_MISSILE_HIT) {
            swing_missed = false;
            guaranteed_hit = true;
            guaranteed_penetration = false;
            roll = Random(100) + 1;
        } else if (g_combat.state()->missile_hit_result == W8_MISSILE_HIT_DEFLECTED) {
            swing_missed = false;
            deflected = true;
        } else {
            swing_missed = true;
        }
    }
    if (!verbose) {
        report->deferred = true;
    }
    if (swing_missed) {
        if (g_settings.verbose_combat_messages != 0) {
            ShowNotice(W8_FONT_PALETTE_RUST, g_localized_strings[0x209]);
        }
        ResetCombatSlot(&g_combat.state()->TargetHit);
    } else if (deflected) {
        if (g_settings.verbose_combat_messages == 0) {
            report->missed = true;
        } else {
            ShowNotice(W8_FONT_PALETTE_RUST, g_localized_strings[0x20a]);
        }
        ResetCombatSlot(&g_combat.state()->TargetHit);
    } else {
        source.target_diverted = (g_combat.state()->TargetHit != monster_info->Target);
        if (!IsTargetStillPresent(&g_combat.state()->TargetHit)) {
            goto invalid_target;
        }
        if (!verbose && source.target_diverted) {
            local_report = {};
            report = &local_report;
        }
        ResolveGuardianInterception(&source, &g_combat.state()->TargetHit);
        PointCameraAtCombatTarget(&source, &g_combat.state()->TargetHit);
        if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_CHARACTER) {
            unsigned int total = 0;
            unsigned int location_roll = Random(100);
            for (hit_location = 0;; ++hit_location) {
                if (hit_location >= W8_PC_HIT_LOCATIONS) {
                    if (monster_info == nullptr) {
                        DebugMessage(1, "ERROR: gubLocalACPercent total only %d%%");
                    } else {
                        DebugMessage(
                            0, fmt::format(
                                   "ERROR: DBS Attack Locations total only {:d}% for monster {}",
                                   total, GetMonsterName(monster_info, nullptr, 0)));
                    }
                    hit_location = 1;
                    break;
                }
                total += record->attack_body_part_chances[hit_location];
                if (location_roll < total) {
                    break;
                }
            }
            location_text = g_localized_strings[g_pc_hit_location_labels[hit_location][0]];
        } else {
            if (g_combat.state()->TargetHit.iType != W8_TARGET_KIND_MONSTER) {
                goto invalid_target;
            }
            target_info = FindMonster(g_combat.state()->TargetHit.iMonsterID);
            target_record = GetMonsterDataForInfo(target_info);
            hit_location = RollMonsterHitLocation(target_record);
            if (target_record->hit_location_chances[hit_location] < 100) {
                location_text =
                    g_localized_strings[g_monster_hit_location_labels[hit_location]
                                                                     [target_record->constitution]];
            } else {
                location_text = "";
            }
        }
        location_name = location_text;
        queued_fatigue = 1;
        if (!guaranteed_hit && to_hit < roll) {
            if (!BlockedForSpecialReason(attack->weapon_class, &g_combat.state()->TargetHit, roll,
                                         to_hit, 9)) {
                if (verbose) {
                    ShowNotice(W8_FONT_PALETTE_RUST, g_localized_strings[0x209]);
                }
                if (!TryPanicWoundedCharacter(&g_combat.state()->TargetHit) &&
                    g_combat.state()->TargetHit.iType == W8_TARGET_KIND_CHARACTER &&
                    Random(100) < 0x32) {
                    event_ids[0] = g_special_event19;
                    event_ids[1] = g_special_event15;
                    event_ids[2] = g_special_event20;
                    QueueCharacterEvent(
                        &g_status.party.characters[g_combat.state()->TargetHit.iChar],
                        event_ids[Random(3)], 0, kCharacterEventFullVolume);
                }
                if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER &&
                    target_info->p3D->IsFacingMonster(monster_info->p3D) != 0 &&
                    !target_info->fMotionless) {
                    StartMonsterCycle(target_info, W8_MONSTER_CYCLE_DODGE, W8_ANIMATION_PLAY_ONCE);
                }
            }
        } else {
            if (g_settings.verbose_combat_messages == 0) {
                report->target = g_combat.state()->TargetHit;
            } else {
                ShowNotice(W8_FONT_PALETTE_RUST,
                           fmt::sprintf(g_localized_strings[0x20c], location_name));
            }
            queued_fatigue = 2;
            int penetration =
                (monster_info->modifiers.attack_bonus + monster_info->modifiers.hit_bonus) * 5 +
                attack->attack_score;
            penetration += AttackModeMod(1, action_detail);
            for (unsigned char g_low_fatigue_attack_verb : g_low_fatigue_attack_verbs) {
                if (attack->ubWeaponNameIndex == g_low_fatigue_attack_verb) {
                    penetration -= FatigueArmorPenalty(monster_info->fatigue_band);
                    break;
                }
            }
            penetration -= TargetArmorClassAtLocation(&g_combat.state()->TargetHit, action_detail,
                                                      hit_location) *
                           5;
            const int target_sum = SumTargetCombatAttributes(g_combat.state()->TargetHit);
            penetration += (SumMonsterCombatAttributes(*monster_info) - target_sum) / 10;
            ScaleValueForMonsterDifficulty(monster_info, &penetration);
            CombatLog(fmt::format("TO PENETRATE: Chance {:d}, Rolled was {:d}", penetration, roll));
            if (!guaranteed_penetration && penetration < roll) {
                message_id = 0x20f;
            } else {
                queued_fatigue = 3;
                damage = ResolveMonsterAttackDamage(monster_info, attack, &dice_count);
                if (damage == 0) {
                    message_id = 0x20e;
                } else {
                    if (fumbled) {
                        damage = CapAttackDamageByTargetHealth(damage);
                    } else if (dice_count > 1 && verbose) {
                        ShowNotice(W8_FONT_PALETTE_RUST,
                                   fmt::sprintf(g_localized_strings[0x20d], dice_count));
                    }
                    MakeMonsterHitSound(attack, &g_combat.state()->TargetHit, hit_location, -1);
                    if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_CHARACTER) {
                        applied = ApplyDamageToCharacter(g_combat.state()->TargetHit.iChar, damage,
                                                         false, verbose, verbose,
                                                         verbose ? nullptr : report, false);
                    } else {
                        applied = ApplyDamageToMonster(target_info, damage, &source, false, verbose,
                                                       verbose, verbose ? nullptr : report, false);
                    }
                    if (applied != 0) {
                        memset(&effect, 0, sizeof(effect));
                        memcpy(effect.condition_chances, attack->missile_values, 0x10);
                        effect.power_level =
                            record->effective_level +
                            (record->effective_level > 0xe ? 0xf : record->effective_level);
                        effect.magnitude_base = attack->missile_magnitude;
                        ApplyEffectConditions(&source, &g_combat.state()->TargetHit, &effect,
                                              verbose, false, report);
                        if (range < W8_RANGE_LONG &&
                            g_combat.state()->TargetHit.iType == W8_TARGET_KIND_CHARACTER &&
                            hit_location == 1 &&
                            g_status.party.characters[g_combat.state()->TargetHit.iChar]
                                    .EquippedItem[W8_EQUIP_SLOT_TORSO]
                                    .iItemNo == 500) {
                            SetTargetSourceToCharacter(g_combat.state()->TargetHit.iChar,
                                                       &victim_source);
                            ApplyDirectDamageToMonster(monster_info, &victim_source, damage);
                        }
                    }
                }
            }
            if (damage == 0) {
                MakeMonsterHitSound(attack, &g_combat.state()->TargetHit, hit_location, 0x2a);
                if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER) {
                    MonsterReactsToBeingStruck(target_info, &source, false);
                }
                if (!verbose) {
                    ++report->count;
                } else {
                    ShowNotice(W8_FONT_PALETTE_RUST, g_localized_strings[message_id]);
                }
            }
            if (range < W8_RANGE_LONG) {
                ApplyRazorCloakBacklash(
                    g_combat.state()->TargetHit, target_info,
                    [&](W8TargetSource* backlash_source, W8Enchantment* enchantment) {
                        ApplyDiceDamageToMonster(monster_info, backlash_source, enchantment);
                    });
            }
        }
        ApplyQueuedFatigue(&g_combat.state()->TargetHit, queued_fatigue, 1);
    }
    if (combat->uiSwingsRemaining == 0) {
        Fail("pCmbt->uiSwingsRemaining > 0");
    }
    --combat->uiSwingsRemaining;
    if (monster_info->hp_current == 0 && monster_info->highest_condition > W8_CONDITION_WEBBED) {
        combat->uiSwingsRemaining = 0;
    }
    if (combat->uiSwingsRemaining != 0) {
        if (record->attack_multiple_targets != 0 && range < W8_RANGE_LONG) {
            repick_target = monster_info->Target;
            if (!ChooseRandomMonsterAction(monster_info, false, true, false) ||
                monster_info->action_kind != W8_MONSTER_ACTION_ATTACK) {
                combat->uiSwingsRemaining = 0;
            } else {
                repicked = true;
                if ((repick_target != monster_info->Target)) {
                    ReportMonsterAttackResult(monster_info, report);
                    if (fumbled && g_combat.state()->attack_report.deferred) {
                        ReportMonsterAttackResult(monster_info, &g_combat.state()->attack_report);
                    }
                    g_combat.state()->attack_message = g_localized_strings[0x210];
                    AnnounceMonsterAttack(monster_info, record, 1);
                }
            }
        }
        if (combat->uiSwingsRemaining != 0) {
            if (((entry_target == monster_info->Target) || repicked) &&
                TargetMatchesNeeded(&monster_info->Target, W8_TARGET_NEED_ENEMY)) {
                if (!repicked) {
                    StartMonsterAttackCycle(monster_info, action_detail);
                }
                if ((!verbose && fumbled) || source.target_diverted) {
                    ReportMonsterAttackResult(monster_info, report);
                    PostMonsterNotice(
                        monster_info,
                        fmt::sprintf(g_localized_strings[0x269],
                                     CombatTargetName(&source, &monster_info->Target)));
                }
                return W8CombatActionStatus::resolving;
            }
        }
    }
    if (!verbose) {
        ReportMonsterAttackResult(monster_info, report);
        if (fumbled || source.target_diverted) {
            PostMonsterNotice(monster_info,
                              fmt::sprintf(g_localized_strings[0x269],
                                           CombatTargetName(&source, &monster_info->Target)));
            if (g_combat.state()->attack_report.deferred) {
                ReportMonsterAttackResult(monster_info, &g_combat.state()->attack_report);
            }
        }
    }
    return W8CombatActionStatus::finishing;

invalid_target:
    ReportInvalidAttackTarget(g_combat.state()->TargetHit, source);
    return W8CombatActionStatus::finishing;
}

/* The monster-side counterpart: the same swing/damage/casualty walk reported
   against the monster's own notice stream. */
void ReportMonsterAttackResult(W8MonsterInfo* monster_info, W8SpellEffectResult* report)
{
    ReportAttackResult(-1, monster_info, report);
}

/* Begin one of the monster's attacks for the round: validate the state, rate
   the pending attack, confirm the target is still reachable, roll the swing
   count, announce it and start the cycle. */
bool StartMonsterAttack(W8MonsterInfo* monster_info, W8MonsterRecord* record)
{
    W8MonsterCombatState* combat;
    unsigned int attack;

    if (monster_info == 0) {
        Fail("pMonsterInfo != NULL");
    }
    if (record == 0) {
        Fail("pMonsterDB != NULL");
    }
    combat = monster_info->pCombat.get();
    if (combat == 0) {
        Fail("pCmbt != NULL");
    }
    g_combat.state()->attack_report = {};
    if (combat->attacks_per_round == 0) {
        DebugMessage(
            1, fmt::format(
                   "ERROR: Monster ID {:d} is starting attack with 0 of {:d} attacks remaining!",
                   monster_info->location_id, combat->attacks_per_round0));
        return false;
    }
    attack = combat->attack_index;
    --combat->attacks_per_round;
    W8AttackMode action_detail = static_cast<W8AttackMode>(monster_info->action_detail);
    if (RateMonsterAttack(monster_info, record, attack, 0, false) != 0) {
        return false;
    }
    if (!TargetMatchesNeeded(&monster_info->Target, W8_TARGET_NEED_ENEMY) ||
        !MonsterActionReachesTarget(monster_info, record, attack, &monster_info->Target)) {
        if (!ClearMonsterCombatSlot(monster_info)) {
            return false;
        }
    }
    combat->uiSwingsRemaining = Random(record->swings_per_round) + 1;
    AnnounceMonsterAttack(monster_info, record, 0);
    StartMonsterAttackCycle(monster_info, action_detail);
    return true;
}

/* Attack weapon-type name string ids; the monster attack announcement indexes
   it two words per type. */
static unsigned short g_monster_attack_name_ids[0x25] = {
    0x619, 0x61a, 0x61b, 0x61c, 0x61d, 0x61e, 0x61f, 0x620, 0x621, 0x622, 0x623, 0x624, 0x625,
    0x626, 0x627, 0x628, 0x629, 0x62a, 0x62b, 0x62c, 0x62d, 0x62e, 0x62f, 0x630, 0x631, 0x632,
    0x633, 0x634, 0x635, 0x636, 0x637, 0x638, 0x639, 0x63a, 0x63b, 0x63c, 0x63d,
};

/* Attack verb-class string ids; the monster attack announcement appends the
   entry the attack's verb byte selects. */
static unsigned short g_monster_attack_verb_ids[0x60] = {
    0x5b8, 0x5b9, 0x5ba, 0x5bb, 0x5bc, 0x5bd, 0x5be, 0x5bf, 0x5c0, 0x5c1, 0x5c2, 0x5c3,
    0x5c4, 0x5c5, 0x5c6, 0x5c7, 0x5c8, 0x5c9, 0x5ca, 0x5cb, 0x5cc, 0x5cd, 0x5ce, 0x5cf,
    0x5d0, 0x5d1, 0x5d2, 0x5d3, 0x5d4, 0x5d5, 0x5d6, 0x5d7, 0x5d8, 0x5d9, 0x5da, 0x5db,
    0x5dc, 0x5dd, 0x5de, 0x5df, 0x5e0, 0x5e1, 0x5e2, 0x5e3, 0x5e4, 0x5e5, 0x5e6, 0x5e7,
    0x5e8, 0x5e9, 0x5ea, 0x5eb, 0x5ec, 0x5ed, 0x5ee, 0x5ef, 0x5f0, 0x5f1, 0x5f2, 0x5f3,
    0x5f4, 0x5f5, 0x5f6, 0x5f7, 0x5f8, 0x5f9, 0x5fa, 0x5fb, 0x5fc, 0x5fd, 0x5fe, 0x5ff,
    0x600, 0x601, 0x602, 0x603, 0x604, 0x605, 0x606, 0x607, 0x608, 0x609, 0x60a, 0x60b,
    0x60c, 0x60d, 0x60e, 0x60f, 0x610, 0x611, 0x612, 0x613, 0x614, 0x615, 0x616, 0x617,
};

/* Whether a character catches the incoming attack in time to turn toward it.
   Only a living, alert defender or protector gets the roll; the first try
   always succeeds and each retry is 25 points harder on the senses check. */
bool CharacterNoticesAttacker(int party_slot)
{
    W8CombatCharacterRow* row = &g_combat.state()->characters[party_slot];
    bool noticed = false;

    if (g_status.party.characters[party_slot].hp_current == 0 ||
        g_status.party.characters[party_slot].stamina == 0 ||
        g_status.party.characters[party_slot].highest_condition >= W8_CONDITION_WEBBED ||
        g_status.party.characters[party_slot].uiCondition[W8_CONDITION_BLIND] != 0) {
        return false;
    }
    if (!TryCharacterAction(party_slot, W8_ACTION_DEFEND, true) &&
        !TryCharacterAction(party_slot, W8_ACTION_PROTECT, true)) {
        return false;
    }
    if (row->spot_attempts == 0) {
        row->spot_attempts = 1;
        return true;
    }
    int senses = g_status.party.characters[party_slot].attributes[W8_ATTRIBUTE_SENSES].effective -
                 row->spot_attempts * 0x19;
    noticed = Random(100) < static_cast<unsigned int>(senses);
    ++row->spot_attempts;
    return noticed;
}

/* Build the "<monster> <attack> <target>" announcement for the monster's
   committed attack, flag a target that never saw it coming, highlight the
   target's name and turn the monster toward what it is striking. */
static void AppendAttackWeaponName(std::string_view name, int mode)
{
    if (g_settings.verbose_combat_messages != 0) {
        if (g_combat.state()->natural_attack != 0) {
            g_combat.state()->attack_message += " ";
            g_combat.state()->attack_message += g_localized_strings[0x215];
        } else if (mode == W8_ATTACK_MODE_BERSERK) {
            g_combat.state()->attack_message += g_localized_strings[0x215];
        }
        g_combat.state()->attack_message += name;
    }
}

void AnnounceMonsterAttack(W8MonsterInfo* monster_info, W8MonsterRecord* record,
                           char target_changed)
{
    W8MonsterCombatState* combat = monster_info->pCombat.get();
    W8AttackMode action_detail = static_cast<W8AttackMode>(monster_info->action_detail);
    unsigned int attack = combat->attack_index;
    W8MonsterAttack* pAttack = &record->attacks[attack];
    W8RangeCategory range = GetMonsterActionRangeCategory(monster_info, record, attack);
    W8MonsterCombatState* second_combat;
    W8MonsterInfo* second;
    std::string text;
    unsigned char palette;
    unsigned char highlight_start;
    unsigned char highlight_end;
    unsigned short name_id;
    int attempts;
    bool notices;

    g_combat.state()->unaware = 0;
    if (record->kind == 3 && range <= W8_RANGE_TOUCH) {
        g_combat.state()->natural_attack = 1;
    } else {
        g_combat.state()->natural_attack = 0;
    }
    g_combat.state()->attack_message = fmt::format("{} ", GetMonsterName(monster_info, record, 0));
    if (target_changed != 0) {
        g_combat.state()->attack_message += g_localized_strings[0x211];
        g_combat.state()->attack_message += " ";
    }
    if (g_combat.state()->natural_attack == 0) {
        if (pAttack->weapon_type == 1) {
            name_id = g_attack_flag_name_ids[action_detail][1];
        } else {
            if (pAttack->weapon_type == 0) {
                DebugMessage(
                    0, fmt::format(
                           "DATA ERROR: Attack {:d} weapon type is NONE for monster {} -> Charles",
                           attack, GetMonsterName(monster_info, 0, 0)));
            }
            name_id = g_monster_attack_name_ids[pAttack->weapon_type * 2];
        }
        g_combat.state()->attack_message += g_localized_strings[name_id];
        g_combat.state()->attack_message += " ";
        if (pAttack->ubWeaponNameIndex != 0) {
            text = g_localized_strings[g_monster_attack_verb_ids[pAttack->ubWeaponNameIndex]];
            AppendAttackWeaponName(text, action_detail);
        }
    } else {
        g_combat.state()->attack_message += g_localized_strings[0x204];
        g_combat.state()->attack_message += " ";
    }
    if (g_combat.state()->natural_attack == 0 && pAttack->ubWeaponNameIndex != 0) {
        if (g_settings.verbose_combat_messages == 0) {
            switch (action_detail) {
            case W8_ATTACK_MODE_SWING:
            case W8_ATTACK_MODE_THRUST:
            case W8_ATTACK_MODE_THROW:
                g_combat.state()->attack_message += g_localized_strings[0x217];
                break;
            case W8_ATTACK_MODE_BERSERK:
                g_combat.state()->attack_message += g_localized_strings[0x218];
                break;
            default:
                break;
            }
        } else {
            g_combat.state()->attack_message += g_localized_strings[0x216];
        }
    }
    highlight_start = g_combat.state()->attack_message.size();
    if (monster_info->Target.iType == W8_TARGET_KIND_CHARACTER) {
        int party_slot = monster_info->Target.iChar;
        MonsterVsCharDisposition(party_slot, monster_info);
        g_combat.state()->attack_message += g_status.party.characters[party_slot].name;
        palette = g_status.party.rows[party_slot].party_order_index;
        if (range <= W8_RANGE_SHORT && !IsCharacterFacingMonster(party_slot, monster_info)) {
            if (!CharacterNoticesAttacker(party_slot)) {
                g_combat.state()->unaware = IsMonsterBehindCharacter(monster_info, party_slot);
            } else {
                TurnCharacterTowardMonster(party_slot, monster_info);
            }
        }
    } else {
        if (monster_info->Target.iType != W8_TARGET_KIND_MONSTER) {
            Fail("pMonsterInfo->Target.iType == TARGET_TYPE_MONSTER");
        }
        second = FindMonster(monster_info->Target.iMonsterID);
        MonsterHostility(monster_info, second);
        g_combat.state()->attack_message += GetMonsterName(second, 0, 0);
        palette = 9;
        if (range <= W8_RANGE_SHORT && IsMonsterFacingMonster(second, monster_info) == 0) {
            second_combat = second->pCombat.get();
            notices = false;
            if (second->hp_current != 0 && second->stamina != 0 &&
                second->highest_condition < W8_CONDITION_WEBBED &&
                second->uiCondition[W8_CONDITION_BLIND] == 0 &&
                second->action_kind == W8_MONSTER_ACTION_WAIT) {
                attempts = second_combat->spot_attempts;
                if (attempts == 0) {
                    notices = true;
                } else {
                    notices = Random(100) < static_cast<unsigned int>(
                                                second->attributes[W8_MONSTER_ATTRIBUTE_SENSES] +
                                                attempts * -0x19);
                }
                ++second_combat->spot_attempts;
                if (notices) {
                    second->p3D->AimAtMonster(monster_info->p3D, false);
                }
            }
            if (!notices) {
                g_combat.state()->unaware = IsMonsterLookingAwayFrom(monster_info, second);
            }
        }
    }
    highlight_end = g_combat.state()->attack_message.size();
    if (g_combat.state()->natural_attack == 0) {
        if (g_combat.state()->unaware != 0) {
            text = g_localized_strings[0x207];
            g_combat.state()->attack_message += text;
        }
    } else {
        g_combat.state()->attack_message += g_localized_strings[0x212];
        if (pAttack->ubWeaponNameIndex == 0) {
            Fail("pAttack->ubWeaponNameIndex != MON_WEAPON_NAME_UNARMED");
        }
        if (pAttack->ubWeaponNameIndex != 0) {
            text = g_localized_strings[g_monster_attack_verb_ids[pAttack->ubWeaponNameIndex]];
            g_combat.state()->attack_message += text;
        }
    }
    if (combat->uiSwingsRemaining > 1 &&
        (record->attack_multiple_targets == 0 || range > W8_RANGE_SHORT)) {
        g_combat.state()->attack_message += fmt::format(" {:d}x", combat->uiSwingsRemaining);
    }
    ShowNotice(W8_FONT_PALETTE_RUST, g_combat.state()->attack_message);
    if (palette != 9) {
        HighlightTextBoxRange(palette, highlight_start, highlight_end, -1);
    }
    OrientMonsterTowardTarget(monster_info, false);
}

/* The character-side attack score against the pending action's target: the
   hand attack's own score plus five per point of hit and modifier-block
   bonus, the attack-mode term, the caught-unaware and natural-attack bonuses,
   the fatigue penalty - halved for modern weapons - and a repick-count
   surprise term. When the flag is clear the target's armour class and the
   int/DEX/SPEED/SENSES differential count too; a blinded attacker caps at ten
   unless a trait lets it fight blind. Finally the difficulty scaler runs. */
static int GetTargetAttackAttributes(int party_slot, int hand, W8AttackMode attack_mode,
                                     char ignore_target_defenses)
{
    W8Character* character = &g_status.party.characters[party_slot];
    W8HandAttack* attack = &character->Hand[hand];
    W8CombatSlot* target;
    int score;
    int surprise;
    int penalty;
    int target_sum;
    int attacker_sum;
    unsigned int fatigue;

    score = attack->attack_score + attack->hit_bonus * 5;
    score += character->bonus.hit_bonus * 5;
    score += AttackModeMod(0, attack_mode);
    if (g_combat.state()->unaware != 0) {
        score += 10;
    }
    if (g_combat.state()->natural_attack != 0) {
        score += 10;
    }
    fatigue = FatigueArmorPenalty(character->fatigue_band);
    if (attack->weapon_skill == W8_SKILL_MODERN_WEAPONS) {
        fatigue >>= 1;
    }
    score -= fatigue;
    if (g_combat.state()->characters[party_slot].pending_action_repick_count > 0) {
        surprise = character->attributes[W8_ATTRIBUTE_SPEED].effective -
                   g_combat.state()->characters[party_slot].pending_action_repick_count * 25 - 50 +
                   attack->attack_score;
        if (surprise < 0) {
            surprise = 0;
        }
        penalty = static_cast<int>(50.0 - pow(static_cast<double>(surprise), 0.75));
        if (penalty < 0) {
            penalty = 0;
        }
        score -= penalty;
    }
    if (ignore_target_defenses == 0) {
        target = &g_status.party.rows[party_slot].target_out_of_combat;
        score -= GetTargetArmorClass(target, attack_mode) * 5;
        target_sum = SumTargetCombatAttributes(*target);
        attacker_sum = SumCharacterCombatAttributes(*character);
        score += (attacker_sum - target_sum) / 10;
    }
    if (character->uiCondition[W8_CONDITION_BLIND] != 0 && score > 10) {
        if (!CharacterHasTrait(character, W8_TRAIT_EFFECTIVE_WHILE_BLIND)) {
            score = 10;
        } else {
            score =
                static_cast<int>(ScaleValueByProfessionLevel(
                    character, W8_TRAIT_EFFECTIVE_WHILE_BLIND, static_cast<float>(score - 10))) +
                10;
        }
    }
    ScaleValueForCharacterDifficulty(party_slot, &score);
    return score;
}

/* The same score against the in-combat TargetHit slot: the hand attack's
   score with the hit, value_25 and both modifier-block bonuses, the
   character-side attack-mode term, the fatigue penalty skipped entirely for
   modern weapons, then the target's armour class at the hit location and the
   same attribute differential. */
int GetTargetHitAttackAttributes(int party_slot, int hand, W8AttackMode attack_mode,
                                 int hit_location)
{
    W8Character* character = &g_status.party.characters[party_slot];
    W8HandAttack* attack = &character->Hand[hand];
    int score;
    int target_sum;
    int attacker_sum;
    W8CombatSlot* target = &g_combat.state()->TargetHit;

    score = attack->attack_score + attack->hit_bonus * 5;
    score += attack->attack_bonus * 5;
    score += character->bonus.hit_bonus * 5;
    score += character->bonus.attack_bonus * 5;
    score += AttackModeMod(1, attack_mode);
    if (attack->weapon_skill != W8_SKILL_MODERN_WEAPONS) {
        score -= FatigueArmorPenalty(character->fatigue_band);
    }
    score -= TargetArmorClassAtLocation(target, attack_mode, hit_location) * 5;
    target_sum = SumTargetCombatAttributes(*target);
    attacker_sum = SumCharacterCombatAttributes(*character);
    score += (attacker_sum - target_sum) / 10;
    ScaleValueForCharacterDifficulty(party_slot, &score);
    return score;
}

/* Resolves one character swing: picks the dice source for the attack mode,
   rolls the effective dice count, reports hit or miss and the dice count
   through the out params, and returns the damage after bonuses and the
   target's reduction. */
int ResolveCharacterAttackDamage(int party_slot, int hand, W8AttackMode attack_mode,
                                 unsigned int* out_dice_count, unsigned char* out_hit)
{
    W8Character* pPC = &g_status.party.characters[party_slot];
    W8CombatCharacterRow* row = &g_combat.state()->characters[party_slot];
    W8MonsterInfo* monster_info = nullptr;
    W8MonsterRecord* record = nullptr;
    W8Character* target = nullptr;
    unsigned char out_of_formation;
    bool target_defending;
    bool target_moving;
    const bool target_is_monster = g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER;

    if (target_is_monster) {
        if (g_combat.state()->TargetHit.iMonsterID == -1) {
            Fail("gpCombat->TargetHit.iMonsterID != BAD_INDEX");
        }
        monster_info = FindMonster(g_combat.state()->TargetHit.iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        out_of_formation = monster_info->modifiers.out_of_formation;
        W8MonsterActionKind action_kind = monster_info->action_kind;
        target_moving = action_kind == W8_MONSTER_ACTION_ADVANCE;
        target_defending =
            action_kind == W8_MONSTER_ACTION_WAIT || action_kind == W8_MONSTER_ACTION_PROTECT;
    } else {
        if (g_combat.state()->TargetHit.iChar == -1) {
            Fail("gpCombat->TargetHit.iChar != BAD_INDEX");
        }
        target = &g_status.party.characters[g_combat.state()->TargetHit.iChar];
        W8PartyAction party_action = GetEffectivePartyAction();
        out_of_formation = target->bonus.out_of_formation;
        target_moving = party_action == W8_PARTY_ACTION_RUN;
        target_defending =
            TryCharacterAction(g_combat.state()->TargetHit.iChar, W8_ACTION_DEFEND, true) ||
            TryCharacterAction(g_combat.state()->TargetHit.iChar, W8_ACTION_PROTECT, true);
    }

    unsigned int dice_count = 1;
    if (attack_mode != W8_ATTACK_MODE_THROW &&
        (attack_mode < W8_ATTACK_MODE_LASH || attack_mode > W8_ATTACK_MODE_SHOOT)) {
        if (target_moving || out_of_formation != 0 || g_combat.state()->unaware != 0) {
            dice_count = 2;
        }
        if (g_combat.state()->natural_attack != 0) {
            int chance =
                static_cast<int>(ScaleValueByProfessionLevel(pPC, W8_TRAIT_BACKSTAB, 100.0f));
            dice_count += RollBackstabExtraDice(chance);
        }
    }
    if (attack_mode == W8_ATTACK_MODE_BERSERK) {
        dice_count += 1;
    }
    if (dice_count > 1 && target_defending && g_combat.state()->unaware == 0) {
        dice_count -= 1;
    }
    if (target_is_monster && pPC->Hand[hand].uiHolds != HOLDS_NOTHING) {
        if (g_item_records[pPC->EquippedItem[row->current_equip_slot].iItemNo].slays_kind != 0xff &&
            g_item_records[pPC->EquippedItem[row->current_equip_slot].iItemNo].slays_kind ==
                record->kind) {
            dice_count += 1;
        }
        if (row->paired_equip_slot != -1 &&
            g_item_records[pPC->EquippedItem[row->paired_equip_slot].iItemNo].slays_kind != 0xff &&
            g_item_records[pPC->EquippedItem[row->paired_equip_slot].iItemNo].slays_kind ==
                record->kind) {
            dice_count += 1;
        }
    }

    W8Dice dice;
    switch (attack_mode) {
    case W8_ATTACK_MODE_PUNCH:
        if (pPC->Hand[hand].uiHolds != HOLDS_NOTHING) {
            Fail("pPC->Hand[uiHand].uiHolds == HOLDS_NOTHING");
        }
        if (pPC->Hand[1].uiHolds != HOLDS_NOTHING) {
            dice = g_item_records[pPC->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo]
                       .damage_dice;
        } else {
            dice = pPC->Hand[1].damage_dice;
        }
        break;
    case W8_ATTACK_MODE_KICK:
        if (pPC->Hand[hand].uiHolds != HOLDS_NOTHING) {
            Fail("pPC->Hand[uiHand].uiHolds == HOLDS_NOTHING");
        }
        if (pPC->Hand[0].uiHolds != HOLDS_NOTHING) {
            W8EquipSlot paired = W8_EQUIP_SLOT_NONE;
            if (ItemHasSingledOutGenericName(
                    pPC->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo) &&
                (paired = GetPairedEquipSlot(W8_EQUIP_SLOT_PRIMARY_WEAPON)) != W8_EQUIP_SLOT_NONE) {
                dice = g_item_records[pPC->EquippedItem[paired].iItemNo].damage_dice;
            } else {
                dice = g_item_records[pPC->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo]
                           .damage_dice;
            }
        } else {
            dice = pPC->Hand[0].damage_dice;
        }
        break;
    default:
        if (attack_mode != W8_ATTACK_MODE_BERSERK && pPC->Hand[hand].uiHolds == HOLDS_NOTHING) {
            Fail("pPC->Hand[uiHand].uiHolds != HOLDS_NOTHING");
        }
        if (pPC->Hand[hand].uiHolds == HOLDS_NOTHING) {
            dice = pPC->Hand[hand].damage_dice;
        } else {
            int item_id;
            if (hand == 0) {
                W8EquipSlot paired = W8_EQUIP_SLOT_NONE;
                if (ItemHasSingledOutGenericName(
                        pPC->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo) &&
                    (paired = GetPairedEquipSlot(W8_EQUIP_SLOT_PRIMARY_WEAPON)) !=
                        W8_EQUIP_SLOT_NONE) {
                    item_id = pPC->EquippedItem[paired].iItemNo;
                } else {
                    item_id = pPC->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
                }
            } else {
                item_id = pPC->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo;
            }
            dice = g_item_records[item_id].damage_dice;
        }
    }

    int rolled = RollDice(&dice, dice_count);
    *out_hit = (dice.count != 0 &&
                rolled >= (1.0f - dice.count * g_facing_tolerance0) * dice.Maximum() * dice_count)
                   ? 1
                   : 0;
    if (dice_count > 1 && Random(100) < dice_count * 20 - 20) {
        QueueCharacterEvent(pPC, g_learn_sound, 0, kCharacterEventFullVolume);
    }

    int damage = (pPC->bonus.damage_percent + 100 + pPC->Hand[hand].damage_percent) * rolled + 50;
    damage /= 100;
    if (damage < 1) {
        damage = 1;
    }
    if (target_is_monster) {
        damage = ApplyDamageReduction(monster_info, record, damage);
    } else {
        damage = ApplyCharacterDamageReduction(target, damage);
    }
    if (damage < 0) {
        *out_dice_count = dice_count;
        return 0;
    }
    *out_dice_count = dice_count;
    return damage;
}

/* The monster side of the attack-score pipeline: the attack's own score plus
   modifier and mode terms, unaware/natural bonuses, a fatigue penalty that
   halves for natural verbs, a repick surprise penalty, an optional armor and
   attribute comparison against the target, the blind cap, and difficulty. */
int GetMonsterAttackScore(W8MonsterInfo* monster_info, W8MonsterAttack* attack,
                          W8AttackMode attack_mode, char ignore_target_defenses)
{
    W8MonsterCombatState* combat = monster_info->pCombat.get();
    int score = monster_info->modifiers.hit_bonus * 5 + attack->attack_score;
    score += AttackModeMod(0, attack_mode);
    if (g_combat.state() != nullptr) {
        if (g_combat.state()->unaware != 0) {
            score += 10;
        }
        if (g_combat.state()->natural_attack != 0) {
            score += 10;
        }
    }
    int penalty = FatigueArmorPenalty(monster_info->fatigue_band);
    for (unsigned int i = 0; i < 0x12; i = i + 1) {
        if (attack->ubWeaponNameIndex == g_low_fatigue_attack_verbs[i]) {
            penalty >>= 1;
            break;
        }
    }
    score -= penalty;
    if (combat->pending_action_repick_count > 0) {
        int surprise = monster_info->attributes[W8_MONSTER_ATTRIBUTE_SPEED] -
                       (combat->pending_action_repick_count * 5 + 10) * 5 + attack->attack_score;
        if (surprise < 0) {
            surprise = 0;
        }
        int penalty_roll = static_cast<int>(50.0 - pow(static_cast<double>(surprise), 0.75));
        if (penalty_roll < 0) {
            penalty_roll = 0;
        }
        score -= penalty_roll;
    }
    if (ignore_target_defenses == 0) {
        score -= GetTargetArmorClass(&monster_info->Target, attack_mode) * 5;
        int target_sum;
        if (monster_info->Target.iType == W8_TARGET_KIND_MONSTER) {
            W8MonsterInfo* target_info = FindMonster(monster_info->Target.iMonsterID);
            target_sum = target_info->attributes[W8_MONSTER_ATTRIBUTE_SENSES] +
                         target_info->attributes[W8_MONSTER_ATTRIBUTE_SPEED] +
                         target_info->attributes[W8_MONSTER_ATTRIBUTE_DEXTERITY] +
                         target_info->attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE];
        } else if (monster_info->Target.iType == W8_TARGET_KIND_CHARACTER) {
            int target_char = monster_info->Target.iChar;
            target_sum =
                g_status.party.characters[target_char].attributes[W8_ATTRIBUTE_SENSES].effective +
                g_status.party.characters[target_char].attributes[W8_ATTRIBUTE_SPEED].effective +
                g_status.party.characters[target_char]
                    .attributes[W8_ATTRIBUTE_DEXTERITY]
                    .effective +
                g_status.party.characters[target_char]
                    .attributes[W8_ATTRIBUTE_INTELLIGENCE]
                    .effective;
        } else {
            DebugMessage(
                1, fmt::format(
                       "GetTargetAttackAttributes: Invalid Target Type {:d}(char {:d}, ID {:d})",
                       fmt::underlying(monster_info->Target.iType), monster_info->Target.iChar,
                       monster_info->Target.iMonsterID));
            target_sum = 0;
        }
        score += (monster_info->attributes[W8_MONSTER_ATTRIBUTE_SENSES] +
                  monster_info->attributes[W8_MONSTER_ATTRIBUTE_SPEED] +
                  monster_info->attributes[W8_MONSTER_ATTRIBUTE_DEXTERITY] +
                  monster_info->attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE] - target_sum) /
                 10;
    }
    if (monster_info->uiCondition[W8_CONDITION_BLIND] != 0 && 10 < score) {
        score = 10;
    }
    ScaleValueForMonsterDifficulty(monster_info, &score);
    return score;
}

/* The monster twin of ResolveCharacterAttackDamage: works out the effective
   dice count for the running attack, rolls the attack's dice, floors the
   result at one, applies the target's damage reduction, and reports the dice
   count through the out pointer. */
int ResolveMonsterAttackDamage(W8MonsterInfo* monster_info, W8MonsterAttack* attack,
                               unsigned int* out_dice_count)
{
    W8AttackMode attack_mode = static_cast<W8AttackMode>(monster_info->action_detail);
    W8MonsterInfo* target_info;
    W8MonsterRecord* record;
    W8Character* target;
    unsigned char out_of_formation;
    bool target_defending;
    bool target_moving;
    const bool target_is_character = g_combat.state()->TargetHit.iType == W8_TARGET_KIND_CHARACTER;

    if (target_is_character) {
        if (g_combat.state()->TargetHit.iChar == -1) {
            Fail("gpCombat->TargetHit.iChar != BAD_INDEX");
        }
        target = &g_status.party.characters[g_combat.state()->TargetHit.iChar];
        target_info = nullptr;
        record = nullptr;
        W8PartyAction party_action = GetEffectivePartyAction();
        out_of_formation = target->bonus.out_of_formation;
        target_moving = party_action == W8_PARTY_ACTION_RUN;
        target_defending =
            TryCharacterAction(g_combat.state()->TargetHit.iChar, W8_ACTION_DEFEND, true) ||
            TryCharacterAction(g_combat.state()->TargetHit.iChar, W8_ACTION_PROTECT, true);
    } else {
        if (g_combat.state()->TargetHit.iMonsterID == -1) {
            Fail("gpCombat->TargetHit.iMonsterID != BAD_INDEX");
        }
        target_info = FindMonster(g_combat.state()->TargetHit.iMonsterID);
        record = GetMonsterDataForInfo(target_info);
        FindMonsterGroup(target_info->monster_group_id);
        W8MonsterActionKind action_kind = target_info->action_kind;
        out_of_formation = target_info->modifiers.out_of_formation;
        target_moving = action_kind == W8_MONSTER_ACTION_ADVANCE;
        target_defending =
            action_kind == W8_MONSTER_ACTION_WAIT || action_kind == W8_MONSTER_ACTION_PROTECT;
        target = nullptr;
    }

    unsigned int dice_count = 1;
    if (attack_mode != W8_ATTACK_MODE_THROW &&
        (attack_mode < W8_ATTACK_MODE_LASH || attack_mode > W8_ATTACK_MODE_SHOOT)) {
        if (target_moving || out_of_formation != 0 || g_combat.state()->unaware != 0) {
            dice_count = 2;
        }
        if (g_combat.state()->natural_attack != 0) {
            int chance = static_cast<int>(
                ScaleValueByMonsterLevel(GetMonsterDataForInfo(monster_info), 9, 100.0f));
            dice_count += RollBackstabExtraDice(chance);
        }
    }
    if (attack_mode == W8_ATTACK_MODE_BERSERK) {
        dice_count += 1;
    }
    if (dice_count > 1 && target_defending && g_combat.state()->unaware == 0) {
        dice_count -= 1;
    }

    int rolled = RollDice(&attack->damage_dice, dice_count);
    if (rolled <= 0) {
        rolled = 1;
    }
    if (target_is_character) {
        rolled = ApplyCharacterDamageReduction(target, rolled);
    } else {
        rolled = ApplyDamageReduction(target_info, record, rolled);
    }
    if (rolled < 0) {
        *out_dice_count = dice_count;
        return 0;
    }
    *out_dice_count = dice_count;
    return rolled;
}

/* One condition attack: resolves it against the target and counts a success
   in the accumulator when the target does not resist. */
static bool resolveConditionAttack(const W8TargetSource* source, W8CombatSlot* target,
                                   W8SpellEffectResult* accumulator, W8Condition condition,
                                   W8SpellRealm realm, unsigned int power_level, int argument,
                                   int magnitude, bool verbose, bool announce)
{
    const bool resisted = ResolveAttackOnTarget(source, target, condition, realm, power_level,
                                                argument, magnitude, verbose, announce, 0);
    if (accumulator != nullptr) {
        accumulator->condition_counts[condition] += (!resisted);
    }
    return resisted;
}

/* The condition pass behind ApplyEffectConditions: for each of the effect
   definition's sixteen condition chances, roll the percentage and, when it
   lands, resolve the condition or drain the case handles. A report passed in
   is folded into a local accumulator per condition, then merged back - unless
   a killing blow landed, in which case the report keeps only the kill. */
void ApplyEffectConditions(W8TargetSource* source, W8CombatSlot* target,
                           W8SpellEffectDefinition* definition, bool announce, bool verbose,
                           W8SpellEffectResult* result)
{
    W8SpellEffectResult local;
    W8SpellEffectResult* accumulator;
    W8MonsterInfo* monster_info;
    W8CharacterEvent* event;
    W8Condition highest_condition;
    unsigned int magnitude;
    int count_base;
    bool resisted;
    int i;
    bool drained = false;

    if (result == nullptr) {
        accumulator = nullptr;
    } else {
        local = {};
        accumulator = &local;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        if (target->iChar == -1) {
            Fail("pTarget->iChar != BAD_INDEX");
        }
        if (g_status.party.rows[target->iChar].fOccupied == 0) {
            Fail("fCHAR_OCCUPIED(pTarget->iChar)");
        }
        highest_condition = g_status.party.characters[target->iChar].highest_condition;
    } else {
        if (target->iType != W8_TARGET_KIND_MONSTER) {
            Fail("pTarget->iType == TARGET_TYPE_MONSTER");
        }
        if (target->iMonsterID == -1) {
            Fail("pTarget->iMonsterID != BAD_INDEX");
        }
        highest_condition = FindMonster(target->iMonsterID)->highest_condition;
    }
    for (i = 0; i < 0x10; ++i) {
        if (highest_condition > W8_CONDITION_UNCONSCIOUS) {
            break;
        }
        if (definition->condition_chances[i] != 0) {
            if (Random(100) < definition->condition_chances[i]) {
                count_base = definition->power_level / 3 + 1;
                magnitude = RollEffectDuration(definition);
                if (magnitude < 2) {
                    SetDice(&definition->magnitude, count_base, 2, 1);
                    magnitude = RollEffectMagnitude(definition) + 1;
                }
                if (static_cast<int>(magnitude) < 1) {
                    magnitude = 1;
                }
                if (magnitude > 0xc) {
                    magnitude = 0xc;
                }
                switch (i) {
                case W8_ATTACK_EFFECT_SLEEP:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_ASLEEP, W8_SPELL_REALM_AIR,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_PARALYZE:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_PARALYZED, W8_SPELL_REALM_WATER,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_POISON:
                    magnitude = RollEffectDuration(definition);
                    if (magnitude < 2) {
                        SetDice(&definition->magnitude, count_base * 3, 2,
                                definition->magnitude_base);
                        magnitude = RollEffectMagnitude(definition) + 1;
                    }
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_POISONED, W8_SPELL_REALM_WATER,
                        definition->power_level, definition->magnitude_base, magnitude, verbose,
                        announce);
                    break;
                case W8_ATTACK_EFFECT_HEX:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_HEXED, W8_SPELL_REALM_DIVINE,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_DISEASE:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_DISEASED, W8_SPELL_REALM_WATER,
                        definition->power_level, 0, W8_CONDITION_INDEFINITE, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_KILL:
                    resisted = ResolveAttackOnTarget(source, target, W8_CONDITION_DEAD,
                                                     W8_SPELL_REALM_DIVINE, definition->power_level,
                                                     0, W8_CONDITION_INDEFINITE, verbose, false, 0);
                    if (!resisted) {
                        if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
                            ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x17f]);
                            if (Random(100) < 0x32) {
                                QueueCharacterEvent(&g_status.party.characters[source->iChar],
                                                    g_learn_sound, 0, kCharacterEventFullVolume);
                            } else if (CurrentLevelPopulation()
                                               .monster_roster.hostileCombatCount() > 1 &&
                                       (event = QueueCharacterEvent(
                                            &g_status.party.characters[source->iChar], 56, 0,
                                            kCharacterEventFullVolume)) != nullptr) {
                                event->DelayDispatch(800);
                            }
                        } else {
                            ShowNotice(W8_FONT_PALETTE_RUST, g_localized_strings[0x17f]);
                        }
                        if (target->iType == W8_TARGET_KIND_CHARACTER) {
                            PostCharacterNotice(
                                target->iChar,
                                fmt::format(
                                    "{}!",
                                    g_localized_strings[g_condition_notices[W8_CONDITION_DEAD]
                                                            .singular]));
                        }
                        if (accumulator != nullptr) {
                            accumulator->condition_counts[W8_CONDITION_DEAD]++;
                        }
                    }
                    break;
                case W8_ATTACK_EFFECT_KNOCK_OUT:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_UNCONSCIOUS, W8_SPELL_REALM_EARTH,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_BLIND:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_BLIND, W8_SPELL_REALM_FIRE,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_FRIGHTEN:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_AFRAID, W8_SPELL_REALM_MENTAL,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_SWALLOW:
                    if (TargetSourceIsMonster(source, 0)) {
                        if (target->iType == W8_TARGET_KIND_CHARACTER) {
                            resisted = ResolveAttackOnTarget(
                                source, target, W8_CONDITION_MISSING, W8_SPELL_REALM_DIVINE,
                                definition->power_level, 0, W8_CONDITION_INDEFINITE, verbose,
                                announce, 1);
                            if (!resisted) {
                                if (accumulator != nullptr) {
                                    accumulator->condition_counts[W8_CONDITION_MISSING]++;
                                }
                                BindMonsterToCharacterDependence(
                                    target->iChar, W8_DEPENDENCE_SWALLOWED, source->iMonsterID);
                            }
                        } else if (target->iType == W8_TARGET_KIND_MONSTER) {
                            ResolveAttackOnTarget(source, target, W8_CONDITION_DEAD,
                                                  W8_SPELL_REALM_DIVINE, definition->power_level, 0,
                                                  W8_CONDITION_INDEFINITE, verbose, announce, 0);
                        }
                    }
                    break;
                case W8_ATTACK_EFFECT_POSSESS:
                    if (TargetSourceIsMonster(source, 0) &&
                        target->iType == W8_TARGET_KIND_CHARACTER) {
                        resisted = resolveConditionAttack(
                            source, target, accumulator, W8_CONDITION_TURNCOAT,
                            W8_SPELL_REALM_DIVINE, definition->power_level, 0, magnitude, verbose,
                            announce);
                    }
                    break;
                case W8_ATTACK_EFFECT_DRAIN_HP:
                    if (target->iType == W8_TARGET_KIND_CHARACTER) {
                        resisted =
                            TargetResistsCondition(target, W8_SPELL_REALM_EARTH,
                                                   definition->power_level, W8_CONDITION_NONE);
                        if (!resisted) {
                            magnitude = RollEffectDuration(definition);
                            if (magnitude < 2) {
                                SetDice(&definition->magnitude, count_base, 2, 0);
                                magnitude = RollEffectMagnitude(definition) + 1;
                            }
                            ReduceMagnitudeByResistance(&magnitude, target, W8_SPELL_REALM_EARTH,
                                                        definition->power_level);
                            if (magnitude != 0) {
                                DamageCharacter(target->iChar, magnitude, true);
                                if (accumulator != nullptr) {
                                    accumulator->notice_values[0] += magnitude;
                                    drained = true;
                                }
                            }
                        }
                    }
                    break;
                case W8_ATTACK_EFFECT_DRAIN_STAMINA:
                    resisted = TargetResistsCondition(target, W8_SPELL_REALM_WATER,
                                                      definition->power_level, W8_CONDITION_NONE);
                    if (!resisted) {
                        magnitude = RollEffectDuration(definition);
                        if (magnitude < 2) {
                            SetDice(&definition->magnitude, count_base * 2, 5, count_base * 3);
                            magnitude = RollEffectMagnitude(definition) + 1;
                        }
                        ReduceMagnitudeByResistance(&magnitude, target, W8_SPELL_REALM_WATER,
                                                    definition->power_level);
                        if (magnitude != 0) {
                            if (target->iType == W8_TARGET_KIND_MONSTER) {
                                monster_info = FindMonster(target->iMonsterID);
                                FatigueMonster(monster_info, magnitude, nullptr);
                            } else {
                                FatigueCharacter(target->iChar, magnitude, false, nullptr);
                            }
                            if (accumulator != nullptr) {
                                accumulator->notice_values[1] += magnitude;
                                drained = true;
                            }
                        }
                    }
                    break;
                case W8_ATTACK_EFFECT_DRAIN_SP:
                    if (target->iType == W8_TARGET_KIND_CHARACTER) {
                        resisted =
                            TargetResistsCondition(target, W8_SPELL_REALM_DIVINE,
                                                   definition->power_level, W8_CONDITION_NONE);
                        if (!resisted) {
                            magnitude = RollEffectDuration(definition);
                            if (magnitude < 2) {
                                SetDice(&definition->magnitude, count_base, 6, count_base);
                                magnitude = RollEffectMagnitude(definition) + 1;
                            }
                            ReduceMagnitudeByResistance(&magnitude, target, W8_SPELL_REALM_DIVINE,
                                                        definition->power_level);
                            if (magnitude != 0) {
                                DrainCharacterSpellPoints(target->iChar, magnitude, true);
                                if (target->iChar == g_status.selected_character) {
                                    RefreshFlaggedMainGameState();
                                }
                                if (accumulator != nullptr) {
                                    accumulator->notice_values[2] += magnitude;
                                    drained = true;
                                }
                            }
                        }
                    }
                    break;
                case W8_ATTACK_EFFECT_NAUSEATE:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_NAUSEATED, W8_SPELL_REALM_AIR,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                case W8_ATTACK_EFFECT_INSANE:
                    resisted = resolveConditionAttack(
                        source, target, accumulator, W8_CONDITION_INSANE, W8_SPELL_REALM_MENTAL,
                        definition->power_level, 0, magnitude, verbose, announce);
                    break;
                }
            }
            if (target->iType == W8_TARGET_KIND_CHARACTER) {
                highest_condition = g_status.party.characters[target->iChar].highest_condition;
            } else {
                highest_condition = FindMonster(target->iMonsterID)->highest_condition;
            }
        }
    }
    if (accumulator != nullptr) {
        if (drained) {
            accumulator->condition_counts[W8_CONDITION_DRAINED]++;
        }
        if (accumulator->condition_counts[W8_CONDITION_DEAD] == 0) {
            result->notice_values[0] += accumulator->notice_values[0];
            result->notice_values[1] += accumulator->notice_values[1];
            result->notice_values[2] += accumulator->notice_values[2];
            for (i = 1; i < W8_CONDITION_COUNT; ++i) {
                result->condition_counts[i] += accumulator->condition_counts[i];
            }
        } else {
            result->notice_values[0] = 0;
            result->notice_values[1] = 0;
            result->notice_values[2] = 0;
            for (i = 1; i < W8_CONDITION_COUNT; ++i) {
                if (i == W8_CONDITION_DEAD) {
                    result->condition_counts[i] += accumulator->condition_counts[i];
                } else {
                    result->condition_counts[i] = 0;
                }
            }
        }
    }
}

static void AppendCombatTargetEntry(std::vector<W8CombatSlot>& targets, W8TargetKind kind,
                                    int character, int monster_id)
{
    auto& target = targets.emplace_back();
    ResetCombatSlot(&target);
    target.iType = kind;
    target.iChar = character;
    target.iMonsterID = monster_id;
}

/* Builds the retargeting candidate list for a character: every live party
   member except the one already aimed at, plus self, then every active
   in-combat monster except the aimed one. */
void BuildCharacterTargetList(int party_slot, int action, std::vector<W8CombatSlot>& out_list)
{
    for (int i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
        W8PartySlotRow* row = &g_status.party.rows[i];
        if (CanPartySlotParticipate(i)) {
            if (i == party_slot) {
                AppendCombatTargetEntry(out_list, W8_TARGET_KIND_CHARACTER, i, -1);
            } else {
                if ((row->target_out_of_combat.iType == W8_TARGET_KIND_CHARACTER &&
                     row->target_out_of_combat.iChar == i) ||
                    !CharacterActionReachesSlot(
                        party_slot, action, i,
                        SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
                    continue;
                }
                AppendCombatTargetEntry(out_list, W8_TARGET_KIND_CHARACTER, i, -1);
            }
        }
    }
    for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* monster_info = monster_owner.get();
        if (monster_info->fActive && monster_info->hp_current != 0 && monster_info->pCombat &&
            (g_status.party.rows[party_slot].target_out_of_combat.iType != W8_TARGET_KIND_MONSTER ||
             g_status.party.rows[party_slot].target_out_of_combat.iMonsterID !=
                 monster_info->location_id) &&
            AssessMonsterAim(party_slot, action, monster_info,
                             SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
            AppendCombatTargetEntry(out_list, W8_TARGET_KIND_MONSTER, -1,
                                    monster_info->location_id);
        }
    }
}

/* The monster twin: candidates for the monster's next target. Retail's
   monster exclusion compares the aimed id with the acting monster's own id,
   rather than the candidate's id (0x005441aa..0x005441b2). */
void BuildMonsterTargetList(W8MonsterInfo* monster_info, W8MonsterRecord* record,
                            unsigned int attack, std::vector<W8CombatSlot>& out_list)
{
    for (int i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
        if (CanPartySlotParticipate(i) &&
            (monster_info->Target.iType != W8_TARGET_KIND_CHARACTER ||
             monster_info->Target.iChar != i) &&
            MonsterAttackReachesCharacter(monster_info, record, attack, i)) {
            AppendCombatTargetEntry(out_list, W8_TARGET_KIND_CHARACTER, i, -1);
        }
    }
    for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* candidate = monster_owner.get();
        if (candidate->fActive && candidate->hp_current != 0 && candidate->pCombat) {
            if (candidate == monster_info) {
                AppendCombatTargetEntry(out_list, W8_TARGET_KIND_MONSTER, -1,
                                        candidate->location_id);
            } else {
                if ((monster_info->Target.iType == W8_TARGET_KIND_MONSTER &&
                     monster_info->Target.iMonsterID == monster_info->location_id) ||
                    !MonsterAttackReachesMonster(monster_info, record, attack, candidate)) {

                    continue;
                }
                AppendCombatTargetEntry(out_list, W8_TARGET_KIND_MONSTER, -1,
                                        candidate->location_id);
            }
        }
    }
}

/* Announces a friendly-fire strike: "<attacker> accidentally strikes
   <target>", then highlights the name that belongs to the other side's
   notice palette when the two sides differ. */
void AnnounceAccidentalStrike(W8TargetSource* source, W8CombatSlot* target)
{
    char source_color = GetSourceNoticeColor(source);
    std::string& message = g_combat.state()->attack_message;
    unsigned char color;
    unsigned char highlight_start;
    unsigned char highlight_stop;
    if (TargetSourceIsCharacter(source, 0)) {
        color = static_cast<unsigned char>(g_status.party.rows[source->iChar].party_order_index);
        std::string name = g_status.party.characters[source->iChar].name;
        highlight_start = 0;
        highlight_stop = static_cast<unsigned char>(name.size());
        message = fmt::format("{} {} ", name, "accidentally strikes");
        if (target->iType == W8_TARGET_KIND_CHARACTER) {
            if (source->iChar == target->iChar) {
                W8Character* pc = &g_status.party.characters[source->iChar];
                message += g_localized_strings[g_gender_name_message_rows[pc->gender][3]].c_str();
            } else {
                message += g_status.party.characters[target->iChar].name.c_str();
            }
        } else {
            W8MonsterInfo* monster_info = FindMonster(target->iMonsterID);
            message += GetMonsterName(monster_info, nullptr, 0);
        }
    } else {
        if (!TargetSourceIsMonster(source, 0)) {
            Fail("SourceIsMonster(pSource)");
        }
        W8MonsterInfo* monster_info = FindMonster(source->iMonsterID);
        W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
        message =
            fmt::format("{} {} ", GetMonsterName(monster_info, record, 0), "accidentally strikes");
        color = GetTargetNoticeColor(source, target);
        highlight_start = static_cast<unsigned char>(message.size());
        if (target->iType == W8_TARGET_KIND_MONSTER && source->iMonsterID == target->iMonsterID) {
            message +=
                g_localized_strings[g_gender_name_message_rows[record->name_group][3]].c_str();
        } else if (target->iType == W8_TARGET_KIND_CHARACTER) {
            message += g_status.party.characters[target->iChar].name.c_str();
        } else {
            W8MonsterInfo* target_info = FindMonster(target->iMonsterID);
            message += GetMonsterName(target_info, nullptr, 0);
        }
    }
    if (!TargetSourceIsCharacter(source, 0)) {
        highlight_stop = static_cast<unsigned char>(message.size());
    }
    ShowNotice(source_color, message);
    if (source_color != color) {
        HighlightTextBoxRange(color, highlight_start, highlight_stop, -1);
    }
}

/* Fumble banter after a redirected hit: half the time the attacker reacts to
   its own fumble, otherwise the struck victim speaks, or a random other party
   member when the victim is gone or incapacitated. */
void QueueFumbleReaction(int party_slot)
{
    int slot;
    if (Random(2) != 0) {
        QueueCharacterEvent(&g_status.party.characters[party_slot], 16, 0,
                            kCharacterEventFullVolume);
        return;
    }
    if (g_combat.state()->TargetHit.iType != W8_TARGET_KIND_CHARACTER ||
        party_slot == g_combat.state()->TargetHit.iChar ||
        !IsTargetStillPresent(&g_combat.state()->TargetHit) ||
        (slot = g_combat.state()->TargetHit.iChar,
         g_status.party.characters[slot].highest_condition > W8_CONDITION_WEBBED)) {
        slot = GetRandomCharacter(0, 0, party_slot, -1);
    }
    if (slot == -1) {
        return;
    }
    QueueCharacterEvent(&g_status.party.characters[slot], 17, 0, kCharacterEventFullVolume);
}

/* When GetMonsterByLocationID returns NULL the sight-flag call is skipped and
   retail read `secondary` uninitialized; natively both flags start false. */
/* The missile dispatcher shared by the character and monster attack paths:
   resolves both endpoints' world positions, defers the target when a combat-
   ending missile is already engaged, folds the range category's base speed
   into the shot and fires through FireMissile. */
Missile* FireMissileSourceToTarget(int missile_type, W8TargetSource* source, W8CombatSlot* target,
                                   W8SpellEffectDefinition* attack, bool use_default_accuracy,
                                   W8RangeCategory range_category, int accuracy)
{
    unsigned int target_flag;
    bool blind = false;
    bool primary = false;
    bool secondary = false;
    unsigned char position_ok;
    glm::vec3 source_position;
    glm::vec3 target_position;
    W8MonsterInfo* monster_info;
    Monster* monster;
    Missile* missile;
    float speed;
    float base_speed;
    float radius;
    float yaw;
    float pitch;

    if (!use_default_accuracy && g_combat.state()->engaged_missile != nullptr) {
        Fail("FALSE");
        return nullptr;
    }
    if (TargetSourceIsCharacter(source, 0) && target->iType == W8_TARGET_KIND_CHARACTER &&
        !g_missile_table[missile_type].spell_missile) {
        if (use_default_accuracy) {
            return nullptr;
        }
        g_combat.state()->missile_hit_result = W8_MISSILE_HIT;
        g_combat.state()->TargetHit = *target;
        return nullptr;
    }
    if (target->iType == W8_TARGET_KIND_CHARACTER || target->iType == W8_TARGET_KIND_PARTY) {
        target_position = cameraLocation();
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(target->iMonsterID);
        monster = monster_info->p3D;
        target_position = monster->nav.movement.position;
        target_position.y += monster->nav.movement.height_offset;
    } else if (target->iType == W8_TARGET_KIND_PLACE) {
        target_position = target->point;
    } else {
        Fail("FALSE", "FireMissileSourceToTarget: Unknown target type");
    }
    if (TargetSourceIsCharacter(source, 0)) {
        GetCharacterProjectilePosition(source->iChar, &source_position);
        if (target->iType == W8_TARGET_KIND_PLACE) {
            if (GetWorldSpatialIndex()->TraceLineOfSight(&source_position, &target_position, true,
                                                         -3, -3, true, 0) == 1) {
                source_position = cameraLocation();
            }
        } else if (target->iType == W8_TARGET_KIND_MONSTER) {
            monster = GetMonsterByLocationID(target->iMonsterID);
            if (monster != nullptr) {
                monster->GetPlayerToMonsterSightFlags(&primary, &secondary, &source_position);
            }
            if (!secondary || (!g_missile_table[missile_type].spell_missile && !primary)) {
                source_position = cameraLocation();
            }
        }
        if (target->iType == W8_TARGET_KIND_CHARACTER || target->iType == W8_TARGET_KIND_PARTY) {
            target_flag = 0;
        } else {
            target_flag = 1;
        }
        blind = g_status.party.characters[source->iChar].uiCondition[W8_CONDITION_BLIND] != 0;
    } else if (TargetSourceIsMonster(source, 0)) {
        if (source->iMonsterID == -1) {
            Fail("pSource->iMonsterID != -1");
        }
        monster_info = FindMonster(source->iMonsterID);
        monster = monster_info->p3D;
        if (!g_missile_table[missile_type].spell_missile) {
            position_ok = monster->GetProjectilePosition(&source_position);
        } else if (!source->point_source) {
            position_ok = monster->GetSpellPosition(&source_position);
        } else {
            position_ok = missile_type;
        }
        if (position_ok == 0) {
            source_position =
                glm::vec3(monster->nav.movement.position.x,
                          monster->nav.movement.position.y + monster->nav.movement.height_offset,
                          monster->nav.movement.position.z);
            monster->GetAnimationRadius(&radius);
            pitch = GetElevationAngle(&source_position, &target_position);
            yaw = GetHeadingAngle(&source_position, &target_position);
            OffsetPositionByYawPitch(radius + radius, &source_position, yaw, pitch);
        }
        target_flag = 0;
        blind = monster_info->uiCondition[W8_CONDITION_BLIND] != 0;
    } else {
        if (source->iType != W8_TARGET_SOURCE_INDIRECT) {
            Fail("FALSE");
            return nullptr;
        }
        source_position = source->point;
        if (source->iChar == -1) {
            target_flag = source->iMonsterID != -1;
        } else {
            target_flag = 0;
        }
    }
    speed = sqrt((source_position.x - target_position.x) * (source_position.x - target_position.x) +
                 (source_position.y - target_position.y) * (source_position.y - target_position.y) +
                 (source_position.z - target_position.z) * (source_position.z - target_position.z));
    if (range_category != W8_RANGE_NONE) {
        CalcRangeDistance(range_category, source);
        base_speed = CalcRangeDistance(range_category, source);
        if (base_speed <= speed) {
            base_speed = speed;
        }
        speed = base_speed * kMonsterMotionPush;
    }
    if (accuracy != 9999) {
        ScatterMissileAimPoint(&source_position, &target_position, accuracy, blind);
    }
    missile = FireMissile(missile_type, &source_position, &target_position, 0, target_flag,
                          use_default_accuracy, speed);
    if (missile != nullptr) {
        missile->m_Source = *source;
        missile->combat_slot = *target;
        missile->SetEffectDefinition(attack);
        if (!use_default_accuracy) {
            g_combat.state()->engaged_missile = missile;
            g_combat.state()->missile_hit_result = W8_MISSILE_HIT_NONE;
        }
        PointCameraAtCombatTarget(source, target);
        return missile;
    }
    Fail("pMissileFired");
    return nullptr;
}

/* The character-side missile fire: builds the attack block from the wielded
   (and paired) weapon's item records, scores the shot for the current hand's
   attack mode and fires at the slot's out-of-combat target. */
void FireCharacterItemMissile(int party_slot, W8Character* pc, W8CombatCharacterRow* row,
                              W8RangeCategory range_category)
{
    W8TargetSource source;
    W8SpellEffectDefinition attack_block;
    unsigned char item_modifiers[0x10];
    const unsigned char* modifiers;
    W8ItemDatabaseRecord* weapon;
    W8ItemDatabaseRecord* paired;
    unsigned int level;
    unsigned int capped_level;
    unsigned int missile_value;
    int accuracy;
    int i;
    int missile_type = g_item_records[row->paired_item_id].missile_type;
    if (g_missile_table.size() <= static_cast<unsigned int>(missile_type)) {
        DebugMessage(0,
                     fmt::format("DATA ERROR: Item {} has an invalid missile type ({:d})",
                                 g_item_records[row->paired_item_id].display_name, missile_type));
        missile_type = 0;
    }
    SetTargetSourceToCharacter(party_slot, &source);
    ClearAttackBlock(&attack_block);
    level = pc->uiExpLevel;
    capped_level = level;
    if (0xe < level) {
        capped_level = 0xf;
    }
    attack_block.power_level = level + capped_level;
    attack_block.magnitude = g_item_records[row->paired_item_id].damage_dice;
    weapon = &g_item_records[pc->EquippedItem[row->current_equip_slot].iItemNo];
    missile_value = weapon->missile_magnitude;
    modifiers = weapon->missile_values;
    if (row->paired_equip_slot != -1) {
        paired = &g_item_records[pc->EquippedItem[row->paired_equip_slot].iItemNo];
        for (i = 0x10; i != 0; i = i - 1) {
            item_modifiers[0x10 - i] =
                weapon->missile_values[0x10 - i] + paired->missile_values[0x10 - i];
        }
        modifiers = item_modifiers;
        missile_value += paired->missile_magnitude;
    }
    memcpy(attack_block.condition_chances, modifiers, 0x10);
    attack_block.magnitude_base = missile_value;
    accuracy = GetTargetAttackAttributes(
        party_slot, row->current_hand,
        static_cast<W8AttackMode>(g_status.party.rows[party_slot].attack_mode[row->current_hand]),
        0);
    CombatLog(fmt::format("TO HIT (MISSILE ACCURACY): Chance {:d}", accuracy));
    FireMissileSourceToTarget(missile_type, &source,
                              &g_status.party.rows[party_slot].target_out_of_combat, &attack_block,
                              false, range_category, accuracy);
}

/* Displaces the aim point `to` perpendicular to the from->to line by a
   randomized cone scatter. `accuracy` shrinks the cone; `blind` swaps the
   flat 8% profile for the coarse distance-scaled one. */
void ScatterMissileAimPoint(const glm::vec3* from, glm::vec3* to, int accuracy, bool blind)
{
    float dx = to->x - from->x;
    float dy = to->y - from->y;
    float dz = to->z - from->z;
    float px = dy * -dx;
    float py = dz * dz - dx * -dx;
    float pz = -(dz * dy);
    float sx = py * dz - pz * dy;
    float sy = pz * dx - dz * px;
    float sz = px * dy - py * dx;
    double angle = static_cast<double>(Random(0x168));
    float radians = static_cast<float>(3.141592653589793 * kCameraViewFactor0 * angle);
    float mag = sx * sx + sz * sz + sy * sy;
    if (mag != 0.0f) {
        float scale = static_cast<float>(sin(radians));
        scale = scale / static_cast<float>(sqrt(mag));
        sx *= scale;
        sy *= scale;
        sz *= scale;
    }
    mag = px * px + py * py + pz * pz;
    if (mag != 0.0f) {
        float scale = static_cast<float>(cos(radians));
        scale = scale / static_cast<float>(sqrt(mag));
        px *= scale;
        py *= scale;
        pz *= scale;
    }
    sx = px + sx;
    sy = py + sy;
    sz = pz + sz;
    float distance = static_cast<float>(sqrt(dy * dy + dx * dx + dz * dz));
    if (distance < static_cast<float>(1.0)) {
        return;
    }
    double probability;
    double factor;
    if (!blind) {
        factor = 0.08;
        probability = 0.2;
    } else {
        factor = 500.0 / distance;
        probability = 1.75;
        if (1.75 < factor) {
            factor = 1.75;
        }
    }
    double raw_radius = factor * distance;
    int spread = static_cast<int>(distance * g_world_cursor_scale);
    int effective = ((100 - spread) * accuracy) / 100;
    double magnitude;
    if (effective <= 0) {
        magnitude = probability * distance;
    } else if (effective >= 300) {
        magnitude = raw_radius;
    } else {
        magnitude =
            raw_radius + (probability - factor) * (300.0 - effective) * (1.0 / 300.0) * distance;
    }
    int roll = Random(100) + 1;
    int cap = effective;
    if (cap >= 0x60) {
        cap = 0x5f;
    }
    if (cap >= roll) {
        magnitude = (roll / static_cast<double>(effective)) * raw_radius;
    } else if (effective >= 1 || roll + effective >= 1) {
        double weight;
        if (effective < 1) {
            weight = (100 - (roll + effective)) * 0.01;
        } else {
            weight = (roll - effective) / static_cast<double>(roll);
        }
        magnitude = (magnitude - raw_radius) * weight + raw_radius;
    }
    mag = sx * sx + sz * sz + sy * sy;
    if (mag != 0.0f) {
        magnitude = magnitude / sqrt(mag);
        sx = sx * static_cast<float>(magnitude);
        sy = sy * static_cast<float>(magnitude);
        sz = sz * static_cast<float>(magnitude);
    }
    (*to) = glm::vec3(dx + sx + from->x, dy + sy + from->y, sz + dz + from->z);
}

/* Starts a party member's attack action: validates the hand's attack count and
   target, picks the attack mode, rolls this round's swings, builds the combat
   log line, resolves the facing and surprise checks, then either fires the
   wielded missile weapon or queues the melee swing event. */
static void AppendAttackMessageSuffix(int mode)
{
    if (g_settings.verbose_combat_messages != 0 && g_combat.state()->natural_attack == 0) {
        g_combat.state()->attack_message += g_localized_strings[0x216];
    } else if (g_combat.state()->natural_attack == 0) {
        switch (mode) {
        case W8_ATTACK_MODE_SWING:
        case W8_ATTACK_MODE_THRUST:
        case W8_ATTACK_MODE_THROW:
            g_combat.state()->attack_message += g_localized_strings[0x217];
            break;
        case W8_ATTACK_MODE_BERSERK:
            g_combat.state()->attack_message += g_localized_strings[0x218];
            break;
        }
    }
}

bool StartCharacterAttack(int party_slot, W8AttackMode attack_mode)
{
    W8Character* character;
    W8CombatCharacterRow* row;
    W8PartySlotRow* party_row;
    W8ItemInstance* found;
    W8MonsterInfo* monster_info;
    std::string verb;
    std::string name;
    W8AttackMode mode;
    unsigned int hand;
    W8RangeCategory range;
    bool noticed;
    unsigned int event_ids[3];

    if (static_cast<unsigned int>(party_slot) >= 8) {
        Fail("uiChar < MAX_CHARS");
    }
    found = nullptr;
    character = &g_status.party.characters[party_slot];
    row = &g_combat.state()->characters[party_slot];
    party_row = &g_status.party.rows[party_slot];
    g_combat.state()->attack_report = {};
    if (static_cast<unsigned int>(row->current_hand) >= 2) {
        Fail("uiHand < HAND_COUNT");
    }
    hand = row->current_hand;
    if (!character->Hand[hand].in_play || GetCharAttackRange(character, hand) == W8_RANGE_NONE) {
        row->hand_attack_values[hand] = 0;
        return false;
    }
    if (attack_mode == W8_ATTACK_MODE_NONE) {
        mode = CharChooseHandAttackMode(character, hand);
    } else {
        mode = attack_mode;
    }
    party_row->attack_mode[hand] = mode;
    if (row->hand_attack_values[hand] == 0) {
        DebugMessage(
            1, fmt::format(
                   "ERROR: {} is starting attack with 0 of {:d} attacks remaining (hand {:d})!",
                   character->name, row->saved_attack_value[hand], hand));
        return false;
    }
    row->hand_attack_values[hand]--;
    g_combat.state()->unaware = 0;
    g_combat.state()->natural_attack = 0;
    if (!TargetIsInPlay(party_slot, hand,
                        SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
        DebugMessage(
            1, fmt::format("ERROR: {} is starting attack with invalid target!", character->name));
        return false;
    }
    if (hand == 0) {
        row->current_equip_slot = W8_EQUIP_SLOT_PRIMARY_WEAPON;
        if (ItemHasSingledOutGenericName(
                character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo)) {
            row->paired_equip_slot = GetPairedEquipSlot(row->current_equip_slot);
        } else {
            row->paired_equip_slot = W8_EQUIP_SLOT_NONE;
        }
    } else {
        row->current_equip_slot = W8_EQUIP_SLOT_SECONDARY_WEAPON;
        row->paired_equip_slot = W8_EQUIP_SLOT_NONE;
    }
    row->uiSwingsRemaining = Random(character->Hand[hand].swings) + 1;
    range = GetCharAttackRange(character, hand);
    if (CharacterHasTrait(character, W8_TRAIT_BACKSTAB) && range <= W8_RANGE_TOUCH &&
        (character->Hand[hand].weapon_skill == W8_SKILL_SWORD ||
         character->Hand[hand].weapon_skill == W8_SKILL_DAGGER)) {
        g_combat.state()->natural_attack = 1;
    }
    if (character->EquippedItem[row->current_equip_slot].iItemNo != -1) {
        row->uiSwingsRemaining +=
            g_item_records[character->EquippedItem[row->current_equip_slot].iItemNo].swings_bonus;
    }
    if (row->extra_swings[0] != 0 && hand == 0) {
        if (character->Hand[0].weapon_skill == W8_SKILL_SWORD &&
            character->uiCondition[W8_CONDITION_SLOWED] == 0) {
            PostCharacterNotice(party_slot, g_localized_strings[0x203]);
            if (Random(0xb) + character->profession_levels[character->iProfession] < 0x14) {
                row->uiSwingsRemaining += 3;
            } else {
                row->uiSwingsRemaining += 4;
            }
        }
        row->extra_swings[0] = 0;
    }
    row->weapon_item_id = character->EquippedItem[row->current_equip_slot].iItemNo;
    if (row->paired_equip_slot == W8_EQUIP_SLOT_NONE) {
        row->paired_item_id = row->weapon_item_id;
    } else {
        row->paired_item_id = character->EquippedItem[row->paired_equip_slot].iItemNo;
    }
    found = FindCharacterItem(*character, ItemWithId(row->weapon_item_id), W8ItemReach::Worn);
    if (found == nullptr) {
        DebugMessage(1, fmt::format("ERROR: {} does not have a {}!", character->name,
                                    g_item_records[row->weapon_item_id].display_name));
        return false;
    }
    if (g_combat.state()->natural_attack != 0) {
        g_combat.state()->attack_message = g_localized_strings[0x204];
        g_combat.state()->attack_message += " ";
    } else {
        if (hand == 1 && character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo != -1 &&
            g_item_records[character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo]
                    .equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON) {
            verb = g_localized_strings[g_attack_flag_name_ids[W8_ATTACK_MODE_SHOOT][1]];
        } else {
            verb = g_localized_strings[g_attack_flag_name_ids[mode][1]];
        }
        g_combat.state()->attack_message = verb;
        g_combat.state()->attack_message += " ";
        if (character->Hand[hand].uiHolds != HOLDS_NOTHING) {
            name = GetItemDisplayName(found);
            AppendAttackWeaponName(name, mode);
            AppendAttackMessageSuffix(mode);
        } else {
            if (mode == W8_ATTACK_MODE_BERSERK) {
                g_combat.state()->attack_message += g_localized_strings[0x205];
                g_combat.state()->attack_message += " ";
            }
        }
    }
    if (party_row->target_out_of_combat.iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(party_row->target_out_of_combat.iMonsterID);
        g_combat.state()->attack_message += GetMonsterName(monster_info, 0, 0);
        PointCameraAtMonster(monster_info, false, true);
        if (range <= W8_RANGE_SHORT && !IsMonsterFacingParty(monster_info)) {
            if (monster_info->hp_current != 0 && monster_info->stamina != 0 &&
                monster_info->highest_condition < W8_CONDITION_WEBBED &&
                monster_info->uiCondition[W8_CONDITION_BLIND] == 0 &&
                monster_info->action_kind == W8_MONSTER_ACTION_WAIT) {
                if (monster_info->pCombat->spot_attempts == 0) {
                    noticed = true;
                } else {
                    noticed =
                        Random(100) < static_cast<unsigned int>(
                                          monster_info->attributes[W8_MONSTER_ATTRIBUTE_SENSES] -
                                          monster_info->pCombat->spot_attempts * 25);
                }
                monster_info->pCombat->spot_attempts++;
                if (noticed) {
                    monster_info->p3D->ForwardReferencePosition(0);
                } else {
                    g_combat.state()->unaware = IsPartyLookingAwayFrom(party_slot, monster_info);
                }
            } else {
                g_combat.state()->unaware = IsPartyLookingAwayFrom(party_slot, monster_info);
            }
        }
    } else {
        if (party_row->target_out_of_combat.iType != W8_TARGET_KIND_CHARACTER) {
            Fail("CHAR_CURRENT_TARGET(uiChar).iType == TARGET_TYPE_CHAR");
        }
        if (party_row->target_out_of_combat.iChar == -1) {
            Fail("CHAR_CURRENT_TARGET(uiChar).iChar != BAD_INDEX");
        }
        g_combat.state()->attack_message +=
            g_status.party.characters[party_row->target_out_of_combat.iChar].name;
        if (range <= W8_RANGE_SHORT &&
            !PositionFacesAsDecided(party_row->target_out_of_combat.iChar, party_slot)) {
            if (CharacterNoticesAttacker(party_row->target_out_of_combat.iChar)) {
                FacePositionAsDecided(party_row->target_out_of_combat.iChar, party_slot);
            } else {
                g_combat.state()->unaware = PositionFacesOppositeToDecided(
                    party_slot, party_row->target_out_of_combat.iChar);
            }
        }
    }
    if (g_combat.state()->natural_attack != 0) {
        if (character->Hand[hand].uiHolds == HOLDS_NOTHING) {
            Fail("pPC->Hand[uiHand].uiHolds != HOLDS_NOTHING");
        }
        if (character->Hand[hand].uiHolds != HOLDS_NOTHING) {
            name = GetItemDisplayName(found);
            AppendAttackWeaponName(name, mode);
            AppendAttackMessageSuffix(mode);
        }
    } else {
        if (g_combat.state()->unaware != 0) {
            g_combat.state()->attack_message += g_localized_strings[0x207];
        }
    }
    if (row->uiSwingsRemaining > 1) {
        g_combat.state()->attack_message += fmt::format(" {:d}x", row->uiSwingsRemaining);
    }
    PostCharacterNotice(party_slot, g_combat.state()->attack_message);
    ResetCombatSlot(&g_combat.state()->TargetHit);
    row->attack_sound_played = false;
    if (range >= W8_RANGE_LONG) {
        FireCharacterItemMissile(party_slot, character, row, range);
        MakePCAttackSound(row, &character->Hand[hand], mode, false, -1);
        row->attack_sound_played = true;
    } else {
        event_ids[0] = g_event_range_min;
        event_ids[1] = g_special_event18;
        event_ids[2] = g_event_range_max;
        QueueCharacterEvent(character, event_ids[Random(3)], 0, kCharacterEventFullVolume);
    }
    return true;
}

/* Resolves one queued swing of the character's attack: plays the swing sound
   on the first pass, rolls fumble redirection and guardian interception,
   picks the hit location, rolls to hit and to penetrate, applies damage and
   the struck target's retaliation enchantment, consumes the thrown item or
   charge, and answers whether the attack continues. */
W8CombatActionStatus ResolveCharacterAttack(int party_slot)
{
    W8SpellEffectResult* report = &g_combat.state()->attack_report;
    W8SpellEffectResult local_report;
    W8Character* character = &g_status.party.characters[party_slot];
    unsigned int hand = g_combat.state()->characters[party_slot].current_hand;
    W8CombatCharacterRow* row = &g_combat.state()->characters[party_slot];
    W8PartySlotRow* party_row = &g_status.party.rows[party_slot];
    W8MonsterInfo* monster_info = nullptr;
    W8TargetSource source;
    W8TargetSource target_source;
    W8CombatSlot target;
    W8SpellEffectDefinition effect;
    std::string location_name;
    bool verbose = g_settings.verbose_combat_messages != 0;
    bool special_item = false;
    bool staged_miss = false;
    bool swing_missed = false;
    bool guaranteed_hit = false;
    bool guaranteed_penetration = false;
    bool fumbled = false;
    unsigned int outcome = 1;
    unsigned int queued_fatigue = 1;
    unsigned int hit_location = 0;
    unsigned int damage = 0;
    unsigned int applied = 0;
    int message_id = 0;
    int to_hit = 0;
    int roll = 0;
    W8RangeCategory range = W8_RANGE_TOUCH;
    W8AttackMode attack_mode;

    SetTargetSourceToCharacter(party_slot, &source);
    if (character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == 0x272) {
        special_item = true;
        if (g_combat.state()->missile_hit_result == W8_MISSILE_HIT_DEFLECTED) {
            if (!verbose) {
                report->missed = true;
            } else {
                ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x20a]);
            }
            ResetCombatSlot(&g_combat.state()->TargetHit);
            outcome = 1;
        } else {
            outcome = 2;
        }
    } else if (!TargetIsInPlay(
                   party_slot, hand,
                   SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
        ReportInvalidAttackTarget(party_row->target_out_of_combat, source);
        return W8CombatActionStatus::finishing;
    } else {
        attack_mode = static_cast<W8AttackMode>(party_row->attack_mode[hand]);
        if (!row->attack_sound_played) {
            MakePCAttackSound(row, &character->Hand[hand], attack_mode, true, -1);
            row->attack_sound_played = true;
            return W8CombatActionStatus::resolving;
        }
        target = party_row->target_out_of_combat;
        range = GetCharAttackRange(character, hand);
        if (target.iType == W8_TARGET_KIND_CHARACTER) {
            MarkDefenderSkillUse(target.iChar);
        }
        if (range < W8_RANGE_LONG) {
            to_hit = GetTargetAttackAttributes(party_slot, hand, attack_mode, 0);
            roll = Random(100) + 1;
            guaranteed_hit = roll <= 5;
            swing_missed = roll > 95;
            std::vector<W8CombatSlot> fumble_list;
            BuildCharacterTargetList(party_slot, hand, fumble_list);
            int redirect_chance = GetTargetAttackAttributes(party_slot, hand, attack_mode, 1);
            int fumble_chance = GetCombatFumbleChance(fumble_list, redirect_chance);
            if (character->iRace == W8_RACE_ANDROID) {
                W8NpcState* npc_state = GetNpcState(
                    g_status.party.rows[CharacterPointerToPartySlot(character)].npc_index);
                if (npc_state != nullptr && npc_state->name_style == ' ' &&
                    GetFact(W8_FACT_RFS81_HAS_BEEN_FIXED) == 0) {
                    fumble_chance += 5;
                }
            }
            fumbled = roll > 100 - fumble_chance;
            CombatLog(fmt::format("TO HIT: Chance {:d}, Rolled {:d} (fumble {:d}%)", to_hit, roll,
                                  fumble_chance));
            if (!guaranteed_hit && fumbled) {
                if (!verbose) {
                    local_report = {};
                    report = &local_report;
                }
                g_combat.state()->TargetHit = ChooseCombatFumbleTarget(fumble_list);
                AnnounceAccidentalStrike(&source, &g_combat.state()->TargetHit);
                guaranteed_hit = true;
                guaranteed_penetration = false;
                swing_missed = false;
                roll = Random(100) + 1;
                g_combat.state()->unaware = 0;
                g_combat.state()->natural_attack = 0;

            } else {
                g_combat.state()->TargetHit = party_row->target_out_of_combat;

                guaranteed_penetration = guaranteed_hit;
            }
        } else if (g_combat.state()->missile_hit_result == W8_MISSILE_HIT) {
            swing_missed = false;
            guaranteed_hit = true;
            guaranteed_penetration = false;
            if (CharacterHasTrait(character, W8_TRAIT_THROWN_CRITICALS) &&
                character->Hand[hand].combat_skill == W8_SKILL_RANGED_COMBAT &&
                character->Hand[hand].weapon_skill == W8_SKILL_THROWING_SLING &&
                g_item_records[character->EquippedItem[row->current_equip_slot].iItemNo]
                        .unidentified_name_index != 0xd) {
                guaranteed_penetration = true;
            }
            roll = Random(100) + 1;
        } else {
            if (g_combat.state()->missile_hit_result == W8_MISSILE_HIT_DEFLECTED) {
                swing_missed = false;
                staged_miss = true;
            } else {
                swing_missed = true;
            }
        }
        if (!verbose) {
            report->deferred = true;
        }
        if (swing_missed) {
            if (g_settings.verbose_combat_messages != 0) {
                ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x209]);
            }
            ResetCombatSlot(&g_combat.state()->TargetHit);
        } else if (!staged_miss) {
            source.target_diverted = (target != g_combat.state()->TargetHit);
            if (!IsTargetStillPresent(&g_combat.state()->TargetHit)) {
                ReportInvalidAttackTarget(g_combat.state()->TargetHit, source);
                return W8CombatActionStatus::finishing;
            }
            if (!verbose && source.target_diverted) {
                local_report = {};
                report = &local_report;
            }
            ResolveGuardianInterception(&source, &g_combat.state()->TargetHit);
            PointCameraAtCombatTarget(&source, &g_combat.state()->TargetHit);
            if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER) {
                monster_info = FindMonster(g_combat.state()->TargetHit.iMonsterID);
                W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
                hit_location = RollMonsterHitLocation(record);
                if (record->hit_location_chances[hit_location] < 100) {
                    location_name =
                        g_localized_strings[g_monster_hit_location_labels[hit_location]
                                                                         [record->constitution]];
                } else {
                    location_name = "";
                }
            } else {
                if (g_combat.state()->TargetHit.iType != W8_TARGET_KIND_CHARACTER) {
                    ReportInvalidAttackTarget(g_combat.state()->TargetHit, source);
                    return W8CombatActionStatus::finishing;
                }
                hit_location = RollCharacterHitLocation();
                location_name = g_localized_strings[g_pc_hit_location_labels[hit_location][0]];
            }
            if (!guaranteed_hit && to_hit < roll) {
                /* Retail hands the weapon's material, not its attack sound
                   class, to BlockedForSpecialReason as the impact-table row. */
                int weapon_class;
                if (character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1) {
                    weapon_class = 9;
                } else {
                    weapon_class =
                        g_item_records[character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON]
                                           .iItemNo]
                            .material;
                }
                if (!BlockedForSpecialReason(weapon_class, &g_combat.state()->TargetHit, roll,
                                             to_hit, 8)) {
                    if (g_settings.verbose_combat_messages != 0) {
                        ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x209]);
                    }
                    if (character->hp_current / static_cast<float>(character->uiHPMax) <
                        kNavigatorMode3Scale) {
                        if (Random(100) < 0x1e) {
                            QueueCharacterEvent(character, g_special_event13, 0,
                                                kCharacterEventFullVolume);
                        }
                    } else if (Random(100) < 0x19) {
                        QueueCharacterEvent(character, g_special_event13, 0,
                                            kCharacterEventFullVolume);
                    }
                    if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER &&
                        monster_info->p3D->IsFacingPlayer() != 0 && !monster_info->fMotionless) {
                        StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_DODGE,
                                          W8_ANIMATION_PLAY_ONCE);
                    }
                }
            } else {
                if (g_settings.verbose_combat_messages == 0) {
                    report->target = g_combat.state()->TargetHit;
                } else {
                    ShowNotice(W8_FONT_PALETTE_WHITE,
                               fmt::sprintf(g_localized_strings[0x20c], location_name));
                }
                outcome = 2;
                queued_fatigue = 2;
                W8AttackMode mode = attack_mode;
                if (row->paired_equip_slot != -1) {
                    mode = CharChooseHandAttackMode(character, 1);
                }
                int penetrate = GetTargetHitAttackAttributes(party_slot, hand, mode, hit_location);
                CombatLog(fmt::format("TO PENETRATE: Chance {:d}, Rolled {:d}", penetrate, roll));
                if (!guaranteed_penetration && penetrate < roll) {
                    message_id = 0x20f;
                } else {
                    outcome = 3;
                    queued_fatigue = 3;
                    unsigned int dice_count = 0;
                    unsigned char hit_flag = 0;
                    damage = ResolveCharacterAttackDamage(party_slot, hand, attack_mode,
                                                          &dice_count, &hit_flag);
                    if (damage == 0) {
                        message_id = 0x20e;
                    } else {
                        outcome = 4;
                        if (fumbled) {
                            damage = CapAttackDamageByTargetHealth(damage);
                        } else if (dice_count > 1) {
                            if (verbose) {
                                ShowNotice(W8_FONT_PALETTE_WHITE,
                                           fmt::sprintf(g_localized_strings[0x20d], dice_count));
                            }
                            outcome = 5;
                        }
                        W8HandAttack* hand_attack = &character->Hand[hand];
                        MakePCMeleeHitSound(party_slot, hand_attack, &g_combat.state()->TargetHit,
                                            hit_location, -1);
                        IsTargetStillPresent(&g_combat.state()->TargetHit);
                        if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER) {
                            applied =
                                ApplyDamageToMonster(monster_info, damage, &source, false, verbose,
                                                     verbose, verbose ? nullptr : report, false);
                            if (monster_info->hp_current == 0 && hit_flag != 0 && Random(2) == 0 &&
                                CurrentLevelPopulation().monster_roster.hostileCombatCount() > 1) {
                                W8CharacterEvent* event = QueueCharacterEvent(
                                    character, 56, 0, kCharacterEventFullVolume);
                                if (event != nullptr) {
                                    event->DelayDispatch(800);
                                }
                            }
                        } else {
                            applied = ApplyDamageToCharacter(g_combat.state()->TargetHit.iChar,
                                                             damage, false, verbose, verbose,
                                                             verbose ? nullptr : report, false);
                        }
                        if (fumbled) {
                            QueueFumbleReaction(party_slot);
                        }
                        memset(&effect, 0, sizeof(effect));
                        effect.power_level =
                            character->uiExpLevel +
                            (character->uiExpLevel > 0xe ? 0xf : character->uiExpLevel);
                        if (applied != 0) {
                            if (hand_attack->uiHolds == HOLDS_NOTHING) {
                                memcpy(effect.condition_chances, hand_attack->condition_chances,
                                       0x10);
                                effect.magnitude_base = 0;
                            } else {
                                memcpy(
                                    effect.condition_chances,
                                    g_item_records[character->EquippedItem[row->current_equip_slot]
                                                       .iItemNo]
                                        .missile_values,
                                    0x10);
                                effect.magnitude_base =
                                    g_item_records[character->EquippedItem[row->current_equip_slot]
                                                       .iItemNo]
                                        .missile_magnitude;
                                if (row->paired_equip_slot != -1) {
                                    const unsigned char* paired_values =
                                        g_item_records[character
                                                           ->EquippedItem[row->paired_equip_slot]
                                                           .iItemNo]
                                            .missile_values;
                                    for (int i = 0; i < 0x10; ++i) {
                                        effect.condition_chances[i] += paired_values[i];
                                    }
                                    effect.magnitude_base +=
                                        g_item_records[character
                                                           ->EquippedItem[row->paired_equip_slot]
                                                           .iItemNo]
                                            .missile_magnitude;
                                }
                            }
                            if (!fumbled) {
                                unsigned int skill_level = 0;
                                if (hand_attack->combat_skill == W8_SKILL_CLOSE_COMBAT) {
                                    skill_level = character->skills[W8_SKILL_CRITICAL_STRIKE].level;
                                } else if (hand_attack->combat_skill == W8_SKILL_RANGED_COMBAT &&
                                           hand_attack->weapon_skill > W8_SKILL_SHIELD) {
                                    if (hand_attack->weapon_skill <= W8_SKILL_BOW) {
                                        if (CharacterHasTrait(character,
                                                              W8_TRAIT_RANGED_CRITICALS)) {
                                            skill_level =
                                                character->skills[W8_SKILL_RANGED_COMBAT].level;
                                        }
                                    } else if (hand_attack->weapon_skill ==
                                               W8_SKILL_THROWING_SLING) {
                                        if (g_item_records
                                                [character->EquippedItem[row->current_equip_slot]
                                                     .iItemNo]
                                                    .unidentified_name_index == 0xd) {
                                            if (CharacterHasTrait(character,
                                                                  W8_TRAIT_RANGED_CRITICALS)) {
                                                skill_level =
                                                    character->skills[W8_SKILL_RANGED_COMBAT].level;
                                            }
                                        } else if (CharacterHasTrait(character,
                                                                     W8_TRAIT_THROWN_CRITICALS)) {
                                            skill_level =
                                                character->skills[W8_SKILL_CRITICAL_STRIKE].level;
                                        }
                                    }
                                }
                                if (skill_level != 0) {
                                    char bonus = static_cast<char>(skill_level / 0x19);
                                    if (skill_level % 0x19 != 0 &&
                                        Random(0x19) < skill_level % 0x19) {
                                        ++bonus;
                                    }
                                    effect.condition_chances[W8_ATTACK_EFFECT_KILL] += bonus;
                                }
                                if (CharacterHasTrait(character, W8_TRAIT_KNOCKOUT)) {
                                    effect.condition_chances[W8_ATTACK_EFFECT_KNOCK_OUT] +=
                                        static_cast<char>(ScaleValueByProfessionLevel(
                                            character, W8_TRAIT_KNOCKOUT, 5.0));
                                }
                            }
                            ApplyEffectConditions(&source, &g_combat.state()->TargetHit, &effect,
                                                  verbose, false, report);
                            if (character->EquippedItem[row->current_equip_slot].iItemNo == 0x1f8) {
                                unsigned int heal = applied / 3;
                                unsigned int missing = character->uiHPMax - character->hp_current;
                                if (missing < heal) {
                                    heal = missing;
                                }
                                if (heal != 0) {
                                    HealCharacter(party_slot, heal, verbose);
                                    if (!verbose) {
                                        report->notice_values[5] += heal;
                                    }
                                }
                            }
                            if (range < W8_RANGE_LONG &&
                                g_combat.state()->TargetHit.iType == W8_TARGET_KIND_CHARACTER &&
                                hit_location == 1 &&
                                g_status.party.characters[g_combat.state()->TargetHit.iChar]
                                        .EquippedItem[W8_EQUIP_SLOT_TORSO]
                                        .iItemNo == 500) {
                                SetTargetSourceToCharacter(g_combat.state()->TargetHit.iChar,
                                                           &target_source);
                                ApplyDirectDamageToCharacter(party_slot, &target_source, damage);
                            }
                        }
                    }
                }
                /* Only a swing that hit plays the glance sound, posts the no-damage
                   notice and draws the Razor Cloak backlash; a missed swing goes
                   straight to the queued fatigue. */
                if (damage == 0) {
                    MakePCMeleeHitSound(party_slot, &character->Hand[hand],
                                        &g_combat.state()->TargetHit, hit_location, 0x2a);
                    if (g_combat.state()->TargetHit.iType == W8_TARGET_KIND_MONSTER) {
                        MonsterReactsToBeingStruck(monster_info, &source, false);
                    }
                    if (!verbose) {
                        ++report->count;
                    } else {
                        ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[message_id]);
                    }
                }
                if (range < W8_RANGE_LONG) {
                    ApplyRazorCloakBacklash(
                        g_combat.state()->TargetHit, monster_info,
                        [&](W8TargetSource* backlash_source, W8Enchantment* enchantment) {
                            ApplyDiceDamageToCharacter(party_slot, backlash_source, enchantment);
                        });
                }
            }
            ApplyQueuedFatigue(&g_combat.state()->TargetHit, queued_fatigue, 1);
        } else {
            if (!verbose) {
                report->missed = true;
            } else {
                ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x20a]);
            }
            ResetCombatSlot(&g_combat.state()->TargetHit);
        }
        if (fumbled) {
            outcome = 1;
        } else if (outcome > 3) {
            if (character->Hand[hand].combat_skill == W8_SKILL_CLOSE_COMBAT) {
                if (character->skills[W8_SKILL_CRITICAL_STRIKE].active) {
                    PracticeCharacterSkill(character, W8_SKILL_CRITICAL_STRIKE, 1, false);
                }
                if (character->skills[W8_SKILL_POWER_STRIKE].active) {
                    PracticeCharacterSkill(character, W8_SKILL_POWER_STRIKE, 1, false);
                }
            } else if (character->Hand[hand].combat_skill == W8_SKILL_RANGED_COMBAT) {
                if (character->skills[W8_SKILL_EAGLE_EYE].active) {
                    PracticeCharacterSkill(character, W8_SKILL_EAGLE_EYE, 1, false);
                }
                if (CharacterHasTrait(character, W8_TRAIT_THROWN_CRITICALS) &&
                    character->skills[W8_SKILL_CRITICAL_STRIKE].active) {
                    PracticeCharacterSkill(character, W8_SKILL_CRITICAL_STRIKE, 1, false);
                }
            }
        }
    }
    if (row->hand_records[hand].score < static_cast<int>(outcome)) {
        row->hand_records[hand].score = outcome;
        row->hand_records[hand].weapon_skill = character->Hand[hand].weapon_skill;
        row->hand_records[hand].combat_skill = character->Hand[hand].combat_skill;
        row->hand_records[hand].dual_wielding = character->dual_wielding;
    }
    FatigueCharacter(party_slot, CharacterActionFatigueCost(party_slot, party_row->pending_action),
                     true, 0);
    if (ConsumeAttackItems(party_slot, hand, row->current_equip_slot, row->paired_equip_slot))
        row->uiSwingsRemaining = 1;
    if (row->uiSwingsRemaining == 0) {
        Fail("pCmbt->uiSwingsRemaining > 0");
    }
    --row->uiSwingsRemaining;
    if (character->hp_current == 0 || character->highest_condition >= W8_CONDITION_ASLEEP) {
        row->uiSwingsRemaining = 0;
    }
    if (row->uiSwingsRemaining != 0) {
        if (hand >= 2) {
            Fail("uiHand < HAND_COUNT");
        }
        if (character->Hand[hand].in_play && GetCharAttackRange(character, hand) != W8_RANGE_NONE &&
            (target == party_row->target_out_of_combat) &&
            TargetIsInPlay(party_slot, hand,
                           SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))) {
            if (!verbose && (fumbled || source.target_diverted)) {
                ReportCharacterAttackResult(party_slot, report);
                PostCharacterNotice(
                    party_slot,
                    fmt::sprintf(g_localized_strings[0x269],
                                 CombatTargetName(&source, &party_row->target_out_of_combat)));
            }
            ResetCombatSlot(&g_combat.state()->TargetHit);
            row->attack_sound_played = false;
            if (range >= W8_RANGE_LONG) {
                FireCharacterItemMissile(party_slot, character, row, range);
                MakePCAttackSound(row, &character->Hand[hand], attack_mode, false, -1);
                row->attack_sound_played = true;
            }
            return W8CombatActionStatus::resolving;
        }
    }
    if (!verbose && !special_item) {
        ReportCharacterAttackResult(party_slot, report);
        if (fumbled || source.target_diverted) {
            PostCharacterNotice(
                party_slot,
                fmt::sprintf(g_localized_strings[0x269],
                             CombatTargetName(&source, &party_row->target_out_of_combat)));
            if (g_combat.state()->attack_report.deferred) {
                ReportCharacterAttackResult(party_slot, &g_combat.state()->attack_report);
            }
        }
    }
    return W8CombatActionStatus::finishing;
}
