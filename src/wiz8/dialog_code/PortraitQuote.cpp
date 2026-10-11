#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <SDL3/SDL_rect.h>
#include <string_view>
#include <string>
#include <algorithm>
#include <cstring>

#include "wiz8/surface_storage.h"
#include "wiz8/unicode.h"
#include "Font.h"
#include "himage.h"
#include "image.h"
#include "image_assets.h"
#include "sprite.h"
#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"

enum {
    WRAPPED_TEXT_LEFT = 0x01,
    WRAPPED_TEXT_CENTER = 0x02,
    WRAPPED_TEXT_RIGHT = 0x04,
    WRAPPED_TEXT_SHADOW_RECT = 0x08,
    WRAPPED_TEXT_INVALIDATE = 0x10,
    WRAPPED_TEXT_MEASURE_ONLY = 0x20
};

enum { QUOTE_BUBBLE_FLAT_BACKGROUND = 1 };

/* The NPC quote bubble record, allocated with operator new inside
   LayoutPortraitQuoteBubble. Offset 0 is the rendered text surface; 0x0c and
   0x10 are the popup background surface and border object the bubble is
   composited from. The byte at 0x14 is set once those two resources exist;
   the byte at 0x15 is set once the whole bubble has been created. */
struct W8PortraitQuoteBubble {
    ~W8PortraitQuoteBubble();
    SDLSurfaceOwner surface{nullptr, SDL_DestroySurface};
    unsigned short width;
    unsigned short height;
    unsigned char background_index;
    unsigned char object_index;
    std::shared_ptr<SDL_Surface> background_surface;
    std::shared_ptr<const SpriteSheet> object;
    bool has_resources;
    bool created;
    std::uint32_t flags; /* bit 0 selects the flat fill */
    std::string text;
    std::uint32_t palette;
};

// Flat bubbles fill with yellow and draw with yellow as their transparent key.
static std::uint16_t QuoteBubbleFlatFill()
{
    return Get16BPPColor(SDL_Color{255, 255, 0, SDL_ALPHA_OPAQUE});
}

// GLOBAL: WIZ8 0x0069C598
static W8PortraitQuoteBubble* g_portrait_quotes[10];
// GLOBAL: WIZ8 0x0069C5C0
static W8PortraitQuoteBubble* g_current_portrait_quote;

W8PortraitQuoteBubble::~W8PortraitQuoteBubble()
{
    if (g_current_portrait_quote == this)
        g_current_portrait_quote = nullptr;
}


/* The bubble artwork file tables, indexed by the edge and background selector
   arguments of LayoutPortraitQuoteBubble. */
// GLOBAL: WIZ8 0x0064f550
static const char* g_quote_bubble_edges[] = {
    "data\\NPC Interaction\\npc_PopUp_edge.sti",
};
// GLOBAL: WIZ8 0x0064f554
static const char* g_quote_bubble_backgrounds[] = {
    "data\\NPC Interaction\\npc_popup_back.pcx",
};

/* Flags staged for the next laid-out bubble; LayoutPortraitQuoteBubble copies
   them into the record and clears the staging value. Bit 0 selects the flat
   white fill instead of the background artwork. */
// GLOBAL: WIZ8 0x0069c5c8
static unsigned int g_quote_bubble_flags;

static int MeasureWrappedText(int x, int y, unsigned int wrap_width, int line_spacing, Font& font,
                              std::string_view text, int, int, int,
                              unsigned int* out_edge);
int DrawWrappedText(SDL_Surface& target, int x, int y, unsigned int wrap_width, int line_spacing,
                    Font& font, unsigned char colour, std::string_view text, int background,
                    int dirty, int flags, unsigned char shadow_index);

// FUNCTION: WIZ8 0x005d0590
static int DrawWrappedTextLine(SDL_Surface& target, std::string_view text, int x, int top,
                               int width, Font& font,
                               unsigned char foreground, unsigned char background, bool dirty,
                               unsigned int flags, unsigned char shadow_index)
{
    if (flags & WRAPPED_TEXT_MEASURE_ONLY) return 1;
    if (flags == 0) flags = WRAPPED_TEXT_LEFT;
    SDL_Point position{x, top};
    const SDL_Rect bounds{x, top, width, font.height()};
    if (!(flags & WRAPPED_TEXT_LEFT)) {
        if (flags & WRAPPED_TEXT_CENTER)
            position = font.centeredPosition(bounds, text);
        else if (flags & WRAPPED_TEXT_RIGHT)
            position = font.rightAlignedPosition(bounds, text);
    }
    const auto [draw_x, draw_y] = position;

    auto shadow = font.color(shadow_index);
    if (shadow_index != 0 && shadow == 0) {
        shadow = 1;
    }
    const TextOptions options{.mono = true,
                              .colors = {font.color(foreground), font.color(background), shadow}};
    if (flags & WRAPPED_TEXT_SHADOW_RECT) {
        ShadowRect(GetFrameBufferSurface(),
                   {draw_x - 1, draw_y - 1, font.textWidth(text) + 2, font.height() + 2});
    }
    if (dirty) {
        DrawTextAndInvalidate(target, font, {draw_x, draw_y}, text,
                              {.colors = options.colors});
    }
    DrawText(target, font, {draw_x, draw_y}, text, options);
    if (flags & WRAPPED_TEXT_INVALIDATE) {
        InvalidateRegion(draw_x, draw_y, draw_x + font.textWidth(text),
                         draw_y + font.height(), 0);
    }
    return 1;
}

static void RenderWrappedTextLine(SDL_Surface& target, std::string_view text, int x, int top,
                                  int width, Font& font,
                                  unsigned char foreground, unsigned char background, bool dirty,
                                  unsigned int flags, unsigned char shadow_index)
{
    DrawWrappedTextLine(target, text, x, top, width, font, foreground, background, dirty, flags,
                        shadow_index);
}

// FUNCTION: WIZ8 0x005d0770
int DrawWrappedText(SDL_Surface& target, int x, int y, unsigned int wrap_width, int line_spacing, Font& font,
                    unsigned char colour, std::string_view text, int background, int dirty, int flags,
                    unsigned char shadow_index)
{
    std::string line;
    std::string word;
    std::size_t position = 0;
    unsigned int line_width = 0;
    unsigned int remaining_width = wrap_width;
    unsigned char active_colour = colour;
    int draw_x = x;
    int draw_y = y;
    Font* active_font = &font;
    int line_count = 1;
    short section = 1;
    bool alternate = false;

    line.clear();
    word.clear();
    font.height();

    for (;;) {
        std::size_t word_cursor = 0;
        const auto word_code = word.empty() ? 0 : wiz8::text::next(word, word_cursor);
        const auto scalar_start = position;
        char32_t ch = position < text.size() ? wiz8::text::next(text, position) : 0;
        if (ch != ' ' && ch != '\0') {
            word.append(text.substr(scalar_start, position - scalar_start));
        } else if ((word_code < 0xb2 || word_code > 0xb5) && word_code != '\n') {

            unsigned int word_width = active_font->textWidth(word);
            word += ' ';

            if ((wrap_width & 0xffff) < word_width + line_width) {
                RenderWrappedTextLine(target, line, draw_x, draw_y, remaining_width, *active_font,
                                      active_colour, static_cast<unsigned char>(background),
                                      section != 0, flags, shadow_index);
                draw_y += active_font->height() + (line_spacing & 0xff);
                ++line_count;
                line = word;
                line_width = active_font->textWidth(line);
                remaining_width = wrap_width;
                draw_x = x;
            } else {
                line_width += active_font->textWidth(word);
                line += word;
            }
            word.clear();
        } else {
            if (word_code == '\n') {
                RenderWrappedTextLine(target, line, draw_x, draw_y, remaining_width, *active_font,
                                      active_colour, static_cast<unsigned char>(background),
                                      section != 0, flags, shadow_index);
                draw_y += active_font->height() + (line_spacing & 0xff);
                ++line_count;
                line.clear();
                word.clear();
                line_width = 0;
                remaining_width = wrap_width;
                draw_x = x;
            } else if (word_code == 0xb2) {
                RenderWrappedTextLine(target, line, draw_x, draw_y, remaining_width, *active_font,
                                      active_colour, static_cast<unsigned char>(background),
                                      section != 0, flags, shadow_index);
                if (alternate) {
                    int span = active_font->textWidth(line);
                    remaining_width -= line_width;
                    line.clear();
                    word.clear();
                    alternate = false;
                    draw_x += span;
                    active_font = &font;
                } else {
                    int span = active_font->textWidth(line);
                    active_font = &g_font12point1;
                    remaining_width -= line_width;
                    line.clear();
                    word.clear();
                    alternate = true;
                    shadow_index = 0;
                    draw_x += span;
                }
            } else if (word_code == 0xb3) {
                if (section == 2) {
                    RenderWrappedTextLine(target, line, draw_x, draw_y, remaining_width, *active_font,
                                          active_colour, static_cast<unsigned char>(background),
                                          true, flags, shadow_index);
                    section = 1;
                    draw_y += active_font->height() + (line_spacing & 0xff);
                    ++line_count;
                    line.clear();
                    word.clear();
                    line_width = 0;
                    draw_x = x;
                } else {
                    section = 2;
                    word.clear();
                    line.clear();
                    line_width = 0;
                }
            } else if (word_code == 0xb4 || word_code == 0xb5) {
                RenderWrappedTextLine(target, line, draw_x, draw_y, remaining_width, *active_font,
                                      active_colour, static_cast<unsigned char>(background),
                                      section != 0, flags, shadow_index);
                if (word_code == 0xb4 && word_cursor < word.size() && word[word_cursor] != ' ') {
                    active_colour = static_cast<unsigned char>(wiz8::text::next(word, word_cursor));
                }
                int span = active_font->textWidth(line);
                remaining_width -= line_width;
                bool restore_colour = word_code == 0xb5;
                line.clear();
                word.clear();
                if (restore_colour) {
                    active_colour = colour;
                }
                draw_x += span;
            }
        }

        if (ch == '\0') {
            RenderWrappedTextLine(target, line, draw_x, draw_y, remaining_width, *active_font, active_colour,
                                  static_cast<unsigned char>(background), section != 0, flags,
                                  shadow_index);
            return (font.height() + (line_spacing & 0xff)) * line_count;
        }
    }
}

// FUNCTION: WIZ8 0x005d0050
static int MeasureWrappedText(int x, int y, unsigned int wrap_width, int line_spacing, Font& font,
                              std::string_view text, int, int, int,
                              unsigned int* out_edge)
{
    /* Retail reserves 0x500 bytes for each buffer (0x140-dword clears). */
    std::string line;
    std::string word;
    std::size_t position = 0;
    unsigned int line_width = 0;
    int line_count = 1;
    short section = 1;
    bool alternate = false;
    Font* active_font = &font;

    line.clear();
    word.clear();
    font.height();

    for (;;) {
        std::size_t word_cursor = 0;
        const auto word_code = word.empty() ? 0 : wiz8::text::next(word, word_cursor);
        const auto scalar_start = position;
        char32_t ch = position < text.size() ? wiz8::text::next(text, position) : 0;
        if (ch != ' ' && ch != '\0') {
            if (ch == '\n') {
                active_font->height();
                if (line_count == 1 && out_edge != 0) {
                    *out_edge = 0xffffffff;
                }
                ++line_count;
                line.clear();
                word.clear();
                line_width = 0;
            } else {
                word.append(text.substr(scalar_start, position - scalar_start));
            }
        } else if ((word_code < 0xb2 || word_code > 0xb5) && word_code != '\n') {

            unsigned int word_width = active_font->textWidth(word);
            word += ' ';

            unsigned int next_width = word_width + line_width;
            if ((wrap_width & 0xffff) < next_width) {
                active_font->height();
                if (line_count == 1 && out_edge != 0) {
                    *out_edge = next_width;
                }
                ++line_count;
                line = word;
                line_width = active_font->textWidth(line);
            } else {
                line_width += active_font->textWidth(word);
                line += word;
            }
            word.clear();
        } else {
            switch (word_code) {
            case '\n':
                active_font->height();
                if (line_count == 1 && out_edge != 0) {
                    *out_edge = 0xffffffff;
                }
                ++line_count;
                line.clear();
                word.clear();
                line_width = 0;
                break;
            case 0xb2:
                if (alternate) {
                    active_font->textWidth(line);
                    line.clear();
                    word.clear();
                    alternate = false;
                    active_font = &font;
                } else {
                    active_font->textWidth(line);
                    active_font = &g_font12point1;
                    line.clear();
                    word.clear();
                    alternate = true;
                }
                break;
            case 0xb3:
                if (section == 2) {
                    active_font->height();
                    if (line_count == 1 && out_edge != 0) {
                        *out_edge = 0xffffffff;
                    }
                    ++line_count;
                    line_width = 0;
                    section = 1;
                } else {
                    section = 2;
                    line_width = 0;
                }
                line.clear();
                word.clear();
                break;
            case 0xb4:
            case 0xb5:
                active_font->textWidth(line);
                line.clear();
                word.clear();
                break;
            }
        }

        if (ch == '\0') {
            return (font.height() + (line_spacing & 0xff)) * line_count;
        }
    }
}

// FUNCTION: WIZ8 0x005cf620
unsigned char DrawPortraitQuoteBubble(int quote_handle, short x, short y, SDL_Surface& surface)
{
    W8PortraitQuoteBubble* bubble;

    if (quote_handle == -1) {
        return 0;
    }
    bubble = g_portrait_quotes[quote_handle];
    if (bubble == 0) {
        return 0;
    }
    g_current_portrait_quote = bubble;
    BlitImage(surface, x, y, *bubble->surface, nullptr,
              {.color_key = (bubble->flags & QUOTE_BUBBLE_FLAT_BACKGROUND)
                                ? std::optional<std::uint32_t>{QuoteBubbleFlatFill()}
                                : std::nullopt});
    if (&surface == &GetFrameBufferSurface()) {
        InvalidateRegion(x, y, static_cast<short>(x + bubble->width),
                         static_cast<short>(y + bubble->height), 0);
    }
    return 1;
}

static short MeasurePortraitQuoteLine(std::string_view text)
{
    short width = 0;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto start = cursor;
        const auto codepoint = wiz8::text::next(text, cursor);
        if ((codepoint < 0xb2 || codepoint > 0xb5) && codepoint > 10)
            width += g_font12point1.textWidth(text.substr(start, cursor - start));
    }
    return width;
}

// FUNCTION: WIZ8 0x005cf6c0
int LayoutPortraitQuoteBubble(int quote_handle, unsigned char background_index,
                              unsigned char edge_index, std::string_view text, unsigned int max_width,
                              int margin_x, int margin_top, int margin_bottom,
                              unsigned short* out_width, unsigned short* out_height,
                              unsigned int font_palette)
{
    std::string line;
    const char* read;
    size_t remaining;
    int position;
    short line_width;
    int index;
    unsigned int max_line;
    unsigned int right_edge;
    int text_height;
    unsigned int height;
    int width_px;
    int height_px;
    /* Retail left these unset on the nonzero-background/no-palette path and
       still consumed them; natively they start at zero. */
    unsigned char colour = 0;
    bool shadow = false;
    unsigned short x;
    unsigned short y;
    SDL_Rect rect;

    if (static_cast<unsigned short>(max_width) >= 0x280) {
        return -1;
    }
    if (static_cast<unsigned short>(max_width) <= 0xa) {
        max_width = 10;
    }
    if (quote_handle != -1 && (quote_handle < 0 || quote_handle >= 10 || !g_portrait_quotes[quote_handle]))
        return -1;
    auto pending = quote_handle == -1 ? std::make_unique<W8PortraitQuoteBubble>() : nullptr;
    auto& bubble = pending ? *pending : *g_portrait_quotes[quote_handle];
    if (quote_handle == -1) {
        g_current_portrait_quote = &bubble;
        if (!(bubble.background_surface =
                  g_image_assets.bitmap(g_quote_bubble_backgrounds[background_index]))) {
            return -1;
        }
        if (!(g_current_portrait_quote->object =
                  g_image_assets.sprites(g_quote_bubble_edges[edge_index]))) {
            return -1;
        }
        g_current_portrait_quote->has_resources = true;
        g_current_portrait_quote->background_index = background_index;
        g_current_portrait_quote->object_index = edge_index;
    } else {
        g_current_portrait_quote = &bubble;
        if (background_index != bubble.background_index || edge_index != bubble.object_index ||
            !bubble.has_resources) {
            if (bubble.has_resources) {
                bubble.background_surface.reset();
                g_current_portrait_quote->object.reset();
                g_current_portrait_quote->has_resources = false;
            }
            if (!(g_current_portrait_quote->background_surface =
                      g_image_assets.bitmap(g_quote_bubble_backgrounds[background_index]))) {
                return -1;
            }
            if (!(g_current_portrait_quote->object =
                      g_image_assets.sprites(g_quote_bubble_edges[edge_index]))) {
                return -1;
            }
            g_current_portrait_quote->has_resources = true;
            g_current_portrait_quote->background_index = background_index;
            g_current_portrait_quote->object_index = edge_index;
        }
    }
    g_current_portrait_quote->text = text;
    g_current_portrait_quote->flags = g_quote_bubble_flags;
    g_quote_bubble_flags = 0;
    max_line = 0;
    for (std::size_t start = 0; start <= text.size();) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        max_line = std::max(max_line, static_cast<unsigned int>(MeasurePortraitQuoteLine(text.substr(start, end - start))));
        if (end == text.size()) break;
        start = end + 1;
    }
    if (static_cast<int>(max_line & 0xffff) < static_cast<int>((max_width & 0xffff) - 0x18)) {
        max_width = max_line + 0x18;
        ++max_line;
    } else {
        max_line = (max_width - margin_x) - 0x17;
        right_edge = 0xffffffff;
        MeasureWrappedText(0, 0, max_line, 2, g_font12point1, text, 0, 0, 1, &right_edge);
        if (right_edge != 0xffffffff && static_cast<int>(right_edge - (max_line & 0xffff)) < 0x14) {
            max_line = right_edge;
            max_width = right_edge + 0x18;
        }
    }
    text_height = MeasureWrappedText(0, 0, max_line, 2, g_font12point1, text, 0, 0, 1, 0);
    height = text_height + margin_top + 0x18 + margin_bottom;
    max_width = max_width + margin_x * 2;
    if (static_cast<unsigned short>(max_width) >= 0x15e) {
        max_width = 0x15d;
    }
    if (static_cast<unsigned short>(height) < 200) {
        bubble.surface = CreateFrameSurface(max_width, height);
        bubble.palette = font_palette;
        bubble.created = true;
        bubble.width = static_cast<unsigned short>(max_width);
        bubble.height = static_cast<unsigned short>(height);
        width_px = max_width & 0xffff;
        height_px = height & 0xffff;
        *out_width = static_cast<unsigned short>(max_width);
        *out_height = static_cast<unsigned short>(height);
        rect = {0, 0, width_px, height_px};
        if (bubble.flags & QUOTE_BUBBLE_FLAT_BACKGROUND) {
            SDL_FillSurfaceRect(bubble.surface.get(), nullptr, QuoteBubbleFlatFill());
        } else {
            BlitImage(*bubble.surface, 0, 0, *bubble.background_surface, &rect);
        }
        const auto& object = *bubble.object;
        for (x = 0x10; static_cast<int>(x) < width_px - 0x10; x = x + 0x10) {
            DrawSprite(*bubble.surface, object, 1, x, 0);
            DrawSprite(*bubble.surface, object, 6, x, height_px - 0x10);
        }
        for (y = 0x10; static_cast<int>(y) < height_px - 0x10; y = y + 0x10) {
            DrawSprite(*bubble.surface, object, 3, 0, y);
            DrawSprite(*bubble.surface, object, 4, width_px - 8, y);
        }
        DrawSprite(*bubble.surface, object, 0, 0, 0);
        DrawSprite(*bubble.surface, object, 2, width_px - 0x10, 0);
        DrawSprite(*bubble.surface, object, 5, 0, height_px - 0x10);
        DrawSprite(*bubble.surface, object, 7, width_px - 0x10, height_px - 0x10);
        if (background_index == 0) {
            colour = 0xd0;
            shadow = false;
        }
        if (bubble.palette != 0xffffffff) {
            colour = static_cast<unsigned char>(bubble.palette);
        }
        DrawWrappedText(*bubble.surface, margin_x + 0xc, margin_top + 0xc, max_line, 2,
                        g_font12point1, colour, text, 0, 0, 1, shadow ? 2 : 0);
        if (quote_handle == -1) {
            for (index = 0; index < 10; ++index) {
                if (g_portrait_quotes[index] == 0) {
                    g_portrait_quotes[index] = pending.release();
                    if (index != -1) {
                        g_current_portrait_quote = &bubble;
                    }
                    return index;
                }
            }
            return -1;
        }
        if (quote_handle == -1) {
            return -1;
        }
        if (g_portrait_quotes[quote_handle] != 0) {
            g_current_portrait_quote = g_portrait_quotes[quote_handle];
        }
        return quote_handle;
    }
    if (quote_handle != -1) {
        return -1;
    }
    return -1;
}

// FUNCTION: WIZ8 0x005cffa0
unsigned char ReleasePortraitQuoteBubble(int quote_handle)
{
    if (quote_handle < 0 || quote_handle >= 10 || !g_portrait_quotes[quote_handle])
        return 0;
    auto* quote = g_portrait_quotes[quote_handle];
    g_portrait_quotes[quote_handle] = nullptr;
    delete quote;
    return 1;
}

// FUNCTION: WIZ8 0x005d0750
const char* GetPortraitQuoteText(int quote_handle)
{
    W8PortraitQuoteBubble* quote;

    if (quote_handle != -1 && (quote = g_portrait_quotes[quote_handle]) != 0) {
        g_current_portrait_quote = quote;
        return quote->text.c_str();
    }
    return 0;
}
