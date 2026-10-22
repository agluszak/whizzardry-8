#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ScriptLine {
    std::string text;
    int source_line;
};

struct ScriptLabel {
    std::string name;
    int line;
};

/* Owns parsed script lines and labels. */
class Script {
public:
    const std::string& getName() const { return name; }
    void setName(std::string name) { this->name = std::move(name); }

    int FindLabelLine(std::string_view label) const;
    int GetSourceLine(int line) const;
    bool Load(std::string_view path);
    void Clear();

    std::string name;
    std::vector<ScriptLine> lines;
    std::vector<ScriptLabel> labels;
};
