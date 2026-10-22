#ifndef WIZ8_LOCAL_CODE_BUTTON_SOUND_H
#define WIZ8_LOCAL_CODE_BUTTON_SOUND_H

extern char g_button_click_1[39];

enum class UiPointerAction : unsigned char;
void PlayButtonSound(UiPointerAction action);

#endif
