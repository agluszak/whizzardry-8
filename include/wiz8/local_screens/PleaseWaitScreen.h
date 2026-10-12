#include <cstdint>
#include "Font.h"
#pragma once

extern Font g_level_load_font; /* Font used for level-load and
                                        party-death/ending overlay text */

bool PleaseWaitScreenInitialize(void);
bool PleaseWaitScreenEnter(void);
void PleaseWaitScreenFrame(void);
bool PleaseWaitScreenLeave(std::int32_t leaving);
void UpdatePleaseWaitLoadFrame(void);
