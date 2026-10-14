#pragma once

#include <SDL3/SDL_mouse.h>

enum class ButtonActivation { Press, Release };
enum class ButtonToggle { None, Press, Release, StayLatched };

struct ButtonPolicy {
    ButtonToggle primary_toggle = ButtonToggle::None;
    ButtonToggle secondary_toggle = ButtonToggle::None;
    ButtonActivation activation = ButtonActivation::Release;
    bool repeat = false;
    // Text controls allow controller-driven release/repeat, with only the
    // primary button changing their pressed artwork. Dialog buttons arm each
    // pointer button independently and require its matching press.
    bool controller_activation = false;
};

struct ButtonResponse {
    bool activate = false;
    bool press_sound = false;
    bool repeated_release = false;
};

// Shared interaction state; no rendering, registration, callback or ownership.
// Painting and sound remain with the control that owns this state.
class ButtonInteraction {
public:
    bool pressed = false;
    bool selected = false;

    ButtonResponse press(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        const auto toggle = toggleFor(button, policy);
        if (toggle == ButtonToggle::Press) {
            pressed = !pressed;
            selected = pressed;
            return {.activate = true};
        }
        if (!policy.controller_activation && (armed_ & button)) {
            return {};
        }
        armed_ |= button;
        if (!policy.controller_activation || button == SDL_BUTTON_LMASK) {
            pressed = true;
        }
        return {.activate = policy.activation == ButtonActivation::Press, .press_sound = true};
    }

    ButtonResponse release(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        const auto toggle = toggleFor(button, policy);
        if (toggle == ButtonToggle::Press) {
            return {};
        }
        if (policy.controller_activation) {
            if (button == SDL_BUTTON_LMASK && !pressed) {
                return {};
            }
        } else if (!(armed_ & button)) {
            return {};
        }
        armed_ &= ~button;
        if (toggle == ButtonToggle::Release || toggle == ButtonToggle::StayLatched) {
            selected = !selected || toggle == ButtonToggle::StayLatched;
            pressed = selected;
        } else if (!policy.controller_activation) {
            pressed = armed_ != 0;
        } else if (button == SDL_BUTTON_LMASK) {
            pressed = false;
        }
        const bool repeated =
            policy.controller_activation ? repeated_ != 0 : (repeated_ & button) != 0;
        if (policy.controller_activation) {
            repeated_ = 0;
        } else {
            repeated_ &= ~button;
        }
        return {.activate = !repeated && policy.activation == ButtonActivation::Release,
                .repeated_release = repeated};
    }

    ButtonResponse repeat(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        if (!policy.repeat || toggleFor(button, policy) == ButtonToggle::Press) {
            return {};
        }
        if (policy.controller_activation) {
            if (button == SDL_BUTTON_LMASK && !pressed) {
                return {};
            }
        } else if (!(armed_ & button)) {
            return {};
        }
        repeated_ |= button;
        return {.activate = true};
    }

    bool leave(const ButtonPolicy& policy)
    {
        const bool repeated = repeated_ != 0;
        armed_ = repeated_ = 0;
        if (policy.controller_activation ? !selected
                                         : policy.primary_toggle == ButtonToggle::None) {
            pressed = false;
        }
        return repeated;
    }

    void cancel(bool keep_pressed)
    {
        armed_ = repeated_ = 0;
        if (!keep_pressed) {
            pressed = false;
        }
    }

private:
    static ButtonToggle toggleFor(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        return button == SDL_BUTTON_RMASK ? policy.secondary_toggle : policy.primary_toggle;
    }
    SDL_MouseButtonFlags armed_ = 0;
    SDL_MouseButtonFlags repeated_ = 0;
};
