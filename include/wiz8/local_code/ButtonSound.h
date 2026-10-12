#ifndef WIZ8_LOCAL_CODE_BUTTON_SOUND_H
#define WIZ8_LOCAL_CODE_BUTTON_SOUND_H


#include <cstdint>
extern char g_button_click_1[39];
extern char g_button_click_2[39];
extern char g_button_whoosh[45];

void PlayButtonSound(std::int32_t sound_id);
void PushButtonSoundScheme(std::int32_t scheme, bool replace_current);
void ResetButtonSoundScheme(void);

#endif
