#pragma once

/* Save sections as pure functions between bytes and values. The retail records these
   sections are made of exist only inside the codec (save_codec.cpp). */

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/message_log.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/party.h"
#include "wiz8/story.h"

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

/* The TEXT section: each text box's used lines, oldest first. A line that is still shown
   carries the milliseconds its countdown has left in `saved_remaining_ms`. */
using MessageBoxes = std::array<std::vector<W8MessageStorageRecord>, 4>;
std::vector<std::byte> encode(const MessageBoxes& boxes);
/* Status records older than version 1.1 had three boxes; the fourth decodes empty. Word
   ranges are not saved, so lines decode without them. Throws std::runtime_error when the
   payload is malformed. */
MessageBoxes decode_message_boxes(std::span<const std::byte> payload, float status_version);

// The TVAR section: the level scripts' named variables.
std::vector<std::byte> encode(std::span<const W8LocationVariable> variables);
std::vector<W8LocationVariable> decode_location_variables(std::span<const std::byte> payload);

// The NPCI section: the keywords the party has heard and how the transcript lists them.
struct DialogueTranscript {
    struct View {
        W8DialogueCategory category_filter = W8_DIALOGUE_CATEGORY_ALL;
        bool sorted = false;
    };
    std::vector<W8DialogueTranscriptRecord> keywords;
    // Absent in version-1 transcripts; written as the defaults when absent.
    std::optional<View> view;
};
std::vector<std::byte> encode(const DialogueTranscript& transcript);
DialogueTranscript decode_dialogue_transcript(std::span<const std::byte> payload);

/* The NPCT section: every NPC state with its character and stock. A state names its
   database record in `name_style`; zero names the record at the state's own index. */
std::vector<std::byte> encode(std::span<const std::unique_ptr<W8NpcState>> npcs);
/* Rejects a state naming a record at or past `npc_record_count`. Decoded states are not
   bound to their records or to the level. */
std::vector<std::unique_ptr<W8NpcState>> decode_npc_states(std::span<const std::byte> payload,
                                                           std::size_t npc_record_count);

// The NPCF section: the story's fact bytes.
using Facts = decltype(Story::facts);
std::vector<std::byte> encode(const Facts& facts);
Facts decode_facts(std::span<const std::byte> payload);

// The FATA section: the relations between factions and each faction's standing.
struct Factions {
    W8FactionRelations relations{};
    W8FactionStates states{};
};
std::vector<std::byte> encode(const Factions& factions);
Factions decode_factions(std::span<const std::byte> payload);

// The JRNL section: the journal's fact changes, oldest first.
std::vector<std::byte> encode(std::span<const W8JournalEntry> journal);
std::vector<W8JournalEntry> decode_journal(std::span<const std::byte> payload);

/* The HYPN section: the sustained monster-control effect. Its pointer lists and result are
   rebuilt while it runs, not saved. */
std::vector<std::byte> encode(const W8SpellEffectEntry& monster_control);
W8SpellEffectEntry decode_monster_control(std::span<const std::byte> payload);

/* A CHAR section: a character the game keeps outside the party under its file name, which
   is shorter than 64 bytes. */
struct StoredCharacter {
    std::string file_name;
    W8Character character{};
};
std::vector<std::byte> encode(const StoredCharacter& stored);
/* Throws std::runtime_error when the name is unterminated or the record does not fit the
   payload. */
StoredCharacter decode_stored_character(std::span<const std::byte> payload);

} // namespace wiz8::save
