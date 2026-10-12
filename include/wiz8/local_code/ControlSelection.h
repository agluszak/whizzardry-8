#pragma once

#include "wiz8/local_code/TextControl.h"
#include <functional>
#include <vector>

// Screen-owned radio selection; entries are borrowed from their visual owner.
class W8ControlSelection {
public:
    void AddEntry(W8TextControl* entry);
    void SetSelected(int selected, bool notify = true);
    int m_selectedIndex = -1;
    std::vector<W8TextControl*> m_lsButtons;
    std::function<void(int)> on_selection;
};
