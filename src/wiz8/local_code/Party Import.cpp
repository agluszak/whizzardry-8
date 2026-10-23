#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "layouts/game_status.h"
#include "layouts/learned_spells.h"
#include "wiz8/character_skills.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/ProfRaceInfoDialog.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/learned_spells.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/PartyImport.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/story.h"
#include "wiz8/utility.h"

enum class W8Wiz7CharacterStatus : unsigned char {
    unknown = 0,
    dead_with_status_2 = 2,
    dead_with_status_3 = 3,
};

/* One imported Wizardry 7 item record. */
struct W8Wiz7Item {
    short item_number;
    short unknown_02[5];
};

/* The imported Wizardry 7 character record. */
struct W8Wiz7Character {
    char name[0x10]; /* ASCII name TitleCaseString reads */
    int kill_count;  /* stored verbatim to kill_count */
    unsigned char unknown_014[0x10];
    short level;  /* positive values import as level 1 */
    short deaths; /* stored to death_count minus one */
    unsigned char unknown_028[0x18];
    W8Wiz7Item items[2][10]; /* two ten-item lists, 0x78 each */
    unsigned char unknown_130[0x40];
    unsigned char attributes[8]; /* ConvertAttribute reads [0..5] */
    unsigned char skills[0x22];  /* ConvertSkill indexes it by the
                                          mapped skill id (highest 0x21) and
                                          reads the contiguous ranges inside */
    unsigned char unknown_19a[0x98];
    /* The shared save tag - every record in one import file carries the
       same byte; its high nibble picks the Wiz7 ending and its low nibble the
       difficulty. */
    unsigned char party_tag;
    unsigned char unknown_233[4];
    unsigned char race;
    unsigned char gender;
    unsigned char profession; /* Wiz7 class byte */
    unsigned char unknown_23a;
    W8Wiz7CharacterStatus status; /* 2 or 3 imports as a dead member */
    unsigned char unknown_23c[0xc];
};

static void ConvertAttribute(W8Character* character, const W8Wiz7Character* imported);
static void GrantStartingSpells(W8Character* character, const W8Wiz7Character* imported);
static void ImportEquipment(W8Character* character, const W8Wiz7Character* imported);
static unsigned int ConvertSkill(W8Skill skill_id, W8Character* character,
                                 const W8Wiz7Character* imported);
static void ImportWizardry7Character(W8Character* character, W8Wiz7Character* imported);

/* Retail Local Code\Party Import.cpp: converts imported Wizardry 7
   characters into the Wizardry 8 layout. */

/* The three bonus attributes each profession grants on top of the imported
   values. */
static int g_profession_primary_attributes[15][3] = {
    {W8_ATTRIBUTE_STRENGTH, W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_VITALITY},
    {W8_ATTRIBUTE_STRENGTH, W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_PIETY},
    {W8_ATTRIBUTE_VITALITY, W8_ATTRIBUTE_STRENGTH, W8_ATTRIBUTE_PIETY},
    {W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_SENSES},
    {W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_SPEED, W8_ATTRIBUTE_INTELLIGENCE},
    {W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_SPEED, W8_ATTRIBUTE_INTELLIGENCE},
    {W8_ATTRIBUTE_SPEED, W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_SENSES},
    {W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_SPEED, W8_ATTRIBUTE_SENSES},
    {W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_SENSES, W8_ATTRIBUTE_INTELLIGENCE},
    {W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_SENSES},
    {W8_ATTRIBUTE_PIETY, W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_VITALITY},
    {W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_PIETY},
    {W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_PIETY},
    {W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_SENSES, W8_ATTRIBUTE_PIETY},
    {W8_ATTRIBUTE_INTELLIGENCE, W8_ATTRIBUTE_DEXTERITY, W8_ATTRIBUTE_PIETY},
};

/* The starting spells LearnSpell grants each profession on import. */
static int g_profession_starting_spells[15][6] = {
    {0, 0, 0, 0, 0, 0},   {6, 2, 0, 0, 0, 0},     {6, 13, 0, 0, 0, 0},  {6, 1, 0, 0, 0, 0},
    {5, 12, 0, 0, 0, 0},  {1, 7, 0, 0, 0, 0},     {6, 10, 0, 0, 0, 0},  {0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0},   {0, 0, 0, 0, 0, 0},     {6, 2, 13, 11, 0, 0}, {6, 1, 7, 12, 0, 0},
    {6, 12, 5, 11, 0, 0}, {10, 12, 11, 14, 0, 0}, {5, 12, 14, 4, 0, 0},
};

/* The database ID is kept because imported items store that identity, not their
   legacy number. */
static std::optional<int> FindItemByLegacyNumber(const GameData& data, short item_number)
{
    for (unsigned int index = 0; index < data.items.size(); ++index) {
        if (data.items[index].legacy_item_number == item_number) return index;
    }
    return std::nullopt;
}

struct ImportedParty {
    std::vector<W8Wiz7Character> characters;
    Story::ImportedOrigin origin;
    bool unsupported_ending = false;
};

/* Load a Wizardry 7 save for import: a 0x34c-byte header (two record-skip
   counts at 0x2cc/0x2ce, the flag bitmask at 0x200), thirty-two skipped
   0x100-byte blocks, the 0x4c-byte party block whose last short is the
   character count and whose first marks the ending selector, five skipped
   sections, then the character records. Every record must carry the previous
   one's tag byte. On success the ending and difficulty nibbles decode from
   the shared tag and only the two imported story flags are retained. */
static std::optional<ImportedParty> LoadWizardry7ImportFile(std::string_view path)
try {
    if (path.empty()) return std::nullopt;
    auto file = wiz8::open_file(wiz8::user_path(path), wiz8::OpenMode::read);
    unsigned char header[0x34c];
    short party_block[0x26];
    ImportedParty result;
    file->read_exact(header, sizeof(header));
    short record_skip;
    short bank_skip;
    memcpy(&record_skip, header + 0x2cc, sizeof(record_skip));
    memcpy(&bank_skip, header + 0x2ce, sizeof(bank_skip));
    if (record_skip < 0 || bank_skip < 0) return std::nullopt;
    file->seek(record_skip * 6 + bank_skip * 8 + 0x20 * 0x100, wiz8::SeekOrigin::current);
    file->read_exact(party_block, sizeof(party_block));
    const int character_count = party_block[0x25];
    if (character_count < 1 || character_count > 6) return std::nullopt;
    file->seek(0x80 + 0x90 + 0x68 + 0x14a + 0x344 + 0x42 + 100, wiz8::SeekOrigin::current);
    result.characters.resize(character_count);
    auto& characters = result.characters;
    file->read_exact(characters.data(), characters.size() * sizeof(characters[0]));
    for (int index = 1; index < character_count; ++index) {
        if (characters[index].party_tag != characters[0].party_tag) return std::nullopt;
    }

    const bool ending_record = party_block[0] == -1;
    int ending = -1;
    if (ending_record) {
        switch (characters[0].party_tag & 0xf0) {
        case 0x10:
            ending = 0;
            break;
        case 0x20:
            ending = 1;
            break;
        case 0x40:
            ending = 2;
            break;
        case 0x80:
            ending = 3;
            break;
        default:
            return std::nullopt;
        }
    }

    if (ending == 1) result.origin.alignment = Story::ImportedOrigin::Alignment::Umpani;
    if (ending == 2) result.origin.alignment = Story::ImportedOrigin::Alignment::Trang;
    result.unsupported_ending = ending == 3;
    result.origin.barlone_dead = (header[0x201] & (1 << 3)) != 0;
    result.origin.rodan_dead = (header[0x200] & (1 << 5)) != 0;
    return result;
} catch (const std::exception&) {
    return std::nullopt;
}

PartyImportResult ImportWizardry7Party(GameSession& games, std::string_view path)
{
    auto imported = LoadWizardry7ImportFile(path);
    if (!imported) return PartyImportResult::Unreadable;
    auto& game = BeginNewGame(games);
    game.party.gold = 2500;
    game.status.skip_loose_character_check = true;
    game.story.imported_origin = imported->origin;
    for (auto& record : imported->characters) {
        W8Character character;
        ImportWizardry7Character(&character, &record);
        if (!AddCharacterToParty(character, -1)) return PartyImportResult::Unreadable;
    }
    // Conversion still precedes rejection, preserving its random draws.
    return imported->unsupported_ending ? PartyImportResult::UnsupportedEnding
                                        : PartyImportResult::Success;
}

static void ImportWizardry7Character(W8Character* character, W8Wiz7Character* imported)
{
    W8Profession profession;
    unsigned int level;
    unsigned int skill_id;

    *character = W8Character{};
    character->name = TitleCaseString(imported->name);
    character->name_part_2 = character->name;
    character->iRace = static_cast<W8Race>(imported->race);
    character->gender = static_cast<W8Gender>(imported->gender);
    switch (imported->profession) {
    default:
        profession = W8_PROFESSION_FIGHTER;
        break;
    case 1:
        profession = W8_PROFESSION_MAGE;
        break;
    case 2:
        profession = W8_PROFESSION_PRIEST;
        break;
    case 3:
        profession = W8_PROFESSION_ROGUE;
        break;
    case 4:
        profession = W8_PROFESSION_RANGER;
        break;
    case 5:
        profession = W8_PROFESSION_ALCHEMIST;
        break;
    case 6:
        profession = W8_PROFESSION_BARD;
        break;
    case 7:
        profession = W8_PROFESSION_PSIONIC;
        break;
    case 8:
        profession = W8_PROFESSION_VALKYRIE;
        break;
    case 9:
        profession = W8_PROFESSION_BISHOP;
        break;
    case 10:
        profession = W8_PROFESSION_LORD;
        break;
    case 11:
        profession = W8_PROFESSION_SAMURAI;
        break;
    case 12:
        profession = W8_PROFESSION_MONK;
        break;
    case 13:
        profession = W8_PROFESSION_NINJA;
        break;
    }
    character->iProfession = profession;
    CalcCharacterTableValue(character);
    level = static_cast<unsigned short>(imported->level);
    if (imported->level > 0) {
        level = 1;
    }
    AdvanceCharacterToLevel(character, level);
    character->experience = 13000;
    character->kill_count = imported->kill_count;
    character->death_count = imported->deaths - 1;
    character->profession_levels[character->iProfession] = character->uiExpLevel;
    character->original_profession = character->iProfession;
    character->level_band_base = 0;
    if (imported->status == W8Wiz7CharacterStatus::dead_with_status_2 ||
        imported->status == W8Wiz7CharacterStatus::dead_with_status_3) {
        character->uiCondition[W8_CONDITION_DEAD] = W8_CONDITION_INDEFINITE;
        character->highest_condition = W8_CONDITION_DEAD;
    } else {
        character->highest_condition = W8_CONDITION_NONE;
    }
    character->enchantment_top = W8_ENCHANTMENT_NONE;
    ConvertAttribute(character, imported);
    GrantStartingSpells(character, imported);
    for (skill_id = 0; skill_id < W8_SKILL_COUNT; ++skill_id) {
        character->skills[skill_id].active = false;
        character->skills[skill_id].points =
            ConvertSkill(static_cast<W8Skill>(skill_id), character, imported);
    }
    RefreshCharacterSkillAvailability(Data(), character);
    ImportEquipment(character, imported);
    DeriveCharacterPersonality(character);
    EnsureUniquePartyVoice(character);
    CalcCharacterLevelBand(character);
    RecalculateCharacterDerivedStats(CurrentGame(), *character);
    character->stamina = character->uiStaminaMax;
    character->hp_current = character->uiHPMax;
    for (skill_id = 0; skill_id < 6; ++skill_id) {
        character->iSPLeft[skill_id] = character->sp_max[skill_id];
    }
}

static void ConvertAttribute(W8Character* character, const W8Wiz7Character* imported)
{
    unsigned int imported_values[7] = {0};
    int i;
    int total = 0;
    int average;
    int points;
    int added;
    int spins;
    unsigned int add;
    unsigned int cap;
    unsigned int pick;
    int removed;

    for (i = 0; i < 6; ++i) {
        imported_values[i] = imported->attributes[i];
        total += imported_values[i];
    }
    average = total / 6;
    for (i = 0; i < 3; ++i) {
        imported_values[g_profession_primary_attributes[character->iProfession][i]] += 0x28;
    }
    for (i = 0; i < 7; ++i) {
        character->attributes[i].base = g_race_attribute_minimums[character->iRace].values[i];
    }
    points = 0;
    for (i = 0; i < 7; ++i) {
        if (character->attributes[i].base <
            static_cast<unsigned int>(
                g_profession_attribute_minimums[character->iProfession].values[i])) {
            int deficit = g_profession_attribute_minimums[character->iProfession].values[i] -
                          character->attributes[i].base;
            character->attributes[i].base += deficit;
            points += deficit;
        }
    }
    points = 0x46 - points;
    if (-1 < points) {
        added = 0;
        for (i = 0; i < 7; ++i) {
            add = (points * imported_values[i]) / (total + average + 0x78);
            cap = 100 - character->attributes[i].base;
            if (cap <= add) {
                add = cap;
            }
            added += add;
            character->attributes[i].base += add;
        }
        points -= added;
        spins = points * 3;
        for (i = 0; i < spins; ++i) {
            CurrentGame().rng.below(7);
            if (character->attributes[i].base < 100) {
                --points;
                spins -= 3;
                character->attributes[i].base += 1;
                if (points == 0) {
                    return;
                }
            }
        }
        return;
    }
    removed = 0;
    if (0 < -points) {
        do {
            pick = CurrentGame().rng.below(7);
            if (g_profession_attribute_minimums[character->iProfession].values[pick] != 0 &&
                static_cast<unsigned int>(
                    g_race_attribute_minimums[character->iRace].values[pick]) <
                    character->attributes[pick].base) {
                ++removed;
                character->attributes[pick].base -= 1;
            }
        } while (removed < -points);
    }
    character->attribute_point_deficit = points;
    character->level_band_base = 1;
}

static void GrantStartingSpells(W8Character* character, const W8Wiz7Character*)
{
    W8LearnedSpellState scratch;
    signed char count;
    int offset;
    int i;

    for (i = 0; i < 0x72; ++i) {
        character->spell_learned[i] = 0;
    }
    offset = g_profession_magic_level_offsets[character->iProfession];
    if (offset < 0 && -0xff < offset) {
        count = 2;
    } else {
        if (offset != 0) {
            return;
        }
        count = 4;
    }
    for (i = 0; i < 6; ++i) {
        LearnSpell(character, g_profession_starting_spells[character->iProfession][i], false);
        --count;
        if (count == 0) {
            break;
        }
    }
    BuildLearnedSpellState(&scratch, character);
}

static void ImportEquipment(W8Character* character, const W8Wiz7Character* imported)
{
    auto& game = CurrentGame();
    const auto& data = Data();
    // Keep one imported item worth 3000–4999, and two worth 1000–2999.
    std::array<std::vector<int>, 2> candidates;
    EmptyAllCarriedItems(game, *character);
    for (int source_slot = 0; source_slot < 2; ++source_slot) {
        for (const auto& source : imported->items[source_slot]) {
            if (source.item_number == 0) continue;
            const auto item_id = FindItemByLegacyNumber(data, source.item_number);
            if (!item_id) continue;
            const auto& record = data.items[*item_id];
            if (*item_id != 0x128) {
                const int value = static_cast<int>(record.value);
                if (record.unidentified_name_index == 0x84 || value > 4999) continue;
                if (value > 999) {
                    // Only positive legacy numbers participate in the valuable-item sweep.
                    if (source.item_number > 0)
                        candidates[value > 2999 ? 0 : 1].push_back(*item_id);
                    continue;
                }
            }
            auto item = MakeItem(data, game.rng, *item_id, true, true, true);
            if (record.binds_on_equip == 0) {
                StoreItemWithCharacterOrParty(game, Outsider(*character), OwnedItem(item), false,
                                              false, source_slot == 0);
            } else {
                AddItemToCharacter(game, Outsider(*character), OwnedItem(item), false, false,
                                   false);
            }
        }
    }
    for (unsigned int band = 0; band < candidates.size(); ++band) {
        auto& pool = candidates[band];
        for (unsigned int remaining = band + 1; remaining != 0 && !pool.empty(); --remaining) {
            auto best = pool.end();
            int best_value = 0;
            for (auto candidate = pool.begin(); candidate != pool.end(); ++candidate) {
                const auto& record = data.items[*candidate];
                int price = record.value;
                if (!ItemHasHiddenProperties(data, *candidate)) price /= 2;
                if (best_value < price) {
                    // The comparison uses discounted price, but its threshold is full value.
                    best_value = record.value;
                    best = candidate;
                }
            }
            if (best == pool.end()) break;
            const int item_id = *best;
            auto item = MakeItem(data, game.rng, item_id, true, true, true);
            if (data.items[item_id].binds_on_equip == 0) {
                StoreItemWithCharacterOrParty(game, Outsider(*character), OwnedItem(item), false,
                                              false, true);
            } else {
                AddItemToCharacter(game, Outsider(*character), OwnedItem(item), false, false,
                                   false);
            }
            pool.erase(best);
        }
    }
    auto profession = W8_PROFESSION_COUNT;
    if (character->iRace != W8_RACE_FAERIE) {
        profession = character->iProfession;
    }
    for (int slot = 0; slot < 6; ++slot) {
        const int item_id = g_starting_equipment[profession][slot];
        if (item_id != -1) {
            auto item = MakeItem(data, game.rng, item_id, true, true, true);
            auto equip_slot = GetItemDefaultEquipSlot(data, item_id, GameStarted());
            if (equip_slot == W8_EQUIP_SLOT_NONE) {
                if (!FindCharacterItem(
                        *character, ItemOfKind(data, data.items[item_id].unidentified_name_index),
                        W8ItemReach::Carried)) {
                    AddItemToCharacter(game, Outsider(*character), OwnedItem(item), true, false,
                                       false);
                }
            } else {
                if (equip_slot == W8_EQUIP_SLOT_PRIMARY_WEAPON &&
                    (data.items[item_id].flags & W8_ITEM_FLAG_OFF_HAND_ALLOWED) != 0) {
                    equip_slot = W8_EQUIP_SLOT_SECONDARY_WEAPON;
                }
                if (character->EquippedItem[equip_slot].iItemNo == -1) {
                    AddItemToCharacter(game, Outsider(*character), OwnedItem(item), true, false,
                                       false);
                }
            }
        }
    }
    std::optional<int> give;
    if (character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1) {
        if (character->iProfession == W8_PROFESSION_FIGHTER) {
            if (character->skills[W8_SKILL_SWORD].level < character->skills[W8_SKILL_AXE].level) {
                give = 0x12;
            } else {
                give = 7;
            }
        } else if (character->iProfession == W8_PROFESSION_PRIEST) {
            if (character->skills[W8_SKILL_MACE_FLAIL].level <=
                character->skills[W8_SKILL_STAFF_WAND].level) {
                give = 0x52;
            } else {
                give = 0x16;
            }
        }
        if (give) {
            auto item = MakeItem(data, game.rng, *give, true, true, true);
            AddItemToCharacter(game, Outsider(*character), OwnedItem(item), true, false, false);
        }
    }
    RecalculateCharacterDerivedStats(game, *character);
}

/* Retail fell through the failed assert and read `imported->skills` by
   whatever `mapped` held; the recovery keeps that read. */
static unsigned int ConvertSkill(W8Skill skill_id, W8Character* character,
                                 const W8Wiz7Character* imported)
{
    int mapped;
    unsigned int unlocks;
    unsigned int roll;
    unsigned int base_value;
    bool routed = false;
    int i;

    if (g_skill_attributes[skill_id].import_policy == W8_SKILL_IMPORT_PROFESSION) {
        if (g_profession_skill_availability[skill_id][character->iProfession] != 1) {
            return 0;
        }
    } else if (g_skill_attributes[skill_id].import_policy == W8_SKILL_IMPORT_DISABLED) {
        return 0;
    }
    switch (skill_id) {
    case W8_SKILL_SWORD:
        mapped = 1;
        break;
    case W8_SKILL_AXE:
        mapped = 2;
        break;
    case W8_SKILL_POLEARM:
    case W8_SKILL_STAFF_WAND:
        mapped = 4;
        break;
    case W8_SKILL_MACE_FLAIL:
        mapped = 3;
        break;
    case W8_SKILL_DAGGER:
        mapped = 0;
        break;
    case W8_SKILL_SHIELD:
        mapped = 8;
        break;
    case W8_SKILL_MODERN_WEAPONS:
        mapped = 0x12;
        break;
    case W8_SKILL_BOW:
        mapped = 7;
        break;
    case W8_SKILL_MARTIAL_ARTS:
        mapped = 0x10;
        break;
    case W8_SKILL_LOCKS_TRAPS:
        mapped = 0x11;
        break;
    case W8_SKILL_STEALTH:
        mapped = 0xd;
        break;
    case W8_SKILL_MUSIC:
        mapped = 0xf;
        break;
    case W8_SKILL_PICKPOCKET:
        mapped = 9;
        break;
    case W8_SKILL_SCOUTING:
        mapped = 0xc;
        break;
    case W8_SKILL_CRITICAL_STRIKE:
        mapped = 0x21;
        break;
    case W8_SKILL_MYTHOLOGY:
        mapped = 0x19;
        break;
    case W8_SKILL_COMMUNICATION:
        mapped = 0x1c;
        break;
    case W8_SKILL_SPELLBOOK_WIZARDRY:
        mapped = 0x20;
        break;
    case W8_SKILL_SPELLBOOK_DIVINITY:
        mapped = 0x1e;
        break;
    case W8_SKILL_SPELLBOOK_ALCHEMY:
        mapped = 0x1d;
        break;
    case W8_SKILL_SPELLBOOK_PSIONICS:
        mapped = 0x1f;
        break;
    case W8_SKILL_THROWING_SLING:
    case W8_SKILL_CLOSE_COMBAT:
    case W8_SKILL_RANGED_COMBAT:
    case W8_SKILL_DUAL_WEAPONS:
    case W8_SKILL_ARTIFACTS:
    case W8_SKILL_ENGINEERING:
    case W8_SKILL_FIRE_MAGIC:
    case W8_SKILL_WATER_MAGIC:
    case W8_SKILL_AIR_MAGIC:
    case W8_SKILL_EARTH_MAGIC:
    case W8_SKILL_MENTAL_MAGIC:
    case W8_SKILL_DIVINE_MAGIC:
        routed = true;
        switch (skill_id) {
        case W8_SKILL_THROWING_SLING:
            base_value = imported->skills[5];
            if (base_value <= imported->skills[6]) {
                base_value = imported->skills[6];
            }
            break;
        case W8_SKILL_CLOSE_COMBAT:
            base_value = imported->skills[0];
            for (i = 1; i < 5; ++i) {
                if (base_value <= imported->skills[i]) {
                    base_value = imported->skills[i];
                }
            }
            for (i = 8; i < 10; ++i) {
                if (base_value <= imported->skills[i]) {
                    base_value = imported->skills[i];
                }
            }
            if (100 < base_value) {
                base_value = 100;
            }
            break;
        case W8_SKILL_RANGED_COMBAT:
            base_value = imported->skills[7];
            if (base_value <= imported->skills[6]) {
                base_value = imported->skills[6];
            }
            if (base_value <= imported->skills[5]) {
                base_value = imported->skills[5];
            }
            if (100 < base_value) {
                base_value = 100;
            }
            break;
        case W8_SKILL_DUAL_WEAPONS:
            if (g_profession_skill_availability[0x12][character->iProfession] != 1) {
                base_value = 0;
                break;
            }
            base_value = imported->skills[0];
            for (i = 1; i < 5; ++i) {
                if (base_value <= imported->skills[i]) {
                    base_value = imported->skills[i];
                }
            }
            if (100 < base_value) {
                base_value = 100;
            }
            if (g_profession_bonus_skills[character->iProfession] != 0x12) {
                for (i = 0; i < 4 && g_profession_skills[character->iProfession][i] != 0x12; ++i) {
                }
                if (i == 4 || 3 < i) {
                    base_value >>= 1;
                }
            }
            break;
        case W8_SKILL_ARTIFACTS:
            base_value =
                (static_cast<unsigned int>(imported->skills[0x1b] + imported->skills[0x18] * 4) *
                 0x14) /
                100;
            break;
        case W8_SKILL_ENGINEERING:
        case W8_SKILL_POWER_CAST:
        case W8_SKILL_IRON_SKIN:
            base_value = 0;
            break;
        case W8_SKILL_FIRE_MAGIC:
        case W8_SKILL_WATER_MAGIC:
        case W8_SKILL_AIR_MAGIC:
        case W8_SKILL_EARTH_MAGIC:
        case W8_SKILL_MENTAL_MAGIC:
        case W8_SKILL_DIVINE_MAGIC:
            unlocks = character->skill_unlocks[skill_id];
            if (unlocks == 0) {
                base_value = 0;
                break;
            }
            if (imported->profession == 5 || imported->profession == '\r' ||
                imported->profession == 4) {
                base_value = imported->skills[0x1d];
            } else {
                base_value = imported->skills[0xe];
            }
            if (unlocks < 3) {
                base_value = ((unlocks + 1) * base_value) / 3;
            }
            if (100 < base_value) {
                base_value = 100;
            }
            break;
        default:
            std::unreachable();
        }
        break;
    default:
        std::unreachable();
    }
    if (!routed) {
        base_value = imported->skills[mapped];
        if (0x17 < static_cast<unsigned int>(skill_id) &&
            static_cast<unsigned int>(skill_id) < W8_SKILL_FIRE_MAGIC) {
            if (character->iProfession == W8_PROFESSION_BISHOP &&
                (skill_id == W8_SKILL_SPELLBOOK_ALCHEMY ||
                 skill_id == W8_SKILL_SPELLBOOK_PSIONICS) &&
                base_value == 0) {
                base_value =
                    (static_cast<unsigned int>(imported->skills[0x20] + imported->skills[0x1e])) /
                    2;
            }
            unlocks = 0;
            for (i = 0x1c; i < 0x22; ++i) {
                unlocks += character->skill_unlocks[i];
            }
            if (unlocks == 0) {
                roll = CurrentGame().rng.below(5);
                base_value /= roll + 5;
            } else if (unlocks < 5) {
                base_value = (unlocks * base_value) / 5;
            }
        }
    }
    unlocks = (base_value * 2) / 10;
    if (0x14 < unlocks) {
        unlocks = 0x14;
    }
    return unlocks;
}
