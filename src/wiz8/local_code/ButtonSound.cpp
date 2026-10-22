#include "wiz8/local_code/ButtonSound.h"

#include <cstdint>

#include "wiz8/audio/soundman.h"
#include "wiz8/platform/timer.h"
#include "wiz8/ui_layer.h"

char g_button_click_1[] = "Data\\Sound\\Misc\\Interface Click 01.wav";
static uint64_t g_button_sound_cooldown;

void PlayButtonSound(UiPointerAction action)
{
    const char* path;
    switch (action) {
    case UiPointerAction::Enter:
        if (TimeUntilDeadline(g_button_sound_cooldown) != 0) return;
        g_button_sound_cooldown = MonotonicMilliseconds() + 200;
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
    wiz8::sound::play(path, {.gain_scale = 0.5f});
}
