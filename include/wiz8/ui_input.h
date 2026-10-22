#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

#include "wiz8/platform/input.h"
#include "wiz8/ui_layer.h"

inline constexpr int UI_PRIORITY_BACKGROUND = -1;
inline constexpr int UI_PRIORITY_LOWEST = 0;
inline constexpr int UI_PRIORITY_BASE = 31;
inline constexpr int UI_PRIORITY_NORMAL = UI_PRIORITY_BASE;
inline constexpr int UI_PRIORITY_HIGHEST = 127;

struct UiRoutingState {
    HitTarget *hover = nullptr, *captured = nullptr;
    SDL_Point pointer{};
    void ClearPointer() { hover = captured = nullptr; }
};
extern UiRoutingState g_ui;
extern int g_ui_help_delay;
extern std::uint64_t g_ui_help_clock;
extern bool g_ui_help_forced;

HitTarget* UiTargetAt(SDL_Point point);
void UpdateUiPointer(int x, int y);
bool DispatchUiInput(const InputEvent* event);
/* Make `owner`'s layers the ones that receive input and drop pointer hover,
   capture and help state. Other owners' layers keep their own state. */
void SelectUiInputOwner(int owner);
int UiInputOwner();
void ClearUiHover();
bool UiModalActive();
void ReleaseUiCapture();
std::size_t LiveUiTargetCount();
std::size_t LiveUiLayerCount();
bool InitializeUiHelp();
void UpdateUiHelp();
void ResetUiHelp(bool delayed);
void SetUiHelpDelay(int delay_ms);

enum class InputRoute : unsigned char { Pass, Handled, Stop };

/* How a screen or modal routes its frame's input. Each queued event is
   offered once: `before` may claim it ahead of the UI (gameplay pointer
   capture, a focused text field); otherwise the active input layers (the
   innermost modal scope, else the screen) dispatches it, then `consumed`
   or `unhandled` applies the caller's policy.
   Pass and Handled continue draining; Stop leaves the rest for next frame. */
struct UiInputPolicy {
    std::function<InputRoute(const InputEvent&)> before;
    std::function<InputRoute(const InputEvent&)> consumed;
    std::function<InputRoute(const InputEvent&)> unhandled;
    /* Refresh pointer hover from the current mouse position before draining. */
    bool track_hover = true;
};

/* The single input-queue drain. Returns whether any event was dequeued.
   Deferred structural changes commit after the current policy callback and
   stop draining, leaving the remaining events for the next screen/frame. */
bool PumpUiInput(const UiInputPolicy& policy);
