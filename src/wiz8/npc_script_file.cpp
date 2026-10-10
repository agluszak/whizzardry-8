#include "wiz8/npc_script_file.h"
#include "wiz8/virtual_file.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

/* Preserve partial state and unchecked reads/allocations on failure. */

// Each loaded script owns its header, name, quotes, entries, and text arrays.
// FUNCTION: WIZ8 0x0055a0a0
void ReleaseNpcScriptFile(W8NpcScriptFile* file)
{
    if (!file) return;
    if (file->quotes) {
        for (unsigned int index = 0; index < file->quote_count; ++index) {
            auto& quote = file->quotes[index];
            if (quote.entries) {
                for (unsigned int entry_index = 0; entry_index < quote.entry_count; ++entry_index) {
                    auto& entry = quote.entries[entry_index];
                    if (entry.sub_entries) {
                        for (unsigned int sub = 0; sub < entry.sub_entry_count; ++sub)
                            free(entry.sub_entries[sub].text);
                        free(entry.sub_entries);
                    }
                }
                free(quote.entries);
            }
            if (quote.subquotes) {
                for (unsigned int sub = 0; sub < quote.subquote_count; ++sub)
                    free(quote.subquotes[sub]);
                free(quote.subquotes);
            }
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
    W8NpcQuoteEntry* entry;
    W8NpcQuoteSubEntry* sub_entry;
    char* text;
    unsigned int transferred;
    unsigned short length;
    unsigned short disk_entry_count;
    unsigned char disk_sub_count;
    unsigned int block_size;
    int index;
    int sub_index;
    wchar_t wide[2000];

    W8NpcScriptQuote disk_record{};
    transferred = handle->read(&disk_record, sizeof(disk_record)).bytes;
    if (transferred != sizeof(*record)) {
        return 0;
    }

    const bool has_subquotes = W8SerializedPointerPresent(disk_record.subquotes);
    *record = disk_record;
    record->subquotes = 0;
    record->entries = 0;
    if (has_subquotes) {
        ((transferred = handle->read(record, 1).bytes) == static_cast<std::size_t>(1));
        record->subquotes = static_cast<char**>(calloc(record->subquote_count, sizeof(char*)));
        for (index = 0; index < record->subquote_count; ++index) {
            ((transferred = handle->read(&length, 2).bytes) == static_cast<std::size_t>(2));
            record->subquotes[index] = 0;
            if (transferred != 2) {
                return 0;
            }
            if (length != 0) {
                record->subquotes[index] = static_cast<char*>(malloc(length + 1));
                if (record->subquotes[index] == 0) {
                    return 0;
                }
                ((transferred = handle->read(wide, length * 2).bytes) == static_cast<std::size_t>(length * 2));
                wide[length] = 0;
                wcstombs(record->subquotes[index], wide, length + 1);
            }
        }
    }

    disk_entry_count = record->entry_count;
    if (disk_entry_count != 0) {
        record->entries = 0;
        record->entry_count = 0;
        block_size = disk_entry_count * sizeof(*record->entries);
        record->entries = static_cast<W8NpcQuoteEntry*>(malloc(block_size));
        if (record->entries != 0) {
            memset(record->entries, 0, block_size);
            record->entry_count = disk_entry_count;
        }
    }

    for (index = 0; index < record->entry_count; ++index) {
        entry = &record->entries[index];
        W8NpcQuoteEntry disk_entry{};
        transferred = handle->read(&disk_entry, sizeof(disk_entry)).bytes;
        if (transferred != sizeof(*entry)) {
            return 0;
        }
        *entry = disk_entry;
        disk_sub_count = entry->sub_entry_count;
        entry->sub_entries = 0;
        if (disk_sub_count != 0) {
            entry->sub_entry_count = 0;
            entry->sub_entries = static_cast<W8NpcQuoteSubEntry*>(malloc(disk_sub_count * sizeof(*entry->sub_entries)));
            if (entry->sub_entries == 0) {
                return 0;
            }
            entry->sub_entry_count = disk_sub_count;
            memset(entry->sub_entries, 0, disk_sub_count * sizeof(*entry->sub_entries));
            for (sub_index = 0; sub_index < entry->sub_entry_count; ++sub_index) {
                sub_entry = entry->sub_entries + sub_index;
                W8NpcQuoteSubEntry disk_sub_entry{};
                transferred = handle->read(&disk_sub_entry, 8).bytes;
                if (transferred != 8) {
                    return 0;
                }
                const bool has_text = W8SerializedPointerPresent(disk_sub_entry.text);
                *sub_entry = disk_sub_entry;
                sub_entry->text = 0;
                if (has_text) {
                    ((transferred = handle->read(&length, 2).bytes) == static_cast<std::size_t>(2));
                    if (transferred != 2) {
                        return 0;
                    }
                    text = static_cast<char*>(malloc(length + 1));
                    sub_entry->text = text;
                    if (text == 0) {
                        return 0;
                    }
                    ((transferred = handle->read(text, length).bytes) == static_cast<std::size_t>(length));
                    if (transferred != length) {
                        return 0;
                    }
                    text[length] = 0;
                }
            }
        }
    }
    return 1;
}
catch (const std::exception&) { return false; }

/* Load into a single owner so short reads also release partially initialized records. */
// FUNCTION: WIZ8 0x0055a480
W8NpcScriptFile* LoadNpcScriptFile(char* path)
{
    std::unique_ptr<wiz8::File> handle;
    W8NpcScriptFile* file;
    char* name;
    W8NpcScriptQuote* quotes;
    unsigned int transferred;
    unsigned short length;
    int index;

    handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (handle == 0) {
        return 0;
    }
    W8NpcScriptFile header{};
    if (handle->read(&header, sizeof(header)).bytes != sizeof(header)) return nullptr;
    const bool has_name = W8SerializedPointerPresent(header.name);
    file = static_cast<W8NpcScriptFile*>(calloc(1, sizeof(W8NpcScriptFile)));
    if (!file) return nullptr;
    std::unique_ptr<W8NpcScriptFile, decltype(&ReleaseNpcScriptFile)> owner(file, &ReleaseNpcScriptFile);
    memcpy(file->unknown_00, header.unknown_00, sizeof(file->unknown_00));
    file->quote_count = header.quote_count;
    if (has_name) {
        ((transferred = handle->read(&length, 2).bytes) == static_cast<std::size_t>(2));
        if (transferred != 2) {
            return 0;
        }
        if (length != 0) {
            name = static_cast<char*>(malloc(length + 1));
            file->name = name;
            if (name == 0) {
                return 0;
            }
            ((transferred = handle->read(name, length).bytes) == static_cast<std::size_t>(length));
            if (transferred != length) {
                return 0;
            }
            file->name[length] = 0;
        }
    }
    quotes = static_cast<W8NpcScriptQuote*>(calloc(file->quote_count, sizeof(W8NpcScriptQuote)));
    file->quotes = quotes;
    if (quotes == 0) {
        return 0;
    }
    for (index = 0; index < file->quote_count; ++index) {
        if (!ReadNpcScriptQuote(handle.get(), &file->quotes[index])) {
            return 0;
        }
    }
    if (handle) handle->close();
    handle.reset();
    return owner.release();
}
