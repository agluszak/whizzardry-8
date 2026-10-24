#pragma once

/* The factions. The name table, the runtime score array and the relationship
   matrix keep one row per id. The last two ids name no faction. */
enum W8Faction : signed char {
    W8_FACTION_UNALIGNED = 0,
    W8_FACTION_PARTY = 1,
    W8_FACTION_DARK_SAVANT = 2,
    W8_FACTION_COSMIC_LORDS = 3,
    W8_FACTION_UMPANI = 4,
    W8_FACTION_TRANG = 5,
    W8_FACTION_MOOK = 6,
    W8_FACTION_RATTKIN_COMMON = 7,
    W8_FACTION_RATTKIN_MAFIA = 8,
    W8_FACTION_BROTHERHOOD = 9,
    W8_FACTION_HIGARDI_BANK = 10,
    W8_FACTION_HIGARDI_HLL = 11,
    W8_FACTION_HIGARDI_COMMON = 12,
    W8_FACTION_TRYNNIE = 13,
    W8_FACTION_MAD_MARTEN = 14,
    W8_FACTION_RAPAX_COMMON = 15,
    W8_FACTION_RAPAX_TEMPLAR = 16,
    W8_FACTION_RAPAX_ARMY = 17,
    W8_FACTION_KINGS_ASSASINS = 18,
    W8_FACTION_FILLER_19 = 19,
    W8_FACTION_FILLER_20 = 20,
    W8_FACTION_COUNT = 21,
    /* FindFactionByName answers this for a name no faction carries. */
    W8_FACTION_INVALID = -1
};

enum W8FactionDisposition : unsigned char {
    W8_FACTION_HOSTILE = 0,
    W8_FACTION_NEUTRAL = 1,
    W8_FACTION_FRIENDLY = 2
};
