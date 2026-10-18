#pragma once

struct W8Chunk;

#include <cstdint>
#include <vector>

#include <SDL3/SDL_keycode.h>

#include "compat/compiler.h"
#include "input.h"
#include "wiz8/mgs_commands.h"

namespace wiz8 {
class File;
}

struct MGSKeyBinding {
    SDL_Keycode key;
    unsigned short modifiers;
    unsigned short active;
    W8MGSCommand command;
    // Preserve an unsupported retail key until the user rebinds it.
    std::uint16_t unmapped_retail_key = 0;
};

class MGSKeyboard {
public:
    int FindBinding(W8MGSCommand command) const;
    /* Match a live InputEvent against the binding table; returns command or -1. */
    W8MGSCommand FindCommandForEvent(const InputEvent* event) const;
    MGSKeyBinding* GetBinding(int index);
    bool IsCommandPressed(W8MGSCommand command) const;
    void Clear();
    unsigned char Load(W8Chunk& handle, bool clear);
    unsigned char Save(wiz8::File& handle) const;
    unsigned char LoadDefaults(const char* path);

private:
    void SetBinding(MGSKeyBinding binding);
    std::vector<MGSKeyBinding> m_bindings;
};

extern MGSKeyboard g_mgs_keyboard;

void ResetMGSKeyboardBindings();

/* Discard every queued input atom (screen-entry stale-input flush). */
void DrainInputEventQueue(void);
/* Reset the slot's combat selection and tear down the menu panel and rows. */
void CloseKeyboardMenu(void);
/* Open the keyboard-action menu for one party slot. */
void OpenKeyboardMenuForSlot(int slot);
/* Build the panel and one row per selectable menu entry. */
unsigned char BuildKeyboardMenu(void);
/* Re-enable the menu's region set and every row region. */
void EnableKeyboardMenuInput(void);
/* Whether the cursor sits inside the menu panel rectangle. */
bool KeyboardMenuContainsCursor(void);
/* Re-evaluate every row's availability and restate its icon frames. */
void RefreshKeyboardMenuRows(void);
/* Invalidate (when asked) then redraw the menu panel. */
void RedrawKeyboardMenuPanel(bool invalidate);
/* Dispatch a bound MGS command number through the keyboard system. */
void DispatchMGSCommand(W8MGSCommand command);
/* Route one non-mouse input atom to text entry, dialogue, the trap text box,
   the record-mode console or a bound MGS command. */
unsigned char HandleMainGameInputEvent(const InputEvent* input);
/* Poll the camera/movement command bindings and set the world-render flags. */
void HandleManualCameraHotkeys(void);
void ApplyCameraMotionHotkeys(void);
