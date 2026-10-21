#pragma once

#include <cstdint>

#include "wiz8/rules/targeting.h"

/* What a screen asked the pointer to show. A request lapses when the hand changes; without
   one, the main game and the camp show the held item and other screens the arrow. */
struct PointerRequest {
    enum class Kind { None, Slot, HeldItem, Custom };
    Kind kind = Kind::None;
    // The cursor-table slot, or the catalog overlay drawn over the held item.
    int value = -1;
    // The held item's id when the request was made.
    int hand = -1;
};

/* The mouse pointer the screens share: the image it shows and what the player is asked to
   pick a target for. The router owns it, so a camp opened over a main game, or from party
   creation without one, aims and shows the same pointer. */
class Pointer {
public:
    /* Ask for a main-game cursor-table slot; W8_CURSOR_NONE drops the request. An unchanged
       slot keeps its animation. */
    void showSlot(int cursor);
    // Show the held item under a catalog overlay (the camp's move and swap cursors).
    void showHeldItem(int overlay);
    // A screen drew its own image (a formation drag, an automap tool) into the pointer.
    void showCustom();
    // Drop whatever cursor a screen asked for.
    void reset();
    /* The main-game cursor-table slot the pointer shows: W8_CURSOR_NONE for the default
       arrow, W8_CURSOR_INVALID_TARGET for the held item or a custom image. */
    int shownSlot();
    // The current request, to restore once a dialog closes.
    PointerRequest request();
    void restore(const PointerRequest& request);
    // Bring the pointer up to date with the hand and the request; runs every frame.
    void present();
    // Step a multi-frame cursor's animation when its frame is due.
    void animate();

    // What the player is asked to pick a target for, if anything.
    W8TargetNeed targeting() const { return targeting_; }
    // Ask for a target of `need`, showing its cursor; W8_TARGET_NEED_NONE stops asking.
    void setTargeting(W8TargetNeed need);
    /* The cursor-table slot that asks for the current target need (W8_CURSOR_NONE for
       none); `alternate` picks the slot that also shows the alternative action. */
    int targetingSlot(bool alternate = false) const;

private:
    // What the pointer shows: the arrow, a cursor-table slot, the held item under an
    // overlay, or an image a screen drew itself.
    struct Look {
        PointerRequest::Kind kind = PointerRequest::Kind::None;
        int value = -1;
        int item = -1;
        bool operator==(const Look&) const = default;
    };
    Look wantedLook();
    void showLook(const Look& look);
    void applyShownSlot();
    void ask(PointerRequest request);

    PointerRequest request_;
    Look shown_;
    // A multi-frame slot's animation frame and when the next is due.
    int frame_ = 0;
    std::uint64_t next_frame_ = 0;
    W8TargetNeed targeting_ = W8_TARGET_NEED_NONE;
};

// The router's pointer, for code that does not yet receive it from its screen.
Pointer& ScreenPointer();
