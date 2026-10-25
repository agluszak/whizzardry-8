#pragma once

#include <string_view>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/records.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/monster_spell_icons.h"

class Monster;
struct World;

W8SpellTargetType GetSpellTargetType(int spell_id, bool normalize_single_target);
bool IsCombatEffectSlotSpell(int spell_id);
int GetMinimumCasterLevelForSpell(int spell_id);
bool CanSpellBackfire(int spell_id);
