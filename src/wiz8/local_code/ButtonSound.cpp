#include "wiz8/local_code/ButtonSound.h"

#include <cstdint>

#include "compat/native.h"
#include "soundman.h"
#include "timer.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/ui_layer.h"

// GLOBAL: WIZ8 0x0062A51C
char g_button_click_1[] = "Data\\Sound\\Misc\\Interface Click 01.wav";
// GLOBAL: WIZ8 0x0068DE3C
static uint64_t g_button_sound_cooldown;

void PlayButtonSound(UiPointerAction action)
{
    const char* path;
    switch (action) {
    case UiPointerAction::Enter:
        if (TimeUntilDeadline(g_button_sound_cooldown) != 0) return;
        g_button_sound_cooldown = w8_get_ticks() + 200;
        path = "Data\\Sound\\Misc\\Interface Whoosh 01 Soft.wav";
        break;
    case UiPointerAction::Down:
        path = "Data\\Sound\\Misc\\Interface Click 02.wav";
        break;
    case UiPointerAction::Up:
        path = g_button_click_1;
        break;
    default:
        return;
    }
    wiz8::sound::play(path, {.volume = std::uint32_t(g_settings.sound_effects_volume >> 1)});
}
