#pragma once

#include <string_view>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/monster_spell_icons.h"

class Monster;
struct World;

void ReleaseSpellDatabase(void);
unsigned char InitializeSpellDatabase(void);
W8SpellTargetType GetSpellTargetType(int spell_id, bool normalize_single_target);
bool IsCombatEffectSlotSpell(int spell_id);
int MinimumCasterLevelForSpellLevel(int spell_level);
int GetMinimumCasterLevelForSpell(int spell_id);
bool CanSpellBackfire(int spell_id);
void ClearMonsterSpellIcons(Monster* monster); /* 0x004ACF90 */
void AddMonsterSpellIcon(World& world, Monster* monster, W8MonsterSpellIconId icon);
void SetMonsterSpellIcon(Monster* monster, W8MonsterSpellIconId icon, bool add); /* 0x004ACD80 */
