#include "wiz8/character_skills.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/combat/combat.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/message_box.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/unicode.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

unsigned int GetCharacterSkillNoticeValue(const W8Character* character, W8Skill skill_id)
{
    unsigned int value = character->skills[skill_id].points;
    if (skill_id == g_profession_bonus_skills[character->iProfession]) {
        value = value * 125 / 100;
    }
    return value;
}

void PostSkillIncreaseNotices(Game& game, const W8SkillNoticePayload& notices)
{
    for (const auto& [slot, skill] : notices.entries) {
        W8Character* character = &game.party.characters[slot];
        unsigned int value = GetCharacterSkillNoticeValue(character, skill);
        game.events.push(CharacterNotice{
            slot, fmt::sprintf(game.data.strings[0x1d9],
                               game.data.strings[g_character_skill_name_ids[skill]], value)});
    }
}

/* 0x00554170: append one "race-icon Name's skill +level" clause to a notice
   buffer; when continue_line is set, insert a line break first. */
static std::string FormatSkillIncreaseNoticeText(const Game& game, int party_slot,
                                                 bool continue_line, W8Skill skill_id)
{
    const auto& character = game.party.characters[party_slot];
    std::string text = continue_line ? " \n  " : " ";
    wiz8::text::append(text, 0xb4);
    wiz8::text::append(text, GetPartyOrderTextColor(
                                 static_cast<signed char>(game.party.orderIndexOf(party_slot))));
    text += " " + fmt::format("{}'s", character.name) + " ";
    wiz8::text::append(text, 0xb5);
    text += " " + fmt::sprintf(game.data.strings[0x1db],
                               game.data.strings[g_character_skill_name_ids[skill_id]],
                               GetCharacterSkillNoticeValue(&character, skill_id));
    return text;
}

void AnnounceSkillIncrease(Game& game, unsigned int party_slot, W8Skill skill_id, bool now)
{
    if (!now) {
        game.monster_manager_entries[party_slot].skill_notice_pending[skill_id] = true;
        return;
    }
    game.npc_dialogue.QueuePortraitMessage(
        FormatSkillIncreaseNoticeText(game, static_cast<int>(party_slot), false, skill_id),
        W8SkillNoticePayload{{{party_slot, skill_id}}});
}

void FlushDeferredSkillNotices(Game& game)
{
    std::string text;
    W8SkillNoticePayload notice;
    for (unsigned int slot = 0; slot < 8; ++slot) {
        const auto& character = game.party.characters[slot];
        if (!game.party.members[slot] || character.hp_current == 0 ||
            character.highest_condition >= W8_CONDITION_DEAD) {
            continue;
        }
        for (int skill = 0; skill < W8_SKILL_COUNT; ++skill) {
            if (!game.monster_manager_entries[slot].skill_notice_pending[skill]) {
                continue;
            }
            const auto skill_id = static_cast<W8Skill>(skill);
            text += FormatSkillIncreaseNoticeText(game, slot, !notice.entries.empty(), skill_id);
            notice.entries.push_back({slot, skill_id});
            if (notice.entries.size() == 8) {
                game.npc_dialogue.QueuePortraitMessage(std::move(text), std::move(notice));
                text.clear();
                notice = {};
            }
        }
    }
    if (!notice.entries.empty()) {
        game.npc_dialogue.QueuePortraitMessage(std::move(text), std::move(notice));
    }
    for (auto& entry : game.monster_manager_entries) {
        std::ranges::fill(entry.skill_notice_pending, false);
    }
}

void InvalidateAndRecalculateCharacterClassData(W8Character* character)
{
    character->portrait_index = -1;
    character->unknown_007d = -1;
    character->personality = -1;
    DeriveCharacterPersonality(character);
    CalcCharacterTableValue(character);
}

/* Reveal the target's item bindings with a strength banded by the party
   slot's level in its current profession: level/4 + 1, zero percent. */
int RevealCharacterItemBindingsByProfession(const W8Party& party, int party_slot,
                                            unsigned int target_slot)
{
    const W8Character* character = &party.characters[party_slot];
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
void BrewAlchemistPotion(Game& game, W8Character* character)
{
    W8ItemInstance item;
    unsigned int slot = CharacterPointerToPartySlot(character);
    unsigned int made = 0;
    int alchemy;
    const int* recipe;
    unsigned int recipes;
    unsigned int pick;

    if (game.party.members[slot].has_value() &&
        character->highest_condition < W8_CONDITION_UNCONSCIOUS &&
        CharacterHasTrait(character, W8_TRAIT_MAKE_POTIONS)) {
        alchemy = character->profession_levels[W8_PROFESSION_ALCHEMIST];
        recipes = 0;
        for (recipe = &g_alchemist_brew_recipes[0][2]; recipe[-2] != -1; recipe += 3) {
            if (recipe[-1] <= alchemy && (recipe[0] == -1 || alchemy <= recipe[0])) {
                ++recipes;
            }
        }
        pick = game.rng.below(recipes) + 1;
        for (recipe = &g_alchemist_brew_recipes[0][2];
             alchemy < recipe[-1] || (recipe[0] != -1 && recipe[0] < alchemy) || --pick != 0;
             recipe += 3) {
        }
        item = MakeItem(Data(), game.rng, recipe[-2], false, true, true);
        if (item.stack_count > 1) {
            item.stack_count = 1;
        }
        // Retail shows the notice in the palette numbered like the brewer's slot.
        game.events.push(
            TextNotice{slot, fmt::sprintf(Data().strings[0x183], character->name,
                                          FormatItemDisplayName(Data(), &item, true))});
        StoreItemWithCharacterOrParty(game, PartyMember(game, slot), OwnedItem(item), false, false,
                                      false);
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

/* An attribute's base value changed: flip the at-maximum flag on the
   attribute's pseudo-skill entry (id = attribute + 0x22), resetting or
   refunding its row while the character screen is up and announcing the cap
   in the main game, rebuild the effective value from the modifier block's
   adjustment clamped to 1..125, and refresh the equipment and derived state.
   Retail inlines InitializeSkillBaseLevels at the tail. */
void ApplyAttributeChange(Game& game, W8Character* character, W8Attribute attribute)
{
    const auto skill_id = static_cast<W8Skill>(attribute + 0x22);

    if (character->attributes[attribute].base >= 0x64) {
        if (!character->skills[skill_id].active) {
            character->skills[skill_id].active = true;
            game.events.push(SkillAvailabilityChanged{skill_id, true});
            // Party creation and camp edits leave the announcement to their editors.
            if (game.status.game_started && game.activity.in_world()) {
                game.events.push(NoticeDialog{
                    .text = fmt::sprintf(
                        Data().strings[0x1d6],
                        Data().strings[g_character_description_first_ids[attribute]],
                        character->name, Data().strings[g_character_skill_name_ids[skill_id]])});
            }
        }
    } else {
        if (character->skills[skill_id].active) {
            character->skills[skill_id].active = false;
            game.events.push(SkillAvailabilityChanged{skill_id, false});
        }
    }
    int effective = character->bonus.attribute_adjustments[attribute] +
                    static_cast<int>(character->attributes[attribute].base);
    effective = std::clamp(effective, 1, 0x7d);
    character->attributes[attribute].effective = effective;
    RecalculateCharacterDerivedStats(game, *character);
    InitializeSkillBaseLevels(character);
}

/* A skill's invested value changed: re-scan the availability flags, rebuild
   the skill's level from it - with the profession-primary bonus and the
   modifier block's per-skill adjustment, clamped to 0..125 - and refresh the
   equipment and derived state. The single-skill half of
   ResetCharacterSkills. */
void ApplySkillChange(Game& game, W8Character* character, W8Skill skill_id)
{
    RefreshCharacterSkillAvailability(game.events, character);

    character->skills[skill_id].level = GetEffectiveSkillLevel(character, skill_id);
    RecalculateCharacterDerivedStats(game, *character);
}

/* Re-scan every skill's availability and report each flag change. The realm flags
   array marks which expert skills gained a spell since the last scan. */
void RefreshCharacterSkillAvailability(Events& events, W8Character* character)
{
    bool expert_realm_flags[8];
    int index;

    for (index = 0; index < 8; ++index) {
        expert_realm_flags[index] = false;
    }
    for (index = 0; index < 0x72; ++index) {
        if (character->spell_learned[index + 1] == -1 || character->spell_learned[index + 1] == 2) {
            expert_realm_flags[Data().spells[index].realm] = true;
        }
    }
    for (index = 0; index < 0x29; ++index) {
        bool available =
            IsCharacterSkillAvailable(character, static_cast<W8Skill>(index), expert_realm_flags);
        if (!available) {
            if (character->skills[index].active) {
                character->skills[index].active = false;
                events.push(SkillAvailabilityChanged{static_cast<W8Skill>(index), false});
            }
        } else if (!character->skills[index].active) {
            character->skills[index].active = true;
            events.push(SkillAvailabilityChanged{static_cast<W8Skill>(index), true});
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
   uses, then reports the increase. */
void PracticeCharacterSkill(Game& game, W8Character* character, W8Skill skill_id, int usage_points,
                            bool suppress_notification)
{
    bool improved;
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
            game.events.push(SkillAvailabilityChanged{skill_id, true});
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
                    if (game.rng.below(100) < threshold) {
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
                RefreshCharacterSkillAvailability(game.events, character);
                skill->level = GetEffectiveSkillLevel(character, skill_id);
                RecalculateCharacterDerivedStats(game, *character);
                game.events.push(SkillImproved{CharacterPointerToPartySlot(character), skill_id,
                                               suppress_notification});
            }
        }
    }
}
