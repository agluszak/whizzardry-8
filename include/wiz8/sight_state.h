#pragma once

/* Live sight and the remembered sight window are stored in one byte. */
enum W8SightState : unsigned char { W8_SIGHT_UNSEEN = 0, W8_SIGHT_SEEN = 1, W8_SIGHT_RECENT = 2 };
