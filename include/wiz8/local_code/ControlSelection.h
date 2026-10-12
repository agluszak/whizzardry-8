#pragma once

#include "wiz8/local_code/TextControl.h"
#include <vector>
#include <algorithm>

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

class W8ControlSelection;

class W8ControlSelectionListener {
public:
    virtual void OnSelectionChanged(W8ControlSelection* control, int selected) = 0;
};

/* A radio group of text controls: selecting one deselects the rest. */
// VTABLE: WIZ8 0x005ed654
class W8ControlSelection : public W8TextControl::Listener {
public:
    W8ControlSelection();
    virtual ~W8ControlSelection() = default;
    int AddEntry(W8TextControl* entry);
    void SetSelected(int iSelected);
    virtual void OnPrimary(W8TextControl* entry) override;
    virtual void OnSecondary(W8TextControl*) override {}

public:
    int m_selectedIndex;
    std::vector<W8TextControl*> m_lsButtons;
    W8ControlSelectionListener* m_selectionListener;
};
