#pragma once

union SDL_Event;

namespace wiz8::ui_poc {
// Opt-in RmlUi experiment for the native main menu. No production callers.
bool active() noexcept;
bool open();
void close();
bool handle_event(const SDL_Event& event);
bool frame();
}
