#ifndef WIZ8_NPC_SCRIPT_FILE_H
#define WIZ8_NPC_SCRIPT_FILE_H

namespace wiz8 {
class File;
}

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/* NSF disk records and owning runtime script data have independent layouts. */

/* Quote-entry opcodes with recovered consumers; unknown values remain unnamed. */
typedef unsigned char W8NpcQuoteEntryKind;

enum {
    W8_NPC_ENTRY_NEXT_QUOTE = 2,
    W8_NPC_ENTRY_FACT_CONDITIONAL_QUOTE = 3,
    W8_NPC_ENTRY_KEYWORD = 4,
    W8_NPC_ENTRY_OPTIONS = 5,
    W8_NPC_ENTRY_REPLY = 6,
    W8_NPC_ENTRY_SET_FACT = 7,
    W8_NPC_ENTRY_CLOSE_DIALOGUE = 8,
    W8_NPC_ENTRY_GIVE_ITEM = 9,
    W8_NPC_ENTRY_REMOVE_ITEM = 10,
    W8_NPC_ENTRY_ITEM_REQUEST = 11,
    W8_NPC_ENTRY_MONSTER_SCRIPT_LABEL = 12,
    W8_NPC_ENTRY_NPC_WORLD_ACTION = 13,
    W8_NPC_ENTRY_CLOSE_AND_RESUME = 14,
    W8_NPC_ENTRY_ITEM_MENTION = 15,
    W8_NPC_ENTRY_GIVE_GOLD = 16,
    W8_NPC_ENTRY_GIVE_EXPERIENCE = 17,
    W8_NPC_ENTRY_PRICE_CHECK = 18,
    W8_NPC_ENTRY_KEYWORD_INPUT = 19,
    W8_NPC_ENTRY_SELF_GROUP_ACTION = 20,
    W8_NPC_ENTRY_NPC_INTERACTION = 21,
    W8_NPC_ENTRY_FACTION_CHANGE = 22,
    W8_NPC_ENTRY_DECLINE_QUOTE = 23,
    W8_NPC_ENTRY_PARTY_SPEAKER_EVENT = 24,
    W8_NPC_ENTRY_PERSON_KEYWORD = 25,
    W8_NPC_ENTRY_ITEM_KEYWORD = 26,
    W8_NPC_ENTRY_PLACE_KEYWORD = 27,
    W8_NPC_ENTRY_MISC_KEYWORD = 28,
    W8_NPC_ENTRY_ALWAYS_PRICE_CHECK = 30
};

// Retail NSF structures describe bytes only. Pointer words select optional
// following records; they are never converted to native handles.
namespace wiz8::retail::nsf {
#pragma pack(push, 1)
struct SubEntry {
    std::int32_t operand;
    std::uint32_t text;
};
struct Entry {
    std::uint8_t kind;
    std::int32_t operand0, operand1, operand2;
    std::uint8_t sub_entry_count;
    std::uint32_t sub_entries;
};
struct Quote {
    std::uint8_t subquote_count;
    std::uint32_t subquotes, entries;
    std::uint16_t entry_count;
    std::uint8_t dialogue_alert;
};
struct Header {
    unsigned char unknown_00[4];
    std::uint16_t quote_count;
    std::uint32_t name, quotes;
};
#pragma pack(pop)
static_assert(sizeof(SubEntry) == 8);
static_assert(sizeof(Entry) == 0x12);
static_assert(sizeof(Quote) == 0x0c);
static_assert(sizeof(Header) == 0x0e);
}

struct W8NpcQuoteSubEntry {
    int operand{};
    std::string text;
};
struct W8NpcQuoteEntry {
    W8NpcQuoteEntryKind kind{};
    int operand0{}, operand1{}, operand2{};
    unsigned char sub_entry_count{};
    std::vector<W8NpcQuoteSubEntry> sub_entries;
};
struct W8NpcScriptQuote {
    unsigned char subquote_count{};
    std::vector<std::string> subquotes;
    std::vector<W8NpcQuoteEntry> entries;
    unsigned short entry_count{};
    unsigned char dialogue_alert{};
};
struct W8NpcScriptFile {
    unsigned char unknown_00[4]{};
    unsigned short quote_count{};
    std::string name;
    std::vector<W8NpcScriptQuote> quotes;
};

std::optional<W8NpcScriptQuote> ReadNpcScriptQuote(wiz8::File* handle);
std::unique_ptr<W8NpcScriptFile> LoadNpcScriptFile(std::string_view path);

#endif
