#pragma once

#include <bitset>

#include "wiz8/combat/combat.h"
#include "wiz8/data/records.h"
#include "wiz8/design_rect.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/targeting.h"

extern char s_general_magic_sound[];

/* What a portrait band repaint painted over, so the widgets drawn on top of the
   band are painted again in the same draw. */
struct PortraitBandPaint {
    // The complete band, under the condition icon.
    bool band = false;
    // The combat side-strip portrait's place: the band or a damage splat.
    bool side_strip = false;
};

void ReleasePortraitControls(void);
void ReleaseConditionButtons(void);
/* Create the eight level-up portrait buttons (gpLevelButtons) and their panel. */
void CreateLevelButtons(void);
/* Refresh cached HP/stamina/spell portrait bar widths; dirty + redraw when
   any slot's displayed fraction (or numeric HP) changes. */
short GetCombatPortraitImage(W8ActionKind action, int detail, W8CombatPortraitStatus status,
                             short slot);
void SyncPartyPortraitVitalsBars(void);
void RedrawPartyPortraitBars(unsigned int party_slot, bool slot_enabled);
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
/* The portrait controls exist while the main game is entered (not suspended). */
bool PortraitControlsExist(void);
void InvalidatePortraitControl(unsigned int party_slot);
/* The presentation update keeps the level-advance buttons (outside combat) and
   the condition buttons (each slot's highest condition or enchantment) in sync;
   a condition-icon change requests its repaint. */
void UpdatePortraitAdvanceButtons(void);
void UpdateConditionButtons(void);
/* The draw paints the widgets over the portrait bands: the given combat
   side-strip portraits, the level-advance buttons (forced over repainted
   bands), and the condition icons that were requested or whose band was
   repainted. */
void PaintCombatPortraits(std::bitset<8> slots);
void RefreshPortraitAdvanceButtons(std::bitset<8> repainted);
void PaintConditionIcons(std::bitset<8> requested, std::bitset<8> repainted_bands);
/* Paint a party slot's next animated portrait frame, as the portrait animation
   asked. */
PortraitBandPaint PaintPartyPortraitFrame(unsigned int party_slot);
/* Whether painting the slot's requested animation frame repaints its complete band
   too, as it does for a character dead in combat, surprised or missing. */
bool PartyPortraitFrameCoversBand(unsigned int party_slot);
/* Stage the casting icon on a monster-manager entry; the spell's
   realm picks the icon catalog base and the flag picks the dim variant. */
void StageMonsterCastIcon(unsigned int monster_index, W8SpellRealm spell_realm, bool flag,
                          int spell_id);
/* Hide or show the condition buttons; portraits mode hides them. */
void DisableConditionButtons(void);
void EnableConditionButtons(void);

/* Region callbacks the eight portrait level-up buttons and condition buttons
   share. */

/* Main-game portrait overlay helpers used when a party slot refreshes. */
bool PreparePartyPortraitOverlay(unsigned int party_slot, unsigned int flags, unsigned int top);
PortraitBandPaint RedrawPartyPortraitOverlay(unsigned int party_slot, bool highlighted,
                                             bool overlay_ready, bool slot_enabled);

DesignRect PartyPortraitBounds(unsigned int slot, int offset = 0);

/* The slot's menu/portrait anchor table - the keyboard-menu
   panel's origin, the portrait band's two x edges, the grid row and the
   column pixel; the adjust flag applies the compact-display shift. */
void GetPartySlotMenuAnchor(int party_slot, int* menu_x, int* menu_y, int* band_menu_edge,
                            int* band_portrait_edge, int* grid_row, int* column_x, int adjust);

/* Shade the depleted tail of a vitals bar - two scanlines tall
   under numeric hit points, three otherwise. */
void ShadeStatusBarGap(int length, int left, int top);

/* Dead-character portrait catalog ids - the small party-strip
   image at [race][0] and the large header portrait at [race][1]. */
extern int g_dead_portrait_catalog_ids[W8_RACE_COUNT][2];
/* Empty-hand portrait catalog ids - right hand at [race*2], left
   hand at [race*2+1]. */
extern int g_empty_hand_catalog_ids[32];
