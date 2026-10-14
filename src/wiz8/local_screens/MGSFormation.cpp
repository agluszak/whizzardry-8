#include "wiz8/ui_input.h"
#include <array>
#include <memory>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_mouse.h>
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
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/button_interaction.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/cursor.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/ui_layer.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/xstatus.h"
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
    {0x26, 0x9},  {0x19, 0xd}, {0x33, 0xd}, {0x43, 0x26}, {0x3f, 0x19}, {0x3f, 0x33}, {0x26, 0x43}, {0x33, 0x3f},
    {0x19, 0x3f}, {0x9, 0x26}, {0xd, 0x33}, {0xd, 0x19},  {0x26, 0x1e}, {0x1e, 0x2a}, {0x2e, 0x2a},
};

/* Screen (left, top) of the fifteen cell controls. */
// GLOBAL: WIZ8 0x0064db6c
static srVector2i g_formation_cell_positions[15] = {
    {0x5a, 0x1c}, {0x3d, 0x25}, {0x77, 0x25}, {0x98, 0x5a}, {0x8f, 0x3d}, {0x8f, 0x77}, {0x5a, 0x98}, {0x77, 0x8f},
    {0x3d, 0x8f}, {0x1c, 0x5a}, {0x25, 0x77}, {0x25, 0x3d}, {0x5a, 0x48}, {0x48, 0x61}, {0x6c, 0x61},
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

namespace {
struct FormationPanel {
    UiLayer input{0x1b};
    std::array<HitTarget*, 15> targets{};
    std::array<ButtonInteraction, 15> cells{};
    std::array<bool, 15> highlighted{};
    std::array<std::unique_ptr<W8ButtonWidget>, 3> actions;
    bool dirty = true;
};
std::unique_ptr<FormationPanel> formation;
constexpr ButtonPolicy cell_policy{.primary_toggle = ButtonToggle::Release, .controller_activation = true};
} // namespace

static UiResult HandleFormationCell(const UiPointerEvent& event, int cell);
static UiResult HandleFormationBackground(const UiPointerEvent& event);

void EnableFormationInput()
{
    if (formation) {
        formation->input.setEnabled(true);
    }
}

void DestroyFormationPanel()
{
    formation.reset();
}

static void UpdateFormationCells(void);

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
    const auto board_artwork = CatalogArtwork::Load(g_level_block->formation_board_alternate ? 0x9a : 0x99);
    board_image = RenderSpriteToImage(*board_artwork.spriteSheet(), 0);
    if (!board_image) {
        return;
    }
    const auto compass_artwork = CatalogArtwork::Load(0x9b);
    compass_image = RenderSpriteToImage(*compass_artwork.spriteSheet(), 0);
    if (!compass_image) {
        return;
    }
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

    if (g_level_block->formation_overlay_sprite == 0 && g_level_block->formation_board_sprite != 0) {
        bounds.left = 0x200;
        bounds.top = 0x166;
        bounds.right = 0x269;
        bounds.bottom = 0x1c2;
        g_level_block->formation_overlay_sprite = CreateSpriteFromSurface(GetFrameBufferSurface(), &bounds, 0, 0, 1);
        Position2DNodeUnsnapped(g_level_block->formation_overlay_sprite, 0x200, 0x166);
        SetModelInstance2DDisplayState(g_level_block->formation_overlay_sprite, 4);
    }
}

UiResult HandleFormationBoard(const UiPointerEvent& event)
{
    if (event.action == UiPointerAction::Up && event.inside && event.button == UiPointerButton::Left &&
        (event.captured_buttons & SDL_BUTTON_LMASK) && !gXStatus.fReviewCharacterMode) {
        AfterUiDispatch(OpenFormationPanel);
    }
    if (event.action != UiPointerAction::Move && event.action != UiPointerAction::Enter &&
        event.action != UiPointerAction::Leave) {
        return UiResult::Handled;
    }
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
        if (IsCursorInRectangle(g_formation_marker_offsets[cell].x + 0x207, g_formation_marker_offsets[cell].y + 0x167,
                                g_formation_marker_offsets[cell].x + 0x211,
                                g_formation_marker_offsets[cell].y + 0x171)) {
            hit = slot;
            break;
        }
    }
    if (hit != g_level_block->formation_highlight_party_slot) {
        SetTooltipSubject(6, hit);
    }
    if (event.action != UiPointerAction::Enter) {
        if (event.action == UiPointerAction::Leave) {
            g_level_block->formation_board_alternate = 0;
            RefreshFormationBoard();
        }
        return UiResult::Handled;
    }
    g_level_block->formation_board_alternate = 1;
    RefreshFormationBoard();
    return UiResult::Handled;
}

static void SelectFormationCell(void);
static void AcceptFormationChanges(void);
static void ResetFormationPanel(void);

static void CreateFormationPanel()
{
    formation = std::make_unique<FormationPanel>();
    for (int cell = 0; cell < 15; ++cell) {
        const auto position = g_formation_cell_positions[cell];
        formation->targets[cell] =
            &formation->input.add(SDL_Rect{0xd6 + position.x, 0x3c + position.y, 0x21, 0x21},
                                  [cell](const UiPointerEvent& event) { return HandleFormationCell(event, cell); });
    }
    constexpr std::array<SDL_Rect, 3> bounds{{
        {0xd6 + 0x85, 0x3c + 0xcb, 0x1b, 0x1b},
        {0xd6 + 0xa7, 0x3c + 0xcb, 0x1b, 0x1b},
        {0xd6 + 0xf, 0x3c + 0xcd, 0x1a, 0x1a},
    }};
    static constexpr std::array actions{AcceptFormationChanges, CloseFormationPanel, ResetFormationPanel};
    for (int action = 0; action < 3; ++action) {
        const auto artwork = CatalogArtwork::Load(action == 2 ? 0x8f : 0x8e, 0);
        const int frame = artwork.imageOffset() + (action == 1 ? 4 : 0);
        auto& button = formation->actions[action];
        button = std::make_unique<W8ButtonWidget>(formation->input);
        button->Configure(artwork.spriteSheet(), {frame + 3, frame, frame + 1, -1, frame + 2},
                          {.on_click = [action](W8ButtonWidget*) { AfterUiDispatch(actions[action]); },
                           .priority = UI_PRIORITY_LOWEST,
                           .tooltip = 1981 + action});
        button->SetPosition(bounds[action].x, bounds[action].y);
        button->Resize(bounds[action].w, bounds[action].h);
        button->policy.controller_activation = true;
    }
    formation->input.add(SDL_Rect{214, 60, 214, 244}, HandleFormationBackground);
}

void RefreshFormationPanel(bool force)
{
    if (!formation) {
        return;
    }
    bool dirty = force || formation->dirty;
    for (const auto& button : formation->actions) {
        dirty = dirty || button->m_dirty;
    }
    if (!dirty) {
        return;
    }
    auto& target = GetFrameBufferSurface();
    CatalogArtwork::Load(0x9e, 0).drawAndInvalidate(target, 0, 0xd6, 0x3c);
    const auto arrows = CatalogArtwork::Load(0x9f, 0);
    const auto chips = CatalogArtwork::Load(0xa0, 0);
    for (int cell = 0; cell < 15; ++cell) {
        const int slot = g_formation_cell_slots[cell];
        if (slot == -1) {
            continue;
        }
        const auto position = g_formation_cell_positions[cell];
        const bool disabled = !CanHoldFormationPlace(slot);
        const int arrow = gXStatus.edited_formation.positions[slot].facing * 3 + (disabled                         ? 2
                                                                                  : formation->cells[cell].pressed ? 1
                                                                                                                   : 0);
        const int chip = g_status.buffers.XChar[slot].party_order_index * 3 + (disabled                       ? 2
                                                                               : formation->highlighted[cell] ? 1
                                                                                                              : 0);
        arrows.draw(target, arrow, 0xd6 + position.x, 0x3c + position.y);
        chips.draw(target, chip, 0xd6 + position.x, 0x3c + position.y);
    }
    for (auto& button : formation->actions) {
        button->m_dirty = true;
        button->Draw(target);
    }
    formation->dirty = false;
}

/* Open the formation panel: build the controls, snapshot the live formation
   into gXStatus.edited_formation and pause the world. */
// FUNCTION: WIZ8 0x005b2150
void OpenFormationPanel(void)
{
    DestroyFormationPanel();
    CreateFormationPanel();
    g_formation_active_cell = -1;
    g_formation_drag_cell = -1;
    g_formation_drag_slot = -1;
    g_formation_drag_clock = w8_get_ticks() + (0);
    gXStatus.fReviewCharacterMode = true;
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    CopyPartyFormationState(&gXStatus.edited_formation, &g_status.formation);
    PauseMainGameWorld();
    UpdateFormationCells();
}

/* Re-derive every cell's sprites and enabled state from the edited formation
   and re-highlight the selected character's cell. */
// FUNCTION: WIZ8 0x005b2600
static void UpdateFormationCells(void)
{
    for (auto& slot : g_formation_cell_slots) {
        slot = -1;
    }
    for (int slot = 0; slot < 8; ++slot) {
        const auto& position = gXStatus.edited_formation.positions[slot];
        if (g_status.buffers.XChar[slot].fOccupied && position.bQuadrant != -1) {
            g_formation_cell_slots[position.bQuadrant * 3 + position.bQuadrantSlot] = slot;
        }
    }
    SelectFormationSlotCell(g_status.selected_character);
}

/* The cells' activation callback: highlight the active cell and make its
   party slot the selected character. */
// FUNCTION: WIZ8 0x005b27e0
static void SelectFormationCell(void)
{
    const int slot = g_formation_cell_slots[g_formation_active_cell];
    if (CanHoldFormationPlace(slot)) {
        SelectFormationSlotCell(slot);
        SelectPartyCharacter(slot);
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
            gXStatus.edited_formation.positions[slot].bQuadrant != g_status.formation.positions[slot].bQuadrant) {
            StartBreathCycle(slot, false);
        }
    }
    if (!gXStatus.fCombatMode) {
        ReconcilePartyFormation(&gXStatus.edited_formation, &g_status.formation);
    } else if (memcmp(&g_status.formation, &gXStatus.edited_formation, sizeof(W8PartyFormationState)) != 0) {
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

static void BeginFormationDrag();
static void DropFormationSlot(int cell);

/* Region callback the fifteen formation cells share: left press arms the
   drag clock, a repeat while held begins the drag, left release either
   activates the cell or completes the drop, and mouse transitions drive the
   hover art and tooltip. */
// FUNCTION: WIZ8 0x005b29d0
static UiResult HandleFormationCell(const UiPointerEvent& event, int id)
{
    if (!gXStatus.fReviewCharacterMode) {
        return UiResult::Pass;
    }
    if (g_formation_cell_slots[id] == -1) {
        PushButtonSoundScheme(0, true);
        if (event.action != UiPointerAction::Up || event.button != UiPointerButton::Left ||
            g_formation_drag_cell == -1) {
            return UiResult::Handled;
        }
    }
    if (event.button == UiPointerButton::Left) {
        switch (event.action) {
        case UiPointerAction::Repeat:
            if ((event.captured_buttons & SDL_BUTTON_LMASK) && TimeUntilDeadline(g_formation_drag_clock) == 0 &&
                g_formation_drag_cell == -1 && g_formation_active_cell != -1) {
                if (!CanHoldFormationPlace(g_formation_cell_slots[id])) {
                    formation->targets[id]->releaseCapture();
                    return UiResult::Handled;
                }
                BeginFormationDrag();
                formation->dirty = true;
            }
            return UiResult::Handled;
        case UiPointerAction::Down:
            formation->cells[id].press(SDL_BUTTON_LMASK, cell_policy);
            formation->dirty = true;
            g_formation_drag_clock = w8_get_ticks() + 250;
            return UiResult::Handled;
        case UiPointerAction::Up:
            if (g_formation_cell_slots[id] != -1) {
                g_formation_active_cell = id;
                if (formation->cells[id].release(SDL_BUTTON_LMASK, cell_policy).activate) {
                    SelectFormationCell();
                }
                formation->dirty = true;
            }
            if (g_formation_drag_cell != -1) {
                DropFormationSlot(id);
            }
            return UiResult::Handled;
        default:
            break;
        }
    }
    if (event.action == UiPointerAction::Leave) {
        formation->cells[id].leave(cell_policy);
        formation->highlighted[id] = false;
        formation->dirty = true;
        if ((event.buttons & SDL_BUTTON_LMASK) && g_formation_drag_cell == -1 &&
            CanHoldFormationPlace(g_formation_cell_slots[id])) {
            g_formation_active_cell = id;
            BeginFormationDrag();
            formation->dirty = true;
        }
        g_formation_active_cell = -1;
        SetTooltipSubject(7, -1);
        return UiResult::Handled;
    }
    if (event.action == UiPointerAction::Enter ||
        (event.action == UiPointerAction::Move && g_formation_active_cell == -1)) {
        formation->highlighted[id] = true;
        formation->dirty = true;
        g_formation_active_cell = id;
        SetTooltipSubject(7, g_formation_cell_slots[id]);
        return UiResult::Handled;
    }
    return UiResult::Pass;
}

static UiResult HandleFormationBackground(const UiPointerEvent& event)
{
    if (!gXStatus.fReviewCharacterMode) {
        return UiResult::Pass;
    }
    PushButtonSoundScheme(0, true);
    if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Left) {
        if (g_formation_drag_cell != -1) {
            DropFormationSlot(-1);
        }
        return UiResult::Handled;
    }
    if ((event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
         event.action == UiPointerAction::Leave) &&
        (event.position.x < 234 || event.position.x > 406 || event.position.y < 80 || event.position.y > 252) &&
        g_formation_drag_cell != -1) {
        DropFormationSlot(-1);
    }
    return UiResult::Pass;
}

/* Pick up the active cell's character: clear the cell, switch the mouse
   cursor to the dragged chip and blank both controls' sprites. */
// FUNCTION: WIZ8 0x005b2e10
static void BeginFormationDrag()
{
    SDL_Point point;
    unsigned short region;
    int sprite;

    formation->targets[g_formation_active_cell]->releaseCapture();
    SelectFormationCell();
    formation->cells[g_formation_active_cell] = {};
    g_formation_drag_slot = g_formation_cell_slots[g_formation_active_cell];
    g_formation_drag_cell = g_formation_active_cell;
    // The dragged chip follows the pointer; the drop must reach the cell under
    // it rather than the button region that captured the initial press.
    g_formation_cell_slots[g_formation_active_cell] = -1;
    SGPMouseGetPos(&point);
    ClearMouseSurface();
    sprite = gXStatus.edited_formation.positions[g_formation_drag_slot].facing * 3 + 1;
    const auto artwork = CatalogArtwork::Load(0x9f);
    region = artwork.imageOffset() + static_cast<short>(sprite);
    SetMouseCursorFromVideoObject(*artwork.spriteSheet(), region, 0x10, 0x10);
    const auto overlay_artwork = CatalogArtwork::Load(0xa0);
    const auto overlay = static_cast<std::uint16_t>(
        overlay_artwork.imageOffset() + g_status.buffers.XChar[g_formation_drag_slot].party_order_index * 3 + 1);
    BlitToMouseCursor(*overlay_artwork.spriteSheet(), overlay, 0, 0);
    WarpSystemCursor(point.x - 0x10, point.y - 0x10);
    RefreshMouseCursorTexture();
    gXStatus.iCurrentCursor = W8_CURSOR_INVALID_TARGET;
    formation->dirty = true;
}

/* Drop the dragged character: into an empty cell it re-seats the row, onto a
   movable occupant it swaps positions, onto a dead one it cancels. */
// FUNCTION: WIZ8 0x005b2f70
static void DropFormationSlot(int cell)
{
    if (cell != -1 && cell != g_formation_drag_cell) {
        if (g_formation_cell_slots[cell] == -1) {
            SetFormationPosition(&gXStatus.edited_formation, g_formation_drag_slot, -1, -1, false, true, true);
            SeatFormationSlotInRow(&gXStatus.edited_formation, g_formation_drag_slot, cell / 3);
        } else if (!CanHoldFormationPlace(g_formation_cell_slots[cell])) {
            cell = -1;
        } else {
            SwapFormationSlots(&gXStatus.edited_formation, g_formation_drag_slot, g_formation_cell_slots[cell]);
        }
    }
    UpdateHeldItemCursor();
    UpdateFormationCells();
    formation->highlighted[g_formation_drag_cell] = false;
    if (cell != -1) {
        formation->highlighted[cell] = true;
        g_formation_active_cell = cell;
        g_level_block->formation_highlight_party_slot = g_formation_cell_slots[cell];
        RequestRedraw(1 << (g_level_block->formation_highlight_party_slot & 0x1f));
    }
    g_formation_drag_cell = -1;
}

void SelectFormationSlotCell(int party_slot)
{
    for (int cell = 0; cell < 15; ++cell) {
        auto& state = formation->cells[cell];
        state.pressed = state.selected = party_slot != -1 && g_formation_cell_slots[cell] == party_slot;
    }
    formation->dirty = true;
}

// FUNCTION: WIZ8 0x005B2200
void CloseFormationPanel(void)
{
    DestroyFormationPanel();
    gXStatus.fReviewCharacterMode = false;
    UpdateHeldItemCursor();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    EraseRegion(0xd6, 0x3c, 0x1ab, 0x12f);
    ResumeMainGameWorld();
}
