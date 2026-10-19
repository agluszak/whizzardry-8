#include "wiz8/entities/script.h"

#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "filesystem.h"
#include "wiz8/unicode.h"

void Script::Clear()
{
    lines.clear();
    labels.clear();
}

bool Script::Load(std::string_view path)
try {
    if (path.empty()) return false;
    std::istringstream input(wiz8::open_file(wiz8::asset_path(path))->read_text());
    std::string text;
    Script pending;
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

int Script::FindLabelLine(std::string_view label) const
{
    for (const auto& entry : labels)
        if (CompareTextIgnoreAsciiCase(entry.name, label) == 0) return entry.line;
    return -1;
}

int Script::GetSourceLine(int line) const
{
    return line >= 0 && static_cast<std::size_t>(line) < lines.size() ? lines[line].source_line
                                                                      : -1;
}
