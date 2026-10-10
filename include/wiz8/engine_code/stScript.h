#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <utility>

struct stScriptLine {
    std::string text;
    int source_line;
};

struct stScriptLabel {
    std::string name;
    int line;
};

/* Owns parsed script lines and labels. */
class stScript {
public:
    const std::string& getName() const
    {
        return name;
    }
    void setName(std::string name)
    {
        this->name = std::move(name);
    }

    int FindLabelLine(std::string_view label) const;
    int GetSourceLine(int line) const;
    bool Load(const char* path);
    void Clear();

    std::string name;
    std::vector<stScriptLine> lines;
    std::vector<stScriptLabel> labels;
};
