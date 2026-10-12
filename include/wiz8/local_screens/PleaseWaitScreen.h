#include <cstdint>
#include "Font.h"
#pragma once

extern Font g_level_load_font; /* Font used for level-load and
                                        party-death/ending overlay text */

std::uint8_t PleaseWaitScreenInitialize(void);
std::uint8_t PleaseWaitScreenEnter(void);
void PleaseWaitScreenFrame(void);
std::uint8_t PleaseWaitScreenLeave(std::int32_t leaving);
void UpdatePleaseWaitLoadFrame(void);
