#pragma once

namespace wiz8::retail {
// Unidentified disk fields retained for lossless saves/database edits.
struct MonsterGroupOpaque {
    // Retail cache retained on decode; level saves replace it with the derived count.
    int active_member_count = 0;
    // Freshly spawned groups write 3; loaded groups retain their saved value.
    unsigned char unknown_2b = 3;
    unsigned char unknown_d4[0x57]{};
};
struct GlobalStatusOpaque {
    unsigned char padding_242f{};
    unsigned char padding_49be[2]{};
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
