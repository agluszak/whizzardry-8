#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include "input.h"
#include "wiz8/ui_layer.h"

inline constexpr int REGION_PRIORITY_BACKGROUND = -1;
inline constexpr int REGION_PRIORITY_LOWEST = 0;
inline constexpr int REGION_PRIORITY_LOW = 15;
inline constexpr int REGION_PRIORITY_BASE = 31;
inline constexpr int REGION_PRIORITY_NORMAL = REGION_PRIORITY_BASE;
inline constexpr int REGION_PRIORITY_HIGH = 63;
inline constexpr int REGION_PRIORITY_HIGHEST = 127;

extern bool g_dev_mode;
class TextFieldGroup;
struct UiRoutingState {
    HitTarget *current = nullptr, *hover = nullptr, *captured = nullptr;
    SDL_Point pointer{};
    TextFieldGroup* focus = nullptr;
    void ClearPointer() { current = hover = captured = nullptr; }
};
extern UiRoutingState g_ui;
extern int g_region_help_delay;
extern std::uint64_t g_region_help_clock;
extern bool g_region_help_force_enabled;

HitTarget* UiTargetAt(SDL_Point point);
void UpdateRegionMousePosition(int x, int y);
SDL_Point GetRegionMousePosition();
bool DispatchRegionInput(const InputEvent* event);
void ResetRegions();
void ClearHotRegion();
void CaptureInputSink();
void ReleaseInputSinkCapture();
bool InputSinkCaptured();
void ReleaseUiCapture();
std::size_t LiveDynamicRegionCount();
std::size_t LiveRegionSetCount();
unsigned char InitializeRegionHelpState();
void UpdateRegionHelp();
void ResetRegionHelp(bool delayed);
void SetRegionHelpDelay(int delay_ms);
void SetRegionHelpForceEnabled(bool enabled);

enum class InputRoute { Pass, Handled, Stop };

/* How a screen or modal routes its frame's input. Each queued event is
   offered once: `before` may claim it ahead of the UI (gameplay pointer
   capture, a focused text field); otherwise the active region tree (the
   innermost modal scope, else the screen) dispatches it when `ui_accepts`
   allows, and `consumed` or `unhandled` applies the caller's policy.
   Pass and Handled continue draining; Stop leaves the rest for next frame. */
struct UiInputPolicy {
    std::function<InputRoute(const InputEvent&)> before;
    std::function<bool(const InputEvent&)> ui_accepts;
    std::function<InputRoute(const InputEvent&)> consumed;
    std::function<InputRoute(const InputEvent&)> unhandled;
    /* Refresh pointer hover from the current mouse position before draining. */
    bool track_hover = true;
};

/* The single input-queue drain. Returns whether any event was dequeued.
   Deferred structural changes commit after the current policy callback and
   stop draining, leaving the remaining events for the next screen/frame. */
bool PumpUiInput(const UiInputPolicy& policy);

