#pragma once

#include <SDL3/SDL_mouse.h>

// MatchingPress arms pointer buttons separately; release/repeat require their
// own press, any armed button depresses artwork, and repeats suppress only
// that button's release. PrimaryPressedState gates left release/repeat on the
// pressed state, permits controller-driven right actions, only depresses
// artwork on left press, and shares repeat suppression across both buttons.
enum class ButtonInputRule { MatchingPress, PrimaryPressedState };

enum class ButtonActivation { Press, Release };
enum class ButtonToggle { None, Press, Release, StayLatched };

struct ButtonPolicy {
    ButtonToggle primary_toggle = ButtonToggle::None;
    ButtonToggle secondary_toggle = ButtonToggle::None;
    ButtonActivation activation = ButtonActivation::Release;
    bool repeat = false;
    ButtonInputRule input_rule = ButtonInputRule::MatchingPress;
};

struct ButtonResponse {
    bool activate = false;
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
        if (policy.input_rule == ButtonInputRule::MatchingPress && (armed_ & button)) {
            return {};
        }
        armed_ |= button;
        if (policy.input_rule == ButtonInputRule::MatchingPress || button == SDL_BUTTON_LMASK) {
            pressed = true;
        }
        return {.activate = policy.activation == ButtonActivation::Press};
    }

    ButtonResponse release(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        const auto toggle = toggleFor(button, policy);
        if (toggle == ButtonToggle::Press) return {};
        if (policy.input_rule == ButtonInputRule::PrimaryPressedState) {
            if (button == SDL_BUTTON_LMASK && !pressed) return {};
        } else if (!(armed_ & button)) {
            return {};
        }
        armed_ &= ~button;
        if (toggle == ButtonToggle::Release || toggle == ButtonToggle::StayLatched) {
            selected = !selected || toggle == ButtonToggle::StayLatched;
            pressed = selected;
        } else if (policy.input_rule == ButtonInputRule::MatchingPress) {
            pressed = armed_ != 0;
        } else if (button == SDL_BUTTON_LMASK) {
            pressed = false;
        }
        const bool repeated = policy.input_rule == ButtonInputRule::PrimaryPressedState
                                  ? repeated_ != 0
                                  : (repeated_ & button) != 0;
        if (policy.input_rule == ButtonInputRule::PrimaryPressedState) {
            repeated_ = 0;
        } else
            repeated_ &= ~button;
        return {.activate = !repeated && policy.activation == ButtonActivation::Release,
                .repeated_release = repeated};
    }

    ButtonResponse repeat(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        if (!policy.repeat || toggleFor(button, policy) == ButtonToggle::Press) return {};
        if (policy.input_rule == ButtonInputRule::PrimaryPressedState) {
            if (button == SDL_BUTTON_LMASK && !pressed) return {};
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
        if (policy.input_rule == ButtonInputRule::PrimaryPressedState
                ? !selected
                : policy.primary_toggle == ButtonToggle::None) {
            pressed = false;
        }
        return repeated;
    }

    void cancel(bool keep_pressed)
    {
        armed_ = repeated_ = 0;
        if (!keep_pressed) pressed = false;
    }

private:
    static ButtonToggle toggleFor(SDL_MouseButtonFlags button, const ButtonPolicy& policy)
    {
        return button == SDL_BUTTON_RMASK ? policy.secondary_toggle : policy.primary_toggle;
    }
    SDL_MouseButtonFlags armed_ = 0;
    SDL_MouseButtonFlags repeated_ = 0;
};
