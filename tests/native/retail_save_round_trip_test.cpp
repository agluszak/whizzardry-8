/* A save written by the retail game, decoded and re-encoded section by section
   with the save codec. It records which serialized records we preserve byte for
   byte. Needs no game data or display: only the downloaded save
   (fetch_downloads.cmake), found through WIZ8_RUNTIME_DOWNLOADS. */

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "wiz8/chunk.h"
#include "wiz8/save_codec.h"

namespace {

namespace save = wiz8::save;
using Bytes = std::vector<std::byte>;

std::string Tag(unsigned int id)
{
    std::string tag(4, ' ');
    for (int i = 0; i < 4; ++i) tag[i] = static_cast<char>(id >> (8 * i) & 0xff);
    return tag;
}

Bytes ReadPayload(W8Chunk& chunk)
{
    Bytes payload(static_cast<std::size_t>(chunk.RemainingBytes()));
    chunk.ReadBytes(payload.data(), payload.size());
    return payload;
}

/* Decode then encode one section payload. The message boxes' encoding depends
   on the save version GSTA names, which precedes TEXT in every save. */
Bytes RoundTrip(const std::string& tag, const Bytes& payload, float& save_version)
{
    if (tag == "GSTA") {
        const auto state = save::decode_game_state(payload);
        save_version = state.status.retail.save_version;
        return save::encode(state);
    }
    if (tag == "TEXT") return save::encode(save::decode_message_boxes(payload, save_version));
    if (tag == "TVAR") return save::encode(save::decode_location_variables(payload));
    if (tag == "NPCI") return save::encode(save::decode_dialogue_transcript(payload));
    if (tag == "NPCT") {
        // Record names are checked against the NPC database only when loading a game.
        std::vector<std::shared_ptr<W8NpcState>> npcs;
        for (auto& npc : save::decode_npc_states(payload, std::numeric_limits<std::size_t>::max()))
            npcs.push_back(std::move(npc));
        return save::encode(npcs);
    }
    if (tag == "NPCF") return save::encode(save::decode_facts(payload));
    if (tag == "FATA") return save::encode(save::decode_factions(payload));
    if (tag == "JRNL") return save::encode(save::decode_journal(payload));
    if (tag == "HYPN") return save::encode(save::decode_monster_control(payload));
    if (tag == "CHAR") return save::encode(save::decode_stored_character(payload));
    if (tag == "STAT") return save::encode(save::decode_level_status_header(payload));
    FAIL("no codec for " << tag);
    return {};
}

/* Sections whose re-encoded bytes deliberately differ from what retail wrote,
   though never in size. The test fails if a listed section starts
   round-tripping exactly, so the list stays an accurate record. Every other
   section of this save (NPCI, NPCF, FATA, JRNL and all 26 STAT records)
   round-trips exactly. */
const std::map<std::string, const char*> kRewritten = {
    {"GSTA", "The status record's two buffer pointers become presence words and each "
             "party slot's remembered item-use address is zeroed: addresses are "
             "meaningless outside the process that wrote them. An empty party slot's "
             "member and remembered item are not state, so it is written cleared: NPC "
             "index 0 where retail left -1 after an NPC left the slot (a retail game "
             "where no NPC ever joined has 0 too), and item origin 0xff (none)."},
    {"TEXT", "Each line's word-list pointer is written as zero, and highlight bounds "
             "past the end of a line's text are clamped to its end."},
    {"TVAR", "Retail copies each variable name from an uninitialized buffer; the bytes "
             "after the name's terminator are stale memory, written as zeros."},
    {"NPCT", "Each NPC state's script, database record, stock and character pointers "
             "are written as zero or as presence words."},
};

/* Not compared here:
   - GVER and SHOT have no codec: the writer stamps its own version and a fresh
     screenshot.
   - An LVLS group's sections other than STAT (monsters, items, triggers, props,
     ...) decode only into a loaded World, which needs the level's assets. The
     save loader keeps the groups of levels the party is not on as byte images
     and writes them back unchanged.
   - This save has no CHAR or HYPN section; the codec handles them if present. */
const char* const kNoCodec[] = {"GVER", "SHOT"};

} // namespace

TEST_CASE("A retail save's sections re-encode to the bytes retail wrote", "[save][retail]")
{
    const char* downloads = std::getenv("WIZ8_RUNTIME_DOWNLOADS");
    REQUIRE(downloads);
    const auto path = std::filesystem::path(downloads) / "p2-RapaxAC.sav";
    REQUIRE(std::filesystem::is_regular_file(path));

    W8Chunk chunks;
    REQUIRE(chunks.OpenRead(path));
    float save_version = 0;
    std::map<std::string, int> compared, rewritten;
    int level_groups = 0;
    const auto compare = [&](const std::string& tag) {
        const Bytes payload = ReadPayload(chunks);
        const Bytes encoded = RoundTrip(tag, payload, save_version);
        ++compared[tag];
        INFO(tag << " #" << compared[tag]);
        CHECK(encoded.size() == payload.size());
        const auto [mine, theirs] = std::ranges::mismatch(encoded, payload);
        if (encoded.size() == payload.size() && mine == encoded.end()) return;
        ++rewritten[tag];
        if (!kRewritten.contains(tag))
            FAIL_CHECK("first difference at offset " << (mine - encoded.begin()));
    };
    chunks.ForEachChunk([&](unsigned int id) {
        const std::string tag = Tag(id);
        if (tag == "LVLS") {
            ++level_groups;
            chunks.ReadGroup([&] {
                chunks.ReadValue<int>(); // The group's level.
                chunks.ForEachChunk([&](unsigned int section) {
                    if (Tag(section) == "STAT") compare("STAT");
                });
            });
        } else if (std::ranges::find(kNoCodec, tag) == std::end(kNoCodec)) {
            compare(tag);
        }
    });
    chunks.Close();

    // The save holds the party, its story and every level it has visited.
    for (const char* tag : {"GSTA", "TEXT", "NPCI", "NPCT", "NPCF", "FATA", "JRNL", "STAT"})
        CHECK(compared[tag] > 0);
    CHECK(level_groups > 1);
    CHECK(compared["STAT"] == level_groups);
    for (const auto& [tag, reason] : kRewritten) {
        INFO(tag << " is listed as rewritten: " << reason);
        CHECK(rewritten[tag] > 0);
    }
}
