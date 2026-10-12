#pragma once

#include <cstdint>
#include <utility>

#include "input.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/local_code/TextControl.h"
#include "compat/compiler.h"

struct W8Region;
struct Controls;

extern char s_general_magic_sound[];

/* The eight party-condition buttons beside the portraits, created by
   CreateConditionButtons; its asserts name gpConditionButtonsPanel and
   gpConditionButtons[uiSlot]. W8TextControl is 0xb8; the derived part adds a
   condition-icon selector byte, the catalog image base the refresh copies in
   from the status row, and the party slot the button tracks. */
// VTABLE: WIZ8 0x005eec50
class W8ConditionButton : public W8TextControl {
public:
    W8ConditionButton(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right,
                      std::int32_t bottom, CatalogArtwork artwork, std::int32_t normal_sprite,
                      std::int32_t pressed_sprite, std::int32_t alternate_normal_sprite, std::int32_t alternate_pressed_sprite,
                      std::int32_t disabled_sprite, std::int32_t ui_slot)
        : W8TextControl(panel, region, left, top, right, bottom, std::move(artwork),
                        normal_sprite, pressed_sprite, alternate_normal_sprite,
                        alternate_pressed_sprite, disabled_sprite),
          m_condition(0xff), m_ui_slot(ui_slot)
    {
    }
    virtual void Redraw(bool full_redraw) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;

    /* 0xff while the slot has no shown condition, else which of the two
       condition icon groups Redraw draws. */
    std::uint8_t m_condition;
    std::uint8_t m_pad_b9[3];
    /* Catalog image base copied whole from the status row; Redraw adds
       the per-condition offset (0xb6 or 0xc9). */
    std::int32_t m_image_object;
    std::int32_t m_ui_slot; /* party slot the button tracks */
};

void ReleasePortraitControls(void);
void ReleaseConditionButtons(void);
/* Create the eight level-up portrait buttons (gpLevelButtons) and their panel. */
void CreateLevelButtons(void);
/* Refresh cached HP/stamina/spell portrait bar widths; dirty + redraw when
   any slot's displayed fraction (or numeric HP) changes. */
std::int16_t GetCombatPortraitImage(W8ActionKind action, std::int32_t detail, char status, std::int16_t slot);
void SyncPartyPortraitVitalsBars(void);
void RecordCharacterDamage(std::int32_t party_slot, std::uint32_t amount);
/* Advance per-slot portrait FX counters on a 100ms clock and dirty redraw. */
void TickPartyPortraitFx(void);
/* Clear each occupied slot's damage-splat/effect-icon portrait overlays and
   rearm the shared FX clock; run on the main-game screen leave. */
void ResetPartyPortraitFx(void);
void ToggleNumericHitPoints(void);
void CreateConditionButtons(void);
void DisablePortraitControls(void);
void EnablePortraitAdvanceRegions(void);
void InvalidatePortraitControl(std::uint32_t party_slot);
/* The DrawMainGameScreen portrait tick: combat slots redraw their strip,
   non-combat slots keep their level-advance buttons in sync, and the
   condition buttons track each slot's highest condition or enchantment. */
void RedrawCombatPortraits(void);
void UpdatePortraitAdvanceButtons(void);
void UpdateConditionButtons(void);
/* Stage the casting icon on a monster-manager entry; the spell's
   realm picks the icon catalog base and the flag picks the dim variant. */
void StageMonsterCastIcon(std::uint32_t monster_index, W8SpellRealm spell_realm, bool flag,
                          std::int32_t spell_id);
/* Hide or show the condition-button region set for
   the current layout. SyncMainGameModeRegions picks between them. */
void DisableConditionButtons(void);
void EnableConditionButtons(void);

/* Region callbacks the eight portrait level-up buttons and condition buttons
   share. */
std::uint8_t PortraitControlRegionEvent(const InputEvent* event, W8Region* region);
std::uint8_t ConditionButtonRegionEvent(const InputEvent* event, W8Region* region);

/* Main-game portrait overlay helpers used when a party slot refreshes. */
bool PreparePartyPortraitOverlay(std::uint32_t party_slot, std::uint32_t flags, std::uint32_t top);
void RedrawPartyPortraitOverlay(std::uint32_t party_slot, bool highlighted, bool overlay_ready,
                                bool slot_enabled);

/* The slot's menu/portrait anchor table - the keyboard-menu
   panel's origin, the portrait band's two x edges, the grid row and the
   column pixel; the adjust flag applies the compact-display shift. */
void GetPartySlotMenuAnchor(std::int32_t party_slot, std::int32_t* menu_x, std::int32_t* menu_y, std::int32_t* band_menu_edge,
                            std::int32_t* band_portrait_edge, std::int32_t* grid_row, std::int32_t* column_x, std::int32_t adjust);
void TickPartyPortraitOverlayClocks(void);

/* Shade the depleted tail of a vitals bar - two scanlines tall
   under numeric hit points, three otherwise. */
void ShadeStatusBarGap(std::int32_t length, std::int32_t left, std::int32_t top);

/* Dead-character portrait catalog ids - the small party-strip
   image at [race][0] and the large header portrait at [race][1]. */
extern std::int32_t g_dead_portrait_catalog_ids[W8_RACE_COUNT][2];
/* Empty-hand portrait catalog ids - right hand at [race*2], left
   hand at [race*2+1]. */
extern std::int32_t g_empty_hand_catalog_ids[32];
