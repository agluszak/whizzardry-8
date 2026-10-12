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

inline constexpr std::int32_t REGION_PRIORITY_BACKGROUND = -1;
inline constexpr std::int32_t REGION_PRIORITY_LOWEST = 0;
inline constexpr std::int32_t REGION_PRIORITY_LOW = 15;
inline constexpr std::int32_t REGION_PRIORITY_BASE = 31;
inline constexpr std::int32_t REGION_PRIORITY_NORMAL = REGION_PRIORITY_BASE;
inline constexpr std::int32_t REGION_PRIORITY_HIGH = 63;
inline constexpr std::int32_t REGION_PRIORITY_HIGHEST = 127;
inline constexpr std::int32_t REGION_PRIORITY_AUTO = -1;

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
inline constexpr std::int32_t kDynamicRegionSetOrder = 0x33;

struct W8RegionSet {
    W8RegionSet() = default;
    std::uint32_t enabled = 0;
    std::int32_t order = kDynamicRegionSetOrder;
    std::uint64_t sequence = 0;
    std::span<const std::uint32_t> Regions() const { return {region_ids.data(), active_count}; }
    void Reset() { active_count = 0; }
    std::uint32_t AddRegion(std::int32_t priority = 0);
    void AttachRegion(std::uint32_t region_index);
    bool RemoveRegion(std::uint32_t region_index);

private:
    // Keep slots for screen rebuilds, without assuming contiguous allocation.
    std::vector<std::uint32_t> region_ids;
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
typedef std::uint8_t (*W8RegionCallback)(const InputEvent* event, struct W8Region* region);

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
    virtual std::int32_t HandleRegionInput(W8Region& region, const InputEvent& input) = 0;

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

    std::uint32_t flags;
    std::int16_t x1;
    std::int16_t y1;
    std::int16_t x2;
    std::int16_t y2;
    W8RegionCallback callback;
    std::uint16_t callback_id;
    bool help_enabled;
    std::uint8_t unknown_13;
    std::int32_t help_text_id;
    W8Widget* widget; // Non-owning callback target; cleared by widget teardown.
    std::uint64_t binding_generation = 0; // Detect replacement during callback dispatch.
    std::int32_t priority = 0;
    std::string help_text{};
    TEXTINPUTNODE* text_field = nullptr;
    W8RegionHandler* handler = nullptr;
};

class OwnedRegion {
public:
    OwnedRegion() = default;
    OwnedRegion(std::uint32_t set, std::int32_t priority, W8RegionHandler& handler);
    OwnedRegion(std::uint32_t set, const RegionTemplate& entry, std::int32_t priority = REGION_PRIORITY_LOWEST);
    ~OwnedRegion();
    OwnedRegion(const OwnedRegion&) = delete;
    OwnedRegion& operator=(const OwnedRegion&) = delete;
    OwnedRegion(OwnedRegion&& other) noexcept;
    OwnedRegion& operator=(OwnedRegion&& other) noexcept;

    explicit operator bool() const;
    W8Region& region();
    const W8Region& region() const;
    std::uint32_t index() const { return index_; }
    std::uint32_t set() const { return set_; }
    void reset();

private:
    friend void UnbindWidgetRegion(std::uint32_t region_index, W8Widget* widget, OwnedRegion* owner);

    std::uint32_t set_ = 0;
    std::uint32_t index_ = 0;
    std::uint64_t generation_ = 0;
};

class OwnedRegionSet {
public:
    explicit OwnedRegionSet(std::span<const RegionTemplate> entries,
                            std::int32_t order = kDynamicRegionSetOrder);
    ~OwnedRegionSet();
    OwnedRegionSet(const OwnedRegionSet&) = delete;
    OwnedRegionSet& operator=(const OwnedRegionSet&) = delete;

    std::uint32_t set() const { return set_; }
    std::size_t size() const { return regions_.size(); }
    W8Region& operator[](std::size_t index) { return regions_.at(index).region(); }
    std::uint32_t index(std::size_t index) const { return regions_.at(index).index(); }
    void Enable();
    void Disable();
    void EnableInput();
    void DisableInput();
    bool IsEnabled() const;

private:
    std::uint32_t set_ = 0;
    std::vector<OwnedRegion> regions_;
};

// An owned region set over a constexpr template table, created and destroyed with its screen.
class ScreenRegionSet {
public:
    explicit ScreenRegionSet(std::span<const RegionTemplate> templates,
                             std::int32_t order = kDynamicRegionSetOrder)
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
    std::int32_t order_;
    std::optional<OwnedRegionSet> set_;
};

// A second region set over regions owned by an OwnedRegionSet. It never owns or frees regions.
// Destroy it before its source.
class RegionSetView {
public:
    RegionSetView(const OwnedRegionSet& source, std::size_t first, std::size_t count,
                  std::int32_t order = kDynamicRegionSetOrder);
    ~RegionSetView();
    RegionSetView(const RegionSetView&) = delete;
    RegionSetView& operator=(const RegionSetView&) = delete;

    std::uint32_t set() const { return set_; }
    void Enable();
    void Disable();
    bool IsEnabled() const;

private:
    std::uint32_t set_ = 0;
};

class ExclusiveRegionSet {
public:
    explicit ExclusiveRegionSet(std::uint32_t set);
    ~ExclusiveRegionSet();
    ExclusiveRegionSet(const ExclusiveRegionSet&) = delete;
    ExclusiveRegionSet& operator=(const ExclusiveRegionSet&) = delete;

private:
    struct SavedRegionState {
        std::uint32_t index;
        std::uint64_t generation;
        std::uint32_t flags;
    };
    std::uint32_t set_;
    std::uint64_t sequence_ = 0;
    SavedRegionState saved_current_{};
    SavedRegionState saved_captured_{};
    SavedRegionState saved_hover_{};
};

std::uint8_t InitializeRegionHelpState(void);

/* Text-box static-catalog callbacks, implemented in Local
   Screens\MGSTextBox.cpp. */
std::uint8_t TextBoxScrollUpRegionEvent(const InputEvent*, W8Region*);    /* 0x0058E2A0 */
std::uint8_t TextBoxScrollDownRegionEvent(const InputEvent*, W8Region*);  /* 0x0058E650 */
std::uint8_t TextBoxScrollThumbRegionEvent(const InputEvent*, W8Region*); /* 0x0058E9F0 */
std::uint8_t TextBoxBodyRegionEvent(const InputEvent*, W8Region*);        /* 0x0058ED90 */
std::uint8_t TextBoxChannelTabRegionEvent(const InputEvent*, W8Region*);  /* 0x0058EFD0 */
std::uint8_t TextBoxMuteRegionEvent(const InputEvent*, W8Region*);        /* 0x0058F240 */

extern std::deque<W8RegionSet> g_region_sets;
extern std::deque<W8Region> g_regions;
extern std::int32_t g_region_help_delay;
extern std::uint64_t g_region_help_clock;
extern std::uint32_t g_current_region_index;
extern std::uint32_t g_captured_region_index;
extern std::uint32_t g_hover_region_index;
extern bool g_region_help_force_enabled;
extern std::string g_default_help_text;

std::uint32_t GetForcedRegion(void);
void ReleaseDefaultHelpText(void);
std::uint32_t UpdateRegionMousePosition(std::int32_t x, std::int32_t y);
SDL_Point GetRegionMousePosition();
std::uint32_t FindRegionAtPoint(std::uint16_t x, std::uint16_t y);
void RegionSetEnable(std::uint32_t region_set_index);
void RegionSetDisable(std::uint32_t region_set_index);
void EnableRegionSetInput(std::uint32_t region_set_index);
void DisableRegionSetInput(std::uint32_t region_set_index);
void EnableRegionInput(std::uint32_t region_index);
void DisableRegionInput(std::uint32_t region_index);
void SetRegionBounds(std::uint32_t region_index, std::uint16_t x1, std::uint16_t y1,
                     std::uint16_t x2, std::uint16_t y2);
bool RegionContainsPoint(std::uint32_t region_index, std::uint16_t x, std::uint16_t y);
bool RegionHasFlags(std::uint32_t region_index, std::uint32_t flags);
std::uint32_t CreateRegionSet(std::int32_t order = kDynamicRegionSetOrder);
void DestroyRegionSet(std::uint32_t region_set_index);
void ResetRegionSet(std::uint32_t region_set_index);
std::uint32_t AddRegionToSet(std::uint32_t region_set_index, std::int32_t priority = 0);
void RemoveRegionFromSet(std::uint32_t region_set_index, std::uint32_t region_index);
void SetRegionCallback(std::uint32_t region_index, W8RegionCallback callback,
                       std::uint16_t callback_id);
void SetRegionHelp(std::uint32_t region_index, bool enabled, std::int32_t help_text_id);
void ClearHotRegion(void);
void UpdateRegionHelp(void);
void ShowRegionHelp(std::uint32_t region_index);
void SetRegionHelpText(std::string_view text);
void ResetRegionHelp(bool delayed);
void SetRegionHelpDelay(std::int32_t delay_ms);
std::uint32_t ControlRegionSetIndex(void);
std::size_t LiveDynamicRegionCount(void);
// Dynamic region sets currently allocated, including empty ones.
std::size_t LiveRegionSetCount(void);
void CaptureInputSink(void);
void ReleaseInputSinkCapture(void);
bool InputSinkCaptured(void);
void UnbindWidgetRegion(std::uint32_t region_index, W8Widget* widget, OwnedRegion* owner);
std::int32_t RegionReasonForInput(const W8Region& region, const InputEvent& input);
void EnableRegionHelp(std::uint32_t region_index);
void DisableRegionHelp(std::uint32_t region_index);
std::uint8_t ClearActiveRegionIfMatches(std::uint32_t region_index);
void ActivateDialogRegion(std::uint32_t region_index); /* 0x004F2040 */
/* 0x004F1910 returns the byte produced by the selected region callback. */
std::uint8_t DispatchRegionInput(const InputEvent* event);

void SetRegionHelpForceEnabled(bool enabled);
void EnableRegionHelpFlag(W8Region* region);
void DisableRegionHelpFlag(W8Region* region);


void ResetRegions(void);

#endif
