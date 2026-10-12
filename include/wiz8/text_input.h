#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "input.h"
#include <stddef.h>

struct TEXTINPUTNODE;
struct Font;

void InitTextInputMode(Font& font);
void InitTextInputModeWithScheme(std::int32_t mode);
void KillTextInputMode(void);
char AddTextInputField(std::int32_t left, std::int32_t top, std::int32_t width, std::int32_t height, std::int32_t priority, std::string_view text,
                       std::size_t capacity, std::int16_t input_type,
                       std::uint8_t use_inactive_text_field_color);
void RemoveTextInputField(std::int32_t index);
std::size_t GetTextInputFieldByteLength(std::uint8_t index);
void SetActiveField(char index);
std::int16_t GetActiveTextInputField(void);
void SetInputFieldText(std::uint8_t field, std::string_view text);
std::string GetTextFromField(std::uint8_t field);
void ClearActiveField(void);
void SelectNextField(void);
bool EditingText(void); /* 0x005D5A00 */
std::uint32_t HandleTextInput(const InputEvent* input);
void RenderActiveTextField(void);
void RenderAllTextFields(void);

std::int32_t GetTextInputCursor(void); /* 0x0055EF80: mouse cursor image */
std::size_t GetTextInputByteCursor(void);
void MouseMovedInTextRegionCallback(TEXTINPUTNODE* field, std::int32_t reason);
void MouseClickedInTextRegionCallback(TEXTINPUTNODE* field, std::int32_t reason);
void SetTextInputScheme(char mode);
std::size_t CalculateCursorPos(std::int32_t width, std::size_t cursor, std::string_view text, std::int32_t* cursor_width,
                                size_t* visible_count);
void RenderBackgroundField(TEXTINPUTNODE* field);
void RenderInactiveTextFieldNode(TEXTINPUTNODE* field);
void SelectAllText(void);
void HandleExclusiveInput(std::uint16_t character);
void AddChar(char32_t character);
void SetTextInputCursor(std::size_t cursor);
void SetInputFieldBlocksMouseCallback(std::uint8_t field_id, bool blocks);
