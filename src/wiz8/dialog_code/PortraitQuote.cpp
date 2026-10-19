#include "wiz8/dialog_code/PortraitQuote.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/image_assets.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/fonts.h"
#include "wiz8/render/frame.h"
#include "wiz8/unicode.h"

namespace {

// Markup scalars embedded in quote text.
constexpr char32_t kFontToggle = 0xb2;
constexpr char32_t kSectionToggle = 0xb3;
constexpr char32_t kColourChange = 0xb4;
constexpr char32_t kColourReset = 0xb5;

constexpr int kLineSpacing = 2;
constexpr int kEdgeTile = 16;
constexpr int kTextInset = 12;
constexpr int kFrame = 2 * kTextInset;
constexpr int kMinRequestedWidth = 10;
constexpr int kMaxRequestedWidth = 639;
constexpr int kMaxBubbleWidth = 349;
constexpr int kMaxBubbleHeight = 199;
// A wrap this close to the requested width is avoided by widening the bubble.
constexpr int kWrapSlack = 20;
constexpr std::uint8_t kDefaultTextColour = 0xd0;

constexpr std::string_view kEdgeSprites = "data\\NPC Interaction\\npc_PopUp_edge.sti";
constexpr std::string_view kBackground = "data\\NPC Interaction\\npc_popup_back.pcx";

bool IsMarkup(char32_t scalar)
{
    return scalar >= kFontToggle && scalar <= kColourReset;
}

void DrawQuoteLine(SDL_Surface& target, Font& font, SDL_Point at, std::string_view text,
                   std::uint8_t colour)
{
    const TextOptions options{.mono = true,
                              .colors = {font.color(colour), font.color(0), font.color(0)}};
    DrawTextAndInvalidate(target, font, at, text, {.colors = options.colors});
    DrawText(target, font, at, text, options);
}

void DrawWrappedText(SDL_Surface& target, SDL_Point origin, int wrap_width, Font& font,
                     std::uint8_t colour, std::string_view text)
{
    std::string line;
    std::string word;
    std::size_t position = 0;
    int line_width = 0;
    std::uint8_t active_colour = colour;
    SDL_Point at = origin;
    Font* active_font = &font;
    bool second_section = false;
    bool alternate_font = false;
    const auto flush = [&] { DrawQuoteLine(target, *active_font, at, line, active_colour); };
    const auto new_line = [&] {
        at.x = origin.x;
        at.y += active_font->height() + kLineSpacing;
    };
    for (;;) {
        std::size_t word_cursor = 0;
        const char32_t word_code = word.empty() ? 0 : wiz8::text::next(word, word_cursor);
        const std::size_t scalar_start = position;
        const char32_t ch = position < text.size() ? wiz8::text::next(text, position) : 0;
        if (ch != ' ' && ch != 0) {
            word.append(text.substr(scalar_start, position - scalar_start));
        } else if (!IsMarkup(word_code) && word_code != '\n') {
            const int word_width = active_font->textWidth(word);
            word += ' ';
            if (word_width + line_width > wrap_width) {
                flush();
                new_line();
                line = word;
                line_width = active_font->textWidth(line);
            } else {
                line_width += active_font->textWidth(word);
                line += word;
            }
            word.clear();
        } else if (word_code == '\n') {
            flush();
            new_line();
            line.clear();
            word.clear();
            line_width = 0;
        } else if (word_code == kSectionToggle) {
            if (second_section) {
                flush();
                new_line();
            }
            second_section = !second_section;
            line.clear();
            word.clear();
            line_width = 0;
        } else {
            flush();
            if (word_code == kColourChange && word_cursor < word.size() && word[word_cursor] != ' ')
                active_colour = static_cast<std::uint8_t>(wiz8::text::next(word, word_cursor));
            at.x += active_font->textWidth(line);
            if (word_code == kFontToggle) {
                alternate_font = !alternate_font;
                active_font = alternate_font ? &g_font12point1 : &font;
            } else if (word_code == kColourReset) {
                active_colour = colour;
            }
            line.clear();
            word.clear();
        }
        if (ch == 0) {
            flush();
            return;
        }
    }
}

struct TextMeasure {
    int height = 0;
    // Width of the first line that word wrapping broke, if the first break was a wrap.
    std::optional<int> first_wrap_width;
};

TextMeasure MeasureWrappedText(int wrap_width, Font& font, std::string_view text)
{
    TextMeasure result;
    std::string word;
    std::size_t position = 0;
    int line_width = 0;
    int line_count = 1;
    bool second_section = false;
    bool alternate_font = false;
    Font* active_font = &font;
    const auto break_line = [&](std::optional<int> wrapped_width) {
        if (line_count == 1) result.first_wrap_width = wrapped_width;
        ++line_count;
    };
    for (;;) {
        std::size_t word_cursor = 0;
        const char32_t word_code = word.empty() ? 0 : wiz8::text::next(word, word_cursor);
        const std::size_t scalar_start = position;
        const char32_t ch = position < text.size() ? wiz8::text::next(text, position) : 0;
        if (ch == '\n') {
            break_line(std::nullopt);
            word.clear();
            line_width = 0;
        } else if (ch != ' ' && ch != 0) {
            word.append(text.substr(scalar_start, position - scalar_start));
        } else if (!IsMarkup(word_code)) {
            const int next_width = active_font->textWidth(word) + line_width;
            word += ' ';
            if (next_width > wrap_width) {
                break_line(next_width);
                line_width = active_font->textWidth(word);
            } else {
                line_width += active_font->textWidth(word);
            }
            word.clear();
        } else {
            if (word_code == kFontToggle) {
                alternate_font = !alternate_font;
                active_font = alternate_font ? &g_font12point1 : &font;
            } else if (word_code == kSectionToggle) {
                if (second_section) break_line(std::nullopt);
                second_section = !second_section;
                line_width = 0;
            }
            word.clear();
        }
        if (ch == 0) {
            result.height = (font.height() + kLineSpacing) * line_count;
            return result;
        }
    }
}

int MeasureLineWidth(std::string_view text)
{
    int width = 0;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto start = cursor;
        const auto scalar = wiz8::text::next(text, cursor);
        if (!IsMarkup(scalar) && scalar > '\n')
            width += g_font12point1.textWidth(text.substr(start, cursor - start));
    }
    return width;
}

int WidestLine(std::string_view text)
{
    int widest = 0;
    for (std::size_t start = 0;;) {
        const auto end = std::min(text.find('\n', start), text.size());
        widest = std::max(widest, MeasureLineWidth(text.substr(start, end - start)));
        if (end == text.size()) return widest;
        start = end + 1;
    }
}

} // namespace

QuoteBubble::QuoteBubble(std::shared_ptr<SDL_Surface> surface, std::string_view text)
    : surface_(std::move(surface)), text_(text)
{
}

std::optional<QuoteBubble> QuoteBubble::layout(std::string_view text, int max_width,
                                               std::optional<std::uint8_t> text_colour)
{
    if (max_width > kMaxRequestedWidth) return std::nullopt;
    max_width = std::max(max_width, kMinRequestedWidth);
    const auto background = g_image_assets.bitmap(kBackground);
    const auto edges = g_image_assets.sprites(kEdgeSprites);
    if (!background || !edges) return std::nullopt;

    const int widest = WidestLine(text);
    int wrap_width = 0;
    if (widest < max_width - kFrame) {
        max_width = widest + kFrame;
        wrap_width = widest + 1;
    } else {
        wrap_width = max_width - kFrame + 1;
        const auto first_wrap =
            MeasureWrappedText(wrap_width, g_font12point1, text).first_wrap_width;
        if (first_wrap && *first_wrap - wrap_width < kWrapSlack) {
            wrap_width = *first_wrap;
            max_width = *first_wrap + kFrame;
        }
    }
    const int width = std::min(max_width, kMaxBubbleWidth);
    const int height = MeasureWrappedText(wrap_width, g_font12point1, text).height + kFrame;
    if (height > kMaxBubbleHeight) return std::nullopt;

    std::shared_ptr<SDL_Surface> rendered = CreateFrameSurface(width, height);
    SDL_Surface& surface = *rendered;
    const SDL_Rect area{0, 0, width, height};
    BlitImage(surface, 0, 0, *background, &area);
    for (int x = kEdgeTile; x < width - kEdgeTile; x += kEdgeTile) {
        DrawSprite(surface, *edges, 1, x, 0);
        DrawSprite(surface, *edges, 6, x, height - kEdgeTile);
    }
    for (int y = kEdgeTile; y < height - kEdgeTile; y += kEdgeTile) {
        DrawSprite(surface, *edges, 3, 0, y);
        DrawSprite(surface, *edges, 4, width - kEdgeTile / 2, y);
    }
    DrawSprite(surface, *edges, 0, 0, 0);
    DrawSprite(surface, *edges, 2, width - kEdgeTile, 0);
    DrawSprite(surface, *edges, 5, 0, height - kEdgeTile);
    DrawSprite(surface, *edges, 7, width - kEdgeTile, height - kEdgeTile);
    DrawWrappedText(surface, {kTextInset, kTextInset}, wrap_width, g_font12point1,
                    text_colour.value_or(kDefaultTextColour), text);

    return QuoteBubble(std::move(rendered), text);
}

void QuoteBubble::draw(SDL_Surface& target, int x, int y) const
{
    BlitImage(target, x, y, *surface_, nullptr);
    if (&target == &GetFrameBufferSurface()) InvalidateRegion(x, y, x + width(), y + height());
}

int QuoteBubble::width() const
{
    return surface_->w;
}

int QuoteBubble::height() const
{
    return surface_->h;
}
