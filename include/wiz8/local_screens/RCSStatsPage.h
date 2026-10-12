#include "wiz8/ui_layer.h"
#pragma once

#include "wiz8/regions.h"
#include "wiz8/ui_layer.h"
#include "wiz8/local_code/ButtonWidget.h"
#include <array>
#include <memory>
#include "wiz8/local_code/RangeControl.h"
#include "input.h"
#include "compat/compiler.h"

struct W8Region;

/* One row of the stats page's effect list: a character condition, an
   enchantment, or the modifiers of one equipped item. */
struct W8CampEffectEntry {
    bool items;          /* row lists an equipped item */
    bool visible;        /* passes the current filter */
    bool beneficial;
    bool detrimental;
    int kind;        /* 0 condition, 1 enchantment, 2 equipment */
    int index;       /* condition, enchantment or equipment-slot index */
    int enchantment; /* the enchantment id for kind 1 */
    int turns;       /* remaining turns; 9999 is permanent */
    int lines;       /* rendered height in 0xe-pixel lines */
};

/* Camp stats page (page 1) and skills page (page 2) machinery. The scrollbar and
   the three toggle buttons drive the stats page's condition/equipment effect
   list: button 0 selects beneficial rows, button 1 detrimental rows and
   button 2 switches the list to equipped items. */

class W8CampStatsControls {
public:
    W8CampStatsControls();
    void Redraw(SDL_Surface& target, bool force);
    void InvalidateAll();
    void SelectFilter(int index);
    UiLayer input;
    std::array<std::unique_ptr<W8ButtonWidget>, 3> m_buttons;
};

/* Camp page-1 renderer - attributes, traits and the effect list trigger. */
void DrawCampStatsPage(void);
void DrawCampSkillsPage(void);

/* Rebuilds the stats page's effect list from the reviewed character's
   conditions, enchantments and equipped items, then refilters it. Called when
   the reviewed character changes and when the stats page is entered. */
void RebuildCampEffectList(void);
void FilterCampEffectList(void);

/* Lazily creates and enables the five per-category skill regions of camp page
   2; DisableCampSkillRegions releases them on camp leave. */
void CreateCampSkillRegions(void);
void DisableCampSkillRegions(void);
UiResult HandleCampSkillList(const UiPointerEvent& event, int category);
