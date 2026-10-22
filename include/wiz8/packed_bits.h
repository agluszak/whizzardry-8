#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wiz8 {
class File;
}

/* Out-of-range bits read as clear and writes leave the set unchanged. */
inline bool TestBit(const std::vector<bool>& bits, std::size_t bit)
{
    return bit < bits.size() && bits[bit];
}

/* Set a bit and report whether it was already set. */
inline bool SetBit(std::vector<bool>& bits, std::size_t bit)
{
    if (bit >= bits.size()) {
        return false;
    }
    bool was_set = bits[bit];
    bits[bit] = true;
    return was_set;
}

inline void ClearBit(std::vector<bool>& bits, std::size_t bit)
{
    if (bit < bits.size()) {
        bits[bit] = false;
    }
}

/* Retail octree mesh-alpha/prop-sunlight flags and saved automap visited cells.
   The Huffman-packed bit set stores magic
   0xDEADD00D, the bit count, the packed size, the packed bit_count / 32 + 1
   words and the magic again. A failed load leaves `bits` unchanged. */
inline constexpr std::uint32_t kPackedBitsMagic = 0xdeadd00d;
bool LoadPackedBits(wiz8::File& file, std::vector<bool>& bits);
void SavePackedBits(wiz8::File& file, const std::vector<bool>& bits);

struct W8Chunk;
bool LoadPackedBits(W8Chunk& chunk, std::vector<bool>& bits);
