#include "wiz8/npc_script_file.h"

#include <cstdlib>
#include <cwchar>
#include <memory>
#include <vector>

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

static void ReleaseNpcScriptQuote(W8NpcScriptQuote* record)
{
    if (record->entries != nullptr) {
        for (unsigned int index = 0; index < record->entry_count; ++index) {
            W8NpcQuoteEntry& entry = record->entries[index];
            if (entry.sub_entries != nullptr) {
                for (unsigned int sub_index = 0; sub_index < entry.sub_entry_count; ++sub_index) {
                    free(entry.sub_entries[sub_index].text);
                }
                free(entry.sub_entries);
            }
        }
        free(record->entries);
    }
    if (record->subquotes != nullptr) {
        for (unsigned int index = 0; index < record->subquote_count; ++index) {
            free(record->subquotes[index]);
        }
        free(record->subquotes);
    }
}

// FUNCTION: WIZ8 0x0055a0a0
void ReleaseNpcScriptFile(W8NpcScriptFile* file)
{
    if (file == nullptr) return;
    if (file->quotes != nullptr) {
        for (unsigned int index = 0; index < file->quote_count; ++index) {
            ReleaseNpcScriptQuote(&file->quotes[index]);
        }
        free(file->quotes);
    }
    free(file->name);
    free(file);
}

// FUNCTION: WIZ8 0x0055a140
unsigned char ReadNpcScriptQuote(wiz8::File* handle, W8NpcScriptQuote* record)
try
{
    W8NpcScriptQuote pending{};
    handle->read_exact(&pending, sizeof(pending));
    const bool has_subquotes = W8SerializedPointerPresent(pending.subquotes);
    pending.subquotes = nullptr;
    pending.entries = nullptr;
    std::unique_ptr<W8NpcScriptQuote, decltype(&ReleaseNpcScriptQuote)> owner(
        &pending, ReleaseNpcScriptQuote);

    if (has_subquotes) {
        handle->read_exact(&pending.subquote_count, sizeof(pending.subquote_count));
        if (pending.subquote_count > (handle->size() - handle->tell()) / sizeof(unsigned short)) return 0;
        pending.subquotes = static_cast<char**>(calloc(pending.subquote_count, sizeof(char*)));
        if (pending.subquote_count != 0 && pending.subquotes == nullptr) throw std::bad_alloc();
        for (unsigned int index = 0; index < pending.subquote_count; ++index) {
            unsigned short length;
            handle->read_exact(&length, sizeof(length));
            if (length == 0) continue;
            if (length > (handle->size() - handle->tell()) / sizeof(wchar_t)) return 0;
            std::vector<wchar_t> wide(static_cast<std::size_t>(length) + 1);
            handle->read_exact(wide.data(), length * sizeof(wchar_t));
            const auto bytes = wcstombs(nullptr, wide.data(), 0);
            if (bytes == static_cast<std::size_t>(-1)) return 0;
            pending.subquotes[index] = static_cast<char*>(malloc(bytes + 1));
            if (pending.subquotes[index] == nullptr) throw std::bad_alloc();
            if (wcstombs(pending.subquotes[index], wide.data(), bytes + 1) == static_cast<std::size_t>(-1)) {
                return 0;
            }
            pending.subquotes[index][bytes] = 0;
        }
    } else {
        pending.subquote_count = 0;
    }

    if (pending.entry_count > (handle->size() - handle->tell()) / sizeof(W8NpcQuoteEntry)) return 0;
    if (pending.entry_count != 0) {
        pending.entries = static_cast<W8NpcQuoteEntry*>(calloc(pending.entry_count, sizeof(W8NpcQuoteEntry)));
        if (pending.entries == nullptr) throw std::bad_alloc();
    }
    for (unsigned int index = 0; index < pending.entry_count; ++index) {
        W8NpcQuoteEntry disk_entry;
        handle->read_exact(&disk_entry, sizeof(disk_entry));
        disk_entry.sub_entries = nullptr;
        W8NpcQuoteEntry& entry = pending.entries[index];
        entry = disk_entry;
        if (entry.sub_entry_count == 0) continue;
        if (entry.sub_entry_count > (handle->size() - handle->tell()) / sizeof(W8NpcQuoteSubEntry)) return 0;
        entry.sub_entries = static_cast<W8NpcQuoteSubEntry*>(calloc(entry.sub_entry_count, sizeof(W8NpcQuoteSubEntry)));
        if (entry.sub_entries == nullptr) throw std::bad_alloc();
        for (unsigned int sub_index = 0; sub_index < entry.sub_entry_count; ++sub_index) {
            W8NpcQuoteSubEntry disk_sub_entry;
            handle->read_exact(&disk_sub_entry, sizeof(disk_sub_entry));
            const bool has_text = W8SerializedPointerPresent(disk_sub_entry.text);
            disk_sub_entry.text = nullptr;
            W8NpcQuoteSubEntry& sub_entry = entry.sub_entries[sub_index];
            sub_entry = disk_sub_entry;
            if (!has_text) continue;
            unsigned short length;
            handle->read_exact(&length, sizeof(length));
            sub_entry.text = static_cast<char*>(malloc(static_cast<std::size_t>(length) + 1));
            if (sub_entry.text == nullptr) throw std::bad_alloc();
            handle->read_exact(sub_entry.text, length);
            sub_entry.text[length] = 0;
        }
    }
    *record = pending;
    (void)owner.release();
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x0055a480
W8NpcScriptFile* LoadNpcScriptFile(char* path)
try
{
    if (path == nullptr) return nullptr;
    auto handle = wiz8::open_file(path, wiz8::OpenMode::read);
    W8NpcScriptFile header;
    handle->read_exact(&header, sizeof(header));
    const bool has_name = W8SerializedPointerPresent(header.name);
    header.name = nullptr;
    header.quotes = nullptr;
    std::unique_ptr<W8NpcScriptFile, decltype(&ReleaseNpcScriptFile)> file(
        static_cast<W8NpcScriptFile*>(malloc(sizeof(W8NpcScriptFile))), ReleaseNpcScriptFile);
    if (!file) throw std::bad_alloc();
    *file = header;
    if (has_name) {
        unsigned short length;
        handle->read_exact(&length, sizeof(length));
        if (length != 0) {
            file->name = static_cast<char*>(malloc(static_cast<std::size_t>(length) + 1));
            if (file->name == nullptr) throw std::bad_alloc();
            handle->read_exact(file->name, length);
            file->name[length] = 0;
        }
    }
    if (file->quote_count > (handle->size() - handle->tell()) / sizeof(W8NpcScriptQuote)) return nullptr;
    if (file->quote_count != 0) {
        file->quotes = static_cast<W8NpcScriptQuote*>(calloc(file->quote_count, sizeof(W8NpcScriptQuote)));
        if (file->quotes == nullptr) throw std::bad_alloc();
    }
    for (unsigned int index = 0; index < file->quote_count; ++index) {
        if (!ReadNpcScriptQuote(handle.get(), &file->quotes[index])) return nullptr;
    }
    return file.release();
}
catch (const std::exception&) { return nullptr; }
