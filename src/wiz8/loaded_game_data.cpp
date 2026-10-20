#include "wiz8/loaded_game_data.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "layouts/encounter_tables.h"
#include "retail_records.h"
#include "wiz8/data/encounter_records.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/missile_records.h"
#include "wiz8/data/records.h"
#include "wiz8/filesystem.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/unicode.h"

namespace {
GameData loaded;

[[noreturn]] void Malformed(std::string_view file)
{
    throw std::runtime_error("Malformed " + std::string(file));
}

std::unique_ptr<wiz8::File> OpenDatabase(std::string_view name)
{
    return wiz8::open_file(wiz8::asset_path("Data\\Databases\\" + std::string(name)),
                           wiz8::OpenMode::read);
}

// Windows localization uses %S for UTF-16 strings and I64 for integers.
// Convert that vocabulary once during import; {fmt} handles runtime formatting.
std::string ImportLocalizedPattern(std::string text)
{
    static const std::regex specifier(
        R"(%%|%([-+ #0]*[0-9]*(?:\.[0-9]+)?)(?:[hl])?S|%([-+ #0]*[0-9]*(?:\.[0-9]+)?)I64([diuoxX]))");
    std::string result;
    std::size_t position = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), specifier);
         it != std::sregex_iterator(); ++it) {
        const auto& match = *it;
        result.append(text, position, match.position() - position);
        if (match.str() == "%%")
            result += "%%";
        else if (match[3].matched)
            result += "%" + match[2].str() + "ll" + match[3].str();
        else
            result += "%" + match[1].str() + "s";
        position = match.position() + match.length();
    }
    result.append(text, position);
    return result;
}

/* MissileTables.dbs: each row is a 0x101-byte editor prefix followed by the
   0x1e5-byte runtime record. */
std::vector<W8MissileTableRecord> ReadMissileDatabase()
{
    auto handle = OpenDatabase("MissileTables.dbs");
    std::int32_t count;
    std::uint32_t version;
    handle->read_exact(&count, sizeof(count));
    handle->read_exact(&version, sizeof(version));
    if (count < 0 || count > 100000) Malformed("MissileTables.dbs");
    std::vector<W8MissileTableRecord> rows(count);
    for (auto& row : rows) {
        handle->seek(0x101, wiz8::SeekOrigin::current);
        if (wiz8::retail::read(*handle, row).bytes != wiz8::retail::kMissileTableRecordBytes)
            Malformed("MissileTables.dbs");
    }
    return rows;
}

std::optional<std::string> ParseKeywordToken(std::string_view& line)
{
    const auto start = line.find_first_not_of(' ');
    if (start == std::string_view::npos) return {};
    line.remove_prefix(start);
    const auto end = line.find_first_of("/\r\n");
    auto word = line.substr(0, end);
    const auto last = word.find_last_not_of(' ');
    if (last == std::string_view::npos) return {};
    word = word.substr(0, last + 1);
    line = end == std::string_view::npos ? std::string_view{} : line.substr(end + 1);
    return std::string(word);
}

/* One keyword file: a header line, then one row per line after an eleven-character
   prefix. */
std::vector<std::vector<std::string>> ReadKeywordFile(std::string_view path)
{
    auto input = wiz8::open_file(wiz8::asset_path(path));
    if (input->size() < 0 || input->size() > 16 * 1024 * 1024) Malformed(path);
    const auto bytes = input->read_text();
    if (bytes.find('\0') != std::string::npos) Malformed(path);
    std::istringstream stream(bytes);
    std::string line;
    std::getline(stream, line);
    std::vector<std::vector<std::string>> rows;
    while (std::getline(stream, line)) {
        // Retail's line limit and eleven-character metadata prefix are format constraints.
        if (wiz8::text::to_utf16(line).size() >= 1000) Malformed(path);
        std::string_view remaining(line);
        remaining.remove_prefix(wiz8::text::codepoint_prefix(remaining, 11));
        std::vector<std::string> words;
        while (auto word = ParseKeywordToken(remaining)) words.push_back(std::move(*word));
        rows.push_back(std::move(words));
    }
    return rows;
}
} // namespace

std::vector<std::string> ReadStringTable(std::string_view path)
{
    auto handle = wiz8::open_file(wiz8::asset_path(path));
    int count;
    handle->read_exact(&count, 4);
    if (count <= 0 || count > 100000) throw std::runtime_error("Invalid localization entry count");
    std::vector<std::string> strings;
    strings.reserve(count);
    for (int index = 0; index < count; ++index) {
        int byte_count;
        handle->read_exact(&byte_count, 4);
        if (byte_count < 0 || byte_count > 1024 * 1024 || byte_count % 2)
            throw std::runtime_error("Invalid localization text length");
        std::vector<std::byte> bytes(byte_count);
        handle->read_exact(bytes.data(), bytes.size());
        strings.push_back(ImportLocalizedPattern(wiz8::text::from_utf16le(bytes, true)));
    }
    return strings;
}

std::vector<W8ItemDatabaseRecord> ReadItemDatabase()
{
    return wiz8::retail::read_database<W8ItemDatabaseRecord>(*OpenDatabase("Items.DBS"));
}

/* ItemTables.DBS carries fixed 0x100-byte category names, then table records. */
W8ItemTables ReadItemTables()
{
    auto handle = OpenDatabase("ItemTables.DBS");
    unsigned int category_count;
    handle->read_exact(&category_count, sizeof(category_count));
    const auto remaining = handle->size() - handle->tell();
    if (remaining < std::int64_t(sizeof(unsigned int)) ||
        category_count > (remaining - std::int64_t(sizeof(unsigned int))) / 0x100)
        Malformed("ItemTables.DBS");
    W8ItemTables result{.categories = std::vector<std::string>(category_count)};
    for (auto& category : result.categories) {
        std::array<char, 256> bytes;
        handle->read_exact(bytes.data(), bytes.size());
        const auto end = std::find(bytes.begin(), bytes.end(), '\0');
        if (end == bytes.end()) Malformed("ItemTables.DBS");
        category.assign(bytes.begin(), end);
    }

    unsigned int table_count;
    handle->read_exact(&table_count, sizeof(table_count));
    if (table_count > (handle->size() - handle->tell()) / std::int64_t(sizeof(W8ItemTableRecord)))
        Malformed("ItemTables.DBS");
    result.tables.resize(table_count);
    handle->read_exact(result.tables.data(), result.tables.size() * sizeof(W8ItemTableRecord));
    for (const auto& table : result.tables)
        if (std::find(std::begin(table.name), std::end(table.name), '\0') == std::end(table.name))
            Malformed("ItemTables.DBS");
    return result;
}

/* NPC.DBS records carry an optional sub-list, stored after the record when its
   leading count exceeds one and its 0x9D flag is clear: a count, then that many
   six-byte elements appended to a freshly created list. */
std::vector<W8NpcDatabaseRecord> ReadNpcDatabase()
{
    auto handle = OpenDatabase("NPC.DBS");
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count >
        (handle->size() - handle->tell()) / std::int64_t(wiz8::retail::kNpcDatabaseRecordBytes))
        Malformed("NPC.DBS");
    std::vector<W8NpcDatabaseRecord> records(count);
    for (unsigned int index = 0; index < count; ++index) {
        auto& record = records[index];
        wiz8::retail::read_exact(*handle, record);
        if (record.no_item_stock != 0 || record.version <= 1) continue;
        unsigned int entry_count;
        handle->read_exact(&entry_count, sizeof(entry_count));
        const auto remaining = handle->size() - handle->tell();
        const auto following_records =
            std::int64_t(count - index - 1) * std::int64_t(wiz8::retail::kNpcDatabaseRecordBytes);
        if (remaining < following_records ||
            entry_count >
                (remaining - following_records) / std::int64_t(sizeof(W8NpcItemStockRule)) ||
            entry_count > std::numeric_limits<int>::max())
            Malformed("NPC.DBS");
        record.item_stock_rules.resize(entry_count);
        for (auto& rule : record.item_stock_rules) handle->read_exact(&rule, sizeof(rule));
    }
    return records;
}

/* SpellTables.dbs: a count, a version, then rows that each begin with a 257-byte
   label. Spell ids in retail records are 16-bit. */
std::vector<W8SpellRuntimeRecord> ReadSpellDatabase()
{
    auto handle = wiz8::open_file(wiz8::asset_path("data/databases/spelltables.dbs"));
    std::int32_t count;
    std::uint32_t version;
    handle->read_exact(&count, sizeof(count));
    handle->read_exact(&version, sizeof(version));
    constexpr std::int64_t entry_bytes = 0x101 + wiz8::retail::kSpellRuntimeRecordBytes;
    if (count <= 0 || count > std::numeric_limits<std::uint16_t>::max() ||
        std::int64_t(count) * entry_bytes > handle->size() - handle->tell())
        Malformed("SpellTables.dbs");
    std::vector<W8SpellRuntimeRecord> records(count);
    for (auto& record : records) {
        handle->seek(0x101, wiz8::SeekOrigin::current);
        if (wiz8::retail::read(*handle, record).bytes != wiz8::retail::kSpellRuntimeRecordBytes)
            Malformed("SpellTables.dbs");
    }
    return records;
}

/* EncounterTables.dbs: a count of category names, the names, a count of tables, then
   each table's header followed by its entries stored as columns. */
std::vector<W8EncounterTable> ReadEncounterTables()
{
    auto handle = OpenDatabase("EncounterTables.dbs");
    int name_count;
    handle->read_exact(&name_count, sizeof(name_count));
    if (name_count < 0) Malformed("EncounterTables.dbs");
    handle->seek(std::int64_t(name_count) * 256, wiz8::SeekOrigin::current);
    int table_count;
    handle->read_exact(&table_count, sizeof(table_count));
    if (table_count < 0) Malformed("EncounterTables.dbs");
    std::vector<W8EncounterTable> tables(table_count);
    for (auto& table : tables) {
        W8EncounterTableDiskHeader header;
        handle->read_exact(&header, sizeof(header));
        table.name.assign(header.name, std::find(std::begin(header.name),
                                                 std::prev(std::end(header.name)), '\0'));
        table.entries.resize(header.entry_count);
        for (auto& entry : table.entries) handle->read_exact(&entry.species, sizeof(entry.species));
        for (auto& entry : table.entries) handle->read_exact(&entry.rarity_class, 1);
        for (auto& entry : table.entries) handle->read_exact(&entry.time_condition, 1);
        for (auto& entry : table.entries) handle->read_exact(&entry.challenge_level, 1);
        for (auto& entry : table.entries) {
            W8EncounterScriptName script;
            handle->read_exact(&script, sizeof(script));
            entry.script.assign(script.value, std::find(std::begin(script.value),
                                                        std::prev(std::end(script.value)), '\0'));
        }
        // Version two adds a flag byte nothing reads.
        if (header.version != 1) handle->seek(1, wiz8::SeekOrigin::current);
    }
    return tables;
}

W8DialogueKeywords ReadDialogueKeywords()
try {
    return {.english = ReadKeywordFile("Data\\Strings\\English_Keywords.txt"),
            .translated = ReadKeywordFile("Data\\Strings\\translated_Keywords.txt")};
} catch (const std::exception&) {
    return {};
}

/* Monsters.DBS: a count, then the records. Names drop their editor suffix after '#'. */
std::vector<W8MonsterRecord> ReadMonsterDatabase()
{
    auto handle = OpenDatabase("Monsters.DBS");
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count > kMaxMonsterSpecies ||
        count > (handle->size() - handle->tell()) / std::int64_t(wiz8::retail::kMonsterRecordBytes))
        Malformed("Monsters.DBS");
    std::vector<W8MonsterRecord> records(count);
    for (auto& record : records) {
        wiz8::retail::read_exact(*handle, record);
        for (auto* name : {&record.name0, &record.name1, &record.name2, &record.name3})
            if (const auto suffix = name->find('#'); suffix != std::string::npos)
                name->resize(suffix);
    }
    return records;
}

GameData LoadGameData(std::string_view string_data_path)
{
    auto item_tables = ReadItemTables();
    return {
        .strings = ReadStringTable(string_data_path),
        .items = ReadItemDatabase(),
        .item_table_categories = std::move(item_tables.categories),
        .item_tables = std::move(item_tables.tables),
        .spells = ReadSpellDatabase(),
        .npcs = ReadNpcDatabase(),
        .facts = wiz8::retail::read_database<W8FactDatabaseRecord>(*OpenDatabase("FACT.DBS"),
                                                                   W8_FACT_SLOT_COUNT),
        .levels = wiz8::retail::read_database<W8LevelDatabaseRecord>(*OpenDatabase("LEVELS.DBS")),
        .monsters = ReadMonsterDatabase(),
        .missiles = ReadMissileDatabase(),
        .encounter_tables = ReadEncounterTables(),
        .keywords = ReadDialogueKeywords(),
    };
}

const GameData& Data()
{
    return loaded;
}

GameData InstallGameData(GameData data)
{
    return std::exchange(loaded, std::move(data));
}
