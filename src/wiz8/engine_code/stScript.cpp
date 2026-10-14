#include "wiz8/engine_code/stScript.h"

#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "filesystem.h"
#include "wiz8/unicode.h"
#include "wiz8/virtual_file.h"

/* Read one byte at a time through the virtual-file layer.  End-of-file after
   at least one byte still returns a line, while an empty end-of-file clears
   `more`.  The terminator is not retained and CRLF is normalised by removing
   the CR after the loop. */
// FUNCTION: WIZ8 0x004CEE40
bool ReadTextLine(wiz8::File* handle, char* destination, int capacity, bool& more)
try {
    more = 0;
    if (capacity <= 0) return 0;
    destination[0] = 0;
    if (capacity == 1) return 0;
    more = 1;
    int length = 0;
    for (;;) {
        char character;
        if (handle->read(&character, sizeof(character)).eof) {
            more = 0;
            break;
        }
        if (character == '\n') break;
        destination[length++] = character;
        destination[length] = 0;
        if (length == capacity - 1) return 0;
    }
    if (length != 0 && destination[length - 1] == '\r') destination[length - 1] = 0;
    return more != 0 || length != 0;
} catch (const std::exception&) {
    more = 0;
    return false;
}

// FUNCTION: WIZ8 0x004CF690
void stScript::Clear()
{
    lines.clear();
    labels.clear();
}

// FUNCTION: WIZ8 0x004CF3B0
bool stScript::Load(const char* path)
try {
    if (!path) return false;
    std::istringstream input(wiz8::open_file(path)->read_text());
    std::string text;
    stScript pending;
    int source_line = 0;
    while (std::getline(input, text)) {
        if (source_line == std::numeric_limits<int>::max()) return false;
        ++source_line;
        if (text.ends_with('\r')) text.pop_back();
        const auto start = text.find_first_not_of(" \t");
        if (start == text.npos || text[start] == '*') continue;
        const auto stop = text.find_first_of(" \t", start);
        std::string_view token(text.data() + start,
                               (stop == text.npos ? text.size() : stop) - start);
        if (token.ends_with(':')) {
            token.remove_suffix(1);
            if (FindLabelLine(token) == -1 && pending.FindLabelLine(token) == -1)
                pending.labels.push_back(
                    {std::string(token), static_cast<int>(lines.size() + pending.lines.size())});
        } else {
            if (lines.size() + pending.lines.size() >=
                static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return false;
            pending.lines.push_back({std::move(text), source_line});
        }
    }
    lines.reserve(lines.size() + pending.lines.size());
    labels.reserve(labels.size() + pending.labels.size());
    for (auto& line : pending.lines) lines.push_back(std::move(line));
    for (auto& label : pending.labels) labels.push_back(std::move(label));
    return true;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004CF730
int stScript::FindLabelLine(std::string_view label) const
{
    for (const auto& entry : labels)
        if (CompareTextIgnoreAsciiCase(entry.name, label) == 0) return entry.line;
    return -1;
}

// FUNCTION: WIZ8 0x004CF790
int stScript::GetSourceLine(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < lines.size() ? lines[line].source_line
                                                                      : -1;
}
