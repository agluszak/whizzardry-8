#include "Font.h"
#include <cstdint>
#include <string_view>
#include <functional>
#pragma once

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/regions.h"

/* Tooltip string-table sentinel; independent of the five frame indices. */
enum { W8_DIALOG_BUTTON_NO_TOOLTIP = -1 };

class W8DialogBase;
class W8DialogButton;

/* Callbacks stored through Configure and dispatched by the button's own SGP
   callback with the W8DialogButton as the subject. */
using W8DialogButtonCallback = std::function<void(W8DialogButton*)>;
enum class DialogButtonActivation { Press, Release };

// VTABLE: WIZ8 0x005efa98
class W8DialogButton : public W8RegionHandler {
public:
    W8DialogButton();
    virtual ~W8DialogButton();
    void Draw();
    void Reset();
    bool IsConfigured() const { return static_cast<bool>(m_region); }
    void SetPosition(int x, int y);
    void Resize(int width, int height);
    void SetText(std::string_view text);
    void SetTextOffsets(int x, int y, bool shift);
    void SetMultiColor(bool enabled);
    int GetWidth();
    int GetHeight();
    int GetX();
    int GetY();
    SDL_Point GetMousePosition() const { return GetRegionMousePosition(); }
    bool IsLeftButtonHeld() const
    {
        return m_region && (m_region.region().flags & W8_REGION_LEFT_BUTTON_HELD) != 0;
    }
    SDL_Rect GetBounds() const { return m_appearance.bounds; }
    void SetEnabled(bool enabled);
    void UpdateEnabledState(bool enabled)
    {
        if (!enabled) {
            SetEnabled(false);
            m_dirty = true;
        } else if (!IsEnabled()) {
            SetEnabled(true);
            m_dirty = true;
        }
    }
    bool IsEnabled();
    void SetPressed(bool pressed);
    unsigned char IsPressed();
    void SetVisible(bool visible);
    /* Installs the tooltip string on the mouse region (when one is configured
       and tooltips are on), or clears it. */
    void SetTooltipEnabled(bool enabled);
    int GetUserData();
    /* Load the frame images, configure the owned region, and store the
       per-button callbacks, left-toggle flag, priority and tooltip index. */
    bool Configure(const char* image_path, int gray_frame, int off_normal_frame,
                   int off_hover_frame, int on_normal_frame, int on_hover_frame,
                   W8DialogButtonCallback left_callback, W8DialogButtonCallback move_callback,
                   bool left_toggles, int priority, int tooltip_index,
                   W8DialogButtonCallback right_callback,
                             W8DialogButtonCallback double_click_callback);
    bool ConfigureIcon(const char* image_path, int disabled_frame, int normal_frame, int priority,
                       int tooltip_index)
    {
        return Configure(image_path, disabled_frame, normal_frame, BUTTON_NO_IMAGE, normal_frame,
                         BUTTON_NO_IMAGE, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK, false, priority,
                         tooltip_index, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK);
    }

    /* Configure a text button with an owner-local numeric callback payload. */
    bool ConfigureTextButton(std::string_view text, Font& font, short fore_color,
                             short shadow_color, short x, short y, short width, short height,
                             W8DialogButtonCallback left_callback, int user_data,
                             GenericButtonImage image = {}, int priority = REGION_PRIORITY_HIGHEST);
    /* Store the tooltip string-table index and, when tooltips are
       enabled, push its text onto the mouse region. */
    void SetTooltipIndex(int tooltip_index);

private:
    ButtonAppearance m_appearance;
    ButtonVisualState m_visual;
    OwnedRegion m_region;
    SDL_MouseButtonFlags m_armed_buttons = 0;
    SDL_MouseButtonFlags m_repeated_buttons = 0;
    int m_user_data = 0;
    void ConfigureRegion(int priority);
    void HandleMouseEvent(std::int32_t reason);
    int HandleRegionInput(W8Region& region, const InputEvent& input) override;
    int m_tooltip_index; /* g_localized_strings index, or -1 */
    W8DialogButtonCallback m_left_callback;
    W8DialogButtonCallback m_right_callback;
    W8DialogButtonCallback m_move_callback;
    W8DialogButtonCallback m_double_click_callback;
    bool m_left_toggles; /* left down xors CLICKED_ON */

public:
    /* Raised by the dialog factories on their item-row buttons; right
       down then toggles CLICKED_ON the same way left-toggle does. */
    bool m_right_toggles;
    bool m_dirty; /* set by owning screens before Draw */
private:
    bool silent;       /* suppresses all button sounds */
    bool hover_silent; /* suppresses hover/exit sounds only */

public:
    /* Spinner repeats activate while held; a repeated press does not activate again on release. */
    bool m_repeat_enabled;
    DialogButtonActivation m_activation = DialogButtonActivation::Release;
    /* Set by the dialog factories to the owning dialog; the per-button
       dispatch callback reads it back when a stored callback needs it. */
    W8DialogBase* m_owner;

private:
    /* Snapshot of the live-dialog count; dispatch ignores events when a
       nested dialog has changed that count. */
    int m_live_dialog_count;
};

W8_ABI_ASSERT(sizeof(W8DialogButton) == 0x48, "W8DialogButton_size");

/* Button arrays keep unallocated slots as supplied by their owner. Failure
   deletes every non-null slot in the full array, in ascending order. */
inline void DestroyDialogButtons(W8DialogButton** buttons, int count)
{
    for (int index = 0; index < count; ++index) {
        if (buttons[index] != 0) {
            delete buttons[index];
            buttons[index] = 0;
        }
    }
}

inline bool AllocateDialogButtons(W8DialogButton** buttons, int count)
{
    for (int index = 0; index < count; ++index) {
        buttons[index] = new W8DialogButton;
        if (buttons[index] == 0) {
            DestroyDialogButtons(buttons, count);
            return false;
        }
    }
    return true;
}

/* Dialog handles keep their sentinel until release finishes. Removal,
   ordinary images and generic borders have distinct SGP resource owners. */
