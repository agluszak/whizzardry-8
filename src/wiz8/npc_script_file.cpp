#include "wiz8/npc_script_file.h"
#include "wiz8/virtual_file.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

/* Preserve partial state and unchecked reads/allocations on failure. */

/* Free each record's string table and the first entry's sub-entry array. The
   header, name, quotes array, and remaining entry allocations are left for
   the caller; NPC rebinding overwrites the pointer without releasing them. */
// FUNCTION: WIZ8 0x0055a0a0
void ReleaseNpcScriptFile(W8NpcScriptFile* file)
{
    W8NpcScriptQuote* record;
    unsigned int index;
    unsigned int string_index;

    if (file == 0) {
        return;
    }
    if (file->quote_count == 0) {
        return;
    }
    for (index = 0; index < file->quote_count; ++index) {
        record = file->quotes + index;
        if (record == 0) {
            continue;
        }
        if (record->entries != 0 && record->entries->sub_entries != 0) {
            free(record->entries->sub_entries);
        }
        if (record->subquotes != 0) {
            for (string_index = 0; string_index < record->subquote_count; ++string_index) {
                free(record->subquotes[string_index]);
            }
            free(record->subquotes);
        }
    }
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

    ((transferred = handle->read(record, sizeof(*record)).bytes) == static_cast<std::size_t>(sizeof(*record)));
    if (transferred != sizeof(*record)) {
        return 0;
    }

    const bool has_subquotes = W8SerializedPointerPresent(record->subquotes);
    record->subquotes = 0;
    record->entries = 0;
    if (has_subquotes) {
        ((transferred = handle->read(record, 1).bytes) == static_cast<std::size_t>(1));
        record->subquotes = static_cast<char**>(malloc(record->subquote_count * sizeof(char*)));
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
        ((transferred = handle->read(entry, sizeof(*entry)).bytes) == static_cast<std::size_t>(sizeof(*entry)));
        if (transferred != sizeof(*entry)) {
            return 0;
        }
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
                ((transferred = handle->read(sub_entry, 8).bytes) == static_cast<std::size_t>(8));
                if (transferred != 8) {
                    return 0;
                }
                const bool has_text = W8SerializedPointerPresent(sub_entry->text);
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

/* Load the whole file: the fixed header, the optional length-prefixed name, then
   one 0x0c-byte record per header count. Every failure returns without releasing
   what it already allocated, which is the original's behaviour and not an
   omission here. */
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
    file = static_cast<W8NpcScriptFile*>(malloc(sizeof(W8NpcScriptFile)));
    if (file == 0) {
        return 0;
    }
    ((transferred = handle->read(file, sizeof(*file)).bytes) == static_cast<std::size_t>(sizeof(*file)));
    if (transferred != sizeof(*file)) {
        return 0;
    }
    const bool has_name = W8SerializedPointerPresent(file->name);
    file->name = 0;
    file->quotes = 0;
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
    quotes = static_cast<W8NpcScriptQuote*>(malloc(file->quote_count * sizeof(W8NpcScriptQuote)));
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
    return file;
}
