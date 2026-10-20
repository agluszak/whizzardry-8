#pragma once

/* Save sections as pure functions between bytes and values. The retail records these
   sections are made of exist only inside the codec (save_codec.cpp). */

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "wiz8/layouts/game_status.h"
#include "wiz8/rules/party.h"

namespace wiz8::save {

/* What the GSTA section stores: the game's status and its party. A save snapshot is a
   copy of this value. */
struct GameState {
    GameStatus status;
    W8Party party;
};

/* The GSTA payload: the status record, each character and each party row, every record
   behind its four-byte length. */
std::vector<std::byte> encode(const GameState& state);
/* Decode a GSTA payload. Records that older versions wrote shorter are zero-extended,
   pre-1.1 message counters and pre-v2 professions are migrated, and bytes after the last
   row are ignored. Throws std::runtime_error when the payload is malformed. */
GameState decode_game_state(std::span<const std::byte> payload);

/* The STAT record of a level's status group: the game's id counters and its status
   block, which holds the turn counter. */
struct LevelStatusHeader {
    int next_group_id = 0;
    int next_monster_location_id = 0;
    int next_world_item_id = 0;
    int next_trigger_id = 0;
    unsigned int turns_elapsed = 0;
    std::array<unsigned char, 0xd4> status_prefix{};
    std::array<unsigned char, 0x28> status_suffix{};
    // Unidentified; the game writes zeros and ignores what it reads.
    std::array<unsigned char, 0x200> unknown_tail{};

    bool operator==(const LevelStatusHeader&) const = default;
};

std::vector<std::byte> encode(const LevelStatusHeader& header);
/* Throws std::runtime_error unless the payload is a complete version-2 STAT record. */
LevelStatusHeader decode_level_status_header(std::span<const std::byte> payload);

} // namespace wiz8::save
