#pragma once

#include <cstdint>
#include "input.h"

struct Controls;

/* Parent strip for the top-row spell icons (0x19d,0)-(0x280,0x12). */
extern std::unique_ptr<Controls> g_spell_icon_strip;
/* Combat-effect HUD panels flanking the center strip. */
extern std::unique_ptr<Controls> g_combat_effect_left_panel;
extern std::unique_ptr<Controls> g_combat_effect_right_panel;
/* Live icon counts for the three panels above. */
extern std::uint32_t g_spell_icon_count;
extern std::uint32_t g_combat_effect_left_count;
extern std::uint32_t g_combat_effect_right_count;

/* Allocate the three HUD Controls panels MainGameScreenEnter needs before the
   spell/effect icon rows are filled in. */
std::uint8_t CreateSpellIconHudControls(void);
/* Tear down the icon rows and destroy the three HUD panels. */
void DestroySpellIconHudControls(void);
/* Drop and rebuild the spell-icon rows on the existing top strip. */
void RefreshSpellIconHudRows(void);
/* Rebuild the top-strip spell-icon text controls from active party effects. */
void RebuildSpellIconHudRows(void);
/* Drop the combat-effect icon rows without destroying their parent panels. */
void DestroyCombatEffectHudRows(void);
/* Tear down combat-effect rows, rebuild them in combat, and enable/redraw the
   flanking panels (honoring portrait-refresh pending on slots 0/1). */
void RefreshCombatEffectHud(void);
/* Clear and invalidate the main-game effect strip while it is up. */
void InvalidateMainGameEffectHud(void);

void ShowPartyEffectIconHelp(std::int32_t slot_index);
void ShowCombatLeftEffectIconHelp(std::int32_t slot_index);
void ShowCombatRightEffectIconHelp(std::int32_t slot_index);
std::uint8_t PartyEffectIconRegionEvent(const InputEvent* event, struct W8Region* region);
std::uint8_t CombatLeftEffectIconRegionEvent(const InputEvent* event, struct W8Region* region);
std::uint8_t CombatRightEffectIconRegionEvent(const InputEvent* event, struct W8Region* region);
