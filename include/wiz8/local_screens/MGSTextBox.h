#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/canvas/Font.h"
#include "wiz8/message_log.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/events.h"
#include "wiz8/ui_input.h"

struct W8MonsterInfo;

void AdvanceNoticeLine(short text_box);

enum class TextBoxRegion : std::size_t {
    ScrollUp,
    ScrollDown,
    ScrollThumb,
    Body,
    ChannelTab1,
    ChannelTab2,
    ChannelTab3,
    Mute
};

void InitTextBoxRegions();
void ShutdownTextBoxRegions();
void SetTextBoxEnabled(bool enabled);
void EnableDialogueTextBoxRegions();
void DisableDialogueTextBoxRegions();
void SetTextBoxScrollInput(bool enabled);
void SetTextBoxChannelTabInput(bool enabled);
void SetTextBoxMuteInput(bool enabled);

/* Reset one editor status line; -1 selects the current line. */
void ResetEditorStatusLine(short line);

signed char TextBoxHandleKey(const InputEvent* event);
void ClearSelectedTextLine(int index);
void ClearHoveredTextLine(int index);
void SetHoveredTextLine(int line, int box);
void SetSelectedTextLine(int line, int box);
/* Walk every notice-word list of a box, clearing the
   flag_08 mark and raising flag_09; the first touches only selected (2)
   words, the second everything else. Nonzero redraw repaints the body
   through RedrawTextBoxBody(1). */
void ClearNoticeWordHover(int box, bool redraw);
void ResetUsedNoticeWords(int text_box, bool redraw);
/* Refresh the hover mark on the notice word under (x, y). */
void HighlightNoticeWordAt(int box, unsigned short x, unsigned short y);
/* The notice word under (x, y) in box, or 0; the word's line
   slot is written through line_out. */
W8NoticeWord* HitTestNoticeWord(int text_box, unsigned short x, unsigned short y, int* line_out);
/* Copy a word's text span out of its source line. */
std::string CopyNoticeWordText(const W8NoticeWord* word, int box, int line);
void ScrollTextBoxToCursor(void);
/* The lock-interaction call convention - its caller passes the
   same (level, flag, backfire) triple CastSpellAtLockInteraction
   takes; the body reads the target only. */
void SetKnockKnockTarget(int target, int flag, int backfire);
/* The trap-mode half of CastSpellAtLockInteraction;
   same (level, flag, backfire) triple. */
void AttemptTrapDisarm(int level, int flag, signed char backfire);

/* Wrapped line count of the notice ShowNotice last displayed;
   only maintained while game_status.quote_audit is raised. */
extern int g_notice_line_count;
void PostCharacterNotice(int party_slot, std::string_view text, int context = -1);
void ScrollTextBoxTo(int line);
void ScrollDialogueTextBoxToLine(void);
void ScrollTextBoxUp(int lines);
void ScrollTextBoxDown(int lines);
int GetTextBoxVisibleLineCount(void);

void SelectTextBox(short value);
/* Ask for the text box to be redrawn without changing anything. */
void RedrawTextBox(void);
/* Repaint visible text-box lines; skip_invalidate nonzero skips
   InvalidateRegion of the text rectangle. */
void RedrawTextBoxBody(bool skip_invalidate);
/* Redraw text-box scroll up/down buttons and thumb, then body. */
void RedrawTextBoxScrollChrome(void);
/* Append text to a box's current line; the box argument is optional. */
void AppendTextBoxLine(std::string_view text, short text_box = -1);

bool CurrentTextLineHasContent(void);
bool CurrentDialogueLineHasContent(void);
int FindStoppedTextLine(void);
void SetTextBoxRegionBounds(int left, int top, int right, int bottom);
/* Rebuild every logged line's word hit boxes for the current text-box font. */
void RebuildMessageLogWords(void);
void ShowNotice(unsigned int font_palette, std::string_view text,
                short text_box = W8_NOTICE_TEXT_BOX_AUTOMATIC,
                unsigned int wrap_width = W8_NOTICE_WRAP_AUTOMATIC, bool force_dialog = false,
                NoticeHighlight highlight = {});

void HighlightNoticeRow(int line);
