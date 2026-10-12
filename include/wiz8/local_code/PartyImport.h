#include <cstdint>
#include <string_view>
#ifndef WIZ8_LOCAL_CODE_PARTYIMPORT_H
#define WIZ8_LOCAL_CODE_PARTYIMPORT_H
#include "wiz8/character_skills.h"

struct W8Character;

/* One imported Wizardry 7 item record. */
struct W8Wiz7Item {
    std::int16_t item_number;
    std::int16_t unknown_02[5];
};

/* The imported Wizardry 7 character record. */
struct W8Wiz7Character {
    char name[0x10]; /* ASCII name TitleCaseString reads */
    std::int32_t kill_count;  /* stored verbatim to kill_count */
    std::uint8_t unknown_014[0x10];
    std::int16_t level;  /* positive values import as level 1 */
    std::int16_t deaths; /* stored to death_count minus one */
    std::uint8_t unknown_028[0x18];
    W8Wiz7Item items[2][10]; /* two ten-item lists, 0x78 each */
    std::uint8_t unknown_130[0x40];
    std::uint8_t attributes[8]; /* ConvertAttribute reads [0..5] */
    std::uint8_t skills[0x22];  /* ConvertSkill indexes it by the
                                          mapped skill id (highest 0x21) and
                                          reads the contiguous ranges inside */
    std::uint8_t unknown_19a[0x98];
    /* The shared save tag - every record in one import file carries the
       same byte; its high nibble picks the Wiz7 ending and its low nibble the
       difficulty. */
    std::uint8_t party_tag;
    std::uint8_t unknown_233[4];
    std::uint8_t race;
    std::uint8_t gender;
    std::uint8_t profession; /* Wiz7 class byte */
    std::uint8_t unknown_23a;
    std::uint8_t status; /* 2 or 3 imports as a dead member */
    std::uint8_t unknown_23c[0xc];
};

/* Local Code\Party Import.cpp: the Wizardry 7 character-import conversions. */
void ConvertAttribute(W8Character* character, const W8Wiz7Character* imported);
void GrantStartingSpells(W8Character* character, const W8Wiz7Character* imported);
void ImportEquipment(W8Character* character, const W8Wiz7Character* imported);
std::uint32_t ConvertSkill(W8Skill skill_id, W8Character* character,
                          const W8Wiz7Character* imported);
void ImportWizardry7Character(W8Character* character, W8Wiz7Character* imported);

/* Parse the Wizardry 7 save into the import globals - character
   count, records, ending/difficulty selectors and the 96 flag bits. */
std::uint8_t LoadWizardry7ImportFile(std::string_view path);
/* Load the file, reset the run, seed 2500 gold and convert every
   imported record into a regular party member. 0 ok, 1 load/pool failure,
   2 when the ending selector carries the value three. */
std::uint8_t ImportWizardry7Party(std::string_view path);

extern std::int32_t g_import_character_count;
extern bool g_import_ending_record;
extern std::int32_t g_wiz7_ending; /* Ending selector */
extern std::int32_t g_import_difficulty;
/* The 96 file flags; index 5 doubles as the unsuppress byte and
   index 0xb as the loaded marker the fact seeder reads. */
extern std::uint8_t g_import_flags[0x60];
extern W8Wiz7Character g_imported_characters[6];

#endif
