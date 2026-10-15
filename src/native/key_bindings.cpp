#include "wiz8/key_bindings.h"

#include <initializer_list>

#include "input.h"

namespace {
struct KeyName {
    std::string_view name;
    SDL_Keycode key;
};

// GLOBAL: WIZ8 0x00647770
constexpr KeyName key_names[] = {{"F1", SDLK_F1},
                                 {"F2", SDLK_F2},
                                 {"F3", SDLK_F3},
                                 {"F4", SDLK_F4},
                                 {"F5", SDLK_F5},
                                 {"F6", SDLK_F6},
                                 {"F7", SDLK_F7},
                                 {"F8", SDLK_F8},
                                 {"F9", SDLK_F9},
                                 {"F10", SDLK_F10},
                                 {"F11", SDLK_F11},
                                 {"F12", SDLK_F12},
                                 {"ESC", SDLK_ESCAPE},
                                 {"TAB", SDLK_TAB},
                                 {"PAUSE", SDLK_PAUSE},
                                 {"NUM_LOCK", SDLK_NUMLOCKCLEAR},
                                 {"BACKSPACE", SDLK_BACKSPACE},
                                 {"INSERT", SDLK_INSERT},
                                 {"DEL", SDLK_DELETE},
                                 {"KEY_END", SDLK_END},
                                 {"PGDN", SDLK_PAGEDOWN},
                                 {"PGUP", SDLK_PAGEUP},
                                 {"HOME", SDLK_HOME},
                                 {"ENTER", SDLK_RETURN},
                                 {"SPACE", SDLK_SPACE},
                                 {"DNARROW", SDLK_DOWN},
                                 {"LEFTARROW", SDLK_LEFT},
                                 {"RIGHTARROW", SDLK_RIGHT},
                                 {"UPARROW", SDLK_UP},
                                 {"NUM_0", SDLK_KP_0},
                                 {"NUM_1", SDLK_KP_1},
                                 {"NUM_2", SDLK_KP_2},
                                 {"NUM_3", SDLK_KP_3},
                                 {"NUM_4", SDLK_KP_4},
                                 {"NUM_5", SDLK_KP_5},
                                 {"NUM_6", SDLK_KP_6},
                                 {"NUM_7", SDLK_KP_7},
                                 {"NUM_8", SDLK_KP_8},
                                 {"NUM_9", SDLK_KP_9},
                                 {"NUM_TIMES", SDLK_KP_MULTIPLY},
                                 {"NUM_PLUS", SDLK_KP_PLUS},
                                 {"NUM_ENTER", SDLK_KP_ENTER},
                                 {"NUM_MINUS", SDLK_KP_MINUS},
                                 {"NUM_PERIOD", SDLK_KP_PERIOD},
                                 {"NUM_SLASH", SDLK_KP_DIVIDE},
                                 {"SCRL_LOCK", SDLK_SCROLLLOCK},
                                 {"/", SDLK_SLASH},
                                 {"-", SDLK_MINUS},
                                 {".", SDLK_PERIOD}};

constexpr struct {
    std::uint16_t retail;
    SDL_Keycode native;
} binding_keys[] = {{8, SDLK_BACKSPACE},
                    {9, SDLK_TAB},
                    {13, SDLK_RETURN},
                    {16, SDLK_LSHIFT},
                    {17, SDLK_LCTRL},
                    {18, SDLK_LALT},
                    {19, SDLK_PAUSE},
                    {20, SDLK_CAPSLOCK},
                    {27, SDLK_ESCAPE},
                    {32, SDLK_SPACE},
                    {33, SDLK_PAGEUP},
                    {34, SDLK_PAGEDOWN},
                    {35, SDLK_END},
                    {36, SDLK_HOME},
                    {37, SDLK_LEFT},
                    {38, SDLK_UP},
                    {39, SDLK_RIGHT},
                    {40, SDLK_DOWN},
                    {44, SDLK_PRINTSCREEN},
                    {45, SDLK_INSERT},
                    {46, SDLK_DELETE},
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
                    {96, SDLK_KP_0},
                    {97, SDLK_KP_1},
                    {98, SDLK_KP_2},
                    {99, SDLK_KP_3},
                    {100, SDLK_KP_4},
                    {101, SDLK_KP_5},
                    {102, SDLK_KP_6},
                    {103, SDLK_KP_7},
                    {104, SDLK_KP_8},
                    {105, SDLK_KP_9},
                    {106, SDLK_KP_MULTIPLY},
                    {107, SDLK_KP_PLUS},
                    {108, SDLK_KP_ENTER},
                    {109, SDLK_KP_MINUS},
                    {110, SDLK_KP_PERIOD},
                    {111, SDLK_KP_DIVIDE},
                    {112, SDLK_F1},
                    {113, SDLK_F2},
                    {114, SDLK_F3},
                    {115, SDLK_F4},
                    {116, SDLK_F5},
                    {117, SDLK_F6},
                    {118, SDLK_F7},
                    {119, SDLK_F8},
                    {120, SDLK_F9},
                    {121, SDLK_F10},
                    {122, SDLK_F11},
                    {123, SDLK_F12},
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
                    {144, SDLK_NUMLOCKCLEAR},
                    {145, SDLK_SCROLLLOCK},
                    {186, SDLK_SEMICOLON},
                    {187, SDLK_EQUALS},
                    {188, SDLK_COMMA},
                    {189, SDLK_MINUS},
                    {190, SDLK_PERIOD},
                    {191, SDLK_SLASH},
                    {192, SDLK_GRAVE},
                    {219, SDLK_LEFTBRACKET},
                    {220, SDLK_BACKSLASH},
                    {221, SDLK_RIGHTBRACKET},
                    {222, SDLK_APOSTROPHE}};
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
    for (const auto& entry : key_names)
        if (entry.name == name) return entry.key;
    if (name.size() == 1) {
        const auto character = static_cast<unsigned char>(name[0]);
        // Retail chooses the lowest virtual key, unshifted first.
        for (std::uint16_t modifiers : {std::uint16_t(0), SHIFT_DOWN})
            for (const auto& entry : binding_keys)
                if (TranslateKeyToCharacter(entry.native, modifiers) == character)
                    return entry.native;
    }
    return SDLK_UNKNOWN;
}
