#include <cstdint>
#include <string_view>
#pragma once

#include "wiz8/attack_modes.h"

std::uint8_t LoadHitSoundDatabase(void);
void ReleaseHitSoundDatabase(void);

struct W8CombatCharacterRow;
struct W8CombatSlot;
struct W8HandAttack;
struct W8MonsterAttack;
class W8Missile;

/* Play one Data\Sound\Combat\<name>.wav; a variant count above
   one appends a random digit onto the caller's writable name buffer, and the
   flag registers the handle on the combat state. */
void PlayCombatSound(std::string_view sound_name, std::uint32_t variant_count, bool store_handle,
                     std::int32_t volume);
/* Shared weapon-class/armour-material impact lookup. */
std::string_view GetMaterialImpactSound(std::int32_t weapon_class, std::int32_t target_material);
/* Play the attacking hand's swing sound; the attack mode is unused. */
void MakePCAttackSound(W8CombatCharacterRow* row, const W8HandAttack* hand_attack, W8AttackMode,
                       bool store_handle, std::int32_t volume);
/* Melee hit sound for a PC's paired weapon striking the target's
   armour at one location. */
void MakePCMeleeHitSound(std::int32_t iChar, const W8HandAttack* hand_attack, W8CombatSlot* target,
                         std::int32_t hit_location, std::int32_t volume);
/* Play the hit sound for a missile striking the target's armour
   at one location. */
void MakePCHitSound(W8Missile* missile, W8CombatSlot* target, std::int32_t hit_location, std::int32_t volume);
/* Melee hit sound for a monster striking the target's armour at
   one location; the selected attack supplies its signed weapon class. */
void MakeMonsterHitSound(const W8MonsterAttack* attack, W8CombatSlot* target, std::int32_t hit_location,
                         std::int32_t volume);
