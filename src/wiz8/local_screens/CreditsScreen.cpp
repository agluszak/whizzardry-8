#include "wiz8/unicode.h"
#include <vector>
#include <array>
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/Screens.h"

#include "wiz8/cursor.h"
#include "wiz8/music_playlist.h"
#include "wiz8/regions.h"
#include "wiz8/utility.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/fonts.h"
#include "wiz8/virtual_file.h"
#include "wiz8/wiz8_windows.h"

#include "wiz8/filesystem.h"
#include "Font.h"
#include "input.h"

#include <stdlib.h>

// GLOBAL: WIZ8 0x0069C4A8
static W8GrowableVector<W8CreditLine>* g_credit_lines;
// GLOBAL: WIZ8 0x0069C494
static int g_credit_elapsed_steps;
// GLOBAL: WIZ8 0x0069C498
static bool g_credit_redraw;
// GLOBAL: WIZ8 0x0069C49C
static w8_ulong g_credit_started_at;
// GLOBAL: WIZ8 0x0069C4A0
static int g_credit_y;
// GLOBAL: WIZ8 0x0069C4A4
static int g_credit_line;

/* Read one wide line, stopping at a newline, capacity, or the end of the
   stream. Answers whether the line ended at a newline; trailing carriage
   returns are stripped. */
// FUNCTION: WIZ8 0x004CEED0
unsigned char ReadRetailTextLine(wiz8::File* handle, char* destination, int capacity, unsigned char* more)
try
{
    if (capacity <= 0) return 0;
    destination[0] = 0;
    *more = 1;
    std::vector<std::byte> bytes;
    for (;;) {
        std::array<std::byte, 2> unit{};
        auto result = handle->read(unit.data(), unit.size());
        if (result.bytes == 0) { *more = 0; break; }
        if (result.bytes != 2) { *more = 0; return 0; }
        if (unit[0] == std::byte{10} && unit[1] == std::byte{}) break;
        bytes.insert(bytes.end(), unit.begin(), unit.end());
        if (bytes.size() > static_cast<std::size_t>(capacity) * 2) return 0;
    }
    auto text = wiz8::text::from_utf16le(bytes);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    if (text.size() >= static_cast<std::size_t>(capacity)) return 0;
    wiz8::text::copy(destination, capacity, text);
    return *more || !bytes.empty();
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x005bc130
unsigned char CreditsScreenEnter(void)
try
{
    std::unique_ptr<wiz8::File> handle;
    char line[3 * (128) + 1];

    SetViewport(0, 0, 0x280, 0x1e0);
    ResetRegions();
    RegionSetEnable(2);
    DisableCursorScene();
    g_credit_lines = new W8GrowableVector<W8CreditLine>();

    handle = [&]() { try { return wiz8::open_file((char*)"Data\\Options\\Credits.txt", wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (handle != 0) {
        unsigned char more;
        handle->seek(2, wiz8::SeekOrigin::begin); // UTF-16LE BOM
        while (!(handle->tell() >= handle->size())) {
            if (ReadRetailTextLine(handle.get(), line, sizeof(line), &more) && line[0] != '*') {
                W8CreditLine entry = {0, 0, 0, 0, 0};
                bool blank = false;
                bool bold = false;
                if (line[0] == '!') {
                    bold = true;
                    entry.flags = 1;
                    entry.primary = strdup(line + 1);
                } else {
                    char* separator = strchr(line, '&');
                    if (separator != 0) {
                        separator[-1] = '\0';
                        entry.flags = 2;
                        entry.secondary = strdup(separator + 2);
                        entry.primary = strdup(line);
                    } else if (line[0] != '\0') {
                        entry.primary = strdup(line);
                    } else {
                        blank = true;
                        entry.flags = 4;
                    }
                }
                if (!blank) {
                    entry.pixel_width = StringPixLength(
                        entry.primary, bold ? g_options_title_font : g_options_detail_font);
                }
                entry.line_height = 0x14 + (bold ? 5 : 0);
                g_credit_lines->Add(entry);
            }
        }
        if (handle) handle->close();
        handle.reset();
    }
    g_credit_line = 0;
    g_credit_elapsed_steps = 0;
    g_credit_y = 0x1df;
    g_credit_started_at = GetTickCount();
    g_credit_redraw = true;
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x005bc420
unsigned char CreditsScreenLeave(int)
{
    for (int index = 0; index < g_credit_lines->GetCount(); ++index) {
        W8CreditLine* entry = g_credit_lines->GetAt(index);
        if (entry->primary != 0) {
            free(entry->primary);
        }
        if (entry->secondary != 0) {
            free(entry->secondary);
        }
    }
    delete g_credit_lines;
    ResetRegions();
    EnableCursorScene();
    if (IsCurrentMusicPlaylist("EndCredit.MPL")) {
        StopMusicPlaylist(true);
    }
    return 1;
}

// FUNCTION: WIZ8 0x005bc530
void CreditsScreenFrame(void)
{
    POINT point;
    InputAtom input;

    SGPMouseGetPos(&point);
    UpdateRegionMousePosition(point.x, point.y);
    while (DequeueEvent(&input) == 1) {
        if (!DispatchRegionInput(&input) && input.usEvent == KEY_DOWN) {
            RequestScreenTransition();
        }
    }

    int steps = (GetTickCount() - g_credit_started_at) / 35 - g_credit_elapsed_steps;
    if (steps >= 1) {
        g_credit_elapsed_steps += steps;
        g_credit_redraw = true;
        W8CreditLine entry = *g_credit_lines->GetAt(g_credit_line);
        g_credit_y -= steps;
        while (g_credit_y < 0) {
            ++g_credit_line;
            if (g_credit_line >= g_credit_lines->GetCount()) {
                RequestScreenTransition();
                break;
            }
            entry = *g_credit_lines->GetAt(g_credit_line);
            g_credit_y += entry.line_height;
        }
    }
    if (!g_credit_redraw) {
        return;
    }

    DrawCatalogImage(FRAME_BUFFER, 0xe9, 0, 0, 0, 0, VO_BLT_SRCTRANSPARENCY, 0);
    int y = g_credit_y;
    for (int index = g_credit_line; index < g_credit_lines->GetCount() && y <= 0x1df; ++index) {
        const W8CreditLine* entry = g_credit_lines->GetAt(index);
        if ((entry->flags & 4) == 0) {
            SetFont((entry->flags & 1) ? g_options_title_font : g_options_detail_font);
            if ((entry->flags & 2) == 0) {
                gprintf((0x280 - entry->pixel_width) / 2, y, "%s", entry->primary);
            } else {
                gprintf(0x136 - entry->pixel_width, y, "%s", entry->primary);
                if (entry->secondary != 0) {
                    gprintf(0x14a, y, "%s", entry->secondary);
                }
            }
        }
        y += entry->line_height;
    }
    ResetTransientRenderScenes();
    g_credit_redraw = false;
    RenderFrame();
}

/* Full-screen credits background: left-up or right-up leaves the screen. */
// FUNCTION: WIZ8 0x005BC7A0
unsigned char CreditsBackgroundRegionEvent(const InputAtom* event, W8Region*)
{
    int us_event = event->usEvent;
    if (us_event != LEFT_BUTTON_UP && us_event != RIGHT_BUTTON_UP) {
        return 0;
    }
    RequestScreenTransition();
    return 1;
}
