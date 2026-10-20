#pragma once

#include <string>

#include "wiz8/data/attack_modes.h"

/* One 0x1e5-byte MissileTables.dbs runtime row. Only the fields reached by
   recovered consumers are named. */
struct W8MissileTableRecord {
    /* The retained database row starts with a UTF-16 name, independently
       visible in all 36 canonical MissileTables.dbs rows. */
    std::string display_name;
    /* Retail offset 0x100: the GrCycle resource name the launcher loads through the
       "Data\\Missiles" script path. */
    std::string cycle_name;
    float radius;
    W8AttackMode attack_mode;
    unsigned char unknown_148[8];
    /* Retail offset 0x150: copied into the launched effect block's magnitude_base. */
    int magnitude_base;
    /* Retail offset 0x154: nonzero marks a spell missile - its hits resolve through
       ResolveSpellMissileHit instead of the physical hit/deflect path. */
    bool spell_missile;
    /* Retail offset 0x155: the percentage chances the missile's hit effect assigns each
       condition; CastSpellFromSource copies them into its effect block. */
    unsigned char condition_chances[W8_ATTACK_EFFECT_COUNT];
    /* Retail offset 0x165: the missile weapon's impact sound class; MakePCHitSound bounds it
       against the 28 material-impact rows. */
    int weapon_sound_class;
    unsigned char unknown_169[0x7c];
};
