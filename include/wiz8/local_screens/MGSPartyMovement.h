#pragma once

#include <memory>
#include <cstdint>

#include "input.h"

struct Controls;
struct W8Region;
class W8TextBuffer;
class W8TextControl;
class ScreenRegionSet;

extern W8TextControl* g_free_turn_button;
extern W8TextControl* g_cancel_party_movement_button;
extern std::uint64_t g_party_movement_animation_clock;
extern std::unique_ptr<Controls> g_party_movement_panel;
extern std::unique_ptr<W8TextBuffer> g_party_movement_caption;
extern std::uint32_t g_party_movement_animation_frame;

/* Creates the combat party-movement panel and its two buttons; returns zero
   when an allocation fails. */
std::uint8_t CreatePartyMovementPanel(void);
extern ScreenRegionSet g_party_movement_regions;
/* Releases the party-movement panels and clears the
   combat-UI teardown flag; called when the party regains movement. */
void ReleasePartyMovement(void);
void UpdatePartyMovementPanel(void);
void DrawPartyMovementPanel(void);
void DisablePartyMovementRegions(void);
/* Per-frame movement/fatigue processing; the callers reuse unrelated storage
   for the two elapsed-time outputs. */
std::uint8_t HandlePartyMovement(float* real_elapsed, float* frame_elapsed);

void InvalidatePartyMovementPanel(void);
/* Free-turn / cancel-party-movement button region callback (ids 0 and 1). */
std::uint8_t FreeTurnButtonRegionEvent(const InputEvent* event, W8Region* region);
void DisableFreeTurnButton(void);
void EnableFreeTurnButton(void);
