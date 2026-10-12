#include <array>
#include <memory>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "sprite.h"
#include "wiz8/surface_storage.h"
#include "wiz8/engine_code/stModelInstance.h" // IWYU pragma: keep - complete sprite type for conversion to srNode.
#include <cstdint>
#include <cstring>

#include "wiz8/fonts.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/cursor.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/regions.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/xstatus.h"
#include "input.h"
#include "timer.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "compat/native.h"
#include "layouts/main_game_screen.h"
#include "layouts/party_formation.h"
#include "local_code/ControlsRect.h"
#include "surrender/srMath.h"

/* On-board (left, top) of each of the fifteen cell markers,
   relative to the board anchor. */
// GLOBAL: WIZ8 0x0064daf4
static srVector2i g_formation_marker_offsets[15] = {
    {0x26, 0x9},  {0x19, 0xd},  {0x33, 0xd},  {0x43, 0x26}, {0x3f, 0x19},
    {0x3f, 0x33}, {0x26, 0x43}, {0x33, 0x3f}, {0x19, 0x3f}, {0x9, 0x26},
    {0xd, 0x33},  {0xd, 0x19},  {0x26, 0x1e}, {0x1e, 0x2a}, {0x2e, 0x2a},
};

/* Screen (left, top) of the fifteen cell controls. */
// GLOBAL: WIZ8 0x0064db6c
static srVector2i g_formation_cell_positions[15] = {
    {0x5a, 0x1c}, {0x3d, 0x25}, {0x77, 0x25}, {0x98, 0x5a}, {0x8f, 0x3d},
    {0x8f, 0x77}, {0x5a, 0x98}, {0x77, 0x8f}, {0x3d, 0x8f}, {0x1c, 0x5a},
    {0x25, 0x77}, {0x25, 0x3d}, {0x5a, 0x48}, {0x48, 0x61}, {0x6c, 0x61},
};

// GLOBAL: WIZ8 0x0069c2f0
static int g_formation_active_cell;
// GLOBAL: WIZ8 0x0069c2f4
static int g_formation_drag_slot;
/* The party slot occupying each cell control, or -1. */
// GLOBAL: WIZ8 0x0069c304
static int g_formation_cell_slots[15];
// GLOBAL: WIZ8 0x0069c340
static uint64_t g_formation_drag_clock;
// GLOBAL: WIZ8 0x0069c380
static int g_formation_drag_cell;

// Retail set 0x1b, regions 161-179.
static constexpr std::array<RegionTemplate, 19> g_formation_region_templates{{
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 0, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 1, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 2, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 3, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 4, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 5, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 6, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 7, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 8, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 9, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 10, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 11, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 12, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 13, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationCellRegionEvent, 14, false, -1},
    {0x00000001, 0, 0, 0, 0, FormationActionRegionEvent, 0, true, 1981},
    {0x00000001, 0, 0, 0, 0, FormationActionRegionEvent, 1, true, 1982},
    {0x00000001, 0, 0, 0, 0, FormationActionRegionEvent, 2, true, 1983},
    {0x00000001, 214, 60, 427, 303, FormationBackgroundRegionEvent, 0, false, -1},
}};
ScreenRegionSet g_formation_regions{g_formation_region_templates, 0x1b};

static void UpdateFormationCells(void);
void ResetFormationCellControls(int cell);

/* Draw each occupied slot's facing arrow and marching-order chip into the
   board image; the slot the board highlights gets the +1 frame of each. */
// FUNCTION: WIZ8 0x005b1f40
void DrawFormationSlotMarkers(SDL_Surface& target)
{
    int slot;

    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[slot];
        W8PartyFormationPosition* position = &g_status.formation.positions[slot];
        int left;
        int top;
        int image;
        int order_image;

        if (!row->fOccupied || position->bQuadrant == -1) {
            continue;
        }
        left = g_formation_marker_offsets[position->bQuadrant * 3 + position->bQuadrantSlot].x;
        top = g_formation_marker_offsets[position->bQuadrant * 3 + position->bQuadrantSlot].y;
        image = position->facing * 3;
        order_image = row->party_order_index * 3;
        if (slot == g_level_block->formation_highlight_party_slot) {
            image += 1;
            order_image += 1;
        }
        CatalogArtwork::Load(0x9c, 0).draw(target, image, left, top);
        CatalogArtwork::Load(0x9d, 0).draw(target, order_image, left, top);
    }
}

/* Rebuild the board, compass and overlay sprites and redraw the slot markers
   into the board image. */
// FUNCTION: WIZ8 0x005b1c80
void RefreshFormationBoard(void)
{
    SDLSurfaceOwner board_image{nullptr, SDL_DestroySurface};
    SDLSurfaceOwner compass_image{nullptr, SDL_DestroySurface};

    if (g_status.selected_character != -1) {
        FaceCameraToSelection(g_status.selected_character);
    }
    if (!g_level_block->formation_board_visible) {
        return;
    }
    ReleaseFormationBoard();
    const auto board_artwork =
        CatalogArtwork::Load(g_level_block->formation_board_alternate ? 0x9a : 0x99);
    board_image = RenderSpriteToImage(*board_artwork.spriteSheet(), 0);
    if (!board_image) return;
    const auto compass_artwork = CatalogArtwork::Load(0x9b);
    compass_image = RenderSpriteToImage(*compass_artwork.spriteSheet(), 0);
    if (!compass_image) return;
    DrawFormationSlotMarkers(*board_image);
    g_level_block->formation_compass_sprite = CreateSpriteFromSurface(*compass_image, 0, 1, 0, 1);
    Position2DNodeUnsnapped(g_level_block->formation_compass_sprite, 0x207, 0x167);
    SetModelInstance2DDisplayState(g_level_block->formation_compass_sprite, 4);
    g_level_block->formation_board_sprite = CreateSpriteFromSurface(*board_image, 0, 1, 0, 1);
    Position2DNodeUnsnapped(g_level_block->formation_board_sprite, 0x207, 0x167);
    SetModelInstance2DDisplayState(g_level_block->formation_board_sprite, 4);
    UpdateFormationCompass();
}

/* Rotate the compass needle to party_facing - party_heading. */
// FUNCTION: WIZ8 0x005b1e70
void UpdateFormationCompass(void)
{
    if (g_level_block->formation_compass_sprite != 0) {
        RotateNodeInDegrees(g_level_block->formation_compass_sprite,
                            g_status.party_facing - g_status.party_heading + 0x168);
    }
    SetRendererModePair();
}

/* Lazily create the overlay sprite above the board. */
// FUNCTION: WIZ8 0x005b1ea0
void CreateFormationBoardOverlay(void)
{
    W8ControlsRect bounds;

    if (g_level_block->formation_overlay_sprite == 0 &&
        g_level_block->formation_board_sprite != 0) {
        bounds.left = 0x200;
        bounds.top = 0x166;
        bounds.right = 0x269;
        bounds.bottom = 0x1c2;
        g_level_block->formation_overlay_sprite = CreateSpriteFromSurface(GetFrameBufferSurface(), &bounds, 0, 0, 1);
        Position2DNodeUnsnapped(g_level_block->formation_overlay_sprite, 0x200, 0x166);
        SetModelInstance2DDisplayState(g_level_block->formation_overlay_sprite, 4);
    }
}

unsigned char FormationBoardHoverRegionEvent(const InputEvent*, W8Region* region);

/* Board callback: opens the formation panel on release, hit-tests the cell
   markers for tooltips, and switches board art on hover transitions. */
// FUNCTION: WIZ8 0x005b2020
unsigned char FormationBoardRegionEvent(const InputEvent* event, W8Region* region, unsigned short)
{
    switch (event->type) {
    case InputEventType::LeftButtonDown:
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
        return 1;
    case InputEventType::LeftButtonUp:
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0 && !gXStatus.fReviewCharacterMode) {
            OpenFormationPanel();
        }
        return 1;
    case InputEventType::RightButtonDown:
        region->flags |= W8_REGION_RIGHT_BUTTON_HELD;
        return 1;
    case InputEventType::RightButtonUp:
        return 1;
    case InputEventType::MouseMove:
        break;
    default:
        return 0;
    }

    return FormationBoardHoverRegionEvent(event, region);
}

/* The board region's second handler: just the hover half - hit-tests the
   formation markers for the tooltip subject and swaps the board art on
   enter/leave. Retail emits it as a separate function right after the full
   handler. */
// FUNCTION: WIZ8 0x005B207F
unsigned char FormationBoardHoverRegionEvent(const InputEvent*, W8Region* region)
{
    int slot;
    int hit;

    hit = -1;
    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[slot];
        W8PartyFormationPosition* position = &g_status.formation.positions[slot];
        int cell;

        if (!row->fOccupied || position->bQuadrant == -1) {
            continue;
        }
        cell = position->bQuadrant * 3 + position->bQuadrantSlot;
        if (IsCursorInRectangle(g_formation_marker_offsets[cell].x + 0x207,
                                g_formation_marker_offsets[cell].y + 0x167,
                                g_formation_marker_offsets[cell].x + 0x211,
                                g_formation_marker_offsets[cell].y + 0x171)) {
            hit = slot;
            break;
        }
    }
    if (hit != g_level_block->formation_highlight_party_slot) {
        SetTooltipSubject(6, hit);
    }
    if ((region->flags & W8_REGION_MOUSE_ENTER) == 0) {
        if ((region->flags & W8_REGION_MOUSE_LEAVE) != 0) {
            g_level_block->formation_board_alternate = 0;
            RefreshFormationBoard();
        }
        return 0;
    }
    g_level_block->formation_board_alternate = 1;
    RefreshFormationBoard();
    return 0;
}

static void SelectFormationCell(void);
static void AcceptFormationChanges(void);
static void ResetFormationPanel(void);

/* Build the panel and its fifteen pairs of cell controls plus the three
   buttons (accept, close, reset). On any allocation failure the partial panel
   is released. */
// FUNCTION: WIZ8 0x005b2260
static unsigned char CreateFormationPanel(void)
{
    W8TextControl** button;
    int index;

    g_formation_panel = std::make_unique<Controls>(0xd6, 0x3c, 0x1ab, 0x12f, CatalogArtwork::Load(0x9e, 0), 0);
    if (!g_formation_panel) {
        return 0;
    }
    for (index = 0; index < 15; ++index) {
        int left = g_formation_cell_positions[index].x;
        int top = g_formation_cell_positions[index].y;
        W8TextControl* cell;

        cell = g_formation_panel->EmplaceControl<W8TextControl>(g_formation_regions->index(index), left, top,
                                 left + 0x20, top + 0x20, CatalogArtwork::Load(0x9f, 0));
        g_formation_cell_controls[index] = cell;
        if (cell == 0) {
            DestroyFormationPanel();
            return 0;
        }
        cell->AddLayoutFlags(g_W8TextControlLayoutToggle);
        cell->m_onPrimary = SelectFormationCell;
        cell = g_formation_panel->EmplaceControl<W8TextControl>(-1, left, top, left + 0x20, top + 0x20, CatalogArtwork::Load(0xa0, 0));
        g_formation_cell_overlays[index] = cell;
        if (cell == 0) {
            DestroyFormationPanel();
            return 0;
        }
    }
    g_formation_action_buttons[0] = g_formation_panel->EmplaceControl<W8TextControl>(g_formation_regions->index(15), 0x85, 0xcb, 0xa0, 0xe6,
        CatalogArtwork::Load(0x8e, 0), W8SpriteStates{.normal = 0, .alternate_normal = 1, .alternate_pressed = 2, .disabled = 3});
    g_formation_action_buttons[1] = g_formation_panel->EmplaceControl<W8TextControl>(g_formation_regions->index(16), 0xa7, 0xcb, 0xc2, 0xe6,
        CatalogArtwork::Load(0x8e, 0), W8SpriteStates{.normal = 4, .alternate_normal = 5, .alternate_pressed = 6, .disabled = 7});
    g_formation_action_buttons[2] = g_formation_panel->EmplaceControl<W8TextControl>(g_formation_regions->index(17), 0xf, 0xcd, 0x29, 0xe7,
        CatalogArtwork::Load(0x8f, 0), W8SpriteStates{.normal = 0, .alternate_normal = 1, .alternate_pressed = 2, .disabled = 3});
    button = g_formation_action_buttons;
    while (*button != 0) {
        ++button;
        if (button > &g_formation_action_buttons[2]) {
            g_formation_action_buttons[0]->m_onPrimary = AcceptFormationChanges;
            g_formation_action_buttons[1]->m_onPrimary = CloseFormationPanel;
            g_formation_action_buttons[2]->m_onPrimary = ResetFormationPanel;
            g_formation_panel->SetActive(true);
            return 1;
        }
    }
    DestroyFormationPanel();
    return 0;
}

/* Open the formation panel: build the controls, snapshot the live formation
   into gXStatus.edited_formation and pause the world. */
// FUNCTION: WIZ8 0x005b2150
void OpenFormationPanel(void)
{
    int index;

    g_formation_panel.reset();
    for (index = 0; index < 15; ++index) {
        if (g_formation_cell_controls[index] != 0) {
            g_formation_cell_controls[index] = 0;
        }
        if (g_formation_cell_overlays[index] != 0) {
            g_formation_cell_overlays[index] = 0;
        }
    }
    for (index = 0; index < 3; ++index) {
        if (g_formation_action_buttons[index] != 0) {
            g_formation_action_buttons[index] = 0;
        }
    }
    if (CreateFormationPanel() != 0) {
        g_formation_active_cell = -1;
        g_formation_drag_cell = -1;
        g_formation_drag_slot = -1;
        g_formation_drag_clock = w8_get_ticks() + (0);
        gXStatus.fReviewCharacterMode = true;
        g_formation_regions.Enable();
        RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
        CopyPartyFormationState(&gXStatus.edited_formation, &g_status.formation);
        PauseMainGameWorld();
        UpdateFormationCells();
    }
}

/* Re-derive every cell's sprites and enabled state from the edited formation
   and re-highlight the selected character's cell. */
// FUNCTION: WIZ8 0x005b2600
static void UpdateFormationCells(void)
{
    int cell;
    int slot;

    for (cell = 0; cell < 15; ++cell) {
        g_formation_cell_slots[cell] = -1;
        ResetFormationCellControls(cell);
    }
    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[slot];
        W8PartyFormationPosition* position = &gXStatus.edited_formation.positions[slot];
        W8TextControl* primary;
        W8TextControl* overlay;
        int sprite;

        if (row->fOccupied && position->bQuadrant != -1) {
            cell = position->bQuadrant * 3 + position->bQuadrantSlot;
            primary = g_formation_cell_controls[cell];
            overlay = g_formation_cell_overlays[cell];
            if (!CanHoldFormationPlace(slot)) {
                sprite = position->facing * 3 + 2;
                primary->m_normalSprite = sprite;
                primary->m_alternateNormalSprite = sprite;
                primary->m_pressedSprite = sprite;
                primary->m_alternatePressedSprite = sprite;
                primary->m_disabledSprite = sprite;
                sprite = row->party_order_index * 3 + 2;
                overlay->m_normalSprite = sprite;
                overlay->m_alternateNormalSprite = sprite;
                overlay->m_pressedSprite = sprite;
                overlay->m_alternatePressedSprite = sprite;
            } else {
                sprite = position->facing * 3;
                primary->m_normalSprite = sprite;
                primary->m_alternateNormalSprite = sprite;
                primary->m_pressedSprite = sprite + 1;
                primary->m_alternatePressedSprite = sprite + 1;
                primary->m_disabledSprite = sprite + 2;
                sprite = row->party_order_index * 3;
                overlay->m_normalSprite = sprite;
                overlay->m_alternateNormalSprite = sprite + 1;
                overlay->m_pressedSprite = sprite;
                overlay->m_alternatePressedSprite = sprite + 1;
                sprite += 2;
            }
            overlay->m_disabledSprite = sprite;
            primary->SetActive(true);
            overlay->SetActive(true);
            primary->SetEnabled(true);
            overlay->SetEnabled(true);
            g_formation_cell_slots[cell] = slot;
            if (slot == g_status.selected_character &&
                !primary->IsSelected()) {
                primary->EnableSecondaryState(false);
            }
        }
    }
    g_formation_panel->InvalidateAll();
}

/* The cells' activation callback: highlight the active cell and make its
   party slot the selected character. */
// FUNCTION: WIZ8 0x005b27e0
static void SelectFormationCell(void)
{
    int index;

    if (CanHoldFormationPlace(g_formation_cell_slots[g_formation_active_cell])) {
        for (index = 0; index < 15; ++index) {
            W8TextControl* control = g_formation_cell_controls[index];

            if (index == g_formation_active_cell) {
                control->EnableSecondaryState(false);
            } else if (control->IsSelected()) {
                control->DisableSecondaryState(false);
                g_formation_cell_overlays[index]->Invalidate(false);
            }
        }
        SelectPartyCharacter(g_formation_cell_slots[g_formation_active_cell]);
    }
}

/* The accept button: wake any slot whose row changed, reconcile the edited
   formation into the live one (or just warn while in combat) and close the
   panel. */
// FUNCTION: WIZ8 0x005b2860
static void AcceptFormationChanges(void)
{
    unsigned int slot;

    for (slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied && CanHoldFormationPlace(slot) &&
            gXStatus.edited_formation.positions[slot].bQuadrant !=
                g_status.formation.positions[slot].bQuadrant) {
            StartBreathCycle(slot, false);
        }
    }
    if (!gXStatus.fCombatMode) {
        ReconcilePartyFormation(&gXStatus.edited_formation, &g_status.formation);
    } else if (memcmp(&g_status.formation, &gXStatus.edited_formation,
                      sizeof(W8PartyFormationState)) != 0) {
        ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x7d9]);
    }
    RefreshFormationBoard();
    RefreshRadarMap();
    CloseFormationPanel();
}

/* The reset button: throw the staged edits away and rebuild the cells. */
// FUNCTION: WIZ8 0x005b2960
static void ResetFormationPanel(void)
{
    CopyPartyFormationState(&gXStatus.edited_formation, &g_status.formation);
    UpdateFormationCells();
}

static void BeginFormationDrag(const InputEvent* event);
static void DropFormationSlot(int cell);

/* Region callback the fifteen formation cells share: left press arms the
   drag clock, a repeat while held begins the drag, left release either
   activates the cell or completes the drop, and mouse transitions drive the
   hover art and tooltip. */
// FUNCTION: WIZ8 0x005b29d0
unsigned char FormationCellRegionEvent(const InputEvent* event, W8Region* region, unsigned short id)
{
    if (!gXStatus.fReviewCharacterMode) {
        return 0;
    }
    if (g_formation_cell_slots[id] == -1) {
        PushButtonSoundScheme(0, true);
        if (event->type != InputEventType::LeftButtonUp) {
            return 1;
        }
        if (g_formation_drag_cell == -1) {
            return 1;
        }
    }
    if (IsLeftButtonEvent(event->type)) {
        if (event->type == InputEventType::LeftButtonRepeat) {
            if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0 &&
                TimeUntilDeadline(g_formation_drag_clock) == 0 && g_formation_drag_cell == -1 &&
                g_formation_active_cell != -1) {
                if (!CanHoldFormationPlace(g_formation_cell_slots[id])) {
                    region->flags &= ~W8_REGION_LEFT_BUTTON_HELD;
                    return 1;
                }
                BeginFormationDrag(event);
                g_formation_cell_overlays[id]->Invalidate(false);
                return 1;
            }
        } else {
            if (event->type == InputEventType::LeftButtonDown) {
                g_formation_cell_controls[id]->OnLeftButtonDown(0);
                g_formation_cell_overlays[id]->Invalidate(false);
                region->flags |= W8_REGION_LEFT_BUTTON_HELD;
                g_formation_drag_clock = w8_get_ticks() + (0xfa);
                return 1;
            }
            if (event->type != InputEventType::LeftButtonUp) {
                return 0;
            }
            if (g_formation_cell_slots[id] != -1) {
                g_formation_active_cell = id;
                g_formation_cell_controls[id]->OnLeftButtonUp(0);
                g_formation_cell_overlays[id]->Invalidate(false);
            }
            if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0) {
                region->flags &= ~W8_REGION_LEFT_BUTTON_HELD;
            }
            if (g_formation_drag_cell != -1) {
                DropFormationSlot(id);
                return 1;
            }
        }
    } else {
        if (event->type != InputEventType::MouseMove) {
            return 0;
        }
        if ((region->flags & W8_REGION_MOUSE_LEAVE) != 0) {
            g_formation_cell_controls[id]->OnMouseLeave(0);
            g_formation_cell_overlays[id]->OnMouseLeave(0);
            g_formation_cell_overlays[id]->Invalidate(false);
            if (IsLeftButtonHeld() && g_formation_drag_cell == -1 &&
                CanHoldFormationPlace(g_formation_cell_slots[id])) {
                g_formation_active_cell = id;
                BeginFormationDrag(event);
                g_formation_cell_overlays[id]->Invalidate(false);
            }
            g_formation_active_cell = -1;
            SetTooltipSubject(7, -1);
            return 1;
        }
        if ((region->flags & W8_REGION_MOUSE_ENTER) == 0 && g_formation_active_cell != -1) {
            return 0;
        }
        g_formation_cell_controls[id]->OnMouseEnter(0);
        g_formation_cell_overlays[id]->OnMouseEnter(0);
        g_formation_cell_overlays[id]->Invalidate(false);
        g_formation_active_cell = id;
        SetTooltipSubject(7, g_formation_cell_slots[id]);
    }
    return 1;
}

// FUNCTION: WIZ8 0x005b2cb0
unsigned char FormationActionRegionEvent(const InputEvent* event, W8Region* region, unsigned short id)
{
    if (!gXStatus.fReviewCharacterMode) {
        return 0;
    }
    return DispatchButtonRegionEvent(event, region,
                                     g_formation_action_buttons[id]);
}

// FUNCTION: WIZ8 0x005b2d70
unsigned char FormationBackgroundRegionEvent(const InputEvent* event, W8Region*, unsigned short)
{
    SDL_Point point;
    if (!gXStatus.fReviewCharacterMode) {
        return 0;
    }
    PushButtonSoundScheme(0, true);
    SGPMouseGetPos(&point);
    switch (event->type) {
    case InputEventType::LeftButtonUp:
        if (g_formation_drag_cell != -1) {
            DropFormationSlot(-1);
        }
        return 1;
    case InputEventType::MouseMove:
        if ((point.x < 234 || point.x > 406 || point.y < 80 || point.y > 252) &&
            g_formation_drag_cell != -1) {
            DropFormationSlot(-1);
        }
        break;
    default:
        break;
    }
    return 0;
}

/* Pick up the active cell's character: clear the cell, switch the mouse
   cursor to the dragged chip and blank both controls' sprites. */
// FUNCTION: WIZ8 0x005b2e10
static void BeginFormationDrag(const InputEvent*)
{
    SDL_Point point;
    unsigned short region;
    int sprite;

    SelectFormationCell();
    g_formation_cell_controls[g_formation_active_cell]->DisableSecondaryState(false);
    g_formation_drag_slot = g_formation_cell_slots[g_formation_active_cell];
    g_formation_drag_cell = g_formation_active_cell;
    // The dragged chip follows the pointer; the drop must reach the cell under
    // it rather than the button region that captured the initial press.
    ClearActiveRegionIfMatches(g_formation_cell_controls[g_formation_drag_cell]->m_region);
    g_formation_cell_slots[g_formation_active_cell] = -1;
    SGPMouseGetPos(&point);
    ClearMouseSurface();
    sprite = g_formation_cell_controls[g_formation_drag_cell]->m_alternatePressedSprite;
    const auto artwork = CatalogArtwork::Load(0x9f);
    region = artwork.imageOffset() + static_cast<short>(sprite);
    SetMouseCursorFromVideoObject(*artwork.spriteSheet(), region, 0x10, 0x10);
    const auto overlay_artwork = CatalogArtwork::Load(0xa0);
    const auto overlay =
        static_cast<std::uint16_t>(overlay_artwork.imageOffset() +
                                   static_cast<short>(g_formation_cell_overlays
                                                          [g_formation_drag_cell]
                                                              ->m_alternateNormalSprite));
    BlitToMouseCursor(*overlay_artwork.spriteSheet(), overlay, 0, 0);
    WarpSystemCursor(point.x - 0x10, point.y - 0x10);
    RefreshMouseCursorTexture();
    gXStatus.iCurrentCursor = W8_CURSOR_INVALID_TARGET;
    ResetFormationCellControls(g_formation_drag_cell);
}

/* Drop the dragged character: into an empty cell it re-seats the row, onto a
   movable occupant it swaps positions, onto a dead one it cancels. */
// FUNCTION: WIZ8 0x005b2f70
static void DropFormationSlot(int cell)
{
    if (cell != -1 && cell != g_formation_drag_cell) {
        if (g_formation_cell_slots[cell] == -1) {
            SetFormationPosition(&gXStatus.edited_formation, g_formation_drag_slot, -1, -1, false,
                                 true, true);
            SeatFormationSlotInRow(&gXStatus.edited_formation, g_formation_drag_slot, cell / 3);
        } else if (!CanHoldFormationPlace(g_formation_cell_slots[cell])) {
            cell = -1;
        } else {
            SwapFormationSlots(&gXStatus.edited_formation, g_formation_drag_slot,
                               g_formation_cell_slots[cell]);
        }
    }
    UpdateHeldItemCursor();
    UpdateFormationCells();
    g_formation_cell_overlays[g_formation_drag_cell]->SetAlternateTextEnabled(false);
    if (cell != -1) {
        g_formation_cell_overlays[cell]->SetAlternateTextEnabled(true);
        g_formation_active_cell = cell;
        g_level_block->formation_highlight_party_slot = g_formation_cell_slots[cell];
        RequestRedraw(1 << (g_level_block->formation_highlight_party_slot & 0x1f));
    }
    g_formation_drag_cell = -1;
}

/* Blank every sprite on a cell's control pair. */
// FUNCTION: WIZ8 0x005b3080
void ResetFormationCellControls(int cell)
{
    g_formation_cell_controls[cell]->m_normalSprite = -1;
    g_formation_cell_controls[cell]->m_alternateNormalSprite = -1;
    g_formation_cell_controls[cell]->m_pressedSprite = -1;
    g_formation_cell_controls[cell]->m_alternatePressedSprite = -1;
    g_formation_cell_controls[cell]->m_disabledSprite = -1;
    g_formation_cell_controls[cell]->Invalidate(true);
    g_formation_cell_overlays[cell]->m_normalSprite = -1;
    g_formation_cell_overlays[cell]->m_alternateNormalSprite = -1;
    g_formation_cell_overlays[cell]->m_pressedSprite = -1;
    g_formation_cell_overlays[cell]->m_alternatePressedSprite = -1;
    g_formation_cell_overlays[cell]->m_disabledSprite = -1;
    g_formation_cell_overlays[cell]->Invalidate(true);
}

/* Highlight the cell holding one party slot and un-highlight the rest. */
// FUNCTION: WIZ8 0x005b3100
void SelectFormationSlotCell(int party_slot)
{
    int index;

    for (index = 0; index < 15; ++index) {
        W8TextControl* control = g_formation_cell_controls[index];

        if (g_formation_cell_slots[index] == party_slot) {
            control->EnableSecondaryState(false);
            g_formation_cell_overlays[index]->Invalidate(false);
        } else if (control->IsSelected()) {
            control->DisableSecondaryState(false);
            g_formation_cell_overlays[index]->Invalidate(false);
        }
    }
}

// FUNCTION: WIZ8 0x005B2200
void CloseFormationPanel(void)
{
    DestroyFormationPanel();
    gXStatus.fReviewCharacterMode = false;
    UpdateHeldItemCursor();
    g_formation_regions.Disable();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    ClearSurfaceRect(0xd6, 0x3c, 0x1ab, 0x12f);
    InvalidateRegion(0xd6, 0x3c, 0x1ab, 0x12f);
    ResumeMainGameWorld();
}
