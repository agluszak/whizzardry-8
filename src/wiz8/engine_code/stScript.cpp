#include "wiz8/engine_code/stScript.h"
#include "wiz8/virtual_file.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <stdlib.h>
#include <string.h>

/* Read one byte at a time through the virtual-file layer.  End-of-file after
   at least one byte still returns a line, while an empty end-of-file clears
   `more`.  The terminator is not retained and CRLF is normalised by removing
   the CR after the loop. */
// FUNCTION: WIZ8 0x004CEE40
unsigned char ReadTextLine(wiz8::File* handle, char* destination, int capacity, unsigned char* more)
try
{
    *more = 0;
    if (capacity <= 0) return 0;
    destination[0] = 0;
    if (capacity == 1) return 0;
    *more = 1;
    int length = 0;
    for (;;) {
        char character;
        if (handle->read(&character, sizeof(character)).eof) {
            *more = 0;
            break;
        }
        if (character == '\n') break;
        destination[length++] = character;
        destination[length] = 0;
        if (length == capacity - 1) return 0;
    }
    if (length != 0 && destination[length - 1] == '\r') destination[length - 1] = 0;
    return *more != 0 || length != 0;
}
catch (const std::exception&) { *more = 0; return false; }

// VTABLE: WIZ8 0x005ED328
// class stScript

// VTABLE: WIZ8 0x005ED358
// class srClassSupport<stScript,srClass,1,65549>

// FUNCTION: WIZ8 0x004CF110
srClass* stScript::vInstance()
{
    return new stScript;
}

// FUNCTION: WIZ8 0x004CF260
stScript::~stScript()
{
    Clear();
}

// FUNCTION: WIZ8 0x004CF690
void stScript::Clear()
{
    while (lines.GetCount() != 0) {
        stScriptLine* line = lines.RemoveAt(0);
        if (line != 0) {
            free(line->text);
            delete line;
        }
    }
    while (labels.GetCount() != 0) {
        labels.RemoveAtAndDelete(0);
    }
}

// FUNCTION: WIZ8 0x004CF3B0
unsigned char stScript::Load(const char* path)
try
{
    if (path == nullptr) return 0;
    auto handle = wiz8::open_file(path, wiz8::OpenMode::read);
    auto release_line = [](stScriptLine* line) {
        free(line->text);
        delete line;
    };
    using LineOwner = std::unique_ptr<stScriptLine, decltype(release_line)>;
    std::vector<LineOwner> pending_lines;
    std::vector<std::unique_ptr<stScriptLabel>> pending_labels;
    unsigned char more = 1;
    int source_line = 0;
    int script_line = 0;
    char buffer[256];
    while (more != 0) {
        if (!ReadTextLine(handle.get(), buffer, sizeof(buffer), &more)) {
            if (more != 0 || handle->tell() < handle->size()) return 0;
            break;
        }
        if (source_line == std::numeric_limits<int>::max()) return 0;
        ++source_line;
        const std::string text(buffer);
        char* token = strtok(buffer, " \t");
        if (token == nullptr || token[0] == '*') continue;
        const auto length = strlen(token);
        if (token[length - 1] == ':') {
            token[length - 1] = 0;
            bool add_label = true;
            for (int index = 0; index < labels.GetCount(); ++index) {
                stScriptLabel* existing = *labels.GetAt(index);
                if (_strnicmp(existing->name, token, 0x1f) == 0) {
                    add_label = existing->line == -1;
                    break;
                }
            }
            for (const auto& existing : pending_labels) {
                if (_strnicmp(existing->name, token, 0x1f) == 0) {
                    add_label = false;
                    break;
                }
            }
            if (add_label) {
                auto label = std::make_unique<stScriptLabel>();
                strncpy(label->name, token, 0x1f);
                label->name[0x1f] = 0;
                label->line = script_line;
                pending_labels.push_back(std::move(label));
            }
            continue;
        }
        LineOwner line(new stScriptLine{}, release_line);
        line->text = static_cast<char*>(malloc(text.size() + 1));
        if (line->text == nullptr) throw std::bad_alloc();
        memcpy(line->text, text.c_str(), text.size() + 1);
        line->source_line = source_line;
        pending_lines.push_back(std::move(line));
        ++script_line;
    }
    if (pending_lines.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - lines.GetCount()) ||
        pending_labels.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() - labels.GetCount())) return 0;
    if (!lines.Grow(lines.GetCount() + static_cast<int>(pending_lines.size())) ||
        !labels.Grow(labels.GetCount() + static_cast<int>(pending_labels.size()))) throw std::bad_alloc();
    for (auto& line : pending_lines) {
        (void)lines.Add(line.get());
        (void)line.release();
    }
    for (auto& label : pending_labels) {
        (void)labels.Add(label.get());
        (void)label.release();
    }
    return 1;
}
catch (const std::exception&) { return false; }

/* The label table stores fixed 31-character, case-insensitive identifiers and
   the script line to resume at. A null query or a miss has the source's -1
   sentinel. */
// FUNCTION: WIZ8 0x004CF730
int stScript::FindLabelLine(const char* label) const
{
    int index;

    if (label != 0) {
        for (index = 0; index < labels.GetCount(); ++index) {
            stScriptLabel* entry = *labels.GetAt(index);
            if (_strnicmp(entry->name, label, 0x1f) == 0) {
                return entry->line;
            }
        }
    }
    return -1;
}

/* Diagnostics report the original file line carried by each parsed script
   line. Invalid or empty entries use the same -1 sentinel as label lookup. */
// FUNCTION: WIZ8 0x004CF790
int stScript::GetSourceLine(int line) const
{
    stScriptLine* entry;

    if (line < 0) {
        return -1;
    }
    if (line >= lines.GetCount()) {
        return -1;
    }
    entry = *lines.GetAt(line);
    if (entry != 0) {
        return entry->source_line;
    }
    return -1;
}
