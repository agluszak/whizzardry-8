#pragma once

// A location within the receiving party, never an address in another party.
enum class W8ItemStorage { None, Pool, Equipment, Backpack, Hand };
struct W8ItemLocation {
    W8ItemStorage storage = W8ItemStorage::None;
    unsigned int character = 0;
    unsigned int slot = 0;
    bool operator==(const W8ItemLocation&) const = default;
};
