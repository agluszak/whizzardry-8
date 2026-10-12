#pragma once

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>

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
    std::uint8_t clicks = 0; // SDL click count on Down, including double clicks.
};

struct UiCircle {
    SDL_Point center;
    int radius;
};
using HitShape = std::variant<SDL_Rect, UiCircle>;

class UiLayer;

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

private:
    friend class UiLayer;
    HitTarget(unsigned int set, HitShape shape, Handler handler, int priority);
    struct Backend;
    std::unique_ptr<Backend> backend_;
};

// Temporary RegionManager-backed registration API. There is still exactly one
// input dispatcher, hover state, capture owner and tooltip timer. Lower layer
// orders win; within a layer higher priorities win, then first registration.
class UiLayer {
public:
    explicit UiLayer(int order = 0x33);
    ~UiLayer();
    UiLayer(const UiLayer&) = delete;
    UiLayer& operator=(const UiLayer&) = delete;

    HitTarget& add(HitShape shape, HitTarget::Handler handler, int priority = 0);
    void remove(HitTarget& target);
    void setEnabled(bool enabled);

private:
    struct Backend;
    std::unique_ptr<Backend> backend_;
};

// Callbacks request removal, owner destruction or screen transitions here.
// Actions run after the outermost pointer dispatch, including hover callbacks;
// PumpUiInput includes the screen's event policy in this transaction and yields
// after changes commit. Outside dispatch actions run immediately.
void AfterUiDispatch(std::function<void()> action);
