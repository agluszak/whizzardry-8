#pragma once

struct W8Chunk;

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>

#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/mgs_commands.h"
#include "wiz8/platform/input.h"
#include "wiz8/ui_layer.h"

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
struct MainGameScreen;

class KeyboardActionMenu {
public:
    KeyboardActionMenu(MainGameScreen& screen, int slot);
    const int slot;
    bool containsCursor() const;
    void refresh();
    // Paint the panel and its rows; the HUD repaint record says when.
    void paint() const;

private:
    friend struct MainGameScreen;
    struct Row {
        int menu = -1;
        int item = -1;
        bool enabled = true;
        HitTarget* target = nullptr;
        ButtonInteraction press;
        ButtonAppearance appearance;
    };
    std::string tooltip(const Row& row) const;
    void requestRedraw();
    MainGameScreen& screen_;
    SDL_Point origin;
    std::array<Row, 13> rows;
    UiLayer input{UiLayerOrder::KeyboardMenu};
};
