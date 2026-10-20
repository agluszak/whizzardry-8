#include <span>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "character_skills.h"
#include "difficulty.h"
#include "local_code/Factions.h"
#include "local_code/HealthStaminaMana.h"
#include "monster_actions.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

/* Species 0x224 never counts: both directions answer zero before anything
   else is read. */
enum { W8_NEUTRAL_SPECIES_224 = 0x224 };

void RefreshCombatEngagement(void)
{
    if (g_combat.active() && CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0) {
        g_combat.state()->enemies_engaged = true;
    }
    RequestRefreshPartyState();
}

/* Compare two monsters for hostility. Equal disposition bands answer two;
   either band clear answers zero; otherwise the faction records decide, and
   only matching non-zero factions fall through to the condition-thirteen
   presence test. */
W8Disposition MonsterHostility(W8MonsterInfo* first, W8MonsterInfo* second)
{
    W8MonsterRecord* first_record;
    W8MonsterRecord* second_record;
    unsigned int first_faction;
    unsigned int second_faction;

    if (first->monster_species == W8_NEUTRAL_SPECIES_224 ||
        second->monster_species == W8_NEUTRAL_SPECIES_224) {
        return W8_DISPOSITION_NEUTRAL;
    }
    if (first->ubDisposition == second->ubDisposition) {
        return W8_DISPOSITION_FRIENDLY;
    }
    if (first->ubDisposition == W8_DISPOSITION_NEUTRAL ||
        second->ubDisposition == W8_DISPOSITION_NEUTRAL) {
        return W8_DISPOSITION_NEUTRAL;
    }
    first_record = GetMonsterDataForInfo(first);
    second_record = GetMonsterDataForInfo(second);
    first_faction = first_record->faction_id;
    if (first_faction == 0) {
        return W8_DISPOSITION_HOSTILE;
    }
    second_faction = second_record->faction_id;
    if (second_faction == 0 || first_faction != second_faction) {
        return W8_DISPOSITION_HOSTILE;
    }
    if ((first->uiCondition[W8_CONDITION_TURNCOAT] != 0) ==
        (second->uiCondition[W8_CONDITION_TURNCOAT] != 0)) {
        return W8_DISPOSITION_NEUTRAL;
    }
    return W8_DISPOSITION_HOSTILE;
}

/* Map a monster's disposition band onto the party character: while the
   character carries condition thirteen the hostile and friendly bands swap;
   otherwise the monster's band is returned unchanged. */
W8Disposition MonsterVsCharDisposition(int character_slot, W8MonsterInfo* monster_info)
{
    W8Disposition disposition;

    if (g_status.party.characters[character_slot].uiCondition[W8_CONDITION_TURNCOAT] == 0) {
        return monster_info->ubDisposition;
    }
    disposition = monster_info->ubDisposition;
    switch (disposition) {
    case W8_DISPOSITION_NEUTRAL:
        return W8_DISPOSITION_NEUTRAL;
    case W8_DISPOSITION_HOSTILE:
        return W8_DISPOSITION_FRIENDLY;
    case W8_DISPOSITION_FRIENDLY:
        return W8_DISPOSITION_HOSTILE;
    default:
        Fail("FALSE", "MonsterVsCharDisposition: ERROR - Invalid attack mode");
        return W8_DISPOSITION_NEUTRAL;
    }
}

W8Disposition CharacterVsCharacterDisposition(int first, int second)
{
    W8Character* characters = g_status.party.characters;
    unsigned int first_turns = characters[first].uiCondition[W8_CONDITION_TURNCOAT];
    if (first_turns == 0 && characters[second].uiCondition[W8_CONDITION_TURNCOAT] == 0) {
        return W8_DISPOSITION_FRIENDLY;
    }
    if (first_turns == 0 || characters[second].uiCondition[W8_CONDITION_TURNCOAT] == 0) {
        return W8_DISPOSITION_HOSTILE;
    }
    return W8_DISPOSITION_FRIENDLY;
}

W8Disposition GetOppositeDisposition(W8TargetSource* source)
{
    if (TargetSourceIsCharacter(source, 0)) {
        if (g_status.party.characters[source->iChar].uiCondition[W8_CONDITION_TURNCOAT] == 0) {
            return W8_DISPOSITION_HOSTILE;
        }
    } else if (TargetSourceIsMonster(source, 0)) {
        W8MonsterInfo* monster_info = FindMonster(source->iMonsterID, false);
        if (monster_info == nullptr) {
            DebugMessage(
                1, fmt::format("GetOppositeDisposition - ERROR: checking obsolete Monster ID {:d}",
                               source->iMonsterID));
            return W8_DISPOSITION_NEUTRAL;
        }
        if (monster_info->uiCondition[W8_CONDITION_TURNCOAT] == 0) {
            if (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY) {
                return W8_DISPOSITION_HOSTILE;
            }
            return W8_DISPOSITION_FRIENDLY;
        }
    }
    return W8_DISPOSITION_NEUTRAL;
}

void ProvokeListedMonsterGroups(W8TargetSource* source, const std::vector<int>& monsters)
{
    int own_monster_id = -1;
    if (GetOppositeDisposition(source) == W8_DISPOSITION_NEUTRAL) {
        return;
    }
    if (TargetSourceIsMonster(source, 0)) {
        own_monster_id = source->iMonsterID;
    }
    W8CombatSlot target;
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_MONSTER;
    for (int monster_id : monsters) {
        if (monster_id == own_monster_id) {
            continue;
        }
        W8MonsterInfo* monster_info = FindMonster(monster_id);
        if (monster_info->monster_group_id != 0) {
            target.iMonsterID = monster_id;
            MakeTargetGroupHostile(source, &target);
        }
    }
}

void MakeTargetGroupHostile(W8TargetSource* source, W8CombatSlot* target)
{
    W8Disposition hostility = GetOppositeDisposition(source);
    if (hostility == W8_DISPOSITION_NEUTRAL || !IsTargetStillPresent(target)) {
        return;
    }
    int group_id;
    W8MonsterGroup* group;
    if (target->iType == W8_TARGET_KIND_MONSTER) {
        if (target->iMonsterID == -1) {
            return;
        }
        W8MonsterInfo* monster_info = FindMonster(target->iMonsterID);
        if (monster_info->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
            return;
        }
        group_id = monster_info->monster_group_id;
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        if (target->iGroupID == -1) {
            return;
        }
        group = FindMonsterGroup(target->iGroupID);
        unsigned int index = 0;
        if (group->MemberIds().empty()) {
            return;
        }
        while (true) {
            int monster_id = group->MemberLocationId(index);
            W8MonsterInfo* monster_info = FindMonster(monster_id);
            if (monster_info->uiCondition[W8_CONDITION_TURNCOAT] == 0) {
                break;
            }
            if (group->MemberIds().size() <= ++index) {
                return;
            }
        }
        group_id = target->iGroupID;
    } else {
        return;
    }
    if (group_id != -1) {
        group = FindMonsterGroup(group_id);
        SetMonsterGroupHostility(group, hostility, true);
        if (!group->fInCombat) {
            MonsterGroupEnterCombat(group);
        }
    }
}

/* Whether a party action aims at enemies: the four melee kinds do, as do
   spells (and item-spells) whose target type sits in the enemy band. */
bool CharacterActionTargetsEnemies(const W8Party& party, W8Character* character,
                                   W8ActionKind action_kind, int action_detail,
                                   W8ActionDetailBlock* detail)
{
    const W8ItemInstance* item;
    unsigned char spell_id;
    W8SpellTargetType target_type;

    switch (action_kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
    case W8_ACTION_BREATHE:
    case W8_ACTION_TURN_UNDEAD:
        return true;
    case W8_ACTION_CAST_SPELL:
        if (action_detail > 0x95) {
            Fail("iType < SPELL_COUNT");
        }
        if (action_detail != 3 && action_detail != 0x29) {
            target_type = GetSpellTargetType(action_detail, false);
            if (target_type > W8_TARGET_TYPE_PARTY && target_type < W8_TARGET_TYPE_POINT) {
                return true;
            }
        }
        break;
    case W8_ACTION_USE_ITEM:
        item = party.itemAt(detail->item_use.item);
        if (CanCharacterActivateItem(character, item)) {
            spell_id = g_item_records[item->iItemNo].spell_id;
            if (spell_id != W8_SPELL_NONE) {
                if (spell_id > 0x95) {
                    Fail("iType < SPELL_COUNT");
                }
                if (spell_id != W8_SPELL_CHARM && spell_id != W8_SPELL_MINDREAD) {
                    target_type = GetSpellTargetType(spell_id, false);
                    if (target_type > W8_TARGET_TYPE_PARTY && target_type < W8_TARGET_TYPE_POINT) {
                        return true;
                    }
                }
            }
        }
        break;
    default:
        break;
    }
    return false;
}

/* Monster actions 0 and 3 always count as enemy-aimed; action 2 is a spell id. */
bool MonsterActionTargetsEnemies(W8MonsterActionKind action_kind, int action_detail,
                                 unsigned int* spell_power_level)
{
    W8SpellTargetType target_type;

    switch (action_kind) {
    case W8_MONSTER_ACTION_ATTACK:
        return true;
    case W8_MONSTER_ACTION_SPELL:
        if (action_detail > 0x95) {
            Fail("iType < SPELL_COUNT");
        }
        if (action_detail != 3 && action_detail != 0x29) {
            target_type = GetSpellTargetType(action_detail, false);
            if (target_type > W8_TARGET_TYPE_PARTY && target_type < W8_TARGET_TYPE_POINT) {
                return true;
            }
        }
        return false;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        return true;
    default:
        return false;
    }
}

/* Whether a spell id can be aimed by monster AI: inside the spell table, not
   one of the two self-only kinds, and carrying a middle target type. */
bool MonsterCanAimSpell(int spell_id)
{
    if (spell_id > 0x95) {
        Fail("iType < SPELL_COUNT");
    }
    if (spell_id != W8_SPELL_CHARM && spell_id != W8_SPELL_MINDREAD) {
        W8SpellTargetType target_type = GetSpellTargetType(spell_id, false);
        if (target_type > W8_TARGET_TYPE_PARTY && target_type < W8_TARGET_TYPE_POINT) {
            return true;
        }
        return false;
    }
    return false;
}

bool CombatAllowsLiveGroups(void)
{
    return g_combat.active() && !g_combat.state()->enemies_engaged &&
           g_combat.state()->round_count <= 1;
}

static unsigned short g_group_hostility_notice_ids[3] = {511, 512, 513};

int g_monster_special_attack_name_ids[12] = {0,    1598, 1599, 1600, 1601, 1602,
                                             1603, 1604, 1605, 1606, 1607, 1608};

void SetMonsterGroupHostilityByID(int group_id, W8Disposition hostility, bool recurse)
{
    W8MonsterGroup* group = FindMonsterGroup(group_id);
    SetMonsterGroupHostility(group, hostility, recurse);
}

void SetMonsterGroupHostility(W8MonsterGroup* group, W8Disposition hostility, bool recurse)
{
    if (MonsterGroupAllMembersDying(group)) {
        return;
    }
    W8MonsterInfo* leader = FindMonster(group->leader_location_id);
    if (leader != 0 && leader->p3D->hostility_preserved) {
        return;
    }
    W8Disposition previous = group->ubDisposition;
    if (previous == hostility) {
        return;
    }
    group->ubDisposition = hostility;
    group->forced_neutral = false;
    if (MonsterGroupHasVisibleThreat(group)) {
        ShowNotice(9, fmt::format("{} {} {}!", GetMonsterGroupName(group),
                                  g_localized_strings[0x1d7 + (group->MemberCount() != 1)],
                                  g_localized_strings[g_group_hostility_notice_ids[hostility]]));
    }
    group->hostility_set_at = g_status.world_clock;
    if (previous != W8_DISPOSITION_NEUTRAL) {
        SetTargetToGroup(group->group_id, true);
    }
    for (unsigned int index = 0; index < group->MemberIds().size(); ++index) {
        int location_id = group->MemberLocationId(index);
        W8MonsterInfo* monster = FindMonster(location_id);
        SetMonsterHostility(monster, hostility);
    }
    if (group->leader_group_id != 0) {
        SetMonsterGroupHostility(FindMonsterGroup(group->leader_group_id), hostility, false);
    }
    if (group->allied_group_ids[0] != 0) {
        SetMonsterGroupHostility(FindMonsterGroup(group->allied_group_ids[0]), hostility, false);
    }
    if (group->allied_group_ids[1] != 0) {
        SetMonsterGroupHostility(FindMonsterGroup(group->allied_group_ids[1]), hostility, false);
    }
    if (recurse) {
        AlertSameFactionGroups(group);
    }
    RequestRedrawParty();
}

void SetMonsterHostility(W8MonsterInfo* monster, W8Disposition hostility)
{
    W8Disposition previous = monster->ubDisposition;
    if (previous == hostility || monster->p3D->hostility_preserved) {
        return;
    }
    monster->ubDisposition = hostility;
    if (monster->pCombat &&
        (hostility == W8_DISPOSITION_HOSTILE || previous == W8_DISPOSITION_HOSTILE)) {
        RefreshCombatEngagement();
    }
    if (previous != W8_DISPOSITION_NEUTRAL) {
        SetTargetToMonster(monster->location_id, true);
    }
    if (monster->pCombat && monster->hp_current > 0 &&
        monster->ubDisposition != W8_DISPOSITION_NEUTRAL) {
        if (g_combat.active() &&
            g_combat.state()->action_status != W8CombatActionStatus::scheduled &&
            g_combat.state()->pActionMonsterInfo == monster) {
            EndMonsterAttack(monster);
            monster->action_kind = W8_MONSTER_ACTION_NONE;
            monster->pCombat->phase = 0;
        } else if (!monster->pCombat->active) {
            UpdateMonsterAI(monster);
        } else {
            monster->action_kind = W8_MONSTER_ACTION_NONE;
            monster->pCombat->phase = 0;
        }
    }
}

bool CanPartySlotTurnUndead(int party_slot)
{
    if (!CharacterHasTrait(&g_status.party.characters[party_slot], W8_TRAIT_TURN_UNDEAD) ||
        g_combat.state() == 0 || g_combat.state()->characters[party_slot].turn_undead_used) {
        return false;
    }
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster->fActive && monster->pCombat &&
            monster->ubDisposition == W8_DISPOSITION_HOSTILE && monster->hp_current != 0 &&
            GetMonsterDataForInfo(monster)->kind == 0x14) {
            return true;
        }
    }
    return false;
}

int TurnUndead(int party_slot, int* out_cost, bool check)
{
    std::vector<int> monsters;
    std::vector<int> party;
    if (check) {
        if (!CanPartySlotTurnUndead(party_slot)) {
            PostCharacterNotice(
                party_slot, fmt::sprintf(g_localized_strings[0x1b7], g_localized_strings[0x512]));
            return 0;
        }
        g_combat.state()->characters[party_slot].turn_undead_used = true;
    }

    W8TargetSource source;
    SetTargetSourceToCharacter(party_slot, &source);
    source.aim_resolved = true;
    int power;
    if (!check) {
        power = g_status.party.characters[party_slot].profession_levels[W8_PROFESSION_BISHOP] + 10 +
                g_status.party.characters[party_slot].profession_levels[W8_PROFESSION_PRIEST];
        source.auto_cast = true;
    } else {
        power = g_status.party.characters[party_slot].profession_levels[W8_PROFESSION_BISHOP] +
                g_status.party.characters[party_slot].profession_levels[W8_PROFESSION_PRIEST];
        PostCharacterNotice(party_slot, g_localized_strings[0x182]);
    }

    W8CombatSlot target;
    ResetCombatSlot(&target);
    PopulateSpellTargetMarkers(W8_SPELL_DISPEL_UNDEAD, 1, &source, &target, monsters, party,
                               W8AreaSightRule::RequireLineOfSight);
    if (monsters.empty()) {
        return 0;
    }
    SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
    unsigned int spell_power = power / static_cast<int>(monsters.size());
    if (spell_power == 0) {
        while (power < static_cast<int>(monsters.size())) {
            monsters.erase(monsters.begin() + Random(static_cast<int>(monsters.size())));
        }
        spell_power = 1;
    } else if (spell_power > 6) {
        spell_power = 7;
    }
    CastSpellFromSource(W8_SPELL_DISPEL_UNDEAD, &source, &target, spell_power, 0, 0, false, 0,
                        W8SpellCastKind::Standard, 0, &monsters);
    if (out_cost != 0) {
        *out_cost = CharacterActionFatigueCost(party_slot, W8_ACTION_TURN_UNDEAD);
    }
    return static_cast<int>(monsters.size());
}

bool CanPartySlotPray(int party_slot)
{
    if (!CharacterHasTrait(&g_status.party.characters[party_slot], W8_TRAIT_PRAY) ||
        g_combat.state() == 0 || g_combat.state()->characters[party_slot].pray_used) {
        return false;
    }
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster->fActive && monster->pCombat &&
            monster->ubDisposition == W8_DISPOSITION_HOSTILE && monster->hp_current != 0) {
            return true;
        }
    }
    return false;
}

static int g_pray_roll_weights[14] = {5, 5, 10, 10, 10, 20, 10, 10, 10, 5, 5, 5, 5, 5};

/* Fourteen cumulative weights followed by their grand total (0x0068D84C).
   Retail folds the table with one walk to the end of this single block. */
#define PRAY_ROLL_TIERS 14
static int g_pray_roll_sums[PRAY_ROLL_TIERS + 1];

static char g_pray_dash[] = " -- ";

/* Pray: the trait-eleven once-per-combat divine intervention. The flat
   weight table is folded into cumulative sums on first use - the block's
   last slot is the grand total - and the roll is biased by
   how long the fight has run and by the combat difficulty. The selected tier
   retries downward through the table until an action lands. */
int CharacterPrayAction(int party_slot)
{
    W8Character* character = &g_status.party.characters[party_slot];
    std::vector<int> monster_targets;
    bool found = false;
    bool prayed = false;
    int outcome;
    W8TargetSource source;
    W8CombatSlot target;
    unsigned int cost;
    unsigned int power_level;
    unsigned int roll;
    unsigned int in_range;
    unsigned int index;
    int action;
    int pick;
    int best;

    if (!CanPartySlotPray(party_slot)) {
        return 0;
    }
    if (g_pray_roll_sums[PRAY_ROLL_TIERS] == 0) {
        int* const end = g_pray_roll_sums + PRAY_ROLL_TIERS + 1;
        for (index = 0; index < PRAY_ROLL_TIERS; ++index) {
            const int weight = g_pray_roll_weights[index];
            for (int* sum = &g_pray_roll_sums[index]; sum < end; ++sum) {
                *sum += weight;
            }
        }
    }
    SetTargetSourceToCharacter(party_slot, &source);
    source.auto_cast = true;
    source.aim_resolved = true;
    ResetCombatSlot(&target);
    PostCharacterNotice(party_slot, g_localized_strings[0x174]);
    cost = CharacterActionFatigueCost(party_slot, W8_ACTION_PRAY);
    if (character->stamina < static_cast<int>(cost) &&
        character->stamina < static_cast<int>(Random(cost))) {
        PostCharacterNotice(party_slot, g_localized_strings[0x175]);
        return cost;
    }
    if (g_settings.verbose_combat_messages == 0) {
        SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
        AppendToLastTextLine(g_pray_dash, -1);
        SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
    }
    roll = Random(g_pray_roll_sums[PRAY_ROLL_TIERS]);
    if (g_combat.state()->round_count < 4) {
        roll += (g_combat.state()->round_count * 3 - 12) * 5;
    } else if (g_combat.state()->round_count > 8) {
        roll += Random(10);
    }
    if (g_settings.difficulty == W8_DIFFICULTY_NOVICE) {
        roll -= 10;
    } else if (g_settings.difficulty == W8_DIFFICULTY_EXPERT) {
        roll += 10;
    }
    action = 0;
    for (index = 0; index < 14; ++index) {
        action = index;
        if (static_cast<int>(roll) <= g_pray_roll_sums[index]) {
            break;
        }
        action = 0;
    }
    power_level =
        static_cast<unsigned int>(character->profession_levels[character->iProfession]) / 3 + 2;
    while (!prayed) {
        --action;
        switch (action) {
        case 0:
            if (Random(2) == 0) {
                AppendToLastTextLine(g_localized_strings[0x178], -1);
                g_combat.state()->experience_bonus += 10;
            } else {
                AppendToLastTextLine(g_localized_strings[0x177], -1);
                AddPartyGold(100, true);
            }
            prayed = true;
            break;
        case 1:
            for (index = 0; index < 8; ++index) {
                W8Character* member = &g_status.party.characters[index];
                if (g_status.party.rows[index].fOccupied && member->hp_current != 0 &&
                    member->stamina < member->uiStaminaMax) {
                    AppendToLastTextLine(g_localized_strings[0x179], -1);
                    CastSpellFromSource(W8_SPELL_REST_ALL, &source, &target, 7, 0, 0, true,
                                        &outcome, W8SpellCastKind::Standard, 0, 0);
                    prayed = true;
                    break;
                }
            }
            break;
        case 2:
            found = false;
            in_range = 0;
            for (index = 0; index < 8; ++index) {
                W8Character* member = &g_status.party.characters[index];
                if (CanPartySlotParticipate(index) &&
                    member->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].power == 0) {
                    ++in_range;
                    found = true;
                }
            }
            if (found) {
                pick = Random(in_range) + 1;
                for (index = 0; index < 8; ++index) {
                    W8Character* member = &g_status.party.characters[index];
                    if (CanPartySlotParticipate(index) &&
                        member->enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].power == 0 &&
                        --pick == 0) {
                        AppendToLastTextLine(
                            fmt::sprintf(g_localized_strings[0x17a], member->name, -1), -1);
                        target.iType = W8_TARGET_KIND_CHARACTER;
                        if (power_level > 6) {
                            power_level = 7;
                        }
                        target.iChar = index;
                        CastSpellFromSource(W8_SPELL_GUARDIAN_ANGEL, &source, &target, power_level,
                                            0, 0, false, &outcome, W8SpellCastKind::Standard, 0, 0);
                        prayed = true;
                        break;
                    }
                }
            }
            break;
        case 3:
            found = false;
            in_range = 0;
            for (index = 0; index < 8; ++index) {
                W8Character* member = &g_status.party.characters[index];
                if (CanPartySlotParticipate(index) &&
                    (member->uiCondition[W8_CONDITION_INSANE] != 0 ||
                     member->uiCondition[W8_CONDITION_TURNCOAT] != 0)) {
                    ++in_range;
                    found = true;
                }
            }
            if (found) {
                pick = Random(in_range) + 1;
                for (index = 0; index < 8; ++index) {
                    W8Character* member = &g_status.party.characters[index];
                    if (CanPartySlotParticipate(index) &&
                        (member->uiCondition[W8_CONDITION_INSANE] != 0 ||
                         member->uiCondition[W8_CONDITION_TURNCOAT] != 0) &&
                        --pick == 0) {
                        AppendToLastTextLine(g_localized_strings[0x179], -1);
                        target.iType = W8_TARGET_KIND_CHARACTER;
                        const bool capped = power_level > 7;
                        if (capped) {
                            power_level = 7;
                        }
                        target.iChar = index;
                        CastSpellFromSource(W8_SPELL_SANE_MIND, &source, &target, power_level, 0, 0,
                                            capped, &outcome, W8SpellCastKind::Standard, 0, 0);
                        prayed = true;
                        break;
                    }
                }
            }
            break;
        case 4:
            if (power_level > 7) {
                if ((power_level & ~1u) < 14) {
                    power_level >>= 1;
                } else {
                    power_level = 7;
                }
                ResetCombatSlot(&target);
                target.iType = W8_TARGET_KIND_PARTY;
                AppendToLastTextLine(g_localized_strings[0x179], -1);
                CastSpellFromSource(W8_SPELL_HEAL_ALL, &source, &target, power_level, 0, 0, true,
                                    &outcome, W8SpellCastKind::Standard, 0, 0);
                prayed = true;
                break;
            }
            best = -1;
            for (index = 0; index < 8; ++index) {
                W8Character* member = &g_status.party.characters[index];
                if (CanPartySlotParticipate(index) &&
                    member->hp_current < static_cast<unsigned int>(member->uiHPMax) &&
                    (best == -1 ||
                     member->hp_current < g_status.party.characters[best].hp_current)) {
                    best = index;
                }
            }
            if (best != -1) {
                ResetCombatSlot(&target);
                target.iType = W8_TARGET_KIND_CHARACTER;
                target.iChar = best;
                AppendToLastTextLine(g_localized_strings[0x179], -1);
                CastSpellFromSource(W8_SPELL_HEAL_WOUNDS, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0, 0);
                prayed = true;
            }
            break;
        case 5:
            found = false;
            for (index = 0; index < 6; ++index) {
                if (g_combat.state()->effect_slots0[index].active &&
                    g_combat.state()->effect_slots0[index].effect_id == 2) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                AppendToLastTextLine(g_localized_strings[0x17b], -1);
                CastSpellFromSource(W8_SPELL_BLESS, &source, &target, power_level, 0, 0, false,
                                    &outcome, W8SpellCastKind::Standard, 0, 0);
                prayed = true;
            }
            break;
        case 6: {
            const float range =
                CalcRangeDistance(g_spell_records[W8_SPELL_MAGIC_MISSILES].range_category);
            in_range = 0;
            for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                 ++index) {
                W8MonsterInfo* monster = GetMonsterListEntry(index);
                if (monster->fActive && monster->pCombat &&
                    monster->ubDisposition == W8_DISPOSITION_HOSTILE && monster->hp_current != 0 &&
                    monster->p3D->GetDistanceToPlayer() <= range) {
                    ++in_range;
                }
            }
            if (in_range != 0) {
                pick = Random(in_range) + 1;
                for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                     ++index) {
                    W8MonsterInfo* monster = GetMonsterListEntry(index);
                    if (monster->fActive && monster->pCombat &&
                        monster->ubDisposition == W8_DISPOSITION_HOSTILE &&
                        monster->hp_current != 0 && monster->p3D->GetDistanceToPlayer() <= range &&
                        --pick == 0) {
                        AppendToLastTextLine(g_localized_strings[0x179], -1);
                        target.iType = W8_TARGET_KIND_MONSTER;
                        target.iMonsterID = monster->location_id;
                        if (power_level > 6) {
                            power_level = 7;
                        }
                        monster_targets.push_back(monster->location_id);
                        break;
                    }
                }
                CastSpellFromSource(W8_SPELL_MAGIC_MISSILES, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0,
                                    &monster_targets);
                prayed = true;
            }
            break;
        }
        case 7:
            prayed = TurnUndead(party_slot, 0, false) > 0;
            break;
        case 8:
            AppendToLastTextLine(g_localized_strings[0x179], -1);
            for (index = 0; index < 0x12; ++index) {
                RemoveConditionFromParty(static_cast<W8Condition>(index));
            }
            if (g_combat.state() != 0) {
                for (index = 0; index < 9; ++index) {
                    if (g_combat.state()->effect_slots[index].active) {
                        ResetPartyEffectBlock(&g_combat.state()->effect_slots[index]);
                    }
                }
            }
            prayed = true;
            break;
        case 9:
            if (!CombatHasCondition(0x3b) || !CombatHasCondition(0x35)) {
                target.iType = W8_TARGET_KIND_PARTY;
                target.iChar = -1;
                AppendToLastTextLine(g_localized_strings[0x17b], -1);
                CastSpellFromSource(W8_SPELL_SOUL_SHIELD, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0, 0);
                CastSpellFromSource(W8_SPELL_ELEMENT_SHIELD, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0, 0);
                prayed = true;
            }
            break;
        case 10:
            if (!PartyHasCondition(0x28) || !PartyHasCondition(0x14) || !PartyHasCondition(0x20) ||
                !PartyHasCondition(0x1a)) {
                target.iType = W8_TARGET_KIND_PARTY;
                target.iChar = -1;
                AppendToLastTextLine(g_localized_strings[0x17b], -1);
                CastSpellFromSource(W8_SPELL_MAGIC_SCREEN, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0, 0);
                CastSpellFromSource(W8_SPELL_ENCHANTED_BLADE, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0, 0);
                CastSpellFromSource(W8_SPELL_ARMORPLATE, &source, &target, power_level, 0, 0, false,
                                    &outcome, W8SpellCastKind::Standard, 0, 0);
                CastSpellFromSource(W8_SPELL_MISSILE_SHIELD, &source, &target, power_level, 0, 0,
                                    false, &outcome, W8SpellCastKind::Standard, 0, 0);
                prayed = true;
            }
            break;
        case 0xb:
            for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                 ++index) {
                W8MonsterInfo* monster = GetMonsterListEntry(index);
                if (monster->fActive && monster->pCombat &&
                    monster->ubDisposition == W8_DISPOSITION_HOSTILE && monster->hp_current != 0 &&
                    monster->uiCondition[W8_CONDITION_AFRAID] == 0) {
                    AppendToLastTextLine(
                        fmt::sprintf(
                            g_localized_strings[0x17c],
                            g_localized_strings[g_gender_name_message_rows[character->gender][2]],
                            -1),
                        -1);
                    target.iType = W8_TARGET_KIND_ALL_ENEMIES;
                    CastSpellFromSource(W8_SPELL_ROUT, &source, &target, power_level, 0, 0, false,
                                        &outcome, W8SpellCastKind::Standard, 0, 0);
                    prayed = true;
                    break;
                }
            }
            break;
        case 0xc:
            power_level >>= 1;
            {
                const float range =
                    CalcRangeDistance(g_spell_records[W8_SPELL_FALLING_STARS].range_category);
                for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                     ++index) {
                    W8MonsterInfo* monster = GetMonsterListEntry(index);
                    if (monster->fActive && monster->pCombat &&
                        monster->ubDisposition == W8_DISPOSITION_HOSTILE &&
                        monster->hp_current != 0 && monster->p3D->GetDistanceToPlayer() <= range) {
                        AppendToLastTextLine(g_localized_strings[0x179], -1);
                        ResetCombatSlot(&target);
                        CastSpellFromSource(W8_SPELL_FALLING_STARS, &source, &target, power_level,
                                            0, 0, false, &outcome, W8SpellCastKind::Standard, 0, 0);
                        break;
                    }
                }
            }
            break;
        default:
            AppendToLastTextLine(g_localized_strings[0x176], -1);
            prayed = true;
            break;
        }
    }
    g_combat.state()->characters[party_slot].pray_used = true;
    return cost;
}

void AlertSameFactionGroups(W8MonsterGroup* monster_group)
{
    W8MonsterRecord* record = MonsterGroupGetRecord(monster_group);
    if (record->faction_id != W8_FACTION_UNALIGNED) {
        for (unsigned int index = 0; index < CurrentLevelPopulation().monster_groups.size();
             ++index) {
            W8MonsterGroup* other = CurrentLevelPopulation().monster_groups[index].get();
            W8MonsterRecord* other_record = MonsterGroupGetRecord(other);
            if (other != monster_group &&
                ((other_record->flags & W8_MONSTER_FLAG_NPC) == 0 || !other->forced_neutral) &&
                record->faction_id == other_record->faction_id &&
                MonsterGroupCanSeeGroup(other, monster_group)) {
                SetMonsterGroupHostility(other, monster_group->ubDisposition, false);
            }
        }
    }
}
