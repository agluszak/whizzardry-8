#pragma once
#include <array>
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
#include "wiz8/ui_layer.h"

struct W8MonsterInfo;

void AdvanceNoticeLine(short text_box);

struct MainGameScreen;

// The text box's input layers and regions live with their originating screen.
class MainGameTextInput {
public:
    explicit MainGameTextInput(MainGameScreen& screen);
    void setEnabled(bool enabled);
    void setDialogueEnabled(bool enabled);
    void setPanelEnabled(bool enabled);
    void setScrollEnabled(bool enabled);
    void setChannelTabsEnabled(bool enabled);
    void setMuteEnabled(bool enabled);
    void setBounds(SDL_Rect bounds);

private:
    void setRegions(std::size_t first, std::size_t count, bool enabled);
    UiLayer body{UiLayerOrder::MessageBox}, tabs{UiLayerOrder::MessageBox};
    std::array<HitTarget*, 8> targets{};
};

/* Reset one editor status line; -1 selects the current line. */
void ResetEditorStatusLine(short line);

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
/* Re-derive the text-box mode from the live screen state; 0xffff
   asks for the automatic choice. */
void RefreshTextBoxMode(unsigned short mode);
/* The lock-interaction call convention - its caller passes the
   same (level, flag, backfire) triple CastSpellAtLockInteraction
   takes; the body reads the target only. */
void SetKnockKnockTarget(int target, int flag, int backfire);
/* The trap-mode half of CastSpellAtLockInteraction;
   same (level, flag, backfire) triple. */
void AttemptTrapDisarm(int level, int flag, signed char backfire);

void PostCharacterNotice(int party_slot, std::string_view text, int context = -1);

/* Recolour the character span [start, stop) of the most recent
   line in one text box; -1 picks the box the current game mode writes to. */
void HighlightTextBoxRange(unsigned char color, std::size_t start, std::size_t stop,
                           short text_box);

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
/* Rebuild every logged line's word hit boxes for the current text-box font. */
void RebuildMessageLogWords(void);
// Return the number of wrapped lines posted to the message log, or zero for a dialog.
int ShowNotice(unsigned int font_palette, std::string_view text,
               short text_box = W8_NOTICE_TEXT_BOX_AUTOMATIC,
               unsigned int wrap_width = W8_NOTICE_WRAP_AUTOMATIC, bool force_dialog = false);

void HighlightNoticeRow(int line);
