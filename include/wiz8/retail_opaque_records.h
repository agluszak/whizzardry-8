#pragma once
#include <array>
#include <cstddef>

namespace wiz8::retail {
struct CameraPoseOpaque {
    std::array<std::byte, 20> pitch{}, yaw{};

    bool operator==(const CameraPoseOpaque&) const = default;
};
// Unidentified disk fields retained for lossless saves/database edits.
struct MonsterGroupOpaque {
    // Retail cache retained on decode; level saves replace it with the derived count.
    int active_member_count = 0;
    // Freshly spawned groups write 3; loaded groups retain their saved value.
    unsigned char unknown_2b = 3;
    unsigned char unknown_d4[0x57]{};
};
/* The GSTA status record's serialized fields that are not game state: the record version,
   the pre-1.1 message counters, the file stamp of an Iron Man save, the STAT block around
   the turn counter, and unidentified runs. */
struct GlobalStatusOpaque {
    float save_version = 0;
    std::array<std::array<unsigned int, 3>, 2> legacy_text_box_lines{};
    std::array<unsigned char, 0x55> unknown_187b{};
    std::array<unsigned char, 0xd4> status_header_prefix{};
    std::array<unsigned char, 0x28> status_header_suffix{};
    std::array<unsigned char, 0x294> unknown_2013{};
    unsigned char padding_242f{};
    std::array<unsigned char, 5> unknown_243f{};
    // The save file's creation time, masked; SaveGame stamps it into Iron Man saves.
    std::array<unsigned int, 2> save_filetime_xor{};
    std::array<unsigned char, 0xc> unknown_40c2{};
    std::array<unsigned char, 0x194> unknown_47de{};
    std::array<unsigned char, 4> unknown_497b{};
    std::array<unsigned char, 2> padding_49be{};

    bool operator==(const GlobalStatusOpaque&) const = default;
};
struct EnvironOpaque {
    unsigned char pad_06[2]{};
};
struct SpellOpaque {
    unsigned char unknown_040[4]{};
    unsigned char unknown_055{};
    unsigned char unknown_11b[4]{};
    unsigned char unknown_125{};
    unsigned char unknown_145[2]{};
};
}
