#include "Font.h"
#pragma once

extern std::unique_ptr<Font> g_level_load_font; /* Font used for level-load and
                                        party-death/ending overlay text */

unsigned char PleaseWaitScreenInitialize(void);
unsigned char PleaseWaitScreenEnter(void);
void PleaseWaitScreenFrame(void);
unsigned char PleaseWaitScreenLeave(int leaving);
void UpdatePleaseWaitLoadFrame(void);
