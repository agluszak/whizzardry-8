#pragma once

#include "wiz8/filesystem.h"
#include "wiz8/layouts/screen_state.h"

#include "input.h"
#include "wiz8/vector.h"

struct W8CreditLine {
    unsigned int flags;
    int pixel_width;
    int line_height;
    char* primary;
    char* secondary;
};
W8_ABI_ASSERT(sizeof(W8CreditLine) == 0x14, "W8CreditLine_size");

unsigned char ReadRetailTextLine(wiz8::File* handle, char* destination, int capacity, unsigned char* more);
unsigned char CreditsScreenEnter(void);
void CreditsScreenFrame(void);
unsigned char CreditsScreenLeave(int leaving);
/* Full-screen dismiss on left-up or right-up. */
unsigned char CreditsBackgroundRegionEvent(const InputAtom* event, struct W8Region* region);
