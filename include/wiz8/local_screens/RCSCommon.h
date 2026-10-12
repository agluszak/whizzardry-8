#include "wiz8/ui_layer.h"
#pragma once

#include <memory>
#include <SDL3/SDL_rect.h>
#include <string_view>

#include "image.h"
#include "wiz8/catalog_artwork.h"
#include "input.h"

struct W8Region;

/* Button order differs from W8CampItemAction's byte values. */
enum W8CampItemActionButton {
    W8_CAMP_ACTION_BUTTON_NONE = -1,
    W8_CAMP_ACTION_BUTTON_IDENTIFY = 0,
    W8_CAMP_ACTION_BUTTON_MOVE = 1,
    W8_CAMP_ACTION_BUTTON_SPLIT_STACK = 2,
    W8_CAMP_ACTION_BUTTON_USE = 3,
    W8_CAMP_ACTION_BUTTON_DROP = 4,
    W8_CAMP_ACTION_BUTTON_CAST_SPELL = 5,
    W8_CAMP_ACTION_BUTTON_USE_ON_ITEM = 6,
    W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER = 7,
    W8_CAMP_ACTION_BUTTON_COUNT = 8
};

/* Attribute and resistance bars use the same base/bonus/penalty strips and
   invalidate every drawn segment. Skill bars have a different invalidation policy. */
inline void DrawCampValueBar(unsigned int value, unsigned int base, int left, int top)
{
    unsigned int filled;
    unsigned int extra;
    unsigned int missing;
    if (value < base) {
        filled = value;
        extra = 0;
        missing = base - value;
    } else {
        filled = base;
        extra = value - base;
        missing = 0;
    }
    SDL_Rect clip;
    clip.y = 0;
    clip.h = 0x1e0;
    if (filled != 0) {
        clip.x = left;
        clip.w = filled;
        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
        CatalogArtwork::Load(0x143, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, left, top);
    }
    if (extra != 0) {
        clip.x = left + filled;
        clip.w = extra;
        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
        CatalogArtwork::Load(0x143, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, left, top);
    } else if (missing != 0) {
        clip.x = left + filled;
        clip.w = missing;
        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
        CatalogArtwork::Load(0x143, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, left, top);
    }
}

void DrawCampHeader(void);
void DrawCampVitals(void);
void DrawCampHands(void);
int CreateCampButtonPanel(void);
void DestroyCampButtonPanel(void);
void RefreshCampItemActions(bool invalidate);
void SetCampItemActionMode(char mode);
void SelectCampCharacter(int slot);
/* Right-click on a camp portrait while holding an item: refuse with a notice
   for dead/insane/stoned, else try AddItemToCharacter. */
void TryGiveHeldItemToCampPortrait(int slot);

void RedrawRcsLevelUpPanel(void);
void RedrawRcsDismissPanel(void);

/* Five bottom page buttons created with the item-action strip by
   CreateCampButtonPanel (Items/Skills/...). */
/* The bottom Controls panel CreateCampButtonPanel parents
   the page and item-action strips to. */

UiResult HandleCampDismissPortrait(const UiPointerEvent& event);
UiResult HandleCampPortrait(const UiPointerEvent& event, int slot);
UiResult HandleCampCharacterHeader(const UiPointerEvent& event);
UiResult HandleCampProfessionHistory(const UiPointerEvent& event);

void CreateCampHeaderButtons();
void DestroyCampHeaderButtons();
void UpdateRcsLevelUpPanel(void);
void UpdateRcsDismissPanel(void);
void DrawRcsText(std::string_view text, int left, int top, int width, unsigned int layout_mode);
void DrawRcsBoldText(std::string_view text, int left, int top, int width, unsigned int layout_mode);
void DrawTallRcsText(std::string_view text, int left, int top, int width, unsigned int layout_mode);
/* Like DrawRcsText but the box height is caller-provided and the
   text is rendered through mprintf with the current font. */
void DrawRcsTextJustified(std::string_view text, int left, int top, int width, int height,
                          unsigned int layout_mode);
