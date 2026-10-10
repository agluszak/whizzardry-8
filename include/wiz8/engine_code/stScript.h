#pragma once

#include <vector>
#include <string>

struct stScriptLine {
    char* text;
    int source_line;
};

struct stScriptLabel {
    char name[0x20];
    int line;
};

/* Owns parsed script lines and labels. */
class stScript {
public:
    ~stScript();

    const std::string& getName() const
    {
        return name;
    }
    void setName(std::string name)
    {
        this->name = std::move(name);
    }

    int FindLabelLine(const char* label) const;
    int GetSourceLine(int line) const;
    unsigned char Load(const char* path);
    void Clear();

    std::string name;
    std::vector<stScriptLine*> lines;   /* 0x18 */
    std::vector<stScriptLabel*> labels; /* 0x28 */
};
