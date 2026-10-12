#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <utility>

struct stScriptLine {
    std::string text;
    std::int32_t source_line;
};

struct stScriptLabel {
    std::string name;
    std::int32_t line;
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

    std::int32_t FindLabelLine(std::string_view label) const;
    std::int32_t GetSourceLine(std::int32_t line) const;
    bool Load(const char* path);
    void Clear();

    std::string name;
    std::vector<stScriptLine> lines;
    std::vector<stScriptLabel> labels;
};
