#pragma once
#include <string>
#include <string_view>

#include "input.h"
#include <stddef.h>

struct TEXTINPUTNODE;
struct Font;

void InitTextInputMode(Font& font);
void InitTextInputModeWithScheme(int mode);
void KillTextInputMode(void);
char AddTextInputField(int left, int top, int width, int height, int priority, std::string_view text,
                       std::size_t capacity, short input_type,
                       unsigned char use_inactive_text_field_color);
void RemoveTextInputField(int index);
std::size_t GetTextInputFieldByteLength(unsigned char index);
void SetActiveField(char index);
short GetActiveTextInputField(void);
void SetInputFieldText(unsigned char field, std::string_view text);
std::string GetTextFromField(unsigned char field);
void ClearActiveField(void);
void SelectNextField(void);
bool EditingText(void); /* 0x005D5A00 */
unsigned int HandleTextInput(const InputAtom* input);
void RenderActiveTextField(void);
void RenderAllTextFields(void);

int GetTextInputCursor(void); /* 0x0055EF80: mouse cursor image */
std::size_t GetTextInputByteCursor(void);
void MouseMovedInTextRegionCallback(TEXTINPUTNODE* field, int reason);
void MouseClickedInTextRegionCallback(TEXTINPUTNODE* field, int reason);
void SetTextInputScheme(char mode);
std::size_t CalculateCursorPos(int width, std::size_t cursor, std::string_view text, int* cursor_width,
                                size_t* visible_count);
void RenderBackgroundField(TEXTINPUTNODE* field);
void RenderInactiveTextFieldNode(TEXTINPUTNODE* field);
void SelectAllText(void);
void HandleExclusiveInput(unsigned short character);
void AddChar(char32_t character);
void SetTextInputCursor(std::size_t cursor);
void SetInputFieldBlocksMouseCallback(unsigned char field_id, bool blocks);
