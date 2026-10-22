#include "wiz8/key_bindings.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include <SDL3/SDL_keycode.h>

#include "wiz8/platform/input.h"

namespace {
struct KeyCharacters {
    SDL_Keycode key;
    char plain;
    char shifted;
};

/* US layout, matching retail's 0x005ffc3c table for every non-letter key. */
constexpr KeyCharacters key_characters[] = {
    {SDLK_KP_MULTIPLY, '*', '*'}, {SDLK_KP_PLUS, '+', '+'},    {SDLK_KP_MINUS, '-', '-'},
    {SDLK_KP_PERIOD, '.', '.'},   {SDLK_KP_DIVIDE, '/', '/'},  {SDLK_SEMICOLON, ';', ':'},
    {SDLK_EQUALS, '=', '+'},      {SDLK_COMMA, ',', '<'},      {SDLK_MINUS, '-', '_'},
    {SDLK_PERIOD, '.', '>'},      {SDLK_SLASH, '/', '?'},      {SDLK_GRAVE, '`', '~'},
    {SDLK_LEFTBRACKET, '[', '{'}, {SDLK_BACKSLASH, '\\', '|'}, {SDLK_RIGHTBRACKET, ']', '}'},
    {SDLK_APOSTROPHE, '\'', '"'}};

std::uint16_t BindingCharacter(SDL_Keycode key, std::uint16_t modifiers)
{
    if (modifiers & (CTRL_DOWN | ALT_DOWN)) return 0;
    const bool shift = (modifiers & SHIFT_DOWN) != 0;
    if (key == SDLK_SPACE) return ' ';
    if (key >= SDLK_A && key <= SDLK_Z)
        return shift ? std::uint16_t(key - SDLK_A + 'A') : std::uint16_t(key);
    if (key >= '0' && key <= '9') return shift ? std::uint16_t(")!@#$%^&*("[key - '0']) : key;
    if (key == SDLK_KP_0) return '0';
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return std::uint16_t('1' + key - SDLK_KP_1);
    for (const KeyCharacters& entry : key_characters)
        if (entry.key == key) return std::uint16_t(shift ? entry.shifted : entry.plain);
    return 0;
}

struct BindingKey {
    std::uint16_t retail;
    SDL_Keycode native;
    std::string_view name{};
};

constexpr BindingKey binding_keys[] = {
    {8, SDLK_BACKSPACE, "BACKSPACE"},
    {9, SDLK_TAB, "TAB"},
    {13, SDLK_RETURN, "ENTER"},
    {16, SDLK_LSHIFT},
    {17, SDLK_LCTRL},
    {18, SDLK_LALT},
    {19, SDLK_PAUSE, "PAUSE"},
    {20, SDLK_CAPSLOCK},
    {27, SDLK_ESCAPE, "ESC"},
    {32, SDLK_SPACE, "SPACE"},
    {33, SDLK_PAGEUP, "PGUP"},
    {34, SDLK_PAGEDOWN, "PGDN"},
    {35, SDLK_END, "KEY_END"},
    {36, SDLK_HOME, "HOME"},
    {37, SDLK_LEFT, "LEFTARROW"},
    {38, SDLK_UP, "UPARROW"},
    {39, SDLK_RIGHT, "RIGHTARROW"},
    {40, SDLK_DOWN, "DNARROW"},
    {44, SDLK_PRINTSCREEN},
    {45, SDLK_INSERT, "INSERT"},
    {46, SDLK_DELETE, "DEL"},
    {48, SDLK_0},
    {49, SDLK_1},
    {50, SDLK_2},
    {51, SDLK_3},
    {52, SDLK_4},
    {53, SDLK_5},
    {54, SDLK_6},
    {55, SDLK_7},
    {56, SDLK_8},
    {57, SDLK_9},
    {65, SDLK_A},
    {66, SDLK_B},
    {67, SDLK_C},
    {68, SDLK_D},
    {69, SDLK_E},
    {70, SDLK_F},
    {71, SDLK_G},
    {72, SDLK_H},
    {73, SDLK_I},
    {74, SDLK_J},
    {75, SDLK_K},
    {76, SDLK_L},
    {77, SDLK_M},
    {78, SDLK_N},
    {79, SDLK_O},
    {80, SDLK_P},
    {81, SDLK_Q},
    {82, SDLK_R},
    {83, SDLK_S},
    {84, SDLK_T},
    {85, SDLK_U},
    {86, SDLK_V},
    {87, SDLK_W},
    {88, SDLK_X},
    {89, SDLK_Y},
    {90, SDLK_Z},
    {96, SDLK_KP_0, "NUM_0"},
    {97, SDLK_KP_1, "NUM_1"},
    {98, SDLK_KP_2, "NUM_2"},
    {99, SDLK_KP_3, "NUM_3"},
    {100, SDLK_KP_4, "NUM_4"},
    {101, SDLK_KP_5, "NUM_5"},
    {102, SDLK_KP_6, "NUM_6"},
    {103, SDLK_KP_7, "NUM_7"},
    {104, SDLK_KP_8, "NUM_8"},
    {105, SDLK_KP_9, "NUM_9"},
    {106, SDLK_KP_MULTIPLY, "NUM_TIMES"},
    {107, SDLK_KP_PLUS, "NUM_PLUS"},
    {108, SDLK_KP_ENTER, "NUM_ENTER"},
    {109, SDLK_KP_MINUS, "NUM_MINUS"},
    {110, SDLK_KP_PERIOD, "NUM_PERIOD"},
    {111, SDLK_KP_DIVIDE, "NUM_SLASH"},
    {112, SDLK_F1, "F1"},
    {113, SDLK_F2, "F2"},
    {114, SDLK_F3, "F3"},
    {115, SDLK_F4, "F4"},
    {116, SDLK_F5, "F5"},
    {117, SDLK_F6, "F6"},
    {118, SDLK_F7, "F7"},
    {119, SDLK_F8, "F8"},
    {120, SDLK_F9, "F9"},
    {121, SDLK_F10, "F10"},
    {122, SDLK_F11, "F11"},
    {123, SDLK_F12, "F12"},
    {124, SDLK_F13},
    {125, SDLK_F14},
    {126, SDLK_F15},
    {127, SDLK_F16},
    {128, SDLK_F17},
    {129, SDLK_F18},
    {130, SDLK_F19},
    {131, SDLK_F20},
    {132, SDLK_F21},
    {133, SDLK_F22},
    {134, SDLK_F23},
    {135, SDLK_F24},
    {144, SDLK_NUMLOCKCLEAR, "NUM_LOCK"},
    {145, SDLK_SCROLLLOCK, "SCRL_LOCK"},
    {186, SDLK_SEMICOLON},
    {187, SDLK_EQUALS},
    {188, SDLK_COMMA},
    {189, SDLK_MINUS, "-"},
    {190, SDLK_PERIOD, "."},
    {191, SDLK_SLASH, "/"},
    {192, SDLK_GRAVE},
    {219, SDLK_LEFTBRACKET},
    {220, SDLK_BACKSLASH},
    {221, SDLK_RIGHTBRACKET},
    {222, SDLK_APOSTROPHE},
};
} // namespace

SDL_Keycode KeyFromRetailBinding(std::uint16_t key)
{
    for (const auto& entry : binding_keys)
        if (entry.retail == key) return entry.native;
    return SDLK_UNKNOWN;
}

std::uint16_t KeyToRetailBinding(SDL_Keycode key)
{
    for (const auto& entry : binding_keys)
        if (entry.native == key) return entry.retail;
    return 0;
}

SDL_Keycode KeyFromBindingName(std::string_view name)
{
    for (const auto& entry : binding_keys)
        if (!entry.name.empty() && entry.name == name) return entry.native;
    if (name.size() == 1) {
        const auto character = static_cast<unsigned char>(name[0]);
        // Retail chooses the lowest virtual key, unshifted first.
        for (std::uint16_t modifiers : {std::uint16_t(0), SHIFT_DOWN})
            for (const auto& entry : binding_keys)
                if (BindingCharacter(entry.native, modifiers) == character) return entry.native;
    }
    return SDLK_UNKNOWN;
}
