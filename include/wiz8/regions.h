#ifndef WIZ8_REGIONS_H
#define WIZ8_REGIONS_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <SDL3/SDL_rect.h>

#include "input.h"

class W8Widget;
struct TEXTINPUTNODE;

extern bool g_dev_mode;

inline constexpr int REGION_PRIORITY_BACKGROUND = -1;
inline constexpr int REGION_PRIORITY_LOWEST = 0;
inline constexpr int REGION_PRIORITY_LOW = 15;
inline constexpr int REGION_PRIORITY_BASE = 31;
inline constexpr int REGION_PRIORITY_NORMAL = REGION_PRIORITY_BASE;
inline constexpr int REGION_PRIORITY_HIGH = 63;
inline constexpr int REGION_PRIORITY_HIGHEST = 127;
inline constexpr int REGION_PRIORITY_AUTO = -1;

enum : std::int32_t {
    REGION_REASON_NONE = 0,
    REGION_REASON_INIT = 1,
    REGION_REASON_MOVE = 2,
    REGION_REASON_LBUTTON_DWN = 4,
    REGION_REASON_LBUTTON_UP = 8,
    REGION_REASON_RBUTTON_DWN = 16,
    REGION_REASON_RBUTTON_UP = 32,
    REGION_REASON_LOST_MOUSE = 64,
    REGION_REASON_GAIN_MOUSE = 128,
    REGION_REASON_LBUTTON_REPEAT = 256,
    REGION_REASON_RBUTTON_REPEAT = 512,
    REGION_REASON_LBUTTON_DOUBLECLICK = 1024
};

inline constexpr std::int32_t REGION_REASON_BUTTONS =
    REGION_REASON_LBUTTON_DWN | REGION_REASON_LBUTTON_UP |
    REGION_REASON_RBUTTON_DWN | REGION_REASON_RBUTTON_UP;

// Hit-test precedence between region sets: lower orders win, and sets with the
// same order resolve by creation sequence. Screen sets pass the retail catalog
// set index they replaced; everything else is dynamic.
inline constexpr int kDynamicRegionSetOrder = 0x33;

struct W8RegionSet {
    W8RegionSet() = default;
    unsigned int enabled = 0;
    int order = kDynamicRegionSetOrder;
    std::uint64_t sequence = 0;
    std::span<const unsigned int> Regions() const { return {region_ids.data(), active_count}; }
    void Reset() { active_count = 0; }
    unsigned int AddRegion(int priority = 0);
    void AttachRegion(unsigned int region_index);
    bool RemoveRegion(unsigned int region_index);

private:
    // Keep slots for screen rebuilds, without assuming contiguous allocation.
    std::vector<unsigned int> region_ids;
    std::size_t active_count = 0;
};

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
typedef unsigned char (*W8RegionCallback)(const InputEvent* event, struct W8Region* region);

struct RegionTemplate {
    std::uint32_t flags;
    std::int16_t x1;
    std::int16_t y1;
    std::int16_t x2;
    std::int16_t y2;
    W8RegionCallback callback;
    std::uint16_t callback_id;
    bool help_enabled;
    std::int32_t help_text_id;
};

class W8RegionHandler {
public:
    virtual int HandleRegionInput(W8Region& region, const InputEvent& input) = 0;

protected:
    ~W8RegionHandler() = default;
};

struct W8Region {
    bool CaptureButtonDown(InputEventType event)
    {
        if (event == InputEventType::RightButtonDown) {
            flags |= W8_REGION_RIGHT_BUTTON_HELD;
            return true;
        }
        if (event == InputEventType::LeftButtonDown) {
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
    std::uint64_t binding_generation = 0; // Detect replacement during callback dispatch.
    int priority = 0;
    std::string help_text{};
    TEXTINPUTNODE* text_field = nullptr;
    W8RegionHandler* handler = nullptr;
};

class OwnedRegion {
public:
    OwnedRegion() = default;
    OwnedRegion(unsigned int set, int priority, W8RegionHandler& handler);
    OwnedRegion(unsigned int set, const RegionTemplate& entry);
    ~OwnedRegion();
    OwnedRegion(const OwnedRegion&) = delete;
    OwnedRegion& operator=(const OwnedRegion&) = delete;
    OwnedRegion(OwnedRegion&& other) noexcept;
    OwnedRegion& operator=(OwnedRegion&& other) noexcept;

    explicit operator bool() const;
    W8Region& region();
    const W8Region& region() const;
    unsigned int index() const { return index_; }
    void reset();

private:
    friend void UnbindWidgetRegion(unsigned int region_index, W8Widget* widget, OwnedRegion* owner);

    unsigned int set_ = 0;
    unsigned int index_ = 0;
    uint64_t generation_ = 0;
};

class OwnedRegionSet {
public:
    explicit OwnedRegionSet(std::span<const RegionTemplate> entries,
                            int order = kDynamicRegionSetOrder);
    ~OwnedRegionSet();
    OwnedRegionSet(const OwnedRegionSet&) = delete;
    OwnedRegionSet& operator=(const OwnedRegionSet&) = delete;

    unsigned int set() const { return set_; }
    std::size_t size() const { return regions_.size(); }
    W8Region& operator[](std::size_t index) { return regions_.at(index).region(); }
    unsigned int index(std::size_t index) const { return regions_.at(index).index(); }
    void Enable();
    void Disable();
    void EnableInput();
    void DisableInput();
    bool IsEnabled() const;

private:
    unsigned int set_ = 0;
    std::vector<OwnedRegion> regions_;
};

// An owned region set over a constexpr template table, created and destroyed with its screen.
class ScreenRegionSet {
public:
    explicit ScreenRegionSet(std::span<const RegionTemplate> templates,
                             int order = kDynamicRegionSetOrder)
        : templates_(templates), order_(order)
    {
    }
    void Create();
    void Destroy();
    explicit operator bool() const { return set_.has_value(); }
    OwnedRegionSet& operator*() { return set_.value(); }
    OwnedRegionSet* operator->() { return &set_.value(); }
    void Enable();
    void Disable();
    void EnableInput();
    void DisableInput();

private:
    std::span<const RegionTemplate> templates_;
    int order_;
    std::optional<OwnedRegionSet> set_;
};

// A second region set over regions owned by an OwnedRegionSet. It never owns or frees regions.
// Destroy it before its source.
class RegionSetView {
public:
    RegionSetView(const OwnedRegionSet& source, std::size_t first, std::size_t count,
                  int order = kDynamicRegionSetOrder);
    ~RegionSetView();
    RegionSetView(const RegionSetView&) = delete;
    RegionSetView& operator=(const RegionSetView&) = delete;

    unsigned int set() const { return set_; }
    void Enable();
    void Disable();
    bool IsEnabled() const;

private:
    unsigned int set_ = 0;
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
        std::uint64_t generation;
        unsigned int flags;
    };
    unsigned int set_;
    std::uint64_t sequence_ = 0;
    SavedRegionState saved_current_{};
    SavedRegionState saved_captured_{};
    SavedRegionState saved_hover_{};
};

unsigned char InitializeRegionHelpState(void);

/* Text-box static-catalog callbacks, implemented in Local
   Screens\MGSTextBox.cpp. */
unsigned char TextBoxScrollUpRegionEvent(const InputEvent*, W8Region*);    /* 0x0058E2A0 */
unsigned char TextBoxScrollDownRegionEvent(const InputEvent*, W8Region*);  /* 0x0058E650 */
unsigned char TextBoxScrollThumbRegionEvent(const InputEvent*, W8Region*); /* 0x0058E9F0 */
unsigned char TextBoxBodyRegionEvent(const InputEvent*, W8Region*);        /* 0x0058ED90 */
unsigned char TextBoxChannelTabRegionEvent(const InputEvent*, W8Region*);  /* 0x0058EFD0 */
unsigned char TextBoxMuteRegionEvent(const InputEvent*, W8Region*);        /* 0x0058F240 */

extern std::deque<W8RegionSet> g_region_sets;
extern std::deque<W8Region> g_regions;
extern int g_region_help_delay;
extern std::uint64_t g_region_help_clock;
extern unsigned int g_current_region_index;
extern unsigned int g_captured_region_index;
extern unsigned int g_hover_region_index;
extern bool g_region_help_force_enabled;
extern std::string g_default_help_text;

unsigned int GetForcedRegion(void);
void ReleaseDefaultHelpText(void);
unsigned int UpdateRegionMousePosition(int x, int y);
SDL_Point GetRegionMousePosition();
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
unsigned int CreateRegionSet(int order = kDynamicRegionSetOrder);
void DestroyRegionSet(unsigned int region_set_index);
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
unsigned int ControlRegionSetIndex(void);
std::size_t LiveDynamicRegionCount(void);
void CaptureInputSink(void);
void ReleaseInputSinkCapture(void);
void UnbindWidgetRegion(unsigned int region_index, W8Widget* widget, OwnedRegion* owner);
std::int32_t RegionReasonForInput(const W8Region& region, const InputEvent& input);
void EnableRegionHelp(unsigned int region_index);
void DisableRegionHelp(unsigned int region_index);
unsigned char ClearActiveRegionIfMatches(unsigned int region_index);
void ActivateDialogRegion(unsigned int region_index); /* 0x004F2040 */
/* 0x004F1910 returns the byte produced by the selected region callback. */
unsigned char DispatchRegionInput(const InputEvent* event);

void SetRegionHelpForceEnabled(bool enabled);
void EnableRegionHelpFlag(W8Region* region);
void DisableRegionHelpFlag(W8Region* region);


void ResetRegions(void);

#endif
