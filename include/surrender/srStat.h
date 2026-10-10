#pragma once

/* Channel histogram summary filled by srColorSurfaceIFace::getChannelStatistics. */
#pragma pack(push, 4)
class srStat {
public:
    w8_long count;                   /* 0x00 */
    unsigned char unknown_04_[4]; /* 0x04: alignment hole, never written */
    double mean;                  /* 0x08 */
    double deviation;             /* 0x10 */
    w8_long median;                  /* 0x18 */
    w8_long min;                     /* 0x1c */
    w8_long max;                     /* 0x20 */
};
#pragma pack(pop)

