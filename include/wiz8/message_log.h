#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum W8NoticeWordState : unsigned char {
    W8_NOTICE_WORD_NORMAL = 0,
    W8_NOTICE_WORD_HOVERED = 1,
    W8_NOTICE_WORD_SELECTED = 2
};

/* A word's byte range in its line and its pixel span in the text box. */
struct W8NoticeWord {
    std::size_t start;
    std::size_t end;
    short x_start;
    short x_end;
    W8NoticeWordState keyword;
    bool redraw; /* repaint once after deselection */
};

struct Font;

/* One line of a text box. */
struct W8MessageStorageRecord {
    // Rebuild the word ranges and hit boxes after the text or the font changes.
    void RebuildEntries(const Font& font);

    std::string text;
    unsigned char font_palette = 0;
    /* The recoloured span [start, stop) of this line. */
    unsigned char highlight_color = 0;
    std::size_t highlight_start = 0;
    std::size_t highlight_stop = 0;
    std::uint64_t clock = 0;
    /* SaveGame snapshots the unsigned milliseconds remaining from
       TimeUntilDeadline; load rearms the countdown with this duration. */
    std::uint32_t saved_remaining_ms = 0;
    /* Continuation link count of a wrapped entry; -1 when unlinked. */
    int link = 0;
    int length = 0; /* UTF-8 byte length, -1 when unset */
    std::vector<W8NoticeWord> entries;
    unsigned char unknown_1c[8]{};
};

/* The lines each text box keeps. */
inline constexpr std::size_t W8_TEXT_BOX_LINES = 0x15e;
/* The lines of the four text boxes, saved in the TEXT section. The status record
   counts each box's used and shown lines. */
using W8MessageLog = std::array<std::array<W8MessageStorageRecord, W8_TEXT_BOX_LINES>, 4>;
