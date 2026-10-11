#include <string>
#include <string_view>
#ifndef WIZ8_REGIONS_H
#define WIZ8_REGIONS_H

#include <stddef.h>
#include <deque>
#include <vector>
#include <span>
#include <string>

#include "Types.h"
#include "input.h"

class W8Widget;
struct _GUI_BUTTON;
struct TEXTINPUTNODE;

extern bool g_dev_mode;

struct W8RegionSet {
    W8RegionSet() = default;
    W8RegionSet(unsigned int enabled, unsigned int first, unsigned int last)
        : enabled(enabled)
    {
        for (auto index = first; index <= last; ++index) region_ids.push_back(index);
        active_count = region_ids.size();
    }
    unsigned int enabled = 0;
    bool persistent = false;
    std::span<const unsigned int> Regions() const { return {region_ids.data(), active_count}; }
    void Reset() { active_count = 0; }
    unsigned int AddRegion(int priority = 0);
    bool RemoveRegion(unsigned int region_index);

private:
    // Keep slots for screen rebuilds, without assuming contiguous allocation.
    std::vector<unsigned int> region_ids;
    size_t active_count = 0;
};

/* Region callbacks receive SGP's input atom unchanged. Wheel rotation uses
   usParam; uiParam carries the packed cursor position. */
W8_ABI_ASSERT(sizeof(InputAtom) == 0x10, "InputAtom_size");
W8_ABI_ASSERT(offsetof(InputAtom, usParam) == 0x08, "InputAtom_wheel_payload_offset");
W8_ABI_ASSERT(offsetof(InputAtom, uiParam) == 0x0c, "InputAtom_mouse_position_offset");

enum W8RegionFlags {
    W8_REGION_RECTANGLE = 0x01,
    W8_REGION_CIRCLE = 0x02,
    W8_REGION_INPUT_DISABLED = 0x04,
    W8_REGION_INPUT_MODE_MASK = 0x0c,
    W8_REGION_MOUSE_ENTER = 0x10,
    W8_REGION_MOUSE_LEAVE = 0x20,
    W8_REGION_LEFT_BUTTON_HELD = 0x40,
    W8_REGION_RIGHT_BUTTON_HELD = 0x80,
    W8_REGION_MOUSE_TRANSITION_MASK = 0x30,
    W8_REGION_MOUSE_STATE_MASK = 0xf0,
    W8_REGION_HELP_SHOWN = 0x200
};

struct W8Region;
typedef unsigned char (*W8RegionCallback)(const InputAtom* event, struct W8Region* region);

struct W8Region {
    bool CaptureButtonDown(unsigned int event)
    {
        if (event == RIGHT_BUTTON_DOWN) {
            flags |= W8_REGION_RIGHT_BUTTON_HELD;
            return true;
        }
        if (event == LEFT_BUTTON_DOWN) {
            flags |= W8_REGION_LEFT_BUTTON_HELD;
            return true;
        }
        return false;
    }

    unsigned int flags;
    short x1;
    short y1;
    short x2;
    short y2;
    W8RegionCallback callback;
    unsigned short callback_id;
    bool help_enabled;
    unsigned char unknown_13;
    int help_text_id;
    W8Widget* widget; // Non-owning callback target; cleared by widget teardown.
    uint64_t binding_generation = 0; // Detect replacement during callback dispatch.
    int priority = 0;
    std::string help_text;
    _GUI_BUTTON* button = nullptr;
    TEXTINPUTNODE* text_field = nullptr;
};

class ExclusiveRegionSet {
public:
    explicit ExclusiveRegionSet(unsigned int set);
    ~ExclusiveRegionSet();
    ExclusiveRegionSet(const ExclusiveRegionSet&) = delete;
    ExclusiveRegionSet& operator=(const ExclusiveRegionSet&) = delete;

private:
    struct SavedRegionState {
        unsigned int index;
        uint64_t generation;
        unsigned int flags;
    };
    unsigned int set_;
    SavedRegionState saved_current_{};
    SavedRegionState saved_captured_{};
    SavedRegionState saved_hover_{};
};

unsigned char InitializeRegionHelpState(void);

/* Text-box static-catalog callbacks, implemented in Local
   Screens\MGSTextBox.cpp. */
unsigned char TextBoxScrollUpRegionEvent(const InputAtom*, W8Region*);    /* 0x0058E2A0 */
unsigned char TextBoxScrollDownRegionEvent(const InputAtom*, W8Region*);  /* 0x0058E650 */
unsigned char TextBoxScrollThumbRegionEvent(const InputAtom*, W8Region*); /* 0x0058E9F0 */
unsigned char TextBoxBodyRegionEvent(const InputAtom*, W8Region*);        /* 0x0058ED90 */
unsigned char TextBoxChannelTabRegionEvent(const InputAtom*, W8Region*);  /* 0x0058EFD0 */
unsigned char TextBoxMuteRegionEvent(const InputAtom*, W8Region*);        /* 0x0058F240 */

extern std::deque<W8RegionSet> g_region_sets;
extern std::deque<W8Region> g_regions;
extern int g_region_help_delay;
extern uint64_t g_region_help_clock;
extern unsigned int g_current_region_index;
extern unsigned int g_captured_region_index;
extern unsigned int g_hover_region_index;
extern bool g_region_help_force_enabled;
extern std::string g_default_help_text;

unsigned int GetForcedRegion(void);
void ReleaseDefaultHelpText(void);
unsigned int UpdateRegionMousePosition(int x, int y);
unsigned int FindRegionAtPoint(unsigned short x, unsigned short y);
void RegionSetEnable(unsigned int region_set_index);
void RegionSetDisable(unsigned int region_set_index);
void EnableRegionSetInput(unsigned int region_set_index);
void DisableRegionSetInput(unsigned int region_set_index);
void EnableRegionInput(unsigned int region_index);
void DisableRegionInput(unsigned int region_index);
void SetRegionBounds(unsigned int region_index, unsigned short x1, unsigned short y1,
                     unsigned short x2, unsigned short y2);
bool RegionContainsPoint(unsigned int region_index, unsigned short x, unsigned short y);
bool RegionHasFlags(unsigned int region_index, unsigned int flags);
unsigned int CreateRegionSet(void);
void ResetRegionSet(unsigned int region_set_index);
unsigned int AddRegionToSet(unsigned int region_set_index, int priority = 0);
void RemoveRegionFromSet(unsigned int region_set_index, unsigned int region_index);
void SetRegionCallback(unsigned int region_index, W8RegionCallback callback,
                       unsigned short callback_id);
void SetRegionHelp(unsigned int region_index, bool enabled, int help_text_id);
void ClearHotRegion(void);
void UpdateRegionHelp(void);
void ShowRegionHelp(unsigned int region_index);
void SetRegionHelpText(std::string_view text);
void ResetRegionHelp(bool delayed);
void SetRegionHelpDelay(int delay_ms);
void EnableRegionHelp(unsigned int region_index);
void DisableRegionHelp(unsigned int region_index);
unsigned char ClearActiveRegionIfMatches(unsigned int region_index);
void ActivateDialogRegion(unsigned int region_index); /* 0x004F2040 */
/* 0x004F1910 returns the byte produced by the selected region callback. */
unsigned char DispatchRegionInput(const InputAtom* event);

void SetRegionHelpForceEnabled(bool enabled);
void EnableRegionHelpFlag(W8Region* region);
void DisableRegionHelpFlag(W8Region* region);


void ResetRegions(void);

#endif
