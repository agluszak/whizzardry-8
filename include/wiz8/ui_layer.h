#pragma once

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

enum class UiPointerAction { Enter, Leave, Move, Down, Up, Repeat, Wheel };
enum class UiPointerButton { None, Left, Right };
enum class UiResult { Pass, Handled };

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
    friend void ResetUiInput();
    HitTarget(UiLayer& layer, HitShape shape, Handler handler, int priority);
    UiLayer& layer_;
    HitShape shape_;
    Handler handler_;
    std::function<std::string()> tooltip_;
    int priority_;
    bool enabled_ = true, text_cursor_ = false;
    SDL_MouseButtonFlags captured_buttons_ = 0;
};

// Lower layer orders win; within a layer higher priorities win, then first registration.
enum class UiLayerMode { Normal, Modal };

class UiLayer {
public:
    explicit UiLayer(int order = 0x33);
    explicit UiLayer(UiLayerMode mode, int order = 0x33);
    ~UiLayer();
    UiLayer(const UiLayer&) = delete;
    UiLayer& operator=(const UiLayer&) = delete;

    HitTarget& add(HitShape shape, HitTarget::Handler handler, int priority = 0);
    void remove(HitTarget& target);
    void setModal(bool modal);
    void setEnabled(bool enabled);

private:
    friend class TextFieldGroup;
    friend class W8DialogBase;
    friend class W8ButtonWidget;
    friend struct UiState;
    friend void ResetUiInput();
    friend std::size_t LiveUiTargetCount();
    friend class HitTarget;
    int order_;
    bool enabled_ = true, modal_ = false;
    std::vector<std::unique_ptr<HitTarget>> targets_;
    HitTarget *saved_hover_ = nullptr, *saved_capture_ = nullptr;
    SDL_MouseButtonFlags saved_buttons_ = 0;
};

// Callbacks request removal, owner destruction or screen transitions here.
// Actions run after the outermost pointer dispatch, including hover callbacks;
// PumpUiInput includes the screen's event policy in this transaction and yields
// after changes commit. Outside dispatch actions run immediately.
void AfterUiDispatch(std::function<void()> action);
