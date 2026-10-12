#include "wiz8/ui_layer.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "input.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/regions.h"

namespace {
void ForgetTarget(unsigned int index)
{
    auto& region = g_regions[index];
    if (region.flags & W8_REGION_HELP_SHOWN) VideoRemoveToolTip();
    region.flags &= ~(W8_REGION_MOUSE_STATE_MASK | W8_REGION_HELP_SHOWN);
    g_ui.Forget(index);
}

void CheckBackendCoordinate(std::int64_t value)
{
    if (value < std::numeric_limits<short>::min() || value > std::numeric_limits<short>::max())
        throw std::out_of_range("UI hit geometry exceeds the temporary region backend");
}
}

struct HitTarget::Backend final : W8RegionHandler {
    Handler handler;
    OwnedRegion owned;

    Backend(unsigned int set, Handler callback, int priority)
        : handler(std::move(callback)), owned(set, priority, *this)
    {
    }

    int HandleRegionInput(W8Region& region, const InputEvent& input) override
    {
        UiPointerAction action;
        auto button = UiPointerButton::None;
        switch (input.type) {
        case InputEventType::MouseMove:
            action = (region.flags & W8_REGION_MOUSE_LEAVE) ? UiPointerAction::Leave
                   : (region.flags & W8_REGION_MOUSE_ENTER) ? UiPointerAction::Enter
                                                          : UiPointerAction::Move;
            break;
        case InputEventType::LeftButtonDown:
        case InputEventType::RightButtonDown: action = UiPointerAction::Down; break;
        case InputEventType::LeftButtonUp:
        case InputEventType::RightButtonUp: action = UiPointerAction::Up; break;
        case InputEventType::LeftButtonRepeat:
        case InputEventType::RightButtonRepeat: action = UiPointerAction::Repeat; break;
        case InputEventType::MouseWheel: action = UiPointerAction::Wheel; break;
        default: return 0; // Keyboard/text remain with the screen/focused editor.
        }
        if (IsLeftButtonEvent(input.type)) button = UiPointerButton::Left;
        else if (input.type == InputEventType::RightButtonDown ||
                 input.type == InputEventType::RightButtonUp ||
                 input.type == InputEventType::RightButtonRepeat)
            button = UiPointerButton::Right;

        // Match the backend's cursor-artwork hotspot convention at this boundary.
        const SDL_Point position{input.position.x + (region.text_field ? 0 : g_cursor_hotspot_x),
                                 input.position.y + (region.text_field ? 0 : g_cursor_hotspot_y)};
        const UiPointerEvent event{
            .action = action,
            .button = button,
            .position = position,
            .local = {position.x - region.x1, position.y - region.y1},
            .buttons = input.buttons,
            .captured_buttons =
                ((region.flags & W8_REGION_LEFT_BUTTON_HELD) ? SDL_BUTTON_LMASK : 0u) |
                ((region.flags & W8_REGION_RIGHT_BUTTON_HELD) ? SDL_BUTTON_RMASK : 0u),
            .inside = region.ContainsPoint(position.x, position.y),
            .wheel = input.wheel,
            .clicks = input.clicks,
        };
        return handler ? handler(event) == UiResult::Handled : 1;
    }
};

HitTarget::HitTarget(unsigned int set, HitShape shape, Handler handler, int priority)
    : backend_(std::make_unique<Backend>(set, std::move(handler), priority))
{
    setShape(shape);
}

HitTarget::~HitTarget() = default;

void HitTarget::setShape(HitShape shape)
{
    auto& region = backend_->owned.region();
    if (const auto* rect = std::get_if<SDL_Rect>(&shape)) {
        if (rect->w <= 0 || rect->h <= 0)
            throw std::invalid_argument("UI hit rectangles must have positive extents");
        CheckBackendCoordinate(rect->x);
        CheckBackendCoordinate(rect->y);
        CheckBackendCoordinate(std::int64_t{rect->x} + rect->w - 1);
        CheckBackendCoordinate(std::int64_t{rect->y} + rect->h - 1);
        region.SetBounds(rect->x, rect->y, rect->x + rect->w - 1, rect->y + rect->h - 1);
        region.flags = (region.flags & ~(W8_REGION_RECTANGLE | W8_REGION_CIRCLE)) |
                       W8_REGION_RECTANGLE;
    } else {
        const auto& circle = std::get<UiCircle>(shape);
        if (circle.radius < 0)
            throw std::invalid_argument("UI hit circles must have a nonnegative radius");
        CheckBackendCoordinate(circle.center.x);
        CheckBackendCoordinate(circle.center.y);
        CheckBackendCoordinate(circle.radius);
        region.SetBounds(circle.center.x, circle.center.y, circle.radius, 0);
        region.flags = (region.flags & ~(W8_REGION_RECTANGLE | W8_REGION_CIRCLE)) |
                       W8_REGION_CIRCLE;
    }
}

void HitTarget::setEnabled(bool enabled)
{
    auto& region = backend_->owned.region();
    region.SetInputEnabled(enabled);
    if (!enabled) ForgetTarget(backend_->owned.index());
}

void HitTarget::setTooltip(std::function<std::string()> text)
{
    auto& region = backend_->owned.region();
    if (region.flags & W8_REGION_HELP_SHOWN) VideoRemoveToolTip();
    region.flags &= ~W8_REGION_HELP_SHOWN;
    region.help_enabled = static_cast<bool>(text);
    region.help_text_provider = std::move(text);
}

void HitTarget::releaseCapture()
{
    ClearActiveRegionIfMatches(backend_->owned.index());
    backend_->owned.region().flags &= ~(W8_REGION_LEFT_BUTTON_HELD | W8_REGION_RIGHT_BUTTON_HELD);
}

bool HitTarget::hovered() const
{
    return g_ui.hover == backend_->owned.index() ||
           (backend_->owned.region().flags & W8_REGION_MOUSE_ENTER) != 0;
}

SDL_MouseButtonFlags HitTarget::capturedButtons() const
{
    const auto flags = backend_->owned.region().flags;
    return ((flags & W8_REGION_LEFT_BUTTON_HELD) ? SDL_BUTTON_LMASK : 0u) |
           ((flags & W8_REGION_RIGHT_BUTTON_HELD) ? SDL_BUTTON_RMASK : 0u);
}

void HitTarget::useTextCursor()
{
    backend_->owned.region().text_field = true;
}

struct UiLayer::Backend {
    unsigned int set;
    bool owns_set;
    RegionSetRef inherited_scope{};
    std::vector<std::unique_ptr<HitTarget>> targets;
    std::optional<ExclusiveRegionSet> modal;

    Backend(int order, UiLayerMode mode) : set(CreateRegionSet(order)), owns_set(true)
    {
        g_region_sets[set].enabled = 1;
        if (mode == UiLayerMode::Modal) modal.emplace(set);
    }

    explicit Backend(InheritInputScope)
        : set(InputLayerSet(CurrentInputLayer())), owns_set(false),
          inherited_scope(CaptureRegionSet(set)) {}

    ~Backend()
    {
        targets.clear();
        modal.reset();
        if (owns_set) DestroyRegionSet(set);
    }
};

UiLayer::UiLayer(int order) : UiLayer(UiLayerMode::Normal, order) {}
UiLayer::UiLayer(UiLayerMode mode, int order) : backend_(std::make_unique<Backend>(order, mode)) {}
UiLayer::UiLayer(InheritInputScope scope) : backend_(std::make_unique<Backend>(scope)) {}
UiLayer::~UiLayer() = default;

HitTarget& UiLayer::add(HitShape shape, HitTarget::Handler handler, int priority)
{
    auto target = std::unique_ptr<HitTarget>(
        new HitTarget(legacySet(), shape, std::move(handler), priority));
    auto& result = *target;
    backend_->targets.push_back(std::move(target));
    return result;
}

void UiLayer::remove(HitTarget& target)
{
    std::erase_if(backend_->targets, [&target](const auto& entry) { return entry.get() == &target; });
}

void UiLayer::setEnabled(bool enabled)
{
    g_region_sets[backend_->set].enabled = enabled;
    if (!enabled) {
        for (const auto index : g_region_sets[backend_->set].Regions()) ForgetTarget(index);
    }
}

unsigned int UiLayer::legacySet() const
{
    return backend_->owns_set ? backend_->set : InputLayerSet(backend_->inherited_scope);
}

void UiLayer::setModal(bool modal)
{
    if (modal && !backend_->modal) backend_->modal.emplace(legacySet());
    else if (!modal) backend_->modal.reset();
}
