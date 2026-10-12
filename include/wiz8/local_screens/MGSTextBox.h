#include "Font.h"
#pragma once

#include <cstddef>
#include <vector>
#include <cstdint>
#include <string>
#include <string_view>

#include "input.h"
#include "wiz8/regions.h"

struct W8MonsterInfo;
namespace wiz8 { class File; }

enum W8NoticeWordState {
    W8_NOTICE_WORD_NORMAL = 0,
    W8_NOTICE_WORD_HOVERED = 1,
    W8_NOTICE_WORD_SELECTED = 2
};

struct W8NoticeWord {
    std::size_t start;
    std::size_t end;
    std::int16_t x_start;
    std::int16_t x_end;
    std::uint8_t keyword; /* 0 none, 1 keyword, 2 selected */
    bool redraw;           /* repaint once after deselection */
};

void AdvanceNoticeLine(std::int16_t text_box);

struct W8MessageStorageRecord {
    void RebuildEntries();

    std::string text;
    std::uint8_t font_palette;
    /* The recoloured span [start, stop) of this line. */
    std::uint8_t highlight_color;
    std::size_t highlight_start;
    std::size_t highlight_stop;
    std::uint64_t clock;
    /* SaveGame snapshots the unsigned milliseconds remaining from
       TimeUntilDeadline; load rearms the countdown with this duration. */
    std::uint32_t saved_remaining_ms;
    /* Continuation link count of a wrapped entry; -1 when unlinked. */
    std::int32_t link;
    std::int32_t length; /* UTF-8 byte length, -1 when unset */
    std::vector<W8NoticeWord> entries;
    std::uint8_t unknown_1c[8];
};


extern W8MessageStorageRecord g_message_storage[4][0x15e];

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

// Scroll-arrow visual states stored in text_content_region / dialogue_content_region.
enum TextBoxArrowState : std::int32_t {
    kUpArrowDisabled = 0x56,
    kUpArrowEnabled = 0x57,
    kUpArrowPressed = 0x58,
    kDownArrowDisabled = 0x59,
    kDownArrowEnabled = 0x5a,
    kDownArrowPressed = 0x5b,
};

void InitTextBoxRegions();
void ShutdownTextBoxRegions();
extern ScreenRegionSet g_text_box_regions;
void EnableDialogueTextBoxRegions();
void DisableDialogueTextBoxRegions();
void SetTextBoxScrollInput(bool enabled);
void SetTextBoxChannelTabInput(bool enabled);
void SetTextBoxMuteInput(bool enabled);

/* Reset one editor status line; -1 selects the current line. */
void ResetEditorStatusLine(std::int16_t line);
/* The font currently bound to the main text box. */
Font& GetTextBoxFont(void);
/* Repaint dirty dialogue text-input lines and the caret. */
void RedrawDialogueTextInput(void);
/* Clear one text_lines slot on the level block. */
void ClearTextLineEntry(std::int32_t index);

void ReleaseMessageStorage(void);
/* The TEXT section pair - the four message-storage
   runs persisted around the game-status record. */
std::uint8_t SaveTextBoxState(wiz8::File* file);
bool HandleDialogueTextInput(const InputEvent* input);
char TextBoxHandleKey(const InputEvent* event);
std::int32_t GetSelectedTextLine(std::int32_t index);
void ClearSelectedTextLine(std::int32_t index);
/* Hover and selection track one wrapped-line start per box. Set operations
   snap a visible wrapped index back to its entry's first line. */
std::int32_t GetHoveredTextLine(std::int32_t index);
void ClearHoveredTextLine(std::int32_t index);
void SetHoveredTextLine(std::int32_t line, std::int32_t box);
void SetSelectedTextLine(std::int32_t line, std::int32_t box);
/* Walk every notice-word list of a box, clearing the
   flag_08 mark and raising flag_09; the first touches only selected (2)
   words, the second everything else. Nonzero redraw repaints the body
   through RedrawTextBoxBody(1). */
void ClearNoticeWordHover(std::int32_t box, bool redraw);
void ResetUsedNoticeWords(std::int32_t text_box, bool redraw);
/* Refresh the hover mark on the notice word under (x, y). */
void HighlightNoticeWordAt(std::int32_t box, std::uint16_t x, std::uint16_t y);
/* The notice word under (x, y) in box, or 0; the word's line
   slot is written through line_out. */
W8NoticeWord* HitTestNoticeWord(std::int32_t text_box, std::uint16_t x, std::uint16_t y, std::int32_t* line_out);
/* Copy a word's text span out of its source line. */
std::string CopyNoticeWordText(const W8NoticeWord* word, std::int32_t box, std::int32_t line);
void ScrollTextBoxToCursor(void);
std::uint8_t GetTextBoxMode(void);
void SetTextBoxMode(std::uint8_t mode, std::int32_t value);
/* Re-derive the text-box mode from the live screen state; 0xffff
   asks for the automatic choice. */
void RefreshTextBoxMode(std::uint16_t mode);
/* The lock-interaction call convention - its caller passes the
   same (level, flag, backfire) triple CastSpellAtLockInteraction
   takes; the body reads the target only. */
void SetKnockKnockTarget(std::int32_t target, std::int32_t flag, std::int32_t backfire);
/* The trap-mode half of CastSpellAtLockInteraction;
   same (level, flag, backfire) triple. */
void AttemptTrapDisarm(std::int32_t level, std::int32_t flag, char backfire);

/* Merge text onto a box's last used line, re-posting the combined
   line so wrapping, highlighting and the link counts rebuild; -1 picks the box
   the current game mode writes to. */
void AppendToLastTextLine(std::string_view text, std::int16_t text_box);
/* Wrapped line count of the notice ShowNotice last displayed;
   only maintained while game_status.quote_audit is raised. */
extern std::int32_t g_notice_line_count;
/* The number of lines the notice pane can scroll. */
std::int32_t GetTextBoxScrollRange(void);
void PostCharacterNotice(std::int32_t party_slot, std::string_view text, std::int32_t context = -1);
void PostMonsterNotice(W8MonsterInfo* monster_info, std::string_view text);
void ScrollTextBoxTo(std::int32_t line);
void ScrollDialogueTextBoxToLine(void);
void ScrollTextBoxUp(std::int32_t lines);
void ScrollTextBoxDown(std::int32_t lines);
std::int32_t GetTextBoxVisibleLineCount(void);

/* Recolour the character span [start, stop) of the most recent
   line in one text box; -1 picks the box the current game mode writes to. */
void HighlightTextBoxRange(std::uint8_t color, std::size_t start, std::size_t stop,
                           std::int16_t text_box);

void SelectTextBox(std::int16_t value);
/* Ask for the text box to be redrawn without changing anything. */
void RedrawTextBox(void);
/* Repaint visible text-box lines; skip_invalidate nonzero skips
   InvalidateRegion of the text rectangle. */
void RedrawTextBoxBody(bool skip_invalidate);
/* Redraw text-box scroll up/down buttons and thumb, then body. */
void RedrawTextBoxScrollChrome(void);
/* Append text to a box's current line; the box argument is optional. */
void AppendTextBoxLine(std::string_view text, std::int16_t text_box = -1);

bool CurrentTextLineHasContent(void);
bool CurrentDialogueLineHasContent(void);
std::int32_t FindStoppedTextLine(void);
void SetTextBoxRegionBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
void ResetMessageStorage(void);
/* Write the four message runs into the open TEXT chunk. */
bool SaveMessageStorage(wiz8::File& file);
/* Rebuild the four message runs from the open TEXT chunk. */
std::uint8_t LoadMessageStorage(wiz8::File& file);
/* Defaults select the live mode's text box and its available line width. */
const std::int16_t W8_NOTICE_TEXT_BOX_AUTOMATIC = -1;
const std::uint32_t W8_NOTICE_WRAP_AUTOMATIC = ~0U;

void ShowNotice(std::uint32_t font_palette, std::string_view text,
                std::int16_t text_box = W8_NOTICE_TEXT_BOX_AUTOMATIC,
                std::uint32_t wrap_width = W8_NOTICE_WRAP_AUTOMATIC, bool force_dialog = false);

void HighlightNoticeRow(std::int32_t line);
