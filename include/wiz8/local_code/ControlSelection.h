#pragma once

#include <cstdint>
#include "wiz8/local_code/TextControl.h"
#include <vector>
#include <algorithm>

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

class W8ControlSelection;

class W8ControlSelectionListener {
public:
    virtual void OnSelectionChanged(W8ControlSelection* control, std::int32_t selected) = 0;
};

/* A radio group of text controls: selecting one deselects the rest. */
// VTABLE: WIZ8 0x005ed654
class W8ControlSelection : public W8TextControl::Listener {
public:
    W8ControlSelection();
    virtual ~W8ControlSelection() = default;
    std::int32_t AddEntry(W8TextControl* entry);
    void SetSelected(std::int32_t iSelected);
    virtual void OnPrimary(W8TextControl* entry) override;
    virtual void OnSecondary(W8TextControl*) override {}

public:
    std::int32_t m_value_4;
    std::int32_t m_value_8;
    std::int32_t m_selectedIndex;
    std::vector<W8TextControl*> m_lsButtons;
    W8ControlSelectionListener* m_selectionListener;
};
