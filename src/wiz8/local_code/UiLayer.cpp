#include <cstddef>
#include <memory>
#include <string>
#include "wiz8/compat/native.h"
#include <cstdint>
#include <variant>
#include <SDL3/SDL_mouse.h>
#include "input.h"
#include <SDL3/SDL_rect.h>
#include <functional>
#include <vector>
#include "wiz8/ui_layer.h"
#include "wiz8/ui_input.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "timer.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

#ifdef WIZ8_RUNTIME_TESTS
#include "runtime_instrumentation.h"
#include "wiz8/local_code/Gameloop.h"
#endif

UiRoutingState g_ui;
bool g_dev_mode;
bool g_ui_help_forced;

namespace {
unsigned dispatch_depth = 0;
std::vector<std::function<void()>> pending_actions;
struct DispatchScope {
    DispatchScope() { ++dispatch_depth; }
    ~DispatchScope() noexcept(false)
    {
        if (--dispatch_depth) return;
        if (std::uncaught_exceptions()) { pending_actions.clear(); return; }
        auto actions = std::exchange(pending_actions, {});
        for (auto& action : actions) action();
    }
};
InputEvent PointerMotion(SDL_Point point)
{
    const auto& input = GetInputState();
    return {.type = InputEventType::MouseMove, .modifiers = input.modifiers, .position = point,
            .buttons = (input.left_button ? SDL_BUTTON_LMASK : 0u) |
                       (input.right_button ? SDL_BUTTON_RMASK : 0u)};
}
}

// One dispatcher borrows live layers. Layers own targets, and destruction
// removes every borrowed pointer, including suspended modal capture.
struct UiState {
    std::vector<UiLayer*> layers, modals;
    HitTarget* tooltip = nullptr;
    bool sink = false;

    bool allowed(const UiLayer& layer) const
    {
        if (!layer.enabled_) return false;
        return modals.empty() || modals.back() == &layer;
    }
    bool live(const HitTarget* target) const
    {
        return target && target->enabled_ && allowed(target->layer_);
    }
    SDL_Point position(const HitTarget& target, SDL_Point point) const
    {
        if (!target.text_cursor_) {
            point.x += g_cursor_hotspot_x;
            point.y += g_cursor_hotspot_y;
        }
        return point;
    }
    bool contains(const HitTarget& target, SDL_Point point) const
    {
        point = position(target, point);
        if (const auto* rect = std::get_if<SDL_Rect>(&target.shape_)) return SDL_PointInRect(&point, rect);
        const auto& circle = std::get<UiCircle>(target.shape_);
        const std::int64_t dx = std::int64_t(point.x) - circle.center.x;
        const std::int64_t dy = std::int64_t(point.y) - circle.center.y;
        return dx * dx + dy * dy <= std::int64_t(circle.radius) * circle.radius;
    }
    HitTarget* at(SDL_Point point) const
    {
        if (sink && modals.empty()) return nullptr;
        // Text editors sit above their hosting plate and use the cursor tip.
        for (const bool text : {true, false}) {
            for (auto* layer : layers) {
                if (!allowed(*layer)) continue;
                HitTarget* best = nullptr;
                for (const auto& target : layer->targets_) {
                    if (target->text_cursor_ != text || !target->enabled_ || !contains(*target, point)) continue;
                    if (!best || target->priority_ > best->priority_) best = target.get();
                }
                if (best) return best;
            }
        }
        return nullptr;
    }
    void hideTooltip()
    {
        if (tooltip) VideoRemoveToolTip();
        tooltip = nullptr;
    }
    void forget(HitTarget& target)
    {
        if (tooltip == &target) hideTooltip();
        if (g_ui.current == &target) g_ui.current = nullptr;
        if (g_ui.hover == &target) g_ui.hover = nullptr;
        if (g_ui.captured == &target) g_ui.captured = nullptr;
        target.captured_buttons_ = 0;
        for (auto* layer : layers) {
            if (layer->saved_hover_ == &target) layer->saved_hover_ = nullptr;
            if (layer->saved_capture_ == &target) layer->saved_capture_ = nullptr;
        }
    }
    UiResult send(HitTarget& target, const InputEvent& input, UiPointerAction action,
                  UiPointerButton button = UiPointerButton::None)
    {
        const auto point = position(target, input.position);
        const auto bounds = target.bounds();
        const UiPointerEvent event{.action = action, .button = button, .position = point,
            .local = {point.x - bounds.x, point.y - bounds.y}, .buttons = input.buttons,
            .captured_buttons = target.captured_buttons_, .inside = contains(target, input.position),
            .wheel = input.wheel, .modifiers = input.modifiers, .clicks = input.clicks};
        // Preserve state held by mutable callbacks. Target removal and owner
        // destruction use AfterUiDispatch, so the callable stays alive here.
        return target.handler_ ? target.handler_(event) : UiResult::Handled;
    }
    void leave(const InputEvent& input)
    {
        auto* previous = std::exchange(g_ui.hover, nullptr);
        hideTooltip();
        if (previous) send(*previous, input, UiPointerAction::Leave);
        g_ui_help_delay = g_settings.tooltip_delay_ms;
        g_ui_help_forced = false;
    }
    void motion(const InputEvent& input)
    {
        g_ui.pointer = input.position;
        auto* target = live(g_ui.captured) ? g_ui.captured : at(input.position);
        g_ui.current = target;
        const bool inside = target && contains(*target, input.position);
        if (g_ui.hover && (g_ui.hover != target || !inside)) {
            leave(input);
            PlayButtonSound(1);
            // A leave callback may release capture or request a new owner.
            if (!pending_actions.empty() || (g_ui.captured == target && !inside)) return;
            target = live(g_ui.captured) ? g_ui.captured : at(input.position);
            g_ui.current = target;
        }
        if (!target) return;
        if (contains(*target, input.position) && g_ui.hover != target) {
            g_ui.hover = target;
            send(*target, input, UiPointerAction::Enter);
            g_ui_help_clock = w8_get_ticks() + g_ui_help_delay;
            PlayButtonSound(0);
        } else send(*target, input, UiPointerAction::Move);
    }
    void showTooltip()
    {
        auto* target = g_ui.hover;
        if (!live(target) || !target->tooltip_ || tooltip == target ||
            (!g_settings.tooltips_enabled && !g_ui_help_forced)) return;
        const auto text = target->tooltip_();
        if (text.empty()) return;
        hideTooltip();
        VideoToolTip(text);
        SDL_Point anchor;
        SGPMouseGetPos(&anchor);
        const int width = g_help_box_width + 2, height = g_help_box_height + 2;
        anchor.x = std::clamp(anchor.x, 2, std::max(2, 640 - width));
        anchor.y = std::clamp(anchor.y - height, 2, std::max(2, 480 - height));
        VideoPositionToolTip(anchor.x, anchor.y);
        tooltip = target;
    }
};

namespace {
// Function-local lifetime also outlives owners constructed during startup.
UiState& State() { static UiState state; return state; }
}

void AfterUiDispatch(std::function<void()> action)
{
    if (dispatch_depth) pending_actions.push_back(std::move(action));
    else action();
}

HitTarget::HitTarget(UiLayer& layer, HitShape shape, Handler handler, int priority)
    : layer_(layer), handler_(std::move(handler)), priority_(priority) { setShape(shape); }
HitTarget::~HitTarget() { State().forget(*this); }
void HitTarget::setShape(HitShape shape)
{
    if (const auto* rect = std::get_if<SDL_Rect>(&shape)) {
        if (rect->w <= 0 || rect->h <= 0) throw std::invalid_argument("UI hit rectangles need positive extents");
    } else if (std::get<UiCircle>(shape).radius < 0) throw std::invalid_argument("UI hit circles need a nonnegative radius");
    shape_ = shape;
}
SDL_Rect HitTarget::bounds() const
{
    if (const auto* rect = std::get_if<SDL_Rect>(&shape_)) return *rect;
    const auto& circle = std::get<UiCircle>(shape_);
    return {circle.center.x - circle.radius, circle.center.y - circle.radius, circle.radius * 2 + 1, circle.radius * 2 + 1};
}
void HitTarget::setEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled) State().forget(*this);
}
void HitTarget::setTooltip(std::function<std::string()> text)
{
    if (State().tooltip == this) State().hideTooltip();
    tooltip_ = std::move(text);
}
void HitTarget::releaseCapture()
{
    if (g_ui.captured == this) g_ui.captured = nullptr;
    captured_buttons_ = 0;
}
bool HitTarget::hovered() const { return g_ui.hover == this; }
bool HitTarget::hitAt(SDL_Point point) const { return State().at(point) == this; }
SDL_MouseButtonFlags HitTarget::capturedButtons() const { return captured_buttons_; }
void HitTarget::useTextCursor() { text_cursor_ = true; }

UiLayer::UiLayer(int order) : order_(order)
{
    auto& layers = State().layers;
    const auto pos = std::ranges::upper_bound(layers, order, {}, [](auto* layer) { return layer->order_; });
    layers.insert(pos, this);
}
UiLayer::UiLayer(UiLayerMode mode, int order) : UiLayer(order) { setModal(mode == UiLayerMode::Modal); }
UiLayer::~UiLayer()
{
    setModal(false);
    targets_.clear();
    auto& layers = State().layers;
    std::erase(layers, this);
}
HitTarget& UiLayer::add(HitShape shape, HitTarget::Handler handler, int priority)
{
    auto target = std::unique_ptr<HitTarget>(new HitTarget(*this, shape, std::move(handler), priority));
    auto& result = *target;
    targets_.push_back(std::move(target));
    return result;
}
void UiLayer::remove(HitTarget& target)
{
    std::erase_if(targets_, [&](const auto& entry) { return entry.get() == &target; });
}
void UiLayer::setEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled) for (auto& target : targets_) State().forget(*target);
}
void UiLayer::setModal(bool modal)
{
    if (modal_ == modal) return;
    const DispatchScope scope;
    auto& state = State();
    modal_ = modal;
    if (modal) {
        saved_hover_ = g_ui.hover;
        saved_capture_ = g_ui.captured;
        saved_buttons_ = saved_capture_ ? saved_capture_->captured_buttons_ : 0;
        state.leave(PointerMotion(g_ui.pointer));
        if (g_ui.captured) g_ui.captured->captured_buttons_ = 0;
        g_ui.ClearPointer();
        state.modals.push_back(this);
        return;
    }
    const auto pos = std::ranges::find(state.modals, this);
    if (pos == state.modals.end()) return;
    const bool top = pos + 1 == state.modals.end();
    if (!top) {
        auto* child = *(pos + 1);
        child->saved_hover_ = saved_hover_;
        child->saved_capture_ = saved_capture_;
        child->saved_buttons_ = saved_buttons_;
    }
    state.modals.erase(pos);
    if (top) {
        state.hideTooltip();
        ReleaseUiCapture();
        g_ui.ClearPointer();
        if (state.live(saved_capture_)) {
            g_ui.captured = saved_capture_;
            saved_capture_->captured_buttons_ = saved_buttons_;
        }
        if (state.live(saved_hover_)) g_ui.current = g_ui.hover = saved_hover_;
    }
    saved_hover_ = saved_capture_ = nullptr;
}

HitTarget* UiTargetAt(SDL_Point point) { return State().at(point); }
void UpdateUiPointer(int x, int y)
{
    const DispatchScope scope;
    State().motion(PointerMotion({x, y}));
}
SDL_Point UiPointerPosition() { return g_ui.pointer; }
void ReleaseUiCapture() { if (g_ui.captured) g_ui.captured->releaseCapture(); }
void CaptureInputSink() { ClearUiHover(); ReleaseUiCapture(); State().sink = true; }
void ReleaseInputSinkCapture() { State().sink = false; }
bool InputSinkCaptured() { return State().sink; }

bool DispatchUiInput(const InputEvent* input)
{
    const DispatchScope scope;
    UiPointerAction action;
    UiPointerButton button = UiPointerButton::None;
    switch (input->type) {
    case InputEventType::MouseMove: State().motion(*input); return g_ui.current != nullptr;
    case InputEventType::LeftButtonDown: action = UiPointerAction::Down; button = UiPointerButton::Left; break;
    case InputEventType::RightButtonDown: action = UiPointerAction::Down; button = UiPointerButton::Right; break;
    case InputEventType::LeftButtonUp: action = UiPointerAction::Up; button = UiPointerButton::Left; break;
    case InputEventType::RightButtonUp: action = UiPointerAction::Up; button = UiPointerButton::Right; break;
    case InputEventType::LeftButtonRepeat: action = UiPointerAction::Repeat; button = UiPointerButton::Left; break;
    case InputEventType::RightButtonRepeat: action = UiPointerAction::Repeat; button = UiPointerButton::Right; break;
    case InputEventType::MouseWheel: action = UiPointerAction::Wheel; break;
    default: return false;
    }
    auto& state = State();
    g_ui.pointer = input->position;
#ifdef WIZ8_RUNTIME_TESTS
    RuntimeObserve(RUNTIME_MOUSE_DISPATCH, input->type,
        static_cast<w8_ulong>(input->position.x) | (static_cast<w8_ulong>(input->position.y) << 16), g_current_screen_state.id);
#endif
    auto* target = state.live(g_ui.captured) ? g_ui.captured : state.at(input->position);
    if (!target) return state.sink && state.modals.empty();
    state.hideTooltip();
    g_ui_help_clock = w8_get_ticks() + g_ui_help_delay;
    const auto mask = button == UiPointerButton::Left ? SDL_BUTTON_LMASK : SDL_BUTTON_RMASK;
    int sound = -1;
    if (action == UiPointerAction::Down) {
        target->captured_buttons_ |= mask;
        g_ui.captured = g_ui.current = g_ui.hover = target;
        sound = 2;
    } else if (action == UiPointerAction::Up && (target->capturedButtons() & mask)) sound = 3;
    const bool handled = state.send(*target, *input, action, button) == UiResult::Handled;
    // Removal clears the borrowed pointer; never inspect a destroyed target.
    if (action == UiPointerAction::Up && g_ui.captured == target) {
        target->captured_buttons_ &= ~mask;
        if (!target->captured_buttons_) g_ui.captured = nullptr;
    }
    if (sound != -1) PlayButtonSound(sound);
    return handled;
}

void ClearUiHover()
{
    const DispatchScope scope;
    State().leave(PointerMotion(g_ui.pointer));
    g_ui.current = nullptr;
}
void ResetUiInput()
{
    auto& state = State();
    state.hideTooltip();
    ReleaseUiCapture();
    g_ui.ClearPointer();
    state.sink = false;
    for (auto* layer : state.layers) {
        layer->setEnabled(false);
    }
    g_ui_help_delay = g_settings.tooltip_delay_ms;
    g_ui_help_forced = false;
}
void SetUiHelpDelay(int delay) { g_ui_help_delay = delay ? delay : g_settings.tooltip_delay_ms; }
void SetUiHelpForced(bool enabled) { g_ui_help_forced = enabled; }
void UpdateUiHelp() { if (TimeUntilDeadline(g_ui_help_clock) == 0) State().showTooltip(); }
void ResetUiHelp(bool delayed)
{
    State().hideTooltip();
    if (delayed) g_ui_help_clock = w8_get_ticks() + g_ui_help_delay;
    else State().showTooltip();
}
std::size_t LiveUiTargetCount()
{
    std::size_t count = 0;
    for (auto* layer : State().layers) count += layer->targets_.size();
    return count;
}
std::size_t LiveUiLayerCount() { return State().layers.size(); }

bool PumpUiInput(const UiInputPolicy& policy)
{
    if (policy.track_hover) {
        bool changed;
        {
            const DispatchScope scope;
            SDL_Point point;
            SGPMouseGetPos(&point);
            UpdateUiPointer(point.x, point.y);
            changed = !pending_actions.empty();
        }
        if (changed) return false;
    }
    bool drained = false;
    InputEvent input;
    while (DequeueEvent(&input)) {
        drained = true;
        bool stop;
        {
            const DispatchScope scope;
            auto route = policy.before ? policy.before(input) : InputRoute::Pass;
            if (route == InputRoute::Pass) {
                const bool consumed = (!policy.ui_accepts || policy.ui_accepts(input)) && DispatchUiInput(&input);
                const auto& next = consumed ? policy.consumed : policy.unhandled;
                route = next ? next(input) : InputRoute::Handled;
            }
            stop = route == InputRoute::Stop || !pending_actions.empty();
        }
        if (stop) break;
    }
    return drained;
}
