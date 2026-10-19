#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>

enum class UiPointerAction : std::uint8_t { Enter, Leave, Move, Down, Up, Repeat, Wheel };
enum class UiPointerButton : std::uint8_t { None, Left, Right };
enum class UiResult : std::uint8_t { Pass, Handled };

struct UiPointerEvent {
    UiPointerAction action;
    UiPointerButton button = UiPointerButton::None;
    SDL_Point position{};
    SDL_Point local{};
    SDL_MouseButtonFlags buttons = 0;
    SDL_MouseButtonFlags captured_buttons = 0;
    bool inside = false;
    int wheel = 0;
    std::uint16_t modifiers = 0;
    std::uint8_t clicks = 0; // SDL click count on Down, including double clicks.
};

struct UiCircle {
    SDL_Point center;
    int radius;
};
using HitShape = std::variant<SDL_Rect, UiCircle>;

class UiLayer;
class TextFieldGroup;
class W8TextField;
class W8DialogBase;
struct UiState;
struct InputEvent;

// A persistent input registration, borrowed from its layer. No painting or
// widget lifetime is attached to it. Rectangles use SDL's half-open bounds;
// circles include their circumference. Coordinates are in 640x480 game space.
class HitTarget {
public:
    using Handler = std::function<UiResult(const UiPointerEvent&)>;
    ~HitTarget();
    HitTarget(const HitTarget&) = delete;
    HitTarget& operator=(const HitTarget&) = delete;

    void setShape(HitShape shape);
    void setEnabled(bool enabled);
    void setTooltip(std::function<std::string()> text);
    // Hover-only targets (tooltips, previews) see presses but never capture the
    // pointer. Other targets capture on press whatever their handler returns:
    // gameplay controls that Pass a press still expect the matching release.
    void setPassive() { passive_ = true; }
    // Release capture when gameplay starts a drag whose destination should
    // receive the release. This changes the existing dispatcher's capture.
    void releaseCapture();
    bool hovered() const;
    bool hitAt(SDL_Point point) const;
    SDL_Rect bounds() const;
    SDL_MouseButtonFlags capturedButtons() const;

private:
    friend class UiLayer;
    friend class W8TextField;
    void useTextCursor();
    friend struct UiState;
    friend bool DispatchUiInput(const InputEvent*);
    HitTarget(UiLayer& layer, HitShape shape, Handler handler, int priority);
    UiLayer& layer_;
    HitShape shape_;
    Handler handler_;
    std::function<std::string()> tooltip_;
    int priority_;
    bool enabled_ = true, text_cursor_ = false, passive_ = false;
    SDL_MouseButtonFlags captured_buttons_ = 0;
};

// Named overlay ordering. Lower orders win, and equal layers use insertion
// order. Only relative precedence matters; no retail region-set IDs survive.
enum class UiLayerOrder : std::uint8_t {
    LockInput,
    Combat,
    PortraitButtons,
    PortraitStatus,
    // One layer per party slot, created in slot order.
    PortraitSlots,
    ScreenEdges,
    Radar,
    FormationBase,
    MessageBox,
    NpcTranscript,
    NpcTrade,
    NpcOptions,
    SpellCasting,
    ItemSelection,
    FormationPanel,
    PartyMovement,
    PartyQuotes,
    NpcQuoteBubble,
    KeyboardMenu,
    Submenu,
    GameplayOverlays,
    CampMain,
    CampInventory,
    CampPool,
    CampSpells,
    CampButtons,
    CharacterChoices,
    Standard,
};

// Lower layer orders win; within a layer higher priorities win, then first registration.
enum class UiLayerMode : std::uint8_t { Normal, Modal };

// A layer belongs to the input owner (normally the screen) active when it is
// created, and receives input only while that owner is selected. Shared
// layers outlive screen switches and stay active under every owner.
enum class UiLayerScope : std::uint8_t { Owner, Shared };

class UiLayer {
public:
    explicit UiLayer(UiLayerOrder order = UiLayerOrder::Standard,
                     UiLayerScope scope = UiLayerScope::Owner);
    explicit UiLayer(UiLayerMode mode, UiLayerOrder order = UiLayerOrder::Standard);
    ~UiLayer();
    UiLayer(const UiLayer&) = delete;
    UiLayer& operator=(const UiLayer&) = delete;

    HitTarget& add(HitShape shape, HitTarget::Handler handler, int priority = 0);
    void remove(HitTarget& target);
    void setModal(bool modal);
    void setEnabled(bool enabled);

private:
    static void retire(std::unique_ptr<HitTarget> target);
    friend class TextFieldGroup;
    friend class W8DialogBase;
    friend class W8ButtonWidget;
    friend struct UiState;
    friend std::size_t LiveUiTargetCount();
    friend class HitTarget;
    UiLayerOrder order_;
    std::optional<int> owner_; // Empty for shared layers.
    bool enabled_ = true, modal_ = false;
    std::vector<std::unique_ptr<HitTarget>> targets_;
};

// A UI transaction spans one step of input handling: pointer dispatch,
// PumpUiInput with its screen policy, or a modal's whole input step.
// Transactions nest; when the outermost one ends, the actions queued during it
// run, and then the objects retired during it are destroyed. Code that runs
// inside a transaction never destroys an object whose callback may be on the
// stack; it queues the destruction instead.
class UiTransaction {
public:
    UiTransaction();
    ~UiTransaction() noexcept(false);
    UiTransaction(const UiTransaction&) = delete;
    UiTransaction& operator=(const UiTransaction&) = delete;
};

// Callbacks request owner destruction or screen transitions here. Outside a
// transaction the action runs immediately. PumpUiInput yields once one is queued.
void AfterUiDispatch(std::move_only_function<void()> action);

using RetiredObject = std::unique_ptr<void, void (*)(void*)>;
void RetireAfterUiDispatch(RetiredObject object);
// Destroys `object` when the outermost transaction ends, or now outside one.
// Removed hit targets and replaced modals go through here.
template<class T> void DestroyAfterUiDispatch(std::unique_ptr<T> object)
{
    if (object) {
        RetireAfterUiDispatch({object.release(), [](void* p) { delete static_cast<T*>(p); }});
    }
}
