#include "wiz8/pointer.h"

#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "wiz8/game.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"

namespace {
int HeldItemId()
{
    return GameInProgress() ? CurrentGame().party.held.item.iItemNo : -1;
}

} // namespace

/* The look the pointer should have. A request made while the hand held something else has
   lapsed; without one, the main game and the camp show the held item. */
Pointer::Look Pointer::wantedLook()
{
    const int hand = HeldItemId();
    if (request_.hand != hand) request_ = {};
    using Kind = PointerRequest::Kind;
    switch (request_.kind) {
    case Kind::Slot:
    case Kind::Custom:
        return {.kind = request_.kind, .value = request_.value};
    case Kind::HeldItem:
        if (hand != -1) return {.kind = Kind::HeldItem, .value = request_.value, .item = hand};
        return {};
    case Kind::None:
        break;
    }
    const auto screen = CurrentScreenState().id;
    if (hand != -1 && (screen == W8_SCREEN_MAIN_GAME || screen == W8_SCREEN_CAMP))
        return {.kind = Kind::HeldItem, .value = 0, .item = hand};
    return {};
}

/* Install the shown slot from the main-game resource table: resize, install the slot's
   texture, select the current frame and refresh the hotspot. A multi-frame cursor arms its
   animation. */
void Pointer::applyShownSlot()
{
    auto& slot = g_main_game_resource_slots[shown_.value];
    if (slot.object != 0) {
        ResizeMouseCursorSurface(slot.size_x, slot.size_y);
        SetMouseCursorTexture(slot.object);
        slot.object->SetFrame(frame_);
        SetMouseCursorHotspot(slot.hotspot_x, slot.hotspot_y);
    }
    if (slot.frame_count > 1) next_frame_ = MonotonicMilliseconds() + 0xfa;
}

void Pointer::showLook(const Look& look)
{
    shown_ = look;
    frame_ = 0;
    switch (look.kind) {
    case PointerRequest::Kind::None: {
        const auto cursor = CatalogArtwork::Load(0);
        SetMouseCursorFromVideoObject(*cursor.spriteSheet(), cursor.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        break;
    }
    case PointerRequest::Kind::Slot:
        applyShownSlot();
        break;
    case PointerRequest::Kind::HeldItem: {
        const auto sprites = ItemSprites(look.item);
        SetMouseCursorFromVideoObject(*sprites, 0, 0, 0);
        const auto overlay = CatalogArtwork::Load(look.value);
        BlitToMouseCursor(*overlay.spriteSheet(), overlay.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        break;
    }
    case PointerRequest::Kind::Custom:
        // The screen drew the image itself.
        break;
    }
}

void Pointer::ask(PointerRequest request)
{
    request.hand = HeldItemId();
    request_ = request;
    present();
}

void Pointer::present()
{
    if (const auto look = wantedLook(); look != shown_) showLook(look);
}

int Pointer::shownSlot()
{
    const auto look = wantedLook();
    switch (look.kind) {
    case PointerRequest::Kind::None:
        return W8_CURSOR_NONE;
    case PointerRequest::Kind::Slot:
        return look.value;
    case PointerRequest::Kind::HeldItem:
    case PointerRequest::Kind::Custom:
        break;
    }
    return W8_CURSOR_INVALID_TARGET;
}

PointerRequest Pointer::request()
{
    wantedLook();
    return request_;
}

void Pointer::restore(const PointerRequest& request)
{
    request_ = request;
    present();
}

void Pointer::showCustom()
{
    ask({.kind = PointerRequest::Kind::Custom});
}

void Pointer::showHeldItem(int overlay)
{
    ask({.kind = PointerRequest::Kind::HeldItem, .value = overlay});
}

void Pointer::animate()
{
    if (shown_.kind != PointerRequest::Kind::Slot) return;
    const auto frame_count = static_cast<int>(g_main_game_resource_slots[shown_.value].frame_count);
    if (frame_count <= 1 || TimeUntilDeadline(next_frame_)) return;
    if (++frame_ == frame_count) frame_ = 0;
    applyShownSlot();
}

void Pointer::showSlot(int cursor)
{
    if (cursor == W8_CURSOR_NONE) {
        reset();
        return;
    }
    ask({.kind = PointerRequest::Kind::Slot, .value = cursor});
}

void Pointer::reset()
{
    request_ = {};
    present();
}

// FUNCTION: WIZ8 0x0053A3D0
int Pointer::targetingSlot(bool alternate) const
{
    switch (targeting_) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        return alternate + 3;
    case W8_TARGET_NEED_ENEMY:
        return alternate;
    case W8_TARGET_NEED_GROUP:
        return alternate ? 10 : 11;
    case W8_TARGET_NEED_PLACE:
        return 2;
    case W8_TARGET_NEED_CONE:
        return 12;
    default:
        return W8_CURSOR_NONE;
    }
}

void Pointer::setTargeting(W8TargetNeed need)
{
    targeting_ = need;
    if (const int cursor = targetingSlot(); cursor != shownSlot()) showSlot(cursor);
}
