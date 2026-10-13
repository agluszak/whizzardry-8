#include <SDL3/SDL_mouse.h>
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/ui_layer.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/modal_content.h"
#include "himage.h"
#include <memory>
#include <SDL3/SDL_surface.h>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <string_view>
#include <SDL3/SDL_rect.h>
#include <string>
#include <vector>
#include <span>
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <iterator>
#include <utility>

#include "image.h"
#include "input.h"
#include "random.h"
#include "utf8/checked.h"
#include "wiz8/unicode.h"
#include "wiz8/compat/unaligned.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/mipe.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/xstatus.h"
#include "timer.h"
#include "Font.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/sr_api.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/fonts.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "dialog_code/DialogBase.h"
#include "game_timer.h"
#include "layouts/character.h"
#include "layouts/main_game_screen.h"
#include "local_code/ControlsRect.h"
#include "local_code/MonsterManager.h"
#include "local_code/RangeControl.h"

// GLOBAL: WIZ8 0x0068f2d4
std::unique_ptr<W8MainGameScreen> g_main_game_screen;

#define MGS_TEXT_BOX_CPP __FILE__

namespace {
struct MessageBoxInput {
    UiLayer body{0x14}, tabs{0x14};
    std::array<HitTarget*, 8> targets{};
};
std::unique_ptr<MessageBoxInput> message_input;
UiResult HandleTextScroll(const UiPointerEvent& event, bool up);
UiResult HandleTextThumb(const UiPointerEvent& event);
UiResult HandleTextBody(const UiPointerEvent& event);
UiResult HandleTextChannel(const UiPointerEvent& event, int channel);
}

/* The TEXT chunk's 0x24-byte form is separate from the live message record:
   its first word is a UTF-16 unit count. The obsolete list-pointer word
   remains reserved on disk and is always written as zero. */
struct W8MessageStorageDiskRecord {
    w8_ulong character_count;
    unsigned char font_palette;
    unsigned char highlight_color;
    unsigned char highlight_start;
    unsigned char highlight_stop;
    std::uint32_t clock; // Retail stores 32 bits; live deadlines use the native 64-bit clock.
    std::uint32_t saved_remaining_ms;
    int link;
    int length;
    w8_ulong serialized_entries_18_bits;
    unsigned char trailing_bytes[8];
};

static_assert(sizeof(W8MessageStorageDiskRecord) == 0x24,
              "W8MessageStorageDiskRecord_must_be_0x24");
static_assert(offsetof(W8MessageStorageDiskRecord, character_count) == 0x00,
              "W8MessageStorageDiskRecord_count_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, font_palette) == 0x04,
              "W8MessageStorageDiskRecord_byte_fields_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, highlight_stop) == 0x07,
              "W8MessageStorageDiskRecord_last_byte_field_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, clock) == 0x08,
              "W8MessageStorageDiskRecord_scalar_fields_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, saved_remaining_ms) == 0x0c,
              "W8MessageStorageDiskRecord_saved_remaining_ms_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, link) == 0x10,
              "W8MessageStorageDiskRecord_link_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, length) == 0x14,
              "W8MessageStorageDiskRecord_length_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, serialized_entries_18_bits) == 0x18,
              "W8MessageStorageDiskRecord_entries_bits_offset");
static_assert(offsetof(W8MessageStorageDiskRecord, trailing_bytes) == 0x1c,
              "W8MessageStorageDiskRecord_trailing_offset");
static_assert(sizeof(w8_ulong) == 4, "W8MessageStorageDiskRecord_requires_32_bit_words");
// GLOBAL: WIZ8 0x0069b7b8
static unsigned char g_text_box_mode;
// GLOBAL: WIZ8 0x0069b7bc
int g_notice_line_count;
// GLOBAL: WIZ8 0x0064bd54
static int g_text_box_value = W8_FONT_PALETTE_BEIGE;
/* The screen the text box belongs to; its two panels sit at 0x0c
   and 0x14. */

/* Release every heap object owned by the four-by-350 message store, then
   return each record to its all-zero initial state.  The retail body walks
   the same record boundary twice: 0x15e records per run and four runs up to
   the next global at 0x0069B7D0. */
// GLOBAL: WIZ8 0x0068f2d8
W8MessageStorageRecord g_message_storage[4][0x15e];

static void AppendNoticeLine(unsigned char font_palette, std::string_view text, short text_box,
                             int wrapped_line);

// Rebuild byte ranges and pixel hit boxes when the text or font changes.
void W8MessageStorageRecord::RebuildEntries()
{
    entries.clear();
    const std::string_view value = text;
    for (auto start = value.find_first_not_of(' '); start != value.npos;
         start = value.find_first_not_of(' ', start)) {
        auto end = value.find(' ', start);
        if (end == value.npos)
            end = value.size();
        const auto x = g_level_block->text_box_font->prefixWidth(value, start);
        const auto width =
            g_level_block->text_box_font->textWidth(value.substr(start, end - start));
        entries.push_back(
            {start, end - 1, x, static_cast<short>(x + width), W8_NOTICE_WORD_NORMAL, false});
        start = end;
    }
}

void ResetMessageStorage(void)
{
    int row;
    int index;

    for (row = 0; row < 4; ++row) {
        for (index = 0; index < 0x15e; ++index) {
            W8MessageStorageRecord* record = &g_message_storage[row][index];
            if (record->text.empty()) {
                continue;
            }
            record->RebuildEntries();
        }
    }
}

static void ReleaseMessageRow(int row)
{
    for (auto& record : g_message_storage[row]) {
        record = {};
    }
}

void ResetEditorStatusLine(short line)
{
    if (line == -1) {
        line = g_status.text_line_cursor;
    }
    ReleaseMessageRow(line);
    g_status.text_box_lines_used[line] = 0;
    g_status.text_box_lines_shown[line] = 0;
    if (!IsNpcDialogueTextInputActive()) {
        g_level_block->text_lines[line] = 0;
    }
    g_level_block->text_lines[4 + line] = -1;
    g_level_block->hovered_text_lines[line] = -1;
    g_level_block->selected_text_lines[line] = -1;
    g_level_block->text_content_region = kUpArrowDisabled;
    g_level_block->dialogue_content_region = kDownArrowDisabled;
    g_level_block->dialogue_text_input_open = false;
    RedrawTextBox();
}

/* Shared default-box selection in notice formatting, wrapping and line
   updates. The descriptive name covers the repeated operation; explicit box
   numbers pass through unchanged. */
static short ResolveNoticeTextBox(short text_box)
{
    if (text_box == W8_NOTICE_TEXT_BOX_AUTOMATIC) {
        if ((gXStatus.fNpcDialogueMode && !CanOpenNpcDialogue()) || gXStatus.fCampMode) {
            return IsNpcDialogueTextBoxActive() ? 0 : 2;
        }
        if (IsMipeActive()) {
            return 0;
        }
        return gXStatus.fCombatMode;
    }
    return text_box;
}

static void AppendNoticeLine(unsigned char font_palette, std::string_view text, short text_box,
                             int wrapped_line)
{
    text_box = ResolveNoticeTextBox(text_box);
    if (g_current_screen_state.id != W8_SCREEN_MAIN_GAME &&
        g_current_screen_state.id != W8_SCREEN_CAMP &&
        g_current_screen_state.id != W8_SCREEN_PLEASE_WAIT &&
        g_current_screen_state.id != W8_SCREEN_CHARACTER) {
        return;
    }
    if (g_status.text_box_lines_used[text_box] == 350) {
        W8MessageStorageRecord* first = &g_message_storage[text_box][0];
        for (int i = 0; i < 349; ++i) {
            first[i] = std::move(first[i + 1]);
        }
        first[349] = {};
        --g_status.text_box_lines_used[text_box];
        if (g_status.text_box_lines_shown[text_box] != 0) {
            --g_status.text_box_lines_shown[text_box];
            if (g_status.text_box_lines_shown[text_box] < g_level_block->text_lines[text_box]) {
                g_level_block->text_lines[text_box] = g_status.text_box_lines_shown[text_box];
            }
        }
    }
    unsigned int index = g_status.text_box_lines_used[text_box];
    W8MessageStorageRecord* record = &g_message_storage[text_box][index];
    record->text.assign(text);
    record->RebuildEntries();
    ++g_status.text_box_lines_used[text_box];
    record->font_palette = font_palette;
    record->highlight_color = 0xff;
    record->link = wrapped_line;
    record->length = -1;
    ++g_notice_line_count;
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME && g_status.text_line_cursor == text_box) {
        if (ScreenLifecycleSuccess()) {
            AdvanceNoticeLine(text_box);
        }
    } else if (text_box == 3) {
        g_status.text_box_lines_shown[3] = g_status.text_box_lines_used[3];
        g_level_block->text_lines[3] = g_status.text_box_lines_used[3];
        W8MessageStorageRecord* last = &g_message_storage[3][g_status.text_box_lines_shown[3] - 1];
        unsigned int delay =
            g_settings.text_display_delay_ms * (wiz8::text::grapheme_count(last->text) * 100 / 20 + 100) / 100;
        if (delay > 60000) {
            delay = 60000;
        }
        last->clock = w8_get_ticks() + (delay);
    }
}

static void NoticeDialogDestroyed(W8DialogBase*)
{
    if (g_current_screen_state.id == W8_SCREEN_OPTIONS) {
        NoOp();
    }
}

void InitTextBoxRegions()
{
    message_input = std::make_unique<MessageBoxInput>();
    auto& ui = *message_input;
    ui.targets[0] = &ui.body.add(SDL_Rect{457, 363, 16, 16},
        [](const UiPointerEvent& event) { return HandleTextScroll(event, true); });
    ui.targets[1] = &ui.body.add(SDL_Rect{457, 429, 16, 16},
        [](const UiPointerEvent& event) { return HandleTextScroll(event, false); });
    ui.targets[2] = &ui.body.add(SDL_Rect{461, 379, 8, 50}, HandleTextThumb);
    ui.targets[3] = &ui.body.add(SDL_Rect{166, 362, 291, 84}, HandleTextBody);
    for (int tab = 0; tab < 3; ++tab) {
        ui.targets[4 + tab] = &ui.tabs.add(SDL_Rect{140, 362 + tab * 21, 24, 16},
            [tab](const UiPointerEvent& event) { return HandleTextChannel(event, tab == 2 ? 3 : tab); });
        ui.targets[4 + tab]->setTooltip([tab] { return g_localized_strings[33 + tab]; });
    }
    ui.targets[7] = &ui.tabs.add(SDL_Rect{128, 358, 384, 93}, [](const UiPointerEvent&) {
        PushButtonSoundScheme(0, true);
        return UiResult::Handled;
    }, -1);
    ui.body.setEnabled(false);
    ui.tabs.setEnabled(false);
}

void ShutdownTextBoxRegions() { message_input.reset(); }
void EnableDialogueTextBoxRegions() { if (message_input) message_input->body.setEnabled(true); }
void DisableDialogueTextBoxRegions() { if (message_input) message_input->body.setEnabled(false); }
void SetTextBoxEnabled(bool enabled)
{
    if (!message_input) return;
    message_input->body.setEnabled(enabled);
    message_input->tabs.setEnabled(enabled);
}

static void SetTextBoxRegionInput(TextBoxRegion first, std::size_t count, bool enabled)
{
    if (!message_input) return;
    for (std::size_t offset = 0; offset < count; ++offset)
        message_input->targets[static_cast<std::size_t>(first) + offset]->setEnabled(enabled);
}

void SetTextBoxScrollInput(bool enabled) { SetTextBoxRegionInput(TextBoxRegion::ScrollUp, 4, enabled); }
void SetTextBoxChannelTabInput(bool enabled) { SetTextBoxRegionInput(TextBoxRegion::ChannelTab1, 3, enabled); }
void SetTextBoxMuteInput(bool enabled) { SetTextBoxRegionInput(TextBoxRegion::Mute, 1, enabled); }

void SetTextBoxRegionBounds(int left, int top, int right, int bottom)
{
    g_level_block->text_box_left = left;
    g_level_block->text_box_top = top;
    g_level_block->text_box_right = right;
    g_level_block->text_box_bottom = bottom;
    if (!message_input) return;
    auto& targets = message_input->targets;
    targets[3]->setShape(SDL_Rect{left, top, right - left + 1, bottom - top + 1});
    targets[0]->setShape(SDL_Rect{right + 5, 363, 16, 16});
    targets[1]->setShape(SDL_Rect{right + 5, 429, 16, 16});
    targets[2]->setShape(SDL_Rect{right + 9, 379, 8, 50});
}

void ShowNotice(unsigned int font_palette, std::string_view text_view, short text_box,
                unsigned int wrap_width, bool force_dialog)
{
    const std::string owned_text(text_view);
    const std::string_view text = owned_text;
    IsMipeActive();
    if (g_level_block != 0 && (g_current_screen_state.id == W8_SCREEN_MAIN_GAME ||
                               (g_current_screen_state.id == W8_SCREEN_CAMP && !force_dialog) ||
                               g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT ||
                               g_current_screen_state.id == W8_SCREEN_CHARACTER)) {
        if (font_palette >= W8_FONT_PALETTE_COUNT) {
            srAssertFail("uiFontPaletteIndex <= FONT_PALETTE_COUNT", MGS_TEXT_BOX_CPP, 0x15c, 0);
        }
        if (wrap_width == W8_NOTICE_WRAP_AUTOMATIC) {
            wrap_width = g_level_block->text_box_right - g_level_block->text_box_left - 10;
        }
        text_box = ResolveNoticeTextBox(text_box);
        if (g_status.quote_audit) {
            g_notice_line_count = 0;
            g_status.long_quote = 0;
        }
        g_level_block->text_lines[8 + text_box] = 0;
        if (static_cast<unsigned int>(g_level_block->text_box_font->textWidth(text)) <
            wrap_width) {
            AppendNoticeLine(static_cast<unsigned char>(font_palette), text, text_box, 0);
        } else {
            std::string_view remaining(text);
            int line_index = 0;
            while (!remaining.empty()) {
                std::size_t end = 0, last_space = 0;
                while (end < remaining.size() && remaining[end] != '\n') {
                    const auto next = wiz8::text::next_grapheme(remaining, end);
                    if (g_level_block->text_box_font->textWidth(remaining.substr(0, next)) >= wrap_width && end) {
                        if (last_space) end = last_space;
                        break;
                    }
                    end = next;
                    if (end < remaining.size() && (remaining[end] == ' ' || remaining[end] == '\t')) last_space = end;
                }
                const auto line = remaining.substr(0, end);
                AppendNoticeLine(static_cast<unsigned char>(font_palette), line, text_box, line_index++);
                remaining.remove_prefix(end);
                if (!remaining.empty() && remaining.front() == '\n') remaining.remove_prefix(1);
                if (!remaining.empty()) g_level_block->text_lines[8 + text_box] = line_index == 1 ? line.size() : ~0U;
            }
        }
        if (g_status.quote_audit && g_notice_line_count > 7) {
            g_status.long_quote = 1;
        }
        g_text_box_mode = 0;
    } else if (force_dialog) {
        W8MessageDialogBase* dialog =
            CreateMessageDialog();
        dialog->SetMessage(text, 1, 50, true, false, true, false, 0, 0);
        dialog->m_destroy_callback = NoticeDialogDestroyed;
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        g_modal_owner->DrawModal(GetFrameBufferSurface());
    }
}

void ReleaseMessageStorage(void)
{
    for (int row = 0; row < 4; ++row) {
        ReleaseMessageRow(row);
    }
}

/* How many lines the text box can still be scrolled through. */
int GetTextBoxScrollRange(void)
{
    return g_level_block->text_box_right - g_level_block->text_box_left;
}

/* Write the four message runs into the open TEXT chunk: a format dword, then
   per region the used-line count followed by each 0x24-byte record and its
   wide string. The record's first word is the string count including its
   terminator; its +0x18 pointer word is unused. */
unsigned char SaveMessageStorage(wiz8::File& file)
{
    W8MessageStorageDiskRecord disk_record;
    W8MessageStorageRecord* live_record;
    int format = 1;
    unsigned int region;
    unsigned int index;

    file.write(&format, 4);
    for (region = 0; region < 4; ++region) {
        file.write(&g_status.text_box_lines_used[region], 4);
        for (index = 0; index < g_status.text_box_lines_used[region]; ++index) {
            live_record = &g_message_storage[region][index];
            disk_record.character_count =
                !live_record->text.empty()
                    ? static_cast<w8_ulong>(wiz8::text::to_utf16(live_record->text).size() + 1)
                    : 0;
            disk_record.font_palette = live_record->font_palette;
            disk_record.highlight_color = live_record->highlight_color;
            disk_record.highlight_start = static_cast<unsigned char>(std::min<std::size_t>(255, wiz8::text::to_utf16(
                std::string_view(live_record->text).substr(0, live_record->highlight_start)).size()));
            disk_record.highlight_stop = static_cast<unsigned char>(std::min<std::size_t>(255, wiz8::text::to_utf16(
                std::string_view(live_record->text).substr(0, live_record->highlight_stop)).size()));
            disk_record.clock = static_cast<std::uint32_t>(live_record->clock);
            disk_record.saved_remaining_ms = live_record->saved_remaining_ms;
            disk_record.link = live_record->link;
            disk_record.length = live_record->length < 0 ? -1 : static_cast<int>(
                wiz8::text::to_utf16(std::string_view(live_record->text).substr(
                    0, live_record->length)).size());
            // Runtime word ranges are rebuilt after loading; disk pointer bits are unused.
            disk_record.serialized_entries_18_bits = 0;
            memcpy(disk_record.trailing_bytes, live_record->unknown_1c,
                   sizeof(disk_record.trailing_bytes));
            file.write(&disk_record, sizeof(disk_record));
            if (disk_record.character_count) {
                std::vector<std::byte> bytes(static_cast<std::size_t>(disk_record.character_count) * 2);
                wiz8::text::to_utf16le(live_record->text, bytes);
                file.write(bytes.data(), bytes.size());
            }
        }
    }
    return 1;
}

/* Read the four message runs back from the open TEXT chunk: a format dword,
   then per region the used-line count followed by each 0x24-byte record and
   its wide string. The first word sizes the new live string allocation; the
   serialized +0x18 pointer bits are discarded. Saves older than the prepath
   link-height constant never wrote a fourth region, so its count is forced to
   zero without consuming a count slot. */
unsigned char LoadMessageStorage(wiz8::File& file)
try
{
    W8MessageStorageDiskRecord disk_record;
    W8MessageStorageRecord* live_record;
    int format;
    unsigned int region;
    unsigned int index;

    ReleaseMessageStorage();
    file.read_exact(&format, 4);
    for (region = 0; region < 4; ++region) {
        if (g_status.buffers.save_version < g_prepath_link_height && region == 3) {
            g_status.text_box_lines_used[3] = 0;
        } else {
            file.read_exact(&g_status.text_box_lines_used[region], 4);
        }
        if (g_status.text_box_lines_used[region] > 0x15e) return 0;
        for (index = 0; index < g_status.text_box_lines_used[region]; ++index) {
            file.read_exact(&disk_record, sizeof(disk_record));
            live_record = &g_message_storage[region][index];
            live_record->font_palette = disk_record.font_palette;
            live_record->highlight_color = disk_record.highlight_color;
            live_record->highlight_start = disk_record.highlight_start;
            live_record->highlight_stop = disk_record.highlight_stop;
            live_record->clock = disk_record.clock;
            live_record->saved_remaining_ms = disk_record.saved_remaining_ms;
            live_record->link = disk_record.link;
            live_record->length = disk_record.length;
            memcpy(live_record->unknown_1c, disk_record.trailing_bytes,
                   sizeof(live_record->unknown_1c));
            if (disk_record.character_count > 100000) return 0;
            std::vector<std::byte> bytes(static_cast<std::size_t>(disk_record.character_count) * 2);
            file.read_exact(bytes.data(), bytes.size());
            auto decoded = wiz8::text::from_utf16le(bytes);
            auto byte_offset = [&](std::size_t units) {
                std::size_t offset = 0, count = 0;
                while (offset < decoded.size() && count < units) {
                    auto cp = wiz8::text::next(decoded, offset);
                    count += cp > 0xffff ? 2 : 1;
                }
                return offset;
            };
            live_record->highlight_start = byte_offset(disk_record.highlight_start);
            live_record->highlight_stop = byte_offset(disk_record.highlight_stop);
            if (disk_record.length >= 0) live_record->length = byte_offset(disk_record.length);
            live_record->text = std::move(decoded);
        }
    }
    return 1;
}
catch (const std::exception&) { return false; }

/* The selected wrapped-line start for one text box. NPC trade uses box 2. */
int GetSelectedTextLine(int index)
{
    return g_level_block->selected_text_lines[index];
}

/* Clear one box's hover or selection highlight and request a repaint. */
void ClearHoveredTextLine(int index)
{
    g_level_block->hovered_text_lines[index] = -1;
    RedrawTextBox();
}

void ClearSelectedTextLine(int index)
{
    g_level_block->selected_text_lines[index] = -1;
    RedrawTextBox();
}

/* Ask for the text box to be redrawn without changing anything. */
void RedrawTextBox(void)
{
    RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
}


/* The font the main-game text box measures and draws with. */
Font& GetTextBoxFont(void)
{
    return *g_level_block->text_box_font;
}

/* Whether the line the cursor is on has anything on it. */
bool CurrentTextLineHasContent(void)
{
    return g_level_block->text_lines[g_status.text_line_cursor] != 0;
}

/* Scroll so the line the cursor is on is the last of eight showing, or to the
   top when it would fit anyway. */
void ScrollTextBoxToCursor(void)
{
    if (g_status.text_box_lines_shown[g_status.text_line_cursor] > 7) {
        ScrollTextBoxTo(g_status.text_box_lines_shown[g_status.text_line_cursor] - 7);
        return;
    }
    ScrollTextBoxTo(0);
}

/* Append text onto a box's current line, growing its string in place; the
   optional second argument is the box, -1 for the one the current game mode
   posts to. */
void AppendTextBoxLine(std::string_view text, short text_box)
{
    text_box = ResolveNoticeTextBox(text_box);
    auto& line = g_message_storage[text_box][g_status.text_box_lines_used[text_box] - 1];
    line.length = line.text.size();
    line.text.append(text);
}

void ScrollDialogueTextBoxToLine(void)
{
    W8DialogueTextState* input;
    int offset;

    if (IsNpcDialogueTextInputActive()) {
        return;
    }
    if (g_level_block->dialogue_text_input_open &&
        (input = g_level_block->dialogue_text_input.get()) != 0 &&
        g_status.text_line_cursor == input->text_box) {
        if (gXStatus.fNpcDialogueMode) {
            offset = g_npc_interaction_state->text_box_collapsed ? 1 : 7;
        } else {
            offset = !gXStatus.fSpellCastMode && !gXStatus.fItemSelectMode && !gXStatus.fCampMode
                         ? 1
                         : 7;
        }
        ScrollTextBoxTo(g_status.text_box_lines_shown[g_status.text_line_cursor] +
                        input->line_count - offset);
        return;
    }
    if (gXStatus.fNpcDialogueMode) {
        offset = g_npc_interaction_state->text_box_collapsed ? 1 : 7;
    } else {
        offset =
            !gXStatus.fSpellCastMode && !gXStatus.fItemSelectMode && !gXStatus.fCampMode ? 1 : 7;
    }
    ScrollTextBoxTo(g_status.text_box_lines_shown[g_status.text_line_cursor] - offset);
}

/* Recolour the character span [start, stop) of the most recent line of one
   text box. A -1 box means the one the current game mode posts to. When the
   line was wrapped, the box's split position says where the second row
   begins: a span ending before it recolours the previous row alone, one
   crossing it is cut in two, and one past it moves down by the split. */
void HighlightTextBoxRange(unsigned char color, std::size_t start, std::size_t stop,
                           short text_box)
{
    W8MessageStorageRecord* line;
    W8MessageStorageRecord* previous;

    if (!(stop >= start)) {
        srAssertFail("ubStopChar >= ubStartChar", MGS_TEXT_BOX_CPP, 0x247, 0);
    }
    if (g_current_screen_state.id != W8_SCREEN_MAIN_GAME) {
        return;
    }
    text_box = ResolveNoticeTextBox(text_box);
    if (!(g_status.text_box_lines_used[text_box] > 0)) {
        srAssertFail("gStatus.uiTextBoxLinesUsed[iTextBuffer] > 0", MGS_TEXT_BOX_CPP, 0x261, 0);
    }
    line = &g_message_storage[text_box][g_status.text_box_lines_used[text_box] - 1];
    if (g_level_block->text_lines[8 + text_box] == static_cast<unsigned int>(-1)) {
        return;
    }
    if (g_level_block->text_lines[8 + text_box] == 0) {
        line->highlight_stop = stop;
        line->highlight_start = start;
        line->highlight_color = color;
        return;
    }
    if (!(g_status.text_box_lines_used[text_box] >= 2)) {
        srAssertFail("gStatus.uiTextBoxLinesUsed[iTextBuffer] >= 2", MGS_TEXT_BOX_CPP, 0x277, 0);
    }
    previous = &g_message_storage[text_box][g_status.text_box_lines_used[text_box] - 2];
    if (start >= g_level_block->text_lines[8 + text_box]) {
        line->highlight_start = start - g_level_block->text_lines[8 + text_box];
    } else {
        previous->highlight_start = start;
        previous->highlight_color = color;
        if (stop < g_level_block->text_lines[8 + text_box]) {
            previous->highlight_stop = stop;
            return;
        }
        previous->highlight_stop = previous->text.size();
        line->highlight_start = 0;
    }
    line->highlight_stop = stop - g_level_block->text_lines[8 + text_box];
    line->highlight_color = color;
}

/* Append text to the last used line of one text box. The line's record is
   removed and the combined string posted again so wrapping, the shown count
   and the continuation links rebuild, while the line keeps its channel,
   highlight span and clock. With the merge mode off the text posts as a new
   line instead. A -1 box means the one the current game mode posts to. */
void AppendToLastTextLine(std::string_view text, short text_box)
{
    if (g_current_screen_state.id != W8_SCREEN_MAIN_GAME &&
        g_current_screen_state.id != W8_SCREEN_CAMP) {
        return;
    }
    if (g_text_box_mode == 0) {
        ShowNotice(g_text_box_value, text, text_box);
        return;
    }
    text_box = ResolveNoticeTextBox(text_box);
    w8_unaligned_uint* lines_used = &g_status.text_box_lines_used[text_box];
    if (!(*lines_used > 0)) {
        srAssertFail("gStatus.uiTextBoxLinesUsed[iTextBuffer] > 0", MGS_TEXT_BOX_CPP, 0xf92, 0);
    }
    W8MessageStorageRecord* line = &g_message_storage[text_box][*lines_used - 1];
    const auto merged = fmt::format("{}{}", line->text, text);
    unsigned char channel = line->font_palette;
    int link = line->link;
    unsigned char color = line->highlight_color;
    std::size_t stop = line->highlight_stop;
    std::size_t start = line->highlight_start;
    line->text.clear();
    line->entries.clear();
    if (g_status.text_box_lines_shown[text_box] == *lines_used) {
        if (!(g_status.text_box_lines_shown[text_box] > 0)) {
            srAssertFail("gStatus.uiTextBoxLinesShown[iTextBuffer] > 0", MGS_TEXT_BOX_CPP, 0xfa9,
                         0);
        }
        --g_status.text_box_lines_shown[text_box];
    }
    --*lines_used;
    ShowNotice(channel, merged, text_box);
    if (color != 0xff) {
        HighlightTextBoxRange(color, start, stop, text_box);
    }
    if (link != 0) {
        W8MessageStorageRecord* last = &g_message_storage[text_box][*lines_used - 1];
        int propagated = 0;
        if (last->link != -1 && last->link + 1 > 0) {
            W8MessageStorageRecord* entry = last;
            do {
                entry->link += link;
                ++propagated;
                --entry;
            } while (propagated < last->link + 1);
        }
    }
}

/* The dirty byte of the dormant typed-dialogue input state, or nothing when
   no input is open. */
unsigned char GetOpenDialogueFlag(void)
{
    if (g_level_block->dialogue_text_input_open && g_level_block->dialogue_text_input != 0) {
        return g_level_block->dialogue_text_input->dirty;
    }
    return 0;
}

/* Retail expands this scan in the editor handlers without a separate emission. */
static unsigned int FindDialogueTextLine(const W8DialogueTextState* input)
{
    unsigned int line = 1;
    while (line < input->line_count && input->cursor >= input->line_offsets[line]) {
        ++line;
    }
    return line;
}

/* Retail returns 7 when the text box is in a multi-line mode (spell / item /
   camp / NPC dialogue with the transcript collapsed); otherwise 1. The image
   retains this out-of-line emission while VC6 inlines it at other callers. */
int GetTextBoxVisibleLineCount(void)
{
    bool dialogue = gXStatus.fNpcDialogueMode;
    if (dialogue) {
        if (g_npc_interaction_state->text_box_collapsed) {
            return 1;
        }
    }
    if (!gXStatus.fSpellCastMode && !dialogue && !gXStatus.fItemSelectMode && !gXStatus.fCampMode) {
        return 1;
    }
    return 7;
}

void PostCharacterNotice(int party_slot, std::string_view text, int context)
{
    const std::string_view separator = (text.starts_with("'") || text.starts_with(":"))
                                          ? std::string_view{} : g_W8TextSeparator;
    ShowNotice(W8_FONT_PALETTE_WHITE,
               fmt::format("{}{}{}", g_status.buffers.Char[party_slot].name, separator, text),
               context);
    int stop = g_status.buffers.Char[party_slot].name.size();
    if (text.starts_with("'")) {
        ++stop;
        if (text.starts_with("'s")) {
            ++stop;
        }
    }
    HighlightTextBoxRange(g_status.buffers.XChar[party_slot].party_order_index, 0, stop, context);
}


/* Retail expands this calculation at its callers and has no separate emission. */
static unsigned int GetTextBoxLineCount(short text_box)
{
    unsigned int count = g_status.text_box_lines_shown[text_box];
    if (g_level_block->dialogue_text_input_open && g_level_block->dialogue_text_input != 0 &&
        g_level_block->dialogue_text_input->text_box == text_box) {
        count += g_level_block->dialogue_text_input->line_count;
    }
    return count;
}

/* Whether more lines sit below the visible window on the cursor's text box,
   including the dormant typed-dialogue editor's extra rows when it is open on
   that box. */
bool CurrentDialogueLineHasContent(void)
{
    short text_box = g_status.text_line_cursor;

    return g_level_block->text_lines[text_box] + GetTextBoxVisibleLineCount() <
           GetTextBoxLineCount(text_box);
}

void AdvanceNoticeLine(short text_box)
{
    if (text_box == W8_NOTICE_TEXT_BOX_AUTOMATIC) {
        text_box = g_status.text_line_cursor;
    }
    if (g_status.text_box_lines_shown[text_box] >= 350) {
        srAssertFail("gStatus.uiTextBoxLinesShown[iTextBuffer] < MAX_TEXT_BOX_LINES",
                     MGS_TEXT_BOX_CPP, 0x2a5, 0);
    }
    W8MessageStorageRecord* record =
        &g_message_storage[text_box][g_status.text_box_lines_shown[text_box]];
    unsigned int delay =
        g_settings.text_display_delay_ms * (wiz8::text::grapheme_count(record->text) * 100 / 20 + 100) / 100;
    if (delay > 60000) {
        delay = 60000;
    }
    record->clock = w8_get_ticks() + (delay);
    unsigned int shown = ++g_status.text_box_lines_shown[text_box];
    if (g_level_block->text_scroll_drag_idle) {
        if (gXStatus.fNpcDialogueMode && g_npc_interaction_state->text_box_collapsed) {
            ScrollTextBoxTo(shown);
            g_npc_interaction_state->text_box_collapsed = false;
        } else if (!gXStatus.fSpellCastMode && !gXStatus.fNpcDialogueMode &&
                   !gXStatus.fItemSelectMode && !gXStatus.fCampMode) {
            if (gXStatus.fCombatMode && g_combat_state->notice_scroll_pending) {
                ScrollTextBoxTo(shown);
                g_combat_state->notice_scroll_pending = false;
            } else if (shown == 350) {
                unsigned int scroll = g_level_block->text_lines[text_box];
                if (scroll >= 344) {
                    ScrollTextBoxTo(scroll - 1);
                } else if (scroll <= 342) {
                    ScrollTextBoxTo(343);
                }
            } else if (shown >= 8 && shown - 7 > g_level_block->text_lines[text_box]) {
                ScrollTextBoxTo(shown - 7);
            }
        } else if (!IsNpcDialogueTextInputActive()) {
            ScrollTextBoxTo(GetTextBoxLineCount(g_status.text_line_cursor) -
                            GetTextBoxVisibleLineCount());
        }
    }
    RedrawTextBox();
}

void ScrollTextBoxTo(int line)
{
    short text_box = g_status.text_line_cursor;
    unsigned int input_lines;
    if (!g_level_block->dialogue_text_input_open || g_level_block->dialogue_text_input == 0 ||
        g_level_block->dialogue_text_input->text_box != text_box) {
        input_lines = 0;
    } else {
        input_lines = g_level_block->dialogue_text_input->line_count;
    }
    unsigned int count = g_status.text_box_lines_shown[text_box];
    unsigned int visible = GetTextBoxVisibleLineCount();
    if (input_lines + count <= visible) {
        return;
    }
    unsigned int previous = g_level_block->text_lines[text_box];
    unsigned int target = static_cast<unsigned int>(line);
    if (target + visible <= count + input_lines) {
        if (target > 349) {
            target = 350;
        }
        g_level_block->text_lines[text_box] = target;
    } else {
        g_level_block->text_lines[text_box] = (count - visible) + input_lines;
    }
    if (g_level_block->text_lines[text_box] != previous) {
        g_level_block->text_content_region =
            g_level_block->text_lines[text_box] != 0 ? kUpArrowEnabled : kUpArrowDisabled;
        g_level_block->dialogue_content_region =
            g_level_block->text_lines[text_box] + GetTextBoxVisibleLineCount() <
                    count + input_lines
                ? kDownArrowEnabled
                : kDownArrowDisabled;
        RedrawTextBox();
    }
}

void ScrollTextBoxUp(int lines)
{
    short text_box = g_status.text_line_cursor;
    unsigned int previous = g_level_block->text_lines[text_box];
    if (previous == 0) {
        return;
    }

    unsigned int amount = static_cast<unsigned int>(lines);
    if (previous < amount) {
        g_level_block->text_lines[text_box] = 0;
    } else {
        g_level_block->text_lines[text_box] = previous - amount;
    }

    unsigned int current = g_level_block->text_lines[text_box];
    if (current == previous) {
        return;
    }
    if (current + GetTextBoxVisibleLineCount() < GetTextBoxLineCount(text_box)) {
        g_level_block->dialogue_content_region = kDownArrowEnabled;
    }
    if (current == 0) {
        g_level_block->text_content_region = kUpArrowDisabled;
    }
    RedrawTextBox();
}

void ScrollTextBoxDown(int lines)
{
    short text_box = g_status.text_line_cursor;
    unsigned int count = GetTextBoxLineCount(text_box);
    unsigned int visible = GetTextBoxVisibleLineCount();
    unsigned int previous = g_level_block->text_lines[text_box];
    if (previous + visible >= count) {
        return;
    }

    if (previous + visible + static_cast<unsigned int>(lines) <= count) {
        g_level_block->text_lines[text_box] = previous + lines;
    } else {
        g_level_block->text_lines[text_box] = count - visible;
    }

    unsigned int current = g_level_block->text_lines[text_box];
    if (current == previous) {
        return;
    }
    if (current != 0) {
        g_level_block->text_content_region = kUpArrowEnabled;
    }
    if (current + visible >= count) {
        g_level_block->dialogue_content_region = kDownArrowDisabled;
    }
    RedrawTextBox();
}

/* Draw the dormant typed-dialogue editor's caret: a vertical bar on the row
   holding the insertion point, offset by the first-line prefix and the
   pixel length of the text before the cursor. */
static void DrawDialogueTextCursor(int x, int y)
{
    W8DialogueTextState* input = g_level_block->dialogue_text_input.get();
    unsigned int line = FindDialogueTextLine(input);
    unsigned int hidden = 0;
    short text_box = g_status.text_line_cursor;
    if (g_status.text_box_lines_shown[text_box] < g_level_block->text_lines[text_box]) {
        hidden = g_level_block->text_lines[text_box] - g_status.text_box_lines_shown[text_box];
    }
    if (line <= hidden) {
        return;
    }

    int x_offset = 0;
    if (line == 1 && !input->first_line_prefix.empty()) {
        x_offset = g_level_block->text_box_font->textWidth(input->first_line_prefix);
    }
    unsigned int start = input->line_offsets[line - 1];
    x_offset +=
        g_level_block->text_box_font->prefixWidth(std::string_view(input->text).substr(start), input->cursor - start);

    int y_line = y + (line - hidden - 1) * 0xb;
    if (y_line < g_level_block->text_box_bottom) {
        unsigned int bottom = g_level_block->text_box_bottom;
        unsigned int y_end = y_line + g_level_block->text_box_font->height();
        if (bottom <= y_end) {
            y_end = bottom;
        }
        int x_cursor = x + x_offset + 1;
        const SDL_Rect cursor{ x_cursor, y_line, 1, static_cast<int>(y_end - y_line + 1) };
        SDL_FillSurfaceRect(&GetFrameBufferSurface(), &cursor, 0xff00);
    }
}

/* Print the dormant typed-dialogue editor's wrapped lines starting at
   first_line, temporarily terminating each row at the next line's start
   offset; tracks the widest row and invalidates the printed rectangle. */
void DrawDialogueTextInputLines(int x, int y, unsigned int first_line)
{
    auto* input = g_level_block->dialogue_text_input.get();
    const int first_y = y;
    int maximum_width = 0;
    for (std::size_t line = first_line; line < input->line_offsets.size(); ++line) {
        auto start = input->line_offsets[line];
        auto end = line + 1 < input->line_offsets.size() ? input->line_offsets[line + 1] : input->text.size();
        auto text = std::string_view(input->text).substr(start, end - start);
        int draw_x = x;
        if (line == 0 && !input->first_line_prefix.empty()) {
            DrawText(GetFrameBufferSurface(), *g_level_block->text_box_font, {draw_x, y}, input->first_line_prefix);
            draw_x += g_level_block->text_box_font->textWidth(input->first_line_prefix);
        }
        DrawText(GetFrameBufferSurface(), *g_level_block->text_box_font, {draw_x, y}, text);
        maximum_width = std::max(maximum_width, draw_x - x + g_level_block->text_box_font->textWidth(text));
        y += 11;
        if (y >= g_level_block->text_box_bottom) break;
    }
    InvalidateRegion(x, first_y, x + maximum_width, y);

}

/* Repaint the dormant typed-dialogue editor over the text box: the wrapped
   lines when the input is dirty, then the cursor. */
void RedrawDialogueTextInput(void)
{
    W8DialogueTextState* input = g_level_block->dialogue_text_input.get();
    if ((input->dirty || input->cursor_dirty != 0)) {
        short text_box = g_status.text_line_cursor;
        int offset = g_status.text_box_lines_shown[text_box] - g_level_block->text_lines[text_box];
        if (offset < 7) {
            {
                SDL_Rect clip{g_level_block->text_box_left, g_level_block->text_box_top,
                              g_level_block->text_box_right - g_level_block->text_box_left,
                              g_level_block->text_box_bottom - g_level_block->text_box_top};
                ScopedClipRect clipping(GetFrameBufferSurface(), clip);
                g_level_block->text_box_font->setPalette(g_level_block->palette);

                int y = g_level_block->text_box_top + offset * 0xb;
                if (y < g_level_block->text_box_top) {
                    y = g_level_block->text_box_top;
                }
                if (!input->dirty) {
                    if (input->cursor_dirty != 0) {
                        DrawDialogueTextCursor(g_level_block->text_box_left, y);
                    }
                } else {
                    unsigned int first_line = 0;
                    if (offset < 0) {
                        first_line = g_level_block->text_lines[text_box] -
                                     g_status.text_box_lines_shown[text_box];
                    }
                    DrawDialogueTextInputLines(g_level_block->text_box_left, y, first_line);
                    input->dirty = false;
                    DrawDialogueTextCursor(g_level_block->text_box_left, y);
                }

                g_level_block->text_box_font->setPalette(g_level_block->palette);
            }
        }
    }
}

// Retail allocation routines 0x0058D7E0 / 0x0058D890 are replaced by
// std::string and std::vector ownership, with no engine capacity bookkeeping.
void ReleaseDialogueTextInput(void)
{
    g_level_block->dialogue_text_input.reset();
}

void InvalidateDialogueTextCursor(void)
{
    g_level_block->dialogue_text_input->dirty = true;
    RequestRedraw(W8_MAIN_REDRAW_FRAME);

    W8ControlsRect bounds;
    bounds.left = g_level_block->text_box_left;
    bounds.top = g_status.text_box_lines_shown[g_status.text_line_cursor] -
                 g_level_block->text_lines[g_status.text_line_cursor] + g_level_block->text_box_top;
    bounds.right =
        g_level_block->text_box_font->textWidth(g_level_block->dialogue_text_input->text) +
        bounds.left;
    bounds.bottom = g_level_block->text_box_font->height() + bounds.top;
    InvalidateMainGameActionPanelRect(&bounds);
}

static void RewrapDialogueTextFromLine(unsigned int)
{
    auto& input = *g_level_block->dialogue_text_input;
    input.line_offsets = {0};
    auto line_start = std::size_t{0};
    auto last_break = std::size_t{0};
    for (std::size_t position = 0; position < input.text.size();) {
        auto end = wiz8::text::next_grapheme(input.text, position);
        auto width = g_level_block->text_box_font->textWidth(std::string_view(input.text).substr(line_start, end - line_start));
        if (line_start == 0) width += g_level_block->text_box_font->textWidth(input.first_line_prefix);
        if (width > static_cast<int>(input.wrap_width) && position > line_start) {
            line_start = last_break > line_start ? last_break : position;
            input.line_offsets.push_back(line_start);
        }
        if (input.text[position] == ' ') last_break = end;
        position = end;
    }
    input.line_count = input.line_offsets.size();
}

static void InsertDialogueTextCharacter(char32_t character)
{
    if (!g_level_block->dialogue_text_input_open || !g_level_block->dialogue_text_input) return;
    auto& input = *g_level_block->dialogue_text_input;
    std::string bytes;
    utf8::append(character, std::back_inserter(bytes));
    input.text.insert(input.cursor, bytes);
    input.cursor += bytes.size();
    RewrapDialogueTextFromLine(1);
    InvalidateDialogueTextCursor();
}

void DeleteDialogueTextCharacter(unsigned int key)
{
    auto& input = *g_level_block->dialogue_text_input;
    if (key == 8 && input.cursor) {
        const auto previous = wiz8::text::previous_grapheme(input.text, input.cursor);
        input.text.erase(previous, input.cursor - previous);
        input.cursor = previous;
    } else if (key == 0x2e && input.cursor < input.text.size()) {
        const auto next = wiz8::text::next_grapheme(input.text, input.cursor);
        input.text.erase(input.cursor, next - input.cursor);
    }
    RewrapDialogueTextFromLine(1);
    InvalidateDialogueTextCursor();
}

namespace {
UiResult HandleTextThumb(const UiPointerEvent& event)
{
    PushButtonSoundScheme(0, true);
    if (!(event.buttons & SDL_BUTTON_LMASK)) {
        g_level_block->text_scroll_drag_idle = 1;
        return UiResult::Handled;
    }
    if (event.action != UiPointerAction::Move && event.action != UiPointerAction::Down)
        return UiResult::Handled;
    const short box = g_status.text_line_cursor;
    const auto visible = GetTextBoxVisibleLineCount();
    const auto count = GetTextBoxLineCount(box);
    if (count <= visible) return UiResult::Handled;
    const auto position = std::clamp(event.local.y + 1, 0, 46);
    const auto previous = g_level_block->text_lines[box];
    const auto current = (count - visible) * position / 47;
    g_level_block->text_lines[box] = current;
    if (current != previous) {
        g_level_block->text_content_region = current ? kUpArrowEnabled : kUpArrowDisabled;
        g_level_block->dialogue_content_region = current + visible < count ? kDownArrowEnabled : kDownArrowDisabled;
        RedrawTextBox();
    }
    g_level_block->text_scroll_drag_idle = 0;
    return UiResult::Handled;
}

UiResult HandleTextScroll(const UiPointerEvent& event, bool up)
{
    const short box = g_status.text_line_cursor;
    const bool enabled = up ? g_level_block->text_lines[box] != 0 :
        g_level_block->text_lines[box] + GetTextBoxVisibleLineCount() < GetTextBoxLineCount(box);
    if (!enabled) PushButtonSoundScheme(0, true);
    if (enabled && (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave)) {
        const bool hover = event.action == UiPointerAction::Enter;
        if (up) g_level_block->text_content_region = hover ? kUpArrowPressed : kUpArrowEnabled;
        else g_level_block->dialogue_content_region = hover ? kDownArrowPressed : kDownArrowEnabled;
        RedrawTextBox();
    }
    if ((event.action == UiPointerAction::Up || event.action == UiPointerAction::Repeat) && event.inside) {
        const auto mask = event.button == UiPointerButton::Left ? SDL_BUTTON_LMASK : SDL_BUTTON_RMASK;
        if (event.captured_buttons & mask) {
            const int lines = event.button == UiPointerButton::Left ? 1 : 7;
            if (up) ScrollTextBoxUp(lines);
            else ScrollTextBoxDown(lines);
        }
    }
    return UiResult::Handled;
}

UiResult HandleTextBody(const UiPointerEvent& event)
{
    PushButtonSoundScheme(0, true);
    if (event.action == UiPointerAction::Wheel) {
        if (event.wheel < 0) ScrollTextBoxDown(-event.wheel);
        else ScrollTextBoxUp(event.wheel);
        if (gXStatus.fNpcDialogueMode) NpcDialogueTextBoxWheelAt(event.position.x, event.position.y, true);
        else if (gXStatus.fItemSelectMode) UseItemSelectTextBoxWheelAt(event.position.x, event.position.y, true);
        return UiResult::Handled;
    }
    if (gXStatus.fNpcDialogueMode) return HandleNpcTextBox(event);
    if (gXStatus.fSpellCastMode) return HandleSpellTextBox(event);
    if (gXStatus.fItemSelectMode) return HandleUseItemTextBox(event);
    return UiResult::Handled;
}

UiResult HandleTextChannel(const UiPointerEvent& event, int channel)
{
    PushButtonSoundScheme(0, true);
    if (event.action != UiPointerAction::Up || !event.inside ||
        event.button != UiPointerButton::Left || !(event.captured_buttons & SDL_BUTTON_LMASK))
        return UiResult::Handled;
    g_status.text_line_cursor = static_cast<short>(channel);
    const auto scroll = g_level_block->text_lines[channel];
    g_level_block->text_content_region = scroll ? kUpArrowEnabled : kUpArrowDisabled;
    g_level_block->dialogue_content_region = scroll + GetTextBoxVisibleLineCount() < GetTextBoxLineCount(channel)
        ? kDownArrowEnabled : kDownArrowDisabled;
    if (channel == 3) ScrollTextBoxTo(std::max(0, int(g_status.text_box_lines_shown[channel]) - 7));
    RedrawTextBox();
    return UiResult::Handled;
}
}

unsigned char HandleDialogueTextInput(const InputEvent* input_event)
{
    if (!g_level_block->dialogue_text_input_open ||
        (input_event->modifiers & (CTRL_DOWN | ALT_DOWN)) != 0) {
        return 0;
    }
    W8DialogueTextState* input = g_level_block->dialogue_text_input.get();
    if (input_event->type == InputEventType::Text) {
        if (input_event->text >= 32) InsertDialogueTextCharacter(input_event->text);
        return 1;
    }
    if (input_event->type != InputEventType::KeyDown && input_event->type != InputEventType::KeyRepeat) {
        return 1;
    }
    unsigned int key = input_event->key;
    switch (key) {
    case 8:
    case 0x2e:
        DeleteDialogueTextCharacter(key);
        return 1;
    case 0x0d:
    case 0x1b:
        if (input->completion_callback != 0) {
            input->completion_callback();
        }
        g_level_block->dialogue_text_input_open = false;
        g_level_block->text_lines[input->text_box] = input->saved_scroll_line;
        if (input->text[0] != 0) {
            ShowNotice(input->notice_channel, input->text, input->text_box, input->wrap_width);
        }
        ReleaseDialogueTextInput();
        RedrawTextBox();
        return 1;
    case 0x20:
        InsertDialogueTextCharacter(' ');
        return 1;
    case 0x23: {
        size_t length = input->text.size();
        if (input->cursor < length) {
            input->cursor = length;
            InvalidateDialogueTextCursor();
        }
        return 1;
    }
    case 0x24:
        if (input->cursor != 0) {
            input->cursor = 0;
            InvalidateDialogueTextCursor();
        }
        return 1;
    case 0x25:
        if (input->cursor != 0) {
            input->cursor = wiz8::text::previous_grapheme(input->text, input->cursor);
            InvalidateDialogueTextCursor();
        }
        return 1;
    case 0x27:
        if (input->cursor < input->text.size()) {
            input->cursor = wiz8::text::next_grapheme(input->text, input->cursor);
            InvalidateDialogueTextCursor();
        }
        return 1;
    default:
        break;
    }

    char character = TranslateKeyToCharacter(
        static_cast<unsigned short>(key), static_cast<unsigned char>(input_event->modifiers));
    if ((character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z') || (character >= '!' && character <= '/') ||
        (character >= ':' && character <= '@') || (character >= '[' && character <= '_') ||
        (character >= '{' && character <= '}')) {
        InsertDialogueTextCharacter(character);
        return 1;
    }
    return 0;
}

/* The text box's mode, and the setter that also records a value when one is
   given - passing -1 leaves the value alone. */
unsigned char GetTextBoxMode(void)
{
    return g_text_box_mode;
}

void SetTextBoxMode(unsigned char mode, int value)
{
    g_text_box_mode = mode;
    if (value != -1) {
        g_text_box_value = value;
    }
}

/* Draw clickable notice-word overlays for one painted line. */
static void DrawNoticeWordOverlays(W8MessageStorageRecord* line, int x, int y);

/* Paint one message-storage line at (x, y). hovered_line / selected_line
   select alternate palettes for the editor slot highlights; skip_invalidate
   is forwarded from RedrawTextBoxBody and skips the word-overlay pass when
   set. Retail reuses the leading bytes of the level block as a wchar scratch. */
static void DrawTextBoxLine(W8MessageStorageRecord* line, int x, int y, bool hovered_line,
                            bool selected_line, bool skip_invalidate)
{
    if (skip_invalidate) { DrawNoticeWordOverlays(line, x, y); return; }
    auto text = std::string_view(line->text);
    auto foreground = selected_line ? W8_FONT_PALETTE_BLUE : hovered_line ? W8_FONT_PALETTE_YELLOW : line->font_palette;
    auto set_palette = [&](unsigned int index) {
        g_level_block->text_box_font->setPalette(index < W8_FONT_PALETTE_TEXT_BOX ? g_font_state_palettes[index] : g_level_block->palette);
    };
    auto draw = [&](std::string_view span, unsigned int palette) {
        set_palette(palette);
        DrawTextAndInvalidate(GetFrameBufferSurface(), *g_level_block->text_box_font, {x, y}, span);
        x += g_level_block->text_box_font->textWidth(span);
    };
    auto end = line->length < 0 ? text.size() : wiz8::text::byte_prefix(text, line->length);
    auto visible = text.substr(0, end);
    if (!selected_line && !hovered_line && line->highlight_color != 0xff) {
        auto start = wiz8::text::byte_prefix(visible, line->highlight_start);
        auto stop = std::max(start, wiz8::text::byte_prefix(visible, line->highlight_stop));
        draw(visible.substr(0, start), foreground);
        draw(visible.substr(start, stop - start), line->highlight_color);
        draw(visible.substr(stop), foreground);
    } else draw(visible, foreground);
    if (line->length >= 0) {
        const auto suffix = text.substr(end);
        set_palette(foreground);
        DrawTextAndInvalidate(GetFrameBufferSurface(), *g_level_block->text_box_font, {g_level_block->text_box_right -
                              g_level_block->text_box_font->textWidth(suffix), y}, suffix);
    }
    DrawNoticeWordOverlays(line, x, y);
}

/* Repaint the visible text-box window. When skip_invalidate is clear, also
   invalidate the text rectangle first. */
void RedrawTextBoxBody(bool skip_invalidate)
{
    short text_box;
    unsigned int shown;
    unsigned int scroll;
    unsigned int editor_lines;
    unsigned int rows;
    unsigned int row;
    int y_offset;
    int x;
    int line_index;
    W8MessageStorageRecord* line;
    bool can_scroll_down;

    if (!skip_invalidate) {
        InvalidateRegion(g_level_block->text_box_left, g_level_block->text_box_top,
                         g_level_block->text_box_right, g_level_block->text_box_bottom);
    }

    text_box = g_status.text_line_cursor;
    shown = g_status.text_box_lines_shown[text_box];
    if (shown == 0) {
        return;
    }

    if (!g_level_block->dialogue_text_input_open || g_level_block->dialogue_text_input == 0 ||
        g_level_block->dialogue_text_input->text_box != text_box) {
        editor_lines = 0;
    } else {
        editor_lines = g_level_block->dialogue_text_input->line_count;
    }

    g_level_block->text_lines[4 + text_box] = FindStoppedTextLine();

    scroll = g_level_block->text_lines[text_box];
    rows = (shown - scroll) + editor_lines;
    if (rows < 7) {
        if (rows == 0) {
            return;
        }
    } else {
        rows = 7;
    }

    if (g_level_block->action_panel_visible == 0 && !g_level_block->mipe_editing) {
        can_scroll_down = scroll + static_cast<unsigned int>(GetTextBoxVisibleLineCount()) <
                          GetTextBoxLineCount(text_box);
        if (!can_scroll_down &&
            TimeUntilDeadline(g_message_storage[text_box][scroll + rows - 1].clock) == 0) {
            return;
        }
    }

    {

        SDL_Rect clip{g_level_block->text_box_left, g_level_block->text_box_top,
                      g_level_block->text_box_right - g_level_block->text_box_left,
                      g_level_block->text_box_bottom - g_level_block->text_box_top};
        ScopedClipRect clipping(GetFrameBufferSurface(), clip);
        g_level_block->text_box_font->setPalette(g_level_block->palette);

        y_offset = 0;
        for (row = 0; row < rows; ++row) {
            line_index = static_cast<int>(scroll + row);
            if (static_cast<unsigned int>(line_index) < 0x15e) {
                line = &g_message_storage[text_box][line_index];
                if (!line->text.empty()) {
                    if (text_box == 3 || !g_level_block->text_box_visible) {
                        x = g_level_block->text_box_left;
                    } else {
                        x = g_level_block->text_box_left +
                            ((g_level_block->text_box_right - g_level_block->text_box_left) / 2 -
                             g_level_block->text_box_font->textWidth(line->text) /
                                 2);
                    }
                    DrawTextBoxLine(
                        line, x, g_level_block->text_box_top + y_offset,
                        line_index - line->link == g_level_block->hovered_text_lines[text_box],
                        line_index - line->link == g_level_block->selected_text_lines[text_box],
                        skip_invalidate);
                }
            }
            y_offset += 0xb;
        }

        g_level_block->text_box_font->setPalette(g_level_block->palette);

    }
}

/* Redraw the text-box scroll chrome (up/down buttons and thumb) when the
   action panel is raised, invalidate the chrome strip, then repaint the body. */
void RedrawTextBoxScrollChrome(void)
{
    SDL_Rect clip;
    short text_box;
    unsigned int scroll;
    unsigned int line_count;
    int visible;
    int thumb_y;

    auto& framebuffer = GetFrameBufferSurface();
    SDL_GetSurfaceClipRect(&framebuffer, &clip);
    clip.h = (0x1db) - clip.y;
    ScopedClipRect clipped(framebuffer, clip);

    if (g_level_block->action_panel_visible != 0) {
        text_box = g_status.text_line_cursor;
        scroll = g_level_block->text_lines[text_box];
        if (scroll == 0) {
            g_level_block->text_content_region = kUpArrowDisabled;
        } else if (g_level_block->text_content_region ==
                   kUpArrowDisabled) {
            g_level_block->text_content_region = kUpArrowEnabled;
        }

        switch (g_level_block->text_content_region) {
        case kUpArrowEnabled:
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 0, g_level_block->text_box_right + 5, 0x16b);
            break;
        case kUpArrowPressed:
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 1, g_level_block->text_box_right + 5, 0x16b);
            break;
        case -1:
        case kUpArrowDisabled:
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 3, g_level_block->text_box_right + 5, 0x16b);
            g_level_block->text_content_region = -1;
            break;
        }

        line_count = GetTextBoxLineCount(text_box);
        visible = GetTextBoxVisibleLineCount();
        if (scroll + static_cast<unsigned int>(visible) < line_count) {
            if (g_level_block->dialogue_content_region == kDownArrowDisabled) {
                g_level_block->dialogue_content_region = kDownArrowEnabled;
            }
        } else {
            g_level_block->dialogue_content_region = kDownArrowDisabled;
        }

        switch (g_level_block->dialogue_content_region) {
        case kDownArrowEnabled:
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 8, g_level_block->text_box_right + 5, 0x1ad);
            break;
        case kDownArrowPressed:
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 9, g_level_block->text_box_right + 5, 0x1ad);
            break;
        case -1:
        case kDownArrowDisabled:
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 0xb, g_level_block->text_box_right + 5, 0x1ad);
            g_level_block->dialogue_content_region = -1;
            break;
        }

        if (line_count > static_cast<unsigned int>(visible)) {
            thumb_y = static_cast<int>(scroll * 0x28 / (line_count - visible)) + 0x17b;
            CatalogArtwork::Load(0x86, 0).draw(GetFrameBufferSurface(), 4, g_level_block->text_box_right + 5, thumb_y);
        }
    }

    InvalidateRegion(g_level_block->text_box_right + 5, 0x16b, g_level_block->text_box_right + 0x1e,
                     0x1c1);
    RedrawTextBoxBody(false);
    if (g_level_block->dialogue_text_input_open && g_level_block->dialogue_text_input != 0) {
        g_level_block->dialogue_text_input->dirty = true;
    }
}

/* Invalidate the text and action panels around a status-panel text refresh. */
void RedrawTextBoxComplete(void)
{
    W8MainGameScreen* screen = g_main_game_screen.get();

    screen->m_text_panel->InvalidateAll();
    screen->m_status_panel->RefreshStatusTexts();
    screen->actions_dirty = true;
}

/* The last message on the current line whose clock has stopped, searched from
   the newest backwards - so the first one found is the most recent finished
   message rather than the oldest. */
int FindStoppedTextLine(void)
{
    int index = g_status.text_box_lines_shown[g_status.text_line_cursor];

    if (index == 0) {
        return -1;
    }
    while (--index >= 0) {
        if (TimeUntilDeadline(g_message_storage[g_status.text_line_cursor][index].clock) == 0) {
            return index;
        }
    }
    return -1;
}

/* Hand one key to the text box's own handler. A key that moved the selection
   is followed by the two calls that settle it; a key that did not is not. */
char TextBoxHandleKey(const InputEvent* event)
{
    W8MainGameTextPanel* panel = g_main_game_screen->m_text_panel.get();
    int before = panel->m_selection;
    char handled;

    handled = panel->m_key_handler->HandleKey(event->key);

    if (handled != 0 && panel->m_selection != before) {
        ResetButtonSoundScheme();
        PlayButtonSound(3);
    }
    return handled;
}

/* Record what the Knock Knock spell is aimed at. Casting it anywhere the
   overlay is not up says so and records nothing - the message is the
   function's own name in the player's words. Its caller passes the same
   (level, flag, backfire) triple CastSpellAtLockInteraction takes;
   only the target is read here. */
/* The trap-mode half of CastSpellAtLockInteraction: without a
   backfire the disarm chance is `level * 5 + 0x32 - m_difficulty * 6`, a
   success parks the screen in state 7 (8 on a miss), and either way the text
   and action panels go quiet for a 1.5 second timer. */
void AttemptTrapDisarm(int level, int /*flag*/, char backfire)
{
    W8MainGameScreen* screen = g_main_game_screen.get();
    int chance;

    if (backfire != 0) {
        chance = 0;
    } else {
        chance = level * 5 + 0x32 + screen->m_difficulty * -6;
    }
    if (static_cast<int>(Random(100)) < chance) {
        screen->m_disarm_state = 7;
    } else {
        screen->m_disarm_state = 8;
    }
    screen->m_text_panel->EnableInput(false);
    screen->m_text_panel->m_key_handler->m_range.SetActive(false);
    screen->input.setEnabled(false);
    screen->m_timer.SetDuration(1.5f);
    screen->m_timer.Restart();
}

void SetKnockKnockTarget(int target, int /*flag*/, int /*backfire*/)
{
    W8MainGameScreen* screen = g_main_game_screen.get();

    if (!gXStatus.fTrapInteractMode) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, "You can't cast Knock Knock here!");
        return;
    }
    screen->m_target_difficulty = target;
    screen->m_status_panel->m_target = target;
    screen->m_text_panel->m_target_changed = true;
}

void SelectTextBox(short text_box)
{
    g_status.text_line_cursor = text_box;
    RedrawTextBox();
    if (g_level_block->text_lines[text_box] != 0) {
        g_level_block->text_content_region = kUpArrowEnabled;
    } else {
        g_level_block->text_content_region = kUpArrowDisabled;
    }

    bool can_scroll;
    if (!g_level_block->dialogue_text_input_open || g_level_block->dialogue_text_input == 0 ||
        g_level_block->dialogue_text_input->text_box != text_box) {
        can_scroll = g_level_block->text_lines[text_box] + GetTextBoxVisibleLineCount() <
                     g_status.text_box_lines_shown[text_box];
    } else {
        can_scroll = g_level_block->text_lines[text_box] + GetTextBoxVisibleLineCount() <
                     g_level_block->dialogue_text_input->line_count +
                         g_status.text_box_lines_shown[text_box];
    }
    g_level_block->dialogue_content_region = can_scroll ? kDownArrowEnabled : kDownArrowDisabled;
}

static void SelectWrappedTextLine(int line, int box, int (&selection)[4])
{
    if (line < 0x15e) {
        unsigned int first = g_level_block->text_lines[g_status.text_line_cursor];
        if (first <= static_cast<unsigned int>(line) &&
            static_cast<unsigned int>(line) < first + 7) {
            W8MessageStorageRecord* record = &g_message_storage[box][line];
            while (record->link != 0 && line != 0) {
                --line;
                --record;
            }
            selection[box] = line;
            RedrawTextBox();
        }
    }
}

void SetHoveredTextLine(int line, int index)
{
    SelectWrappedTextLine(line, index, g_level_block->hovered_text_lines);
}

void ClearTextLineEntry(int index)
{
    g_level_block->text_lines[index] = 0;
}

int GetHoveredTextLine(int index)
{
    return g_level_block->hovered_text_lines[index];
}

void SetSelectedTextLine(int line, int index)
{
    SelectWrappedTextLine(line, index, g_level_block->selected_text_lines);
}

static void DrawNoticeWordOverlay(const W8MessageStorageRecord* line, W8NoticeWord* word, int x,
                                  int y)
{
    std::string word_text;
    word_text = line->text.substr(word->start, word->end - word->start + 1);
    DrawTextAndInvalidate(GetFrameBufferSurface(), *g_level_block->text_box_font, {word->x_start + x, y}, word_text);
    word->redraw = false;
}

static void DrawNoticeWordOverlays(W8MessageStorageRecord* line, int x, int y)
{
    for (auto& entry : line->entries) {
        auto* word = &entry;
        if (word->keyword != W8_NOTICE_WORD_NORMAL) {
            Palette16 palette = word->keyword == W8_NOTICE_WORD_SELECTED
                                          ? g_font_state_palettes[W8_FONT_PALETTE_BLUE]
                                          : g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
            g_level_block->text_box_font->setPalette(palette);
            DrawNoticeWordOverlay(line, word, x, y);
        }
        if (word->redraw) {
            Palette16 palette;
            if (line->font_palette < W8_FONT_PALETTE_TEXT_BOX) {
                palette = g_font_state_palettes[line->font_palette];
            } else {
                palette = g_level_block->palette;
            }
            g_level_block->text_box_font->setPalette(palette);
            DrawNoticeWordOverlay(line, word, x, y);
        }
    }
}

static void ClearNoticeWordState(int text_box, bool selected, bool redraw)
{
    for (auto& line : g_message_storage[text_box]) {
        for (auto& word : line.entries) {
            if ((word.keyword == W8_NOTICE_WORD_SELECTED) == selected) {
                word.keyword = W8_NOTICE_WORD_NORMAL;
                word.redraw = true;
            }
        }
    }
    if (redraw) {
        RedrawTextBoxBody(true);
    }
}

void ResetUsedNoticeWords(int text_box, bool redraw)
{
    ClearNoticeWordState(text_box, true, redraw);
}

void ClearNoticeWordHover(int text_box, bool redraw)
{
    ClearNoticeWordState(text_box, false, redraw);
}

static int GetNoticeLineLeft(int text_box, int line)
{
    int x_base;
    if (g_status.text_line_cursor == 3 || !g_level_block->text_box_visible) {
        x_base = g_level_block->text_box_left;
    } else {
        x_base = g_level_block->text_box_left +
                 ((g_level_block->text_box_right - g_level_block->text_box_left) / 2 -
                  g_level_block->text_box_font->textWidth(g_message_storage[text_box][line].text) /
                      2);
    }
    return x_base;
}

void HighlightNoticeWordAt(int text_box, unsigned short x, unsigned short y)
{
    int line;
    int x_base;
    int offset;

    ClearNoticeWordHover(text_box, false);
    if (g_level_block->text_box_top <= y && y <= g_level_block->text_box_bottom) {
        line = g_level_block->text_lines[text_box] + (y - g_level_block->text_box_top) / 0xb;
        if (line < static_cast<int>(g_status.text_box_lines_shown[text_box])) {
            x_base = GetNoticeLineLeft(text_box, line);
            offset = x - x_base;
            for (auto& entry : g_message_storage[text_box][line].entries) {
                auto* word = &entry;
                if (word->x_start <= offset && offset <= word->x_end &&
                    word->keyword != W8_NOTICE_WORD_SELECTED) {
                    word->keyword = W8_NOTICE_WORD_HOVERED;
                }
            }
        }
        RedrawTextBoxBody(true);
    }
}

W8NoticeWord* HitTestNoticeWord(int text_box, unsigned short x, unsigned short y, int* line_out)
{
    int line;
    int x_base;
    int offset;

    if (g_level_block->text_box_top <= y && y <= g_level_block->text_box_bottom) {
        line = g_level_block->text_lines[text_box] + (y - g_level_block->text_box_top) / 0xb;
        if (line < static_cast<int>(g_status.text_box_lines_shown[text_box])) {
            x_base = GetNoticeLineLeft(text_box, line);
            offset = x - x_base;
            for (auto& entry : g_message_storage[text_box][line].entries) {
                auto* word = &entry;
                if (word->x_start <= offset && offset <= word->x_end) {
                    *line_out = line;
                    return word;
                }
            }
        }
    }
    return 0;
}

std::string CopyNoticeWordText(const W8NoticeWord* word, int text_box, int line)
{
    return g_message_storage[text_box][line].text.substr(word->start, word->end - word->start + 1);
}

void PostMonsterNotice(W8MonsterInfo* monster_info, std::string_view text)
{
    const std::string_view separator = (text.starts_with("'") || text.starts_with(":"))
                                          ? std::string_view{} : g_W8TextSeparator;
    ShowNotice(W8_FONT_PALETTE_RUST,
               fmt::format("{}{}{}", GetMonsterName(monster_info, 0, 0), separator, text));
}

/* Re-show the last wrapped entry of `mode`'s message run as a notice line;
   0xffff derives the mode from the live dialogue/camp/combat state. Retail
   only fills `merged` when the run index range is non-empty, so a failing
   (but returning) srAssertFail left it uninitialized at ShowNoticeLine; the recovery keeps that
   read. */
void RefreshTextBoxMode(unsigned short mode)
{
    std::string merged;
    W8MessageStorageRecord* line;
    unsigned int last_line;
    unsigned int group_lines;
    unsigned int first_line;
    unsigned int index;
    size_t length;

    if (g_current_screen_state.id != W8_SCREEN_MAIN_GAME &&
        g_current_screen_state.id != W8_SCREEN_CAMP) {
        return;
    }
    if (mode == 0xffff) {
        if ((!gXStatus.fNpcDialogueMode || CanOpenNpcDialogue()) && !gXStatus.fCampMode) {
            if (!IsMipeActive()) {
                mode = gXStatus.fCombatMode;
            } else {
                mode = 0;
            }
        } else {
            mode = IsNpcDialogueTextBoxActive() ? 0 : 2;
        }
    }
    int text_box = static_cast<short>(mode);
    if (g_status.text_box_lines_used[text_box] == 0) {
        srAssertFail("gStatus.uiTextBoxLinesUsed[iTextBuffer] > 0", MGS_TEXT_BOX_CPP, 0x109e, 0);
    }
    last_line = g_status.text_box_lines_used[text_box] - 1;
    group_lines = g_message_storage[text_box][last_line].link;
    if (last_line < group_lines) {
        srAssertFail("uiLastLineIndex >= uiGroupLine", MGS_TEXT_BOX_CPP, 0x10a6, 0);
    }
    first_line = last_line - group_lines;
    line = &g_message_storage[text_box][first_line];
    length = 0;
    for (index = first_line; index <= last_line; index++, line++) {
        if (index == first_line) {
            merged = line->text;
            length = line->text.size();
        } else {
            if (line->text.size() + 1 + length > 499) {
                break;
            }
            merged += g_W8TextSeparator;
            merged += line->text;
            length += 1 + line->text.size();
        }
    }
    ShowNoticeLine(merged, 0, true, false);
}

/* Shared row hover for the NPC and use-item lists in text box two. */
void HighlightNoticeRow(int line)
{
    ClearHoveredTextLine(2);
    if (line < static_cast<int>(g_status.text_box_lines_shown[2])) {
        SetHoveredTextLine(g_level_block->text_lines[2] + line, 2);
    }
    RedrawTextBox();
}
