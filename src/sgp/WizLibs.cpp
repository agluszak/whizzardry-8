/* Modified for the Wizardry 8 reconstruction: 2026-09-10, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "LibraryDataBase.h"

// GLOBAL: WIZ8 0x006000c8
LibraryInitHeader gGameLibaries[MAX_NUMBER_OF_LIBRARIES] = {{"Data\\Data.slf", 1, 1},
                                       {"Data\\Sound\\Sound.slf", 1, 1},
                                       {"Data\\Sound\\Monsters\\MonsterSound.slf", 1, 1},
                                       {"Data\\Music\\Music.slf", 1, 1},
                                       {"Data\\Monsters\\Monsters.slf", 1, 1},
                                       {"Levels\\Levels.slf", 1, 1}};
