#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "input.h"
#include "wiz8/ui_layer.h"

struct Font;
struct SDL_Surface;
class W8TextField;

struct TextFieldStyle {
    Font* font = nullptr;
    unsigned short field_color = 0;
    unsigned char fore_color = 0;
    unsigned char shadow_color = 0;
    unsigned char highlight_fore_color = 0;
    unsigned char highlight_shadow_color = 0;
    unsigned char highlight_back_color = 0;
    unsigned short cursor_color = 0;
    unsigned short inactive_field_color = 0;
};

/* A screen's text fields, their tab order, and which one has keyboard focus.
   Creating a group suspends the previously focused group (its fields render
   disabled and ignore the mouse) until this group is destroyed. SDL text input
   is enabled exactly while the focused field is editing. */
class TextFieldGroup {
public:
    TextFieldGroup(UiLayer& layer, Font& font);
    ~TextFieldGroup();
    TextFieldGroup(const TextFieldGroup&) = delete;
    TextFieldGroup& operator=(const TextFieldGroup&) = delete;

    /* The innermost live group; it receives typed text. */
    static TextFieldGroup* Focused();

    int Add(int left, int top, int width, int height, int priority, std::string_view text,
            std::size_t capacity, short input_type, bool use_inactive_color);
    void Remove(int id);
    void UseGameStyle();

    void SetText(int id, std::string_view text);
    std::string Text(int id) const;
    void SetBlocksMouse(int id, bool blocks);

    void Focus(int id);
    int FocusedId() const;
    void FocusNext();
    void ClearFocus();
    bool Editing() const { return editing_; }

    /* Returns true when the focused field consumed the key or text event. */
    bool HandleInput(const InputEvent& input);
    std::size_t Cursor() const;
    void SetCursor(std::size_t cursor);

    void RenderAll(SDL_Surface& target);

private:
    friend class W8TextField;

    W8TextField* Find(int id) const;
    void Suspend();
    void Resume();
    void SetEditing(bool editing);
    void FocusField(W8TextField& field);
    void RenderFocused(SDL_Surface& target);
    void RenderBackground(W8TextField& field, SDL_Surface& target);
    void RenderInactive(W8TextField& field, SDL_Surface& target);
    void InsertCharacter(char32_t character);
    void InsertRestricted(char32_t character);
    void DeleteSelection(std::size_t first, std::size_t last);
    void SelectWordAtMouse(int mouse_x);

    TextFieldStyle style_;
    UiLayer& layer_;
    std::vector<std::unique_ptr<W8TextField>> fields_;
    W8TextField* focused_ = nullptr;
    W8TextField* suspended_focus_ = nullptr;
    bool editing_ = false;
    TextFieldGroup* previous_ = nullptr;
};
