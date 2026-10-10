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

#include "Font.h"
#include "input.h"

#include <memory>
#include <stdexcept>

#include <stdlib.h>
#include <wchar.h>

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
unsigned char ReadWideTextLine(wiz8::File* handle, wchar_t* destination, int capacity, unsigned char* more)
try
{
    *more = 0;
    if (capacity <= 0) return 0;
    destination[0] = 0;
    if (capacity == 1) return 0;
    *more = 1;
    int length = 0;
    bool newline = false;
    for (;;) {
        wchar_t character;
        if (handle->read(&character, sizeof(character)).bytes != sizeof(character)) {
            *more = 0;
            break;
        }
        if (character == L'\n') {
            newline = true;
            break;
        }
        destination[length++] = character;
        destination[length] = 0;
        if (length == capacity - 1) break;
    }
    if (length != 0 && destination[length - 1] == L'\r') destination[length - 1] = 0;
    return newline;
}
catch (const std::exception&) { *more = 0; return false; }

// FUNCTION: WIZ8 0x005bc130
unsigned char CreditsScreenEnter(void)
try
{
    SetViewport(0, 0, 0x280, 0x1e0);
    ResetRegions();
    RegionSetEnable(2);
    DisableCursorScene();
    auto release_lines = [](W8GrowableVector<W8CreditLine>* lines) {
        for (int index = 0; index < lines->GetCount(); ++index) {
            W8CreditLine* line = lines->GetAt(index);
            free(line->primary);
            free(line->secondary);
        }
        delete lines;
    };
    std::unique_ptr<W8GrowableVector<W8CreditLine>, decltype(release_lines)> pending(
        new W8GrowableVector<W8CreditLine>, release_lines);
    std::unique_ptr<wiz8::File> handle;
    try { handle = wiz8::open_file("Data\\Options\\Credits.txt", wiz8::OpenMode::read); }
    catch (const std::exception&) {}
    if (handle != nullptr) {
        if (handle->size() % sizeof(wchar_t) != 0)
            throw std::runtime_error("incomplete credits character");
        wchar_t marker;
        handle->read_exact(&marker, sizeof(marker));
        if (marker != 0xfeff) handle->seek(0, wiz8::SeekOrigin::begin);
        wchar_t line[128];
        unsigned char more = 1;
        while (more != 0) {
            if (!ReadWideTextLine(handle.get(), line, 128, &more)) {
                if (more != 0 || handle->tell() < handle->size())
                    throw std::runtime_error("invalid credits line");
                break;
            }
            if (line[0] == L'*') continue;
            W8CreditLine entry{};
            wchar_t* primary_text = line;
            wchar_t* secondary_text = nullptr;
            if (line[0] == L'!') {
                entry.flags = 1;
                ++primary_text;
            } else {
                wchar_t* separator = wcschr(line, L'&');
                if (separator != nullptr) {
                    if (separator == line || separator[1] == 0)
                        throw std::runtime_error("invalid credits separator");
                    separator[-1] = 0;
                    secondary_text = separator + 2;
                    entry.flags = 2;
                } else if (line[0] == 0) {
                    entry.flags = 4;
                }
            }
            using TextOwner = std::unique_ptr<wchar_t, decltype(&free)>;
            TextOwner primary(nullptr, free);
            TextOwner secondary(nullptr, free);
            if ((entry.flags & 4) == 0) {
                primary.reset(_wcsdup(primary_text));
                if (!primary) throw std::bad_alloc();
                entry.primary = primary.get();
                entry.pixel_width = StringPixLength(
                    entry.primary, (entry.flags & 1) ? g_options_title_font : g_options_detail_font);
            }
            if (secondary_text != nullptr) {
                secondary.reset(_wcsdup(secondary_text));
                if (!secondary) throw std::bad_alloc();
                entry.secondary = secondary.get();
            }
            entry.line_height = 0x14 + ((entry.flags & 1) ? 5 : 0);
            if (pending->Add(entry) < 0) throw std::bad_alloc();
            (void)primary.release();
            (void)secondary.release();
        }
    }
    g_credit_lines = pending.release();
    g_credit_line = 0;
    g_credit_elapsed_steps = 0;
    g_credit_y = 0x1df;
    g_credit_started_at = GetTickCount();
    g_credit_redraw = true;
    return 1;
}
catch (const std::exception&) {
    ResetRegions();
    EnableCursorScene();
    return false;
}

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
    if (g_credit_lines->GetCount() == 0) {
        RequestScreenTransition();
        return;
    }
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
                gprintf((0x280 - entry->pixel_width) / 2, y, L"%s", entry->primary);
            } else {
                gprintf(0x136 - entry->pixel_width, y, L"%s", entry->primary);
                if (entry->secondary != 0) {
                    gprintf(0x14a, y, L"%s", entry->secondary);
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
