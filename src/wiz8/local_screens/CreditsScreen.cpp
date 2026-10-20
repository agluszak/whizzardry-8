#include "wiz8/local_screens/CreditsScreen.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/checked_integer.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/unicode.h"

namespace {
class CreditsScreen final : public Screen {
public:
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;

private:
    std::vector<W8CreditLine> lines_;
    std::uint64_t elapsed_steps_ = 0;
    bool redraw_ = false;
    std::uint64_t started_at_ = 0;
    int y_ = 0;
    int line_ = 0;
};
} // namespace

/* Read one wide line, stopping at a newline, capacity, or the end of the
   stream. Answers whether the line ended at a newline; trailing carriage
   returns are stripped. */
std::optional<std::string> ReadRetailTextLine(wiz8::File* handle)
{
    std::vector<std::byte> bytes;
    for (;;) {
        std::array<std::byte, 2> unit{};
        const auto result = handle->read(unit.data(), unit.size());
        if (!result.bytes) {
            if (bytes.empty()) return {};
            break;
        }
        if (result.bytes != 2) throw std::runtime_error("Truncated UTF-16LE line");
        if (unit[0] == std::byte{10} && unit[1] == std::byte{}) break;
        if (bytes.size() >= 254) throw std::runtime_error("Oversized credits line");
        bytes.insert(bytes.end(), unit.begin(), unit.end());
    }
    auto text = wiz8::text::from_utf16le(bytes);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

bool CreditsScreen::enter(const ScreenRequest&)
try {
    std::unique_ptr<wiz8::File> handle;
    std::string line;

    SetViewport({0, 0, 0x281, 0x1e1});
    DisableCursorScene();
    std::vector<W8CreditLine> pending;

    handle = [&]() {
        try {
            return wiz8::open_file(wiz8::asset_path((char*)"Data\\Options\\Credits.txt"),
                                   wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (handle != 0) {
        std::optional<std::string> line_result;
        if (handle->size() % 2) throw std::runtime_error("Truncated credits character");
        std::uint16_t marker;
        handle->read_exact(&marker, sizeof(marker));
        if (marker != 0xfeff) handle->seek(0, wiz8::SeekOrigin::begin);
        while (!(handle->tell() >= handle->size())) {
            if (((line_result = ReadRetailTextLine(handle.get())) &&
                 (line = std::move(*line_result), true)) &&
                line[0] != '*') {
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
                    entry.pixel_width = (bold ? g_options_title_font : g_options_detail_font)
                                            .textWidth(entry.primary);
                }
                entry.line_height = 0x14 + (bold ? 5 : 0);
                pending.push_back(std::move(entry));
            }
        }
        if (handle) handle->close();
        handle.reset();
    }
    line_ = 0;
    lines_.swap(pending);
    elapsed_steps_ = 0;
    y_ = 0x1df;
    started_at_ = MonotonicMilliseconds();
    redraw_ = true;
    return true;
} catch (const std::exception&) {
    return false;
}

bool CreditsScreen::leave(ScreenDeparture)
{
    lines_.clear();
    EnableCursorScene();
    if (IsCurrentMusicPlaylist("EndCredit.MPL")) {
        StopMusicPlaylist(true);
    }
    return true;
}

void CreditsScreen::frame()
{
    InputEvent input;

    // No clickable controls: dismiss directly on a key or mouse release.
    while (auto next_input = PollInputEvent()) {
        input = *next_input;
        if (input.type == InputEventType::KeyDown || input.type == InputEventType::LeftButtonUp ||
            input.type == InputEventType::RightButtonUp)
            ReturnToPreviousScreen();
    }

    const auto steps = (MonotonicMilliseconds() - started_at_) / 35 - elapsed_steps_;
    if (steps >= 1) {
        elapsed_steps_ += steps;
        redraw_ = true;
        y_ -= wiz8::checked_integer<int>(steps);
        while (y_ < 0) {
            ++line_;
            if (line_ >= static_cast<int>(lines_.size())) {
                ReturnToPreviousScreen();
                break;
            }
            y_ += lines_[line_].line_height;
        }
    }
    if (!redraw_) {
        return;
    }

    CatalogArtwork::Load(0xe9, 0).draw(GetFrameBufferSurface(), 0, 0, 0, {}, BitmapKey::Opaque);
    int y = y_;
    for (int index = line_; index < static_cast<int>(lines_.size()) && y <= 0x1df; ++index) {
        const W8CreditLine* entry = &lines_[index];
        if ((entry->flags & 4) == 0) {
            Font& font = (entry->flags & 1) ? g_options_title_font : g_options_detail_font;
            if ((entry->flags & 2) == 0) {
                DrawText(GetFrameBufferSurface(), font, {(0x280 - entry->pixel_width) / 2, y},
                         entry->primary);
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
    redraw_ = false;
    RenderFrame();
}

std::unique_ptr<Screen> CreateCreditsScreen()
{
    return std::make_unique<CreditsScreen>();
}
