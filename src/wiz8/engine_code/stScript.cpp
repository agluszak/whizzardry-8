#include "wiz8/engine_code/stScript.h"
#include "wiz8/virtual_file.h"
#include "wiz8/filesystem.h"

#include <stdlib.h>
#include <string.h>

/* Read one byte at a time through the virtual-file layer.  End-of-file after
   at least one byte still returns a line, while an empty end-of-file clears
   `more`.  The terminator is not retained and CRLF is normalised by removing
   the CR after the loop. */
// FUNCTION: WIZ8 0x004CEE40
bool ReadTextLine(wiz8::File* handle, char* destination, int capacity, bool& more)
try
{
    bool result;
    unsigned int transferred;
    char character;
    int length = 0;

    destination[0] = 0;
    more = true;

    for (;;) {
        result = ((transferred = handle->read(&character, 1).bytes) == static_cast<std::size_t>(1));
        if (transferred == 0) {
            result = length != 0;
            more = false;
            break;
        }
        if (!result) {
            more = false;
        } else {
            if (character == '\n') {
                break;
            }
            destination[length++] = character;
        }
        if (length >= capacity - 1) {
            result = false;
            break;
        }
        if (!result) {
            break;
        }
    }

    destination[length] = 0;
    if (length != 0 && destination[length - 1] == '\r') {
        destination[length - 1] = 0;
    }
    return result;
}
catch (const std::exception&) { more = false; return false; }

// FUNCTION: WIZ8 0x004CF690
void stScript::Clear()
{
    lines.clear();
    labels.clear();
}

// FUNCTION: WIZ8 0x004CF3B0
bool stScript::Load(const char* path)
try
{
    bool more = true;
    int source_line = 0;
    int script_line = 0;
    char buffer[256];
    if (!path) return false;
    auto handle = wiz8::open_file(path);

    for (;;) {
        if (!more) {
            return true;
        }
        while (!ReadTextLine(handle.get(), buffer, sizeof(buffer), more)) {
            if (!more) {
                return true;
            }
        }

        ++source_line;
        if (buffer[0] == 0) {
            continue;
        }

        std::string text = buffer;
        char* token = strtok(buffer, " \t");
        if (token == 0 || token[0] == '*') {
            continue;
        }

        int length = strlen(token);
        if (token[length - 1] == ':') {
            token[length - 1] = 0;
            if (FindLabelLine(token) == -1) {
                auto& label = labels.emplace_back();
                strncpy(label.name, token, sizeof(label.name) - 1);
                label.name[sizeof(label.name) - 1] = '\0';
                label.line = script_line;
            }
            continue;
        }

        lines.push_back({std::move(text), source_line});
        ++script_line;
    }
}

catch (const std::exception&) { return false; }

/* The label table stores fixed 31-character, case-insensitive identifiers and
   the script line to resume at. A null query or a miss has the source's -1
   sentinel. */
// FUNCTION: WIZ8 0x004CF730
int stScript::FindLabelLine(const char* label) const
{
    if (label) {
        for (const auto& entry : labels) {
            if (_strnicmp(entry.name, label, 0x1f) == 0) return entry.line;
        }
    }
    return -1;
}

/* Diagnostics report the original file line carried by each parsed script
   line. Invalid or empty entries use the same -1 sentinel as label lookup. */
// FUNCTION: WIZ8 0x004CF790
int stScript::GetSourceLine(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < lines.size()
               ? lines[line].source_line : -1;
}
