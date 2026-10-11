#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <utility>

#include "compat/compiler.h"
#include "wiz8/unicode.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/regions.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/fonts.h"
#include "wiz8/filesystem.h"
#include "Font.h"
#include "input.h"
#include "compat/native.h"

// GLOBAL: WIZ8 0x0069C4A8
static std::vector<W8CreditLine> g_credit_lines;
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
std::optional<std::string> ReadRetailTextLine(wiz8::File* handle)
{
    std::vector<std::byte> bytes;
    for (;;) {
        std::array<std::byte, 2> unit{};
        const auto result = handle->read(unit.data(), unit.size());
        if (!result.bytes) { if (bytes.empty()) return {}; break; }
        if (result.bytes != 2) throw std::runtime_error("Truncated UTF-16LE line");
        if (unit[0] == std::byte{10} && unit[1] == std::byte{}) break;
        if (bytes.size() >= 254) throw std::runtime_error("Oversized credits line");
        bytes.insert(bytes.end(), unit.begin(), unit.end());
    }
    auto text = wiz8::text::from_utf16le(bytes);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

// FUNCTION: WIZ8 0x005bc130
unsigned char CreditsScreenEnter(void)
try
{
    std::unique_ptr<wiz8::File> handle;
    std::string line;

    SetViewport(0, 0, 0x280, 0x1e0);
    ResetRegions();
    DisableCursorScene();
    std::vector<W8CreditLine> pending;

    handle = [&]() { try { return wiz8::open_file((char*)"Data\\Options\\Credits.txt", wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (handle != 0) {
        std::optional<std::string> line_result;
        if (handle->size() % 2) throw std::runtime_error("Truncated credits character");
        std::uint16_t marker;
        handle->read_exact(&marker, sizeof(marker));
        if (marker != 0xfeff) handle->seek(0, wiz8::SeekOrigin::begin);
        while (!(handle->tell() >= handle->size())) {
            if (((line_result = ReadRetailTextLine(handle.get())) && (line = std::move(*line_result), true)) && line[0] != '*') {
                W8CreditLine entry{};
                bool blank = false;
                bool bold = false;
                if (line[0] == '!') {
                    bold = true;
                    entry.flags = 1;
                    entry.primary = line.substr(1);
                } else {
                    const auto separator = line.find(" & ");
                    if (separator != std::string::npos) {
                        entry.flags = 2;
                        entry.secondary = line.substr(separator + 3);
                        entry.primary = line.substr(0, separator);
                    } else if (line[0] != '\0') {
                        entry.primary = line;
                    } else {
                        blank = true;
                        entry.flags = 4;
                    }
                }
                if (!blank) {
                    entry.pixel_width = (bold ? g_options_title_font : g_options_detail_font).textWidth(entry.primary);
                }
                entry.line_height = 0x14 + (bold ? 5 : 0);
                pending.push_back(std::move(entry));
            }
        }
        if (handle) handle->close();
        handle.reset();
    }
    g_credit_line = 0;
    g_credit_lines.swap(pending);
    g_credit_elapsed_steps = 0;
    g_credit_y = 0x1df;
    g_credit_started_at = w8_get_ticks();
    g_credit_redraw = true;
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x005bc420
unsigned char CreditsScreenLeave(int)
{
    g_credit_lines.clear();
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
    InputEvent input;

    // No clickable controls: dismiss directly on a key or mouse release.
    while (DequeueEvent(&input) == 1) {
        if (input.type == InputEventType::KeyDown || input.type == InputEventType::LeftButtonUp ||
            input.type == InputEventType::RightButtonUp)
            RequestScreenTransition();
    }

    int steps = (w8_get_ticks() - g_credit_started_at) / 35 - g_credit_elapsed_steps;
    if (steps >= 1) {
        g_credit_elapsed_steps += steps;
        g_credit_redraw = true;
        W8CreditLine entry = g_credit_lines[g_credit_line];
        g_credit_y -= steps;
        while (g_credit_y < 0) {
            ++g_credit_line;
            if (g_credit_line >= g_credit_lines.size()) {
                RequestScreenTransition();
                break;
            }
            entry = g_credit_lines[g_credit_line];
            g_credit_y += entry.line_height;
        }
    }
    if (!g_credit_redraw) {
        return;
    }

    CatalogArtwork::Load(0xe9, 0).draw(GetFrameBufferSurface(), 0, 0, 0, {.color_key = false});
    int y = g_credit_y;
    for (int index = g_credit_line; index < g_credit_lines.size() && y <= 0x1df; ++index) {
        const W8CreditLine* entry = &g_credit_lines[index];
        if ((entry->flags & 4) == 0) {
            Font& font = (entry->flags & 1) ? g_options_title_font : g_options_detail_font;
            if ((entry->flags & 2) == 0) {
                DrawText(GetFrameBufferSurface(), font,
                         {(0x280 - entry->pixel_width) / 2, y}, entry->primary);
            } else {
                DrawText(GetFrameBufferSurface(), font, {0x136 - entry->pixel_width, y},
                         entry->primary);
                if (!entry->secondary.empty()) {
                    DrawText(GetFrameBufferSurface(), font, {0x14a, y}, entry->secondary);
                }
            }
        }
        y += entry->line_height;
    }
    ResetTransientRenderScenes();
    g_credit_redraw = false;
    RenderFrame();
}
