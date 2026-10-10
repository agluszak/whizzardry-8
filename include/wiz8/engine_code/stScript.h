#pragma once

#include "surrender/srTypeRegistry.h"
#include <vector>
#include <algorithm>

struct stScriptLine {
    char* text;
    int source_line;
};

struct stScriptLabel {
    char name[0x20];
    int line;
};

/* Owns parsed script lines and labels. Clear frees the records and their text. */
class stScript : public srClassSupport<stScript, srClass, 0x1000d> {
public:
    static const char* sGetClassName()
    {
        return "stScript";
    }

    virtual ~stScript() override;

    stScript& operator=(const stScript&) = default;
    virtual srClass* vInstance() override;

    int FindLabelLine(const char* label) const;
    int GetSourceLine(int line) const;
    unsigned char Load(const char* path);
    void Clear();

    std::vector<stScriptLine*> lines;   /* 0x18 */
    std::vector<stScriptLabel*> labels; /* 0x28 */
};

W8_ABI_ASSERT(sizeof(stScript) == 0x38, "stScript_must_be_0x38");
