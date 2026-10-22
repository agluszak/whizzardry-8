#pragma once

#include <chrono>
#include <cstdint>

struct SDL_Surface;
struct SpriteDraw;

/* Animation state belongs to the portrait being shown, independently of party membership. */
struct W8PortraitAnimation {
    int rendered_frame = -1;
    int frame = 6;
    // When the next mouth frame is due, on the clock its caller passes to AdvancePortraitSpeech.
    std::chrono::milliseconds frame_due{};
    std::uint64_t voice_remaining_ms = 0;
    int rendered_pose = -1;
    int pose = 1;
    int target_pose = 1;
    uint64_t pose_clock = 0;
    uint64_t idle_clock = 0;
    bool blink_closing = false;
    bool pose_dirty = false;
    bool frame_dirty = false;
};

struct W8PortraitCondition {
    bool dead = false;
    bool surprised = false;
    bool missing = false;
    bool zero_hp = false;
};

/* Flicker the mouth while the voice plays. `now` is the caller's clock: the game's step
   time for the party, the monotonic clock for a dialog outside the game's step. */
void AdvancePortraitSpeech(W8PortraitAnimation& state, std::chrono::milliseconds now,
                           bool mouth_open, bool fixed_frame);
void AdvancePortraitPose(W8PortraitAnimation& state);
bool BlitPortraitAnimation(SDL_Surface& target, int portrait, int left, int top, SpriteDraw options,
                           W8PortraitAnimation& state, bool animate,
                           W8PortraitCondition condition = {});
