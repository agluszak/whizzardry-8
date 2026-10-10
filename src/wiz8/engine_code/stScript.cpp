#include <sstream>
#include "input.h"
#include <string_view>
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
unsigned char ReadTextLine(wiz8::File* handle, char* destination, int capacity, unsigned char* more)
try
{
    unsigned char result;
    unsigned int transferred;
    char character;
    int length = 0;

    destination[0] = 0;
    *more = 1;

    for (;;) {
        result = ((transferred = handle->read(&character, 1).bytes) == static_cast<std::size_t>(1));
        if (transferred == 0) {
            result = length != 0;
            *more = 0;
            break;
        }
        if (result == 0) {
            *more = 0;
        } else {
            if (character == '\n') {
                break;
            }
            destination[length++] = character;
        }
        if (length >= capacity - 1) {
            result = 0;
            break;
        }
        if (result == 0) {
            break;
        }
    }

    destination[length] = 0;
    if (length != 0 && destination[length - 1] == '\r') {
        destination[length - 1] = 0;
    }
    return result;
}
catch (const std::exception&) { *more = 0; return false; }

// FUNCTION: WIZ8 0x004CF690
void stScript::Clear()
{
    lines.clear();
    labels.clear();
}

// FUNCTION: WIZ8 0x004CF3B0
unsigned char stScript::Load(const char* path)
try {
    if (!path)
        return false;
    std::istringstream input(wiz8::open_file(path)->read_text());
    std::string text;
    int source_line = 0;
    while (std::getline(input, text)) {
        ++source_line;
        if (text.ends_with('\r'))
            text.pop_back();
        const auto start = text.find_first_not_of(" \t");
        if (start == text.npos || text[start] == '*')
            continue;
        const auto stop = text.find_first_of(" \t", start);
        std::string_view token(text.data() + start,
                               (stop == text.npos ? text.size() : stop) - start);
        if (token.ends_with(':')) {
            token.remove_suffix(1);
            if (FindLabelLine(token) == -1)
                labels.push_back({std::string(token), static_cast<int>(lines.size())});
        } else {
            lines.push_back({std::move(text), source_line});
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004CF730
int stScript::FindLabelLine(std::string_view label) const
{
    for (const auto& entry : labels)
        if (CompareTextIgnoreAsciiCase(entry.name, label) == 0)
            return entry.line;
    return -1;
}

// FUNCTION: WIZ8 0x004CF790
int stScript::GetSourceLine(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < lines.size() ? lines[line].source_line
                                                                      : -1;
}
