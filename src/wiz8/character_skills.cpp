#include "wiz8/character_skills.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "local_code/ConditionsAndEnchantments.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/message_box.h"
#include "wiz8/party_stats.h"
#include "wiz8/platform/random.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/screen.h"
#include "wiz8/unicode.h"
#include "wiz8/xstatus.h"

/* A skill-increase notice waits in a slot's skill_notice_pending flags. */
static bool g_deferred_skill_notices;

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

unsigned int GetCharacterSkillNoticeValue(W8Character* character, W8Skill skill_id)
{
    unsigned int value = character->skills[skill_id].points;
    if (skill_id == g_profession_bonus_skills[character->iProfession]) {
        value = value * 125 / 100;
    }
    return value;
}

void PostSkillIncreaseNotices(const W8SkillNoticePayload* notices)
{
    for (int index = 0; index < notices->count; ++index) {
        int slot = notices->party_slots[index];
        W8Skill skill = static_cast<W8Skill>(notices->skills[index]);
        W8Character* character = &g_status.party.characters[slot];
        unsigned int value = GetCharacterSkillNoticeValue(character, skill);
        PostCharacterNotice(
            slot, fmt::sprintf(g_localized_strings[0x1d9],
                               g_localized_strings[g_character_skill_name_ids[skill]], value));
    }
}

std::string FormatSkillIncreaseNoticeText(int party_slot, bool continue_line, W8Skill skill_id)
{
    auto* character = &g_status.party.characters[party_slot];
    std::string text = continue_line ? " \n  " : " ";
    wiz8::text::append(text, 0xb4);
    wiz8::text::append(text, GetPartyOrderTextColor(static_cast<signed char>(
                                 g_status.party.rows[party_slot].party_order_index)));
    text += " " + fmt::format("{}'s", character->name) + " ";
    wiz8::text::append(text, 0xb5);
    text += " " + fmt::sprintf(g_localized_strings[0x1db],
                               g_localized_strings[g_character_skill_name_ids[skill_id]],
                               GetCharacterSkillNoticeValue(character, skill_id));
    return text;
}

void FlushDeferredSkillNotices(void)
{
    int count;
    bool have_line;
    std::string text;
    W8SkillNoticePayload extra{};
    int slot;
    unsigned int skill_id;

    count = 0;
    have_line = false;
    if (!g_deferred_skill_notices) {
        return;
    }
    text.clear();
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &g_status.party.characters[slot];
        if (!g_status.party.rows[slot].fOccupied || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_DEAD) {
            continue;
        }
        for (skill_id = 0; skill_id < W8_SKILL_COUNT; ++skill_id) {
            if (!gXStatus.monster_manager_entries[slot].skill_notice_pending[skill_id]) {
                continue;
            }
            extra.party_slots[count] = static_cast<signed char>(slot);
            extra.skills[count] = static_cast<signed char>(skill_id);
            ++count;
            text += FormatSkillIncreaseNoticeText(slot, have_line, static_cast<W8Skill>(skill_id));
            have_line = true;
            if (count == 8) {
                extra.count = 8;
                g_npc_dialogue.QueuePortraitMessage(std::move(text), extra);
                count = 0;
                have_line = false;
                text.clear();
                extra = {};
            }
        }
    }
    if (have_line) {
        extra.count = static_cast<signed char>(count);
        g_npc_dialogue.QueuePortraitMessage(std::move(text), extra);
    }
    for (slot = 0; slot < 8; ++slot) {
        memset(&gXStatus.monster_manager_entries[slot].skill_notice_pending[0], 0, W8_SKILL_COUNT);
    }
    g_deferred_skill_notices = false;
}

void InvalidateAndRecalculateCharacterClassData(W8Character* character)
{
    character->portrait_index = -1;
    character->unknown_007d = -1;
    character->personality = -1;
    DeriveCharacterPersonality(character);
    CalcCharacterTableValue(character);
}

/* Whether the character has the trait through profession, race, or - for
   trait 0x1c - a set second enchantment slot. Profession id -1 and race id
   -1 both mean absent and skip their tables. Read-only, so callers agree
   on a const character. */
bool CharacterHasTrait(const W8Character* character, W8Trait trait)
{
    unsigned int index;

    if (character == 0) {
        return false;
    }
    if (character->iProfession != W8_PROFESSION_NONE) {
        const W8Trait* abilities = g_profession_abilities[character->iProfession].ability_ids;
        for (index = 0; index < 3; ++index) {
            if (abilities[index] == trait) {
                return true;
            }
        }
    }
    if (character->iRace != W8_RACE_NONE) {
        const W8Trait* abilities = g_race_abilities[character->iRace].ability_ids;
        for (index = 0; index < 5; ++index) {
            if (abilities[index] == trait) {
                return true;
            }
        }
    }
    if (trait == W8_TRAIT_BREATHE &&
        character->enchantments[W8_ENCHANTMENT_DRACON_BREATH].turns != 0) {
        return true;
    }
    return false;
}

static float ScaleValueByLevel(unsigned int level, float base)
{
    if (level > 0x14) {
        return base;
    }
    float scaled = (level * 2.0f + 60.0f) * base;
    return scaled * kMovementSpeedStep;
}

/* Scale a trait's flat value by the character's level in their current
   profession: full value above twenty levels, sixty percent at zero and two
   percent per level in between. The trait id is carried by the call but the
   body never reads it. */
float ScaleValueByProfessionLevel(W8Character* character, W8Trait, float base)
{
    return ScaleValueByLevel(character->profession_levels[character->iProfession], base);
}

float ScaleValueByMonsterLevel(W8MonsterRecord* record, int, float base)
{
    return ScaleValueByLevel(record->effective_level, base);
}

/* The Valkyrie cheat-death trait fires instead of death while the character
   is not already deep into unconsciousness: a notice, an unconscious stint
   shortened by the profession-level scale, and hit points rolled back up -
   from a scaled share of the maximum once per combat, a scaled share of the
   current pool every other time. */
void CheatDeathRevive(int party_slot)
{
    W8Character* character = &g_status.party.characters[party_slot];

    PostCharacterNotice(party_slot, g_localized_strings[0x173]);
    SetCharacterCondition(
        party_slot, W8_CONDITION_UNCONSCIOUS,
        character->uiCondition[W8_CONDITION_UNCONSCIOUS] -
            static_cast<int>(ScaleValueByProfessionLevel(character, W8_TRAIT_CHEAT_DEATH, 5.0f)) +
            7,
        0, 0, 1);
    if (g_combat.state() != 0 && !g_combat.state()->characters[party_slot].cheat_death_used) {
        character->hp_current = (Random(static_cast<unsigned int>(ScaleValueByProfessionLevel(
                                     character, W8_TRAIT_CHEAT_DEATH,
                                     static_cast<unsigned int>(character->uiHPMax) * 0.5f))) +
                                 0x32) *
                                character->uiHPMax / 100;
        g_combat.state()->characters[party_slot].cheat_death_used = true;
    } else {
        character->hp_current = Random(static_cast<unsigned int>(ScaleValueByProfessionLevel(
                                    character, W8_TRAIT_CHEAT_DEATH, character->hp_current))) +
                                1;
    }
}

/* Reveal the target's item bindings with a strength banded by the party
   slot's level in its current profession: level/4 + 1, zero percent. */
int RevealCharacterItemBindingsByProfession(int party_slot, unsigned int target_slot)
{
    W8Character* character = &g_status.party.characters[party_slot];
    return RevealCharacterItemBindings(
        target_slot, (character->profession_levels[character->iProfession] >> 2) + 1, 0);
}

/* Alchemist auto-brew candidates as {item_id, minimum alchemist profession
   level, maximum level} triples; -1 in the maximum opens the top end and the
   {-1,0,0} row terminates the walk. */
static int g_alchemist_brew_recipes[][3] = {
    {335, 1, 2},   {346, 2, 4},   {336, 3, 5},   {264, 4, -1},  {345, 5, 9},   {337, 6, 12},
    {317, 6, 12},  {269, 3, 8},   {334, 6, 9},   {347, 6, 12},  {341, 10, 13}, {342, 11, 14},
    {348, 13, 15}, {339, 17, -1}, {429, 18, -1}, {598, 5, 8},   {46, 6, 9},    {47, 7, 10},
    {69, 10, 12},  {338, 11, 13}, {260, 13, 14}, {359, 2, 4},   {358, 3, 6},   {360, 4, 8},
    {364, 12, 13}, {361, 14, -1}, {363, 15, -1}, {349, 4, 7},   {353, 7, 10},  {350, 8, 11},
    {357, 8, 11},  {354, 12, 14}, {362, 12, 13}, {355, 14, 15}, {356, 16, -1}, {343, 10, 13},
    {258, 12, 15}, {-1, 0, 0},
};

/* Brew one potion for a conscious alchemist whose cooldown has elapsed. The
   table bands each potion by alchemist profession level (-1 opens the top
   end); a random in-band entry is created, announced and stowed, then the
   cooldown is re-armed. */
void BrewAlchemistPotion(W8Character* character)
{
    W8ItemInstance item;
    unsigned int slot = CharacterPointerToPartySlot(character);
    unsigned int made = 0;
    int alchemy;
    const int* recipe;
    unsigned int recipes;
    unsigned int pick;

    if (g_status.party.rows[slot].fOccupied &&
        character->highest_condition < W8_CONDITION_UNCONSCIOUS &&
        CharacterHasTrait(character, W8_TRAIT_MAKE_POTIONS)) {
        alchemy = character->profession_levels[W8_PROFESSION_ALCHEMIST];
        recipes = 0;
        for (recipe = &g_alchemist_brew_recipes[0][2]; recipe[-2] != -1; recipe += 3) {
            if (recipe[-1] <= alchemy && (recipe[0] == -1 || alchemy <= recipe[0])) {
                ++recipes;
            }
        }
        pick = Random(recipes) + 1;
        for (recipe = &g_alchemist_brew_recipes[0][2];
             alchemy < recipe[-1] || (recipe[0] != -1 && recipe[0] < alchemy) || --pick != 0;
             recipe += 3) {
        }
        item = MakeItem(recipe[-2], false, true, true);
        if (item.stack_count > 1) {
            item.stack_count = 1;
        }
        ShowNotice(slot, fmt::sprintf(g_localized_strings[0x183], character->name,
                                      FormatItemDisplayName(&item, true)));
        StoreItemWithCharacterOrParty(character, &item, false, false, false);
        made = 1;
    }
    if (made != 0) {
        character->potion_brew_cooldown = 0x168;
    }
}

/* Skill ids fall into three bands. Below 0x18 and at 0x1c..0x21 they are
   ordinary skills resolved against the profession; 0x18..0x1b are the magic
   realms, gated by the profession's magic-level offset; 0x22..0x28 index the
   attribute records instead, and count as available only once the attribute has
   reached its cap. */
bool IsCharacterSkillAvailable(W8Character* character, W8Skill skill_id,
                               const bool* expert_realm_flags)
{
    W8Profession profession;
    unsigned int index;
    int magic_offset;

    if (g_profession_skill_availability[skill_id][character->iProfession] == 0) {
        return false;
    }
    if (CharacterHasTrait(character, W8_TRAIT_CANNOT_LEARN)) {
        if (static_cast<unsigned int>(skill_id) >= W8_SKILL_SPELLBOOK_WIZARDRY &&
            static_cast<unsigned int>(skill_id) <= W8_SKILL_SPELLBOOK_PSIONICS) {
            return false;
        }
        if (static_cast<unsigned int>(skill_id) >= W8_SKILL_FIRE_MAGIC &&
            static_cast<unsigned int>(skill_id) <= W8_SKILL_DIVINE_MAGIC) {
            return false;
        }
    }
    if (static_cast<unsigned int>(skill_id) >= W8_SKILL_FIRE_MAGIC &&
        static_cast<unsigned int>(skill_id) <= W8_SKILL_DIVINE_MAGIC) {
        for (index = 0x18; index <= 0x1b; ++index) {
            if (character->skills[index].active) {
                break;
            }
        }
        if (index > 0x1b) {
            return false;
        }
        if (character->skill_unlocks[skill_id] > 0) {
            return true;
        }
        if (expert_realm_flags && expert_realm_flags[skill_id - 0x1c]) {
            return true;
        }
        return false;
    }

    profession = character->iProfession;
    if (skill_id != g_profession_bonus_skills[profession]) {
        for (index = 0; index < 4; ++index) {
            if (skill_id == g_profession_skills[profession][index]) {
                return true;
            }
        }
        if (static_cast<unsigned int>(skill_id) >= W8_SKILL_POWER_STRIKE &&
            static_cast<unsigned int>(skill_id) <= W8_SKILL_EAGLE_EYE) {
            return character->attributes[skill_id - 0x22].base >= 100;
        }
        switch (skill_id) {
        case W8_SKILL_SPELLBOOK_WIZARDRY:
        case W8_SKILL_SPELLBOOK_DIVINITY:
        case W8_SKILL_SPELLBOOK_ALCHEMY:
        case W8_SKILL_SPELLBOOK_PSIONICS:
            magic_offset = g_profession_magic_level_offsets[profession];
            if (magic_offset < 0 && magic_offset > -0xff) {
                return character->profession_levels[profession] + magic_offset > 0;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

/* Rebuild each attribute's effective value from its base and the modifier
   block's seven adjustment bytes, clamped to one through 125, and then every
   skill's base level from the attribute pair g_skill_attributes names. The
   complete values are available before equipment requirements are checked. */
// FUNCTION: WIZ8 0x005539e0
static void ResetCharacterAttributes(W8Character* character)
{
    unsigned int index;

    for (index = 0; index < 7; ++index) {
        int value =
            character->bonus.attribute_adjustments[index] + character->attributes[index].base;
        value = std::clamp(value, 1, 0x7d);
        character->attributes[index].effective = value;
    }
    InitializeSkillBaseLevels(character);
}

static int GetEffectiveSkillLevel(const W8Character* character, W8Skill skill_id)
{
    int level = character->skills[skill_id].points;
    if (skill_id == g_profession_bonus_skills[character->iProfession]) {
        unsigned int bonus = static_cast<unsigned int>(level * 0x19) / 100;
        if (bonus == 0) {
            bonus = 1;
        }
        level += bonus;
    }
    level += character->bonus.skill_bonus[skill_id];
    level = std::clamp(level, 0, 0x7d);
    return level;
}

/* Rebuild every skill level from the value already spent on it, the
   profession's bonus skill and the race and profession adjustment bytes the
   modifier block carries, clamped to zero through 125. Equipment requirements are checked after all skills have been evaluated. */
// FUNCTION: WIZ8 0x00553a60
static void ResetCharacterSkills(W8Character* character)
{
    unsigned int index;

    for (index = 0; index < 0x29; ++index) {
        character->skills[index].level =
            GetEffectiveSkillLevel(character, static_cast<W8Skill>(index));
    }
}

void EvaluateCharacterInputs(W8Character& character, const W8GameplayModifierBlock* party_modifiers)
{
    character.equipment_bonus = {};
    AccumulateEquipmentModifiers(&character, &character.equipment_bonus);
    RebuildCharacterModifierBlock(&character, party_modifiers);
    ResetCharacterAttributes(&character);
    ResetCharacterSkills(&character);
    SetHandType(&character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    SetHandType(&character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
}

bool IsCharacterDefined(const W8Character& character)
{
    return character.iProfession < W8_PROFESSION_COUNT && character.iRace < W8_RACE_COUNT &&
           character.gender != W8_GENDER_UNSET;
}

/* An attribute's base value changed: flip the at-maximum flag on the
   attribute's pseudo-skill entry (id = attribute + 0x22), resetting or
   refunding its row while the character screen is up and announcing the cap
   in the main game, rebuild the effective value from the modifier block's
   adjustment clamped to 1..125, and refresh the equipment and derived state.
   Retail inlines InitializeSkillBaseLevels at the tail. */
void ApplyAttributeChange(W8Character* character, W8Attribute attribute)
{
    int skill_id = attribute + 0x22;

    if (character->attributes[attribute].base >= 0x64) {
        if (!character->skills[skill_id].active) {
            character->skills[skill_id].active = true;
            if (CurrentScreenState().id == W8_SCREEN_CHARACTER) {
                ResetCharacterScreenSkill(static_cast<W8Skill>(skill_id));
            }
            if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
                ShowMainGameNoticeLine(
                    fmt::sprintf(g_localized_strings[0x1d6],
                                 g_localized_strings[g_character_description_first_ids[attribute]],
                                 character->name,
                                 g_localized_strings[g_character_skill_name_ids[skill_id]]),
                    0, true, false);
            }
        }
    } else {
        if (character->skills[skill_id].active) {
            character->skills[skill_id].active = false;
            if (CurrentScreenState().id == W8_SCREEN_CHARACTER) {
                RefundCharacterScreenSkill(static_cast<W8Skill>(skill_id));
            }
        }
    }
    int effective = character->bonus.attribute_adjustments[attribute] +
                    static_cast<int>(character->attributes[attribute].base);
    effective = std::clamp(effective, 1, 0x7d);
    character->attributes[attribute].effective = effective;
    RecalculateCharacterDerivedStats(*character);
    InitializeSkillBaseLevels(character);
}

/* A skill's invested value changed: re-scan the availability flags, rebuild
   the skill's level from it - with the profession-primary bonus and the
   modifier block's per-skill adjustment, clamped to 0..125 - and refresh the
   equipment and derived state. The single-skill half of
   ResetCharacterSkills. */
void ApplySkillChange(W8Character* character, W8Skill skill_id)
{
    RefreshCharacterSkillAvailability(character);

    character->skills[skill_id].level = GetEffectiveSkillLevel(character, skill_id);
    RecalculateCharacterDerivedStats(*character);
}

/* Average the two attribute values g_skill_attributes names for every skill
   into the skill's 0x0a base level. 0x00557D80 and 0x00557B20 seed the
   profession skill levels from these. */
void InitializeSkillBaseLevels(W8Character* character)
{
    for (int index = 0; index < 0x29; ++index) {
        W8Attribute first = g_skill_attributes[index].attribute_1;
        W8Attribute second = g_skill_attributes[index].attribute_2;
        character->skills[index].base_level =
            (character->attributes[first].base + character->attributes[second].base) >> 1;
    }
}

/* Re-scan every skill's availability and mirror each flag change onto the
   open character screen's page 2. The realm flags array marks which expert
   skills gained a spell since the last scan. */
void RefreshCharacterSkillAvailability(W8Character* character)
{
    bool expert_realm_flags[8];
    int index;

    for (index = 0; index < 8; ++index) {
        expert_realm_flags[index] = false;
    }
    for (index = 0; index < 0x72; ++index) {
        if (character->spell_learned[index + 1] == -1 || character->spell_learned[index + 1] == 2) {
            expert_realm_flags[g_spell_records[index].realm] = true;
        }
    }
    for (index = 0; index < 0x29; ++index) {
        bool available =
            IsCharacterSkillAvailable(character, static_cast<W8Skill>(index), expert_realm_flags);
        if (!available) {
            if (character->skills[index].active) {
                character->skills[index].active = false;
                if (CurrentScreenState().id == W8_SCREEN_CHARACTER) {
                    RefundCharacterScreenSkill(static_cast<W8Skill>(index));
                }
            }
        } else if (!character->skills[index].active) {
            character->skills[index].active = true;
            if (CurrentScreenState().id == W8_SCREEN_CHARACTER) {
                ResetCharacterScreenSkill(static_cast<W8Skill>(index));
            }
        }
    }
}

/* A quarter of the skill's current value, never below one. The bonus skill's
   level gets this added after the profession assignment. */
unsigned int GetSkillQuarterValue(W8Character* character, W8Skill skill_id)
{
    unsigned int value = (character->skills[skill_id].points * 0x19) / 100;
    if (value == 0) {
        value = 1;
    }
    return value;
}

/* Practice one skill: mark it practiced, unlock it if the character newly
   qualifies, then roll every usage point against the value-scaled chance -
   (100 - value) * base_level / 100, halved again for magic-realm skills -
   and bank a level on every eighth success. A leveled skill refreshes its
   effective level through the same profession-bonus path ApplySkillChange
   uses, then reports immediately on the idle main-game screen or defers the
   notice into the portrait slot's per-skill flag array. */
void PracticeCharacterSkill(W8Character* character, W8Skill skill_id, int usage_points,
                            bool suppress_notification)
{
    bool improved;
    unsigned int slot;
    unsigned int threshold;

    improved = false;
    if (usage_points != 0 &&
        g_profession_skill_availability[skill_id][character->iProfession] != 0) {
        W8CharacterSkill* skill = &character->skills[skill_id];
        skill->available = true;
        if (!skill->active) {
            if (!IsCharacterSkillAvailable(character, skill_id, nullptr)) {
                return;
            }
            skill->active = true;
            if (CurrentScreenState().id == W8_SCREEN_CHARACTER) {
                ResetCharacterScreenSkill(skill_id);
            }
        }
        if (usage_points != 0) {
            do {
                if (skill->points < 100) {
                    threshold = (100 - (skill->points * 100) / 100) * skill->base_level / 100;
                    if (g_skill_attributes[skill_id].category == W8_SKILL_CATEGORY_EXPERT) {
                        threshold /= 2;
                    }
                    if (threshold == 0) {
                        threshold = 1;
                    }
                    if (Random(100) < threshold) {
                        ++skill->practice_count;
                        if (skill->practice_count >= 8) {
                            ++skill->points;
                            skill->practice_count = 0;
                            skill->improved = true;
                            improved = true;
                        }
                    }
                }
            } while (--usage_points != 0);
            if (improved) {
                RefreshCharacterSkillAvailability(character);
                skill->level = GetEffectiveSkillLevel(character, skill_id);
                RecalculateCharacterDerivedStats(*character);
                slot = CharacterPointerToPartySlot(character);
                if (!g_combat.active() && !IsModalOpen() && !gXStatus.item_pick_pending &&
                    CurrentScreenState().id == W8_SCREEN_MAIN_GAME && IsScreenIdle() &&
                    !suppress_notification) {
                    std::string text;
                    text += FormatSkillIncreaseNoticeText(slot, false, skill_id);
                    W8SkillNoticePayload extra{};
                    extra.count = 1;
                    extra.party_slots[0] = static_cast<signed char>(slot);
                    extra.skills[0] = static_cast<signed char>(skill_id);
                    g_npc_dialogue.QueuePortraitMessage(std::move(text), extra);
                    return;
                }
                gXStatus.monster_manager_entries[slot].skill_notice_pending[skill_id] = true;
                g_deferred_skill_notices = true;
            }
        }
    }
}
