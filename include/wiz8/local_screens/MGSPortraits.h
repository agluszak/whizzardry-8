#pragma once

#include "input.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/targeting.h"
#include "compat/compiler.h"

extern char s_general_magic_sound[];

void ReleasePortraitControls(void);
void ReleaseConditionButtons(void);
/* Create the eight level-up portrait buttons (gpLevelButtons) and their panel. */
void CreateLevelButtons(void);
/* Refresh cached HP/stamina/spell portrait bar widths; dirty + redraw when
   any slot's displayed fraction (or numeric HP) changes. */
short GetCombatPortraitImage(W8ActionKind action, int detail, char status, short slot);
void SyncPartyPortraitVitalsBars(void);
void RecordCharacterDamage(int party_slot, unsigned int amount);
/* Advance per-slot portrait FX counters on a 100ms clock and dirty redraw. */
void TickPartyPortraitFx(void);
/* Clear each occupied slot's damage-splat/effect-icon portrait overlays and
   rearm the shared FX clock; run on the main-game screen leave. */
void ResetPartyPortraitFx(void);
void ToggleNumericHitPoints(void);
void CreateConditionButtons(void);
void DisablePortraitControls(void);
void EnablePortraitAdvanceRegions(void);
void InvalidatePortraitControl(unsigned int party_slot);
/* The DrawMainGameScreen portrait tick: combat slots redraw their strip,
   non-combat slots keep their level-advance buttons in sync, and the
   condition buttons track each slot's highest condition or enchantment. */
void RedrawCombatPortraits(void);
void UpdatePortraitAdvanceButtons(void);
void UpdateConditionButtons(void);
/* Stage the casting icon on a monster-manager entry; the spell's
   realm picks the icon catalog base and the flag picks the dim variant. */
void StageMonsterCastIcon(unsigned int monster_index, W8SpellRealm spell_realm, bool flag, int spell_id);
/* Hide or show the condition-button region set for
   the current layout. SyncMainGameModeRegions picks between them. */
void DisableConditionButtons(void);
void EnableConditionButtons(void);

/* Region callbacks the eight portrait level-up buttons and condition buttons
   share. */

/* Main-game portrait overlay helpers used when a party slot refreshes. */
bool PreparePartyPortraitOverlay(unsigned int party_slot, unsigned int flags, unsigned int top);
void RedrawPartyPortraitOverlay(unsigned int party_slot, bool highlighted, bool overlay_ready, bool slot_enabled);

/* The slot's menu/portrait anchor table - the keyboard-menu
   panel's origin, the portrait band's two x edges, the grid row and the
   column pixel; the adjust flag applies the compact-display shift. */
void GetPartySlotMenuAnchor(int party_slot, int* menu_x, int* menu_y, int* band_menu_edge, int* band_portrait_edge,
                            int* grid_row, int* column_x, int adjust);
void TickPartyPortraitOverlayClocks(void);

/* Shade the depleted tail of a vitals bar - two scanlines tall
   under numeric hit points, three otherwise. */
void ShadeStatusBarGap(int length, int left, int top);

/* Dead-character portrait catalog ids - the small party-strip
   image at [race][0] and the large header portrait at [race][1]. */
extern int g_dead_portrait_catalog_ids[W8_RACE_COUNT][2];
/* Empty-hand portrait catalog ids - right hand at [race*2], left
   hand at [race*2+1]. */
extern int g_empty_hand_catalog_ids[32];
