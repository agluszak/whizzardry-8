#include "wiz8/npc_script_file.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "filesystem.h"
#include "wiz8/unicode.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */
// FUNCTION: WIZ8 0x0055a0a0
void ReleaseNpcScriptFile(W8NpcScriptFile* file)
{
    delete file;
}

static std::string ReadNarrowText(wiz8::File& file)
{
    std::uint16_t length;
    file.read_exact(&length, sizeof(length));
    std::string text(length, '\0');
    file.read_exact(text.data(), text.size());
    if (const auto end = text.find('\0'); end != std::string::npos) text.resize(end);
    return text;
}

// FUNCTION: WIZ8 0x0055a140
std::optional<W8NpcScriptQuote> ReadNpcScriptQuote(wiz8::File* file)
try {
    wiz8::retail::nsf::Quote disk;
    file->read_exact(&disk, sizeof(disk));
    W8NpcScriptQuote quote;
    quote.subquote_count = disk.subquote_count;
    quote.entry_count = disk.entry_count;
    quote.dialogue_alert = disk.dialogue_alert;
    if (disk.subquotes) {
        file->read_exact(&quote.subquote_count, 1);
        quote.subquotes.reserve(quote.subquote_count);
        for (unsigned i = 0; i < quote.subquote_count; ++i) {
            std::uint16_t units;
            file->read_exact(&units, sizeof(units));
            std::vector<std::byte> bytes(static_cast<std::size_t>(units) * 2);
            file->read_exact(bytes.data(), bytes.size());
            quote.subquotes.push_back(wiz8::text::from_utf16le(bytes));
        }
    }
    if (quote.entry_count > (file->size() - file->tell()) / sizeof(wiz8::retail::nsf::Entry))
        return std::nullopt;
    quote.entries.reserve(quote.entry_count);
    for (unsigned i = 0; i < quote.entry_count; ++i) {
        wiz8::retail::nsf::Entry stored;
        file->read_exact(&stored, sizeof(stored));
        W8NpcQuoteEntry entry;
        entry.kind = stored.kind;
        entry.operand0 = stored.operand0;
        entry.operand1 = stored.operand1;
        entry.operand2 = stored.operand2;
        entry.sub_entry_count = stored.sub_entry_count;
        if (entry.sub_entry_count >
            (file->size() - file->tell()) / sizeof(wiz8::retail::nsf::SubEntry))
            return std::nullopt;
        entry.sub_entries.reserve(entry.sub_entry_count);
        for (unsigned j = 0; j < entry.sub_entry_count; ++j) {
            wiz8::retail::nsf::SubEntry part;
            file->read_exact(&part, sizeof(part));
            W8NpcQuoteSubEntry sub;
            sub.operand = part.operand;
            if (part.text) sub.text = ReadNarrowText(*file);
            entry.sub_entries.push_back(std::move(sub));
        }
        quote.entries.push_back(std::move(entry));
    }
    return quote;
} catch (const std::exception&) {
    return std::nullopt;
}

// FUNCTION: WIZ8 0x0055a480
W8NpcScriptFile* LoadNpcScriptFile(std::string_view path)
try {
    auto file = wiz8::open_file(path);
    wiz8::retail::nsf::Header disk;
    file->read_exact(&disk, sizeof(disk));
    auto script = std::make_unique<W8NpcScriptFile>();
    std::copy(std::begin(disk.unknown_00), std::end(disk.unknown_00), script->unknown_00);
    script->quote_count = disk.quote_count;
    if (disk.name) script->name = ReadNarrowText(*file);
    if (script->quote_count > (file->size() - file->tell()) / sizeof(wiz8::retail::nsf::Quote))
        return nullptr;
    script->quotes.reserve(script->quote_count);
    for (unsigned int i = 0; i < script->quote_count; ++i) {
        auto quote = ReadNpcScriptQuote(file.get());
        if (!quote) return nullptr;
        script->quotes.push_back(std::move(*quote));
    }
    return script.release();
} catch (const std::exception&) {
    return nullptr;
}
