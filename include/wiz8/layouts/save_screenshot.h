#pragma once

/* The SHOT record. SaveGame writes 0x2588 bytes; the save-screen producer
   copy-constructs one complete record, including its trailing padding.
   The 80 by 60 16-bit surface begins at offset 6. */
struct W8SaveScreenshot {
    float version;
    unsigned char capture_result;
    unsigned char padding_05;
    unsigned short pixels[60][80];
};
