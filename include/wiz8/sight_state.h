#pragma once


#include <cstdint>
/* Live sight and the remembered sight window are stored in one byte. */
typedef std::uint8_t W8SightState;
enum { W8_SIGHT_UNSEEN = 0, W8_SIGHT_SEEN = 1, W8_SIGHT_RECENT = 2 };
