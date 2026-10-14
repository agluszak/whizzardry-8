#ifndef WIZ8_LAYOUTS_ITEM_TABLES_H
#define WIZ8_LAYOUTS_ITEM_TABLES_H

#include <array>
#include <string>

#include "wiz8/attack_modes.h"
#include "wiz8/dice.h"
#include "wiz8/retail_item_payloads.h"

#pragma pack(push, 1)
struct W8ItemTableEntry {
    short selector;
    unsigned short item_id;
    unsigned char weight;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct W8ItemTableRecord {
    char name[256];
    unsigned int category_id;
    W8ItemTableEntry entries[40];
    unsigned char level_scaled;
    W8Dice item_count_dice;
    unsigned char unknown_1d1[4];
    W8Dice gold_dice;
    unsigned char unknown_1d9[0x18];
};
#pragma pack(pop)

/* One "you need this much of that" entry. CanCharacterUseItem walks two of
   these for attributes and two for skills, stopping at an id of 0xff. */

/* Equipment classes. Labels 1087..1112 name classes 0..25; labels
   1114..1119 name classes 26..31. Stored as a byte. */
enum W8ItemEquipClass {
    W8_ITEM_EQUIP_CLASS_SHORT_WEAPON = 0,
    W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON = 1,
    W8_ITEM_EQUIP_CLASS_THROWN_WEAPON = 2,
    W8_ITEM_EQUIP_CLASS_RANGED_WEAPON = 3,
    W8_ITEM_EQUIP_CLASS_AMMUNITION = 4,
    W8_ITEM_EQUIP_CLASS_SHIELD = 5,
    W8_ITEM_EQUIP_CLASS_TORSO = 6,
    W8_ITEM_EQUIP_CLASS_LEGS = 7,
    W8_ITEM_EQUIP_CLASS_HEAD = 8,
    W8_ITEM_EQUIP_CLASS_HANDS = 9,
    W8_ITEM_EQUIP_CLASS_FEET = 10,
    W8_ITEM_EQUIP_CLASS_MISC = 11,
    W8_ITEM_EQUIP_CLASS_CLOAK = 12,
    W8_ITEM_EQUIP_CLASS_INSTRUMENT = 13,
    W8_ITEM_EQUIP_CLASS_GADGET = 14,
    W8_ITEM_EQUIP_CLASS_MISC_MAGIC = 15,
    W8_ITEM_EQUIP_CLASS_POTION = 16,
    W8_ITEM_EQUIP_CLASS_BOMB = 17,
    W8_ITEM_EQUIP_CLASS_POWDER = 18,
    W8_ITEM_EQUIP_CLASS_SPELLBOOK = 19,
    W8_ITEM_EQUIP_CLASS_SCROLL = 20,
    W8_ITEM_EQUIP_CLASS_FOOD = 21,
    W8_ITEM_EQUIP_CLASS_DRINK = 22,
    W8_ITEM_EQUIP_CLASS_KEY = 23,
    W8_ITEM_EQUIP_CLASS_WRITING = 24,
    W8_ITEM_EQUIP_CLASS_OTHER = 25,
    W8_ITEM_EQUIP_CLASS_DAGGER = 26,
    W8_ITEM_EQUIP_CLASS_LONG_SWORD = 27,
    W8_ITEM_EQUIP_CLASS_BIPENNIS = 28,
    W8_ITEM_EQUIP_CLASS_BATTLE_AXE = 29,
    W8_ITEM_EQUIP_CLASS_FLAIL = 30,
    W8_ITEM_EQUIP_CLASS_MACE = 31,
};

/* Item flag bits, named by labels 1255..1262. Stored as a byte. */
enum W8ItemFlag {
    W8_ITEM_FLAG_AUTO_IDENTIFY = 0x01,
    W8_ITEM_FLAG_NO_DISCARD = 0x02, /* Crucial Item */
    W8_ITEM_FLAG_TWO_HANDED = 0x04,
    W8_ITEM_FLAG_OFF_HAND_ALLOWED = 0x08, /* Secondary Weapon */
    W8_ITEM_FLAG_MERCHANT_CANNOT_SELL = 0x10,
    W8_ITEM_FLAG_CONTAINER = 0x20,
    W8_ITEM_FLAG_MUST_EQUIP_TO_USE = 0x40,
    W8_ITEM_FLAG_NEVER_DEPLETES = 0x80
};

/* Assay quantity labels 1264..1268. Value 5 has no label; the database
   domain is kept open rather than assigning it an inferred meaning. */
enum W8ItemQuantityKind {
    W8_ITEM_QUANTITY_NONE = 0,
    W8_ITEM_QUANTITY_STACK = 1,
    W8_ITEM_QUANTITY_CHARGES = 2,
    W8_ITEM_QUANTITY_USES = 3,
    W8_ITEM_QUANTITY_SHOTS = 4
};

/* Item categories the usability rules distinguish. Three is the spell source
   the magic code already names; six and eight both cast the record's spell but
   read a different profession level to decide whether the caster is strong
   enough for it. */
enum W8ItemCategory {
    W8_ITEM_CATEGORY_SPELL_SOURCE = 3,
    W8_ITEM_CATEGORY_CASTER_ITEM_6 = 6,
    W8_ITEM_CATEGORY_CASTER_ITEM_8 = 8
};

// Runtime ownership is naturally aligned; only the invariant numeric payloads
// retain their format-defined representation. No raw serialization of this type.
struct W8ItemDatabaseRecord : wiz8::retail::ItemProperties, wiz8::retail::ItemEffects {
    std::string display_name;
    std::string internal_name;
    std::string video_object_name;
    std::string sound_name;
};

/* GameplayDatabase.cpp owns the runtime Items.dbs and ItemTables.dbs roots.
   Keeping their declarations beside the record layouts gives item consumers a
   direct owner beside the file-format types they index. */
extern W8ItemDatabaseRecord* g_item_records;
extern W8ItemTableRecord* g_item_tables;
extern std::string* g_item_table_category_names;

#endif
