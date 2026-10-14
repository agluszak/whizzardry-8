#include "wiz8/local_code/ControlsRect.h"
#include <SDL3/SDL_pixels.h>
#include "Types.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_surface.h>
#include <string>
#include <algorithm>
#include <memory>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>

#include "utf8/checked.h"
#include "wiz8/unicode.h"
#include "wiz8/cursor.h"
#include "Font.h"
#include "input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/ui_input.h"
#include "wiz8/fonts.h"
#include "wiz8/text_input.h"
#include "wiz8/local_screens/Screens.h"
#include "himage.h"
#include "image.h"
#include "compat/native.h"

/*
 * Wizardry's product fork of Sir-Tech's released Utils/Text_Input.c.
 *
 * Released ancestor: ja2-stracciatella/ja2-stracciatella commit
 * 5ac0a9d56d27e8a7e2c4a7b48ed8932ae7f64033,
 * ja2/Build/Utils/Text_Input.{c,h}. This derivative is distributed under the
 * SFI Source Code License Agreement retained in src/sgp.
 *
 * The released JA2 source establishes ancestry, not Wizardry's original source
 * path. Retail contains no Text_Input / Utils path string, so the recovered
 * .cpp / local_code/ placement remains a recovery choice.
 */

/* One editable field of a group. Its hit region is hit-tested at the cursor
   tip, ahead of the panel hosting it. */
class W8TextField final {
public:
    W8TextField(TextFieldGroup& group, int id, int priority, SDL_Rect bounds)
        : group(group), id(id), bounds(bounds),
          target(group.layer_->add(
              SDL_Rect{bounds.x, bounds.y, bounds.w + 1, bounds.h + 1},
              [this](const UiPointerEvent& event) { return HandleInput(event); }, priority))
    {
        target.useTextCursor();
    }
    ~W8TextField()
    {
        group.layer_->remove(target);
    }
    W8TextField(const W8TextField&) = delete;
    W8TextField& operator=(const W8TextField&) = delete;

    UiResult HandleInput(const UiPointerEvent& event);
    W8ControlsRect Edges() const
    {
        return ToEdges(bounds);
    }
    int EditWidth() const
    {
        return bounds.w - 10;
    }
    Font& font()
    {
        return *group.style_.font;
    }

    /* Scrolls so the cursor stays visible; updates the caret x and visible byte span. */
    void Relayout()
    {
        const std::string_view view = text;
        const auto at = std::min(cursor, view.size());
        auto start = std::min(visible_start, at);
        while (start < at && font().textWidth(view.substr(start, at - start)) > EditWidth()) {
            start = wiz8::text::next_grapheme(view, start);
        }
        auto end = start;
        while (end < view.size()) {
            const auto next = wiz8::text::next_grapheme(view, end);
            if (font().textWidth(view.substr(start, next - start)) > EditWidth()) {
                break;
            }
            end = next;
        }
        visible_start = start;
        cursor_x = font().textWidth(view.substr(start, at - start)) + 2;
        visible_count = end - start;
        parking = start;
    }
    void SetCursor(std::size_t position)
    {
        position = std::min(position, text.size());
        std::size_t boundary = 0;
        while (boundary < position) {
            const auto next = wiz8::text::next_grapheme(text, boundary);
            if (next > position) {
                break;
            }
            boundary = next;
        }
        cursor = boundary;
        Relayout();
    }
    void SelectAll()
    {
        selection_start = 0;
        selection_end = text.size();
        cursor = text.size();
        selecting = true;
    }
    std::size_t MousePosition(int mouse_x)
    {
        const int offset = mouse_x - bounds.x;
        int previous_width = 0;
        for (auto position = parking; position < text.size();) {
            const auto end = wiz8::text::next_grapheme(text, position);
            const auto width = font().textWidth(std::string_view(text).substr(parking, end - parking));
            if (offset < (width + previous_width) / 2) {
                return position;
            }
            previous_width = width;
            position = end;
        }
        return text.size();
    }
    void MouseMoved(UiPointerAction action, int mouse_x, SDL_MouseButtonFlags buttons);
    void MouseClicked(const UiPointerEvent& event);

    TextFieldGroup& group;
    int id;
    short input_type = 0;
    std::size_t capacity = 0; // UTF-16 code units, the retail name/editor limit
    std::string text;
    bool enabled = true;
    bool use_inactive_color = false;
    bool blocks_mouse = false;
    std::size_t cursor = 0;
    std::size_t selection_start = 0;
    std::size_t selection_end = 0;
    bool selecting = false;
    std::size_t parking = 0;
    std::size_t visible_start = 0;
    std::size_t visible_count = 0;
    std::size_t mouse_down = 0;
    int cursor_x = 0;
    SDL_Rect bounds;
    HitTarget& target;
};

UiResult W8TextField::HandleInput(const UiPointerEvent& event)
{
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave ||
        event.action == UiPointerAction::Move) {
        MouseMoved(event.action, event.position.x, event.buttons);
        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Left &&
        (event.action == UiPointerAction::Down || event.action == UiPointerAction::Up)) {
        MouseClicked(event);
        return UiResult::Handled;
    }
    return UiResult::Pass;
}

void W8TextField::MouseMoved(UiPointerAction action, int mouse_x, SDL_MouseButtonFlags buttons)
{
    if (blocks_mouse) {
        return;
    }
    if (action == UiPointerAction::Enter) {
        SetTargetCursor(GetTextInputCursor());
    }
    if (action == UiPointerAction::Leave) {
        SetTargetCursor(W8_CURSOR_NONE);
    }
    if ((buttons & SDL_BUTTON_LMASK) == 0 || !group.focused_ || action != UiPointerAction::Move) {
        return;
    }
    if (group.focused_ != this) {
        group.focused_ = this;
        mouse_down = 0;
        cursor = 0;
        Relayout();
        selecting = false;
        selection_start = selection_end = 0;
    }
    const auto position = MousePosition(mouse_x);
    if (position == mouse_down) {
        selecting = false;
        return;
    }
    selection_start = std::min(mouse_down, position);
    selection_end = std::max(mouse_down, position);
    selecting = true;
    cursor = position;
    Relayout();
}

void W8TextField::MouseClicked(const UiPointerEvent& event)
{
    const auto mouse_x = event.position.x;
    const auto buttons = event.buttons;
    if (blocks_mouse) {
        return;
    }
    if (event.action == UiPointerAction::Down && event.clicks >= 2) {
        if (group.focused_) {
            group.SelectWordAtMouse(mouse_x);
        }
        return;
    }
    if (event.action == UiPointerAction::Down) {
        if (group.focused_ != this) {
            return;
        }
        SetCursor(MousePosition(mouse_x));
        mouse_down = cursor;
        return;
    }
    if (event.action != UiPointerAction::Up) {
        return;
    }
    if (group.focused_ != this) {
        if (!enabled) {
            return;
        }
        group.FocusField(*this);
        return;
    }
    if ((buttons & SDL_BUTTON_LMASK) != 0) {
        const auto position = MousePosition(mouse_x);
        if (position == mouse_down) {
            selecting = false;
        }
        SetCursor(position);
    }
}

TextFieldGroup::TextFieldGroup(Font& font) : TextFieldGroup(font, nullptr) {}
TextFieldGroup::TextFieldGroup(UiLayer& layer, Font& font) : TextFieldGroup(font, &layer) {}

TextFieldGroup::TextFieldGroup(Font& font, UiLayer* layer)
    : owned_layer_(layer ? nullptr : new UiLayer()), layer_(layer ? layer : owned_layer_.get()), previous_(g_ui.focus)
{
    style_.font = &font;
    style_.cursor_color = Get16BPPColor(SDL_Color{10, 10, 10, SDL_ALPHA_OPAQUE});
    if (previous_) {
        previous_->Suspend();
    }
    g_ui.focus = this;
    SetNativeTextInputEnabled(false);
}

TextFieldGroup::TextFieldGroup(int scheme) : TextFieldGroup(scheme == 0 ? g_font12point1 : g_wiz_text_mono_font)
{
    Assert(scheme >= 0 && scheme <= 2);
    SetScheme(scheme);
}

TextFieldGroup::~TextFieldGroup()
{
    focused_ = suspended_focus_ = nullptr;
    fields_.clear();
    if (g_ui.focus == this) {
        g_ui.focus = previous_;
        if (previous_) {
            previous_->Resume();
        } else {
            SetNativeTextInputEnabled(false);
        }
        return;
    }
    for (auto* group = g_ui.focus; group; group = group->previous_) {
        if (group->previous_ == this) {
            group->previous_ = previous_;
            break;
        }
    }
}

TextFieldGroup* TextFieldGroup::Focused()
{
    return g_ui.focus;
}

void TextFieldGroup::Suspend()
{
    for (auto& field : fields_) {
        field->target.setEnabled(false);
        field->enabled = false;
    }
    suspended_focus_ = focused_;
    focused_ = nullptr;
}

void TextFieldGroup::Resume()
{
    for (auto& field : fields_) {
        field->target.setEnabled(true);
        field->enabled = true;
    }
    focused_ = suspended_focus_ ? suspended_focus_ : fields_.empty() ? nullptr : fields_.front().get();
    suspended_focus_ = nullptr;
    SetNativeTextInputEnabled(editing_ && focused_);
}

void TextFieldGroup::SetEditing(bool editing)
{
    editing_ = editing;
    SetNativeTextInputEnabled(editing);
}

void TextFieldGroup::SetScheme(int scheme)
{
    auto& s = style_;
    if (scheme == 0) {
        s.font = &g_font12point1;
        s.field_color = Get16BPPColor(SDL_Color{200, 200, 0, SDL_ALPHA_OPAQUE});
        s.inactive_field_color = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        s.darker_color = Get16BPPColor(SDL_Color{24, 61, 81, SDL_ALPHA_OPAQUE});
        s.brighter_color = Get16BPPColor(SDL_Color{136, 138, 135, SDL_ALPHA_OPAQUE});
        s.bevelled = true;
        s.fore_color = 0x8d;
        s.shadow_color = 0;
        s.highlight_fore_color = 0xd0;
    } else if (scheme == 1) {
        s.font = &g_wiz_text_mono_font;
        s.field_color = Get16BPPColor(SDL_Color{30, 42, 99, SDL_ALPHA_OPAQUE});
        s.inactive_field_color = Get16BPPColor(SDL_Color{10, 10, 10, SDL_ALPHA_OPAQUE});
        s.darker_color = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        s.brighter_color = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        s.bevelled = true;
        s.fore_color = 4;
        s.shadow_color = 5;
        s.highlight_fore_color = 4;
        s.highlight_shadow_color = 5;
        s.highlight_back_color = 3;
        s.cursor_color = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        return;
    } else if (scheme == 2) {
        s.font = &g_wiz_text_mono_font;
        s.field_color = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        s.inactive_field_color = Get16BPPColor(SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE});
        s.darker_color = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        s.brighter_color = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE});
        s.bevelled = true;
        s.fore_color = 0x8d;
        s.shadow_color = 0;
        s.highlight_fore_color = 0xd0;
    } else {
        return;
    }
    s.highlight_shadow_color = 0xcd;
    s.highlight_back_color = 0xcd;
    s.cursor_color = Get16BPPColor(SDL_Color{10, 10, 10, SDL_ALPHA_OPAQUE});
}

int TextFieldGroup::Add(int left, int top, int width, int height, int priority, std::string_view text,
                        std::size_t capacity, short input_type, bool use_inactive_color)
{
    const int id = fields_.empty() ? 0 : fields_.back()->id + 1;
    auto& field =
        *fields_.emplace_back(std::make_unique<W8TextField>(*this, id, priority, SDL_Rect{left, top, width, height}));
    field.input_type = input_type;
    field.capacity = input_type == 0x1002 ? 6 : capacity;
    field.text.assign(text);
    field.use_inactive_color = use_inactive_color;
    if (fields_.size() == 1) {
        field.SelectAll();
    }
    return id;
}

W8TextField* TextFieldGroup::Find(int id) const
{
    const auto found = std::ranges::find(fields_, id, &W8TextField::id);
    return found == fields_.end() ? nullptr : found->get();
}

void TextFieldGroup::Remove(int id)
{
    const auto found = std::ranges::find(fields_, id, &W8TextField::id);
    if (found == fields_.end()) {
        return;
    }
    if (found->get() == focused_) {
        focused_ = nullptr;
        SetEditing(false);
    }
    if (found->get() == suspended_focus_) {
        suspended_focus_ = nullptr;
    }
    fields_.erase(found);
    if (fields_.empty()) {
        SetEditing(false);
    }
}

void TextFieldGroup::SetText(int id, std::string_view text)
{
    auto* field = Find(id);
    if (!field) {
        return;
    }
    field->text.assign(text);
    if (focused_) {
        focused_->selecting = false;
        focused_->SetCursor(0);
    }
}

std::string TextFieldGroup::Text(int id) const
{
    const auto* field = Find(id);
    return field ? field->text : std::string{};
}

std::size_t TextFieldGroup::ByteLength(int id) const
{
    const auto* field = Find(id);
    return field ? field->text.size() : 0;
}

void TextFieldGroup::SetBlocksMouse(int id, bool blocks)
{
    if (auto* field = Find(id)) {
        field->blocks_mouse = blocks;
    }
}

void TextFieldGroup::FocusField(W8TextField& field)
{
    focused_ = &field;
    field.SelectAll();
    field.Relayout();
    SetEditing(true);
}

void TextFieldGroup::Focus(int id)
{
    auto* field = Find(id);
    if (!field || field == focused_ || !field->enabled) {
        return;
    }
    FocusField(*field);
}

int TextFieldGroup::FocusedId() const
{
    return focused_ ? focused_->id : -1;
}

void TextFieldGroup::FocusNext()
{
    if (!focused_) {
        return;
    }
    auto* previous = focused_;
    auto position = std::ranges::find(fields_, previous, &std::unique_ptr<W8TextField>::get);
    for (std::size_t step = 0; step < fields_.size(); ++step) {
        if (++position == fields_.end()) {
            position = fields_.begin();
        }
        auto& candidate = **position;
        if (!candidate.enabled) {
            continue;
        }
        FocusField(candidate);
        // Retail: wrapping back to the only enabled field ends editing.
        if (&candidate == previous) {
            SetEditing(false);
        }
        return;
    }
    SetEditing(false);
}

void TextFieldGroup::ClearFocus()
{
    if (!focused_) {
        return;
    }
    SetEditing(false);
    focused_ = nullptr;
}

std::size_t TextFieldGroup::Cursor() const
{
    return focused_ ? focused_->cursor : 0;
}

void TextFieldGroup::SetCursor(std::size_t cursor)
{
    if (focused_) {
        focused_->SetCursor(cursor);
    }
}

void TextFieldGroup::DeleteSelection(std::size_t first, std::size_t last)
{
    if (last < first) {
        std::swap(first, last);
    }
    focused_->text.erase(first, last - first);
    focused_->selection_start = focused_->selection_end = 0;
    focused_->SetCursor(first);
}

void TextFieldGroup::InsertCharacter(char32_t character)
{
    auto& field = *focused_;
    std::string bytes;
    utf8::append(character, std::back_inserter(bytes));
    if (wiz8::text::to_utf16(field.text).size() + wiz8::text::to_utf16(bytes).size() > field.capacity) {
        return;
    }
    field.text.insert(field.cursor, bytes);
    field.cursor += bytes.size();
    field.Relayout();
}

/* Input types above 0x0fff accept one fixed format each: identifiers, a
   lowercase letter followed by digits, and HH:MM. */
void TextFieldGroup::InsertRestricted(char32_t character)
{
    auto& field = *focused_;
    const auto is_digit = [](char32_t c) { return c >= '0' && c <= '9'; };
    if (field.input_type == 0x1000) {
        if (IsUppercaseCharacter(character) == 0 && IsLowercaseCharacter(character) == 0 && !is_digit(character) &&
            character != '_' && character != '.') {
            return;
        }
        if (field.cursor == 0 && is_digit(character)) {
            return;
        }
        InsertCharacter(character);
        return;
    }
    if (field.input_type == 0x1001) {
        if (field.cursor == 0) {
            if (IsLowercaseCharacter(character) != 0) {
                InsertCharacter(character);
            } else if (IsUppercaseCharacter(character) != 0) {
                InsertCharacter(ToLowercaseCharacter(character));
            }
            return;
        }
        if (is_digit(character)) {
            InsertCharacter(character);
        }
        return;
    }
    if (field.input_type != 0x1002) {
        return;
    }
    switch (field.cursor) {
    case 0:
        if (character >= '0' && character <= '2') {
            InsertCharacter(character);
        }
        return;
    case 1:
        if (is_digit(character) && (field.text[0] != '2' || character <= '3')) {
            InsertCharacter(character);
        }
        if (field.text.size() <= 2) {
            InsertCharacter(':');
            return;
        }
        field.SetCursor(wiz8::text::next_grapheme(field.text, field.cursor));
        return;
    case 2:
        if (character == ':') {
            InsertCharacter(':');
        } else if (is_digit(character)) {
            InsertCharacter(':');
            InsertCharacter(character);
        }
        return;
    case 3:
        if (character >= '0' && character <= '5') {
            InsertCharacter(character);
        }
        return;
    case 4:
        if (is_digit(character)) {
            InsertCharacter(character);
        }
        return;
    default:
        return;
    }
}

bool TextFieldGroup::HandleInput(const InputEvent& event)
{
    const bool committed = event.type == InputEventType::Text;
    InputEvent normalized = event;
    if (committed) {
        normalized.type = InputEventType::KeyDown;
        normalized.key = 0;
        normalized.modifiers = 0;
    }
    const auto& input = normalized;
    if (!editing_ || !focused_ || (input.type != InputEventType::KeyDown && input.type != InputEventType::KeyRepeat) ||
        input.key == VK_ESCAPE || input.key == VK_RETURN || input.key == VK_TAB || (input.modifiers & ALT_DOWN) != 0 ||
        ((input.modifiers & CTRL_DOWN) != 0 && input.key != VK_DELETE && input.key != VK_RIGHT &&
         input.key != VK_LEFT) ||
        (input.key > 0x6f && input.key < 0x7c)) {
        return false;
    }
    auto& field = *focused_;
    const bool shift = (input.modifiers & SHIFT_DOWN) != 0;
    const auto extend_selection = [&] {
        if (!field.selecting) {
            field.selecting = true;
            field.selection_start = field.cursor;
        }
    };
    const std::size_t selection_end = field.selection_end;
    switch (input.key) {
    case 0x25: /* Left */
        if (shift) {
            extend_selection();
            if (field.cursor != 0) {
                field.cursor = wiz8::text::previous_grapheme(field.text, field.cursor);
                field.Relayout();
            }
            field.selection_end = field.cursor;
            return true;
        }
        if (field.selecting) {
            field.cursor = field.selection_start;
            field.selecting = false;
        } else if (field.cursor != 0) {
            field.cursor = wiz8::text::previous_grapheme(field.text, field.cursor);
        }
        field.Relayout();
        return true;
    case 0x27: /* Right */
        if (shift) {
            extend_selection();
            if (field.cursor < field.text.size()) {
                field.cursor = wiz8::text::next_grapheme(field.text, field.cursor);
                field.Relayout();
            }
            field.selection_end = field.cursor;
            return true;
        }
        if (field.selecting) {
            field.cursor = selection_end;
            field.selecting = false;
        } else if (field.cursor < field.text.size()) {
            field.cursor = wiz8::text::next_grapheme(field.text, field.cursor);
        }
        field.Relayout();
        return true;
    case 0x23: /* End */
        if (shift) {
            extend_selection();
        } else {
            field.selecting = false;
        }
        field.cursor = field.text.size();
        if (shift) {
            field.selection_end = field.cursor;
        }
        field.Relayout();
        return true;
    case 0x24: /* Home */
        if (shift) {
            extend_selection();
            field.selection_end = 0;
        } else {
            field.selecting = false;
        }
        field.cursor = 0;
        field.Relayout();
        return true;
    case 0x2e: /* Delete */
        if ((input.modifiers & CTRL_DOWN) != 0) {
            field.text.clear();
            field.selection_start = field.selection_end = 0;
            field.selecting = false;
            field.SetCursor(0);
            return true;
        }
        if (!field.selecting) {
            // Retail keeps the scroll position computed for the longer string.
            if (field.cursor < field.text.size()) {
                const auto end = wiz8::text::next_grapheme(field.text, field.cursor);
                field.text.erase(field.cursor, end - field.cursor);
            }
            return true;
        }
        field.selecting = false;
        if (field.selection_start != selection_end) {
            DeleteSelection(field.selection_start, selection_end);
        }
        return true;
    case 8: /* Backspace */
        if (!field.selecting) {
            if (field.cursor != 0) {
                const auto old_position = field.cursor;
                field.cursor = wiz8::text::previous_grapheme(field.text, field.cursor);
                field.text.erase(field.cursor, old_position - field.cursor);
                field.Relayout();
            }
        } else {
            field.selecting = false;
            if (field.selection_start != selection_end) {
                DeleteSelection(field.selection_start, selection_end);
            }
        }
        return true;
    default:
        break;
    }
    char32_t character =
        committed ? event.text : TranslateKeyToCharacter(static_cast<unsigned short>(input.key), input.modifiers);
    if (!committed && NativeTextInputEnabled()) {
        return true;
    }
    if (character == 0) {
        return true;
    }
    if (field.selecting) {
        field.selecting = false;
        if (field.selection_start != field.selection_end) {
            DeleteSelection(field.selection_start, field.selection_end);
        }
    }
    const auto input_type = static_cast<unsigned short>(field.input_type);
    if (input_type > 0x0fff) {
        InsertRestricted(character);
        return true;
    }
    const bool space = character == ' ' && (input_type & 4) != 0;
    const bool sign = character == '-' && (input_type & 2) != 0 && field.cursor == 0;
    const bool digit = character >= '0' && character <= '9' && (input_type & 1) != 0;
    const bool letters = (input_type & 2) != 0;
    const bool punctuation = (input_type & 8) != 0 && IsPunctuationCharacter(character) != 0;
    if (letters && IsUppercaseCharacter(character) != 0) {
        InsertCharacter((input_type & 0x20) != 0 ? ToLowercaseCharacter(character) : character);
    } else if (letters && IsLowercaseCharacter(character) != 0) {
        InsertCharacter((input_type & 0x10) != 0 ? ToUppercaseCharacter(character) : character);
    } else if (space || sign || digit || punctuation) {
        InsertCharacter(character);
    }
    return true;
}

void TextFieldGroup::SelectWordAtMouse(int mouse_x)
{
    auto& field = *focused_;
    const auto position = field.MousePosition(mouse_x);
    if (position < field.text.size() && field.text[position] == ' ') {
        return;
    }
    const auto before = position == 0 ? std::string::npos : field.text.rfind(' ', position);
    const auto after = field.text.find(' ', position + 1);
    field.selection_start = before == std::string::npos ? 0 : before + 1;
    field.selection_end = after == std::string::npos ? field.text.size() : after;
    field.selecting = true;
}

void TextFieldGroup::RenderBackground(W8TextField& field, SDL_Surface& target)
{
    const auto [left, top, right, bottom] = field.Edges();
    if (style_.bevelled) {
        const SDL_Rect darker{left, top, right - left, bottom - top};
        const SDL_Rect brighter{left + 1, top + 1, right - left - 1, bottom - top - 1};
        SDL_FillSurfaceRect(&target, &darker, style_.darker_color);
        SDL_FillSurfaceRect(&target, &brighter, style_.brighter_color);
    }
    unsigned short colour =
        !field.enabled && !style_.disabled_auto_shade ? style_.disabled_field_color : style_.field_color;
    if (field.use_inactive_color && &field != focused_) {
        colour = style_.inactive_field_color;
    }
    const SDL_Rect area{left, top, right - left, bottom - top};
    SDL_FillSurfaceRect(&target, &area, colour);
    InvalidateRegion(left, top, right, bottom);
}

namespace {
TextOptions FieldTextOptions(Font& font, unsigned char fore, unsigned char shadow, unsigned char background)
{
    TextOptions options;
    options.colors.foreground = font.color(fore);
    options.colors.shadow = font.color(shadow);
    if (options.colors.shadow == 0 && shadow != 0) {
        options.colors.shadow = 1;
    }
    options.colors.background = font.color(background);
    options.mono = true;
    return options;
}
} // namespace

void TextFieldGroup::RenderFocused(SDL_Surface& target)
{
    if (!focused_) {
        return;
    }
    auto& field = *focused_;
    auto& font = *style_.font;
    if (IsLeftButtonHeld()) {
        if (GetGameMousePosition().x < field.Edges().left) {
            if (field.cursor != 0) {
                field.cursor = wiz8::text::previous_grapheme(field.text, field.cursor);
                field.Relayout();
            }
            if (field.selecting) {
                field.selection_start = field.visible_start;
            }
        } else if (field.Edges().right < GetGameMousePosition().x) {
            if (field.cursor < field.text.size()) {
                field.cursor = wiz8::text::next_grapheme(field.text, field.cursor);
                field.Relayout();
            }
            if (field.selecting) {
                field.selection_end = field.visible_count + field.visible_start;
            }
        }
    }
    const unsigned short font_height = font.height();
    const int text_top = field.Edges().top + (field.Edges().bottom - field.Edges().top - font_height) / 2;
    RenderBackground(field, target);
    const std::string_view visible = std::string_view(field.text).substr(field.parking);
    const bool has_selection = field.selecting && field.selection_start != field.selection_end;
    const auto selection_first = std::min(field.selection_start, field.selection_end);
    const auto selection_last = std::max(field.selection_start, field.selection_end);
    for (std::size_t index = 0; index < field.visible_count && index < visible.size();) {
        const short prefix = font.prefixWidth(visible, index);
        const auto absolute = field.parking + index;
        const bool selected = has_selection && selection_first <= absolute && absolute < selection_last;
        const auto options = selected ? FieldTextOptions(font, style_.highlight_fore_color,
                                                         style_.highlight_shadow_color, style_.highlight_back_color)
                                      : FieldTextOptions(font, style_.fore_color, style_.shadow_color, 0);
        std::size_t end = index;
        wiz8::text::next(visible, end);
        DrawText(target, font, {field.Edges().left + prefix + 3, text_top}, visible.substr(index, end - index),
                 options);
        index = end;
    }
    if (editing_ && !IsLeftButtonHeld() && w8_get_ticks() % 1000 < 500) {
        const SDL_Rect caret{field.Edges().left + field.cursor_x, text_top, 1, font_height};
        SDL_FillSurfaceRect(&target, &caret, style_.cursor_color);
    }
}

void TextFieldGroup::RenderInactive(W8TextField& field, SDL_Surface& target)
{
    auto& font = *style_.font;
    const bool disabled = !field.enabled && style_.disabled_auto_shade;
    const auto options = disabled ? FieldTextOptions(font, style_.disabled_fore_color, style_.disabled_shadow_color, 0)
                                  : FieldTextOptions(font, style_.fore_color, style_.shadow_color, 0);
    const unsigned short font_height = font.height();
    const int text_top = field.Edges().top + (field.Edges().bottom - field.Edges().top - font_height) / 2;
    RenderBackground(field, target);
    const std::string_view text = field.text;
    for (std::size_t index = 0; index < text.size();) {
        const short prefix = font.prefixWidth(text, index);
        if (field.EditWidth() < prefix + 3) {
            break;
        }
        std::size_t end = index;
        wiz8::text::next(text, end);
        DrawText(target, font, {field.Edges().left + prefix + 3, text_top}, text.substr(index, end - index), options);
        index = end;
    }
    if (disabled) {
        const auto bounds = field.bounds;
        ShadowRect(target, SDL_Rect{bounds.x, bounds.y, bounds.w + 1, bounds.h + 1});
    }
}

void TextFieldGroup::RenderAll(SDL_Surface& target)
{
    for (auto* group = previous_; group; group = group->previous_) {
        for (auto& field : group->fields_) {
            group->RenderInactive(*field, target);
        }
    }
    for (auto& field : fields_) {
        if (field.get() == focused_) {
            RenderFocused(target);
        } else {
            RenderInactive(*field, target);
        }
    }
}
