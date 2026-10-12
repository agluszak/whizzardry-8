#pragma once


#include <cstdint>
/* Four-byte packed color, stored B,G,R,A (the little-endian 0xAARRGGBB dword). e_index is the
   logical ARGB channel, not a memory offset. */
class srARGB {
public:
    enum e_index { INDEX_ALPHA = 0, INDEX_RED = 1, INDEX_GREEN = 2, INDEX_BLUE = 3 };

    srARGB() {}

    std::uint8_t blue;
    std::uint8_t green;
    std::uint8_t red;
    std::uint8_t alpha;
};

