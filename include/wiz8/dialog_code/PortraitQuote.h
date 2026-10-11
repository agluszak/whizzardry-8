#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

struct SDL_Surface;

struct QuoteBubbleLayout {
    int handle;
    int width;
    int height;
};

/* Renders `text` into a new NPC quote bubble no wider than `max_width`. */
std::optional<QuoteBubbleLayout> LayoutPortraitQuoteBubble(std::string_view text, int max_width,
                                                           std::optional<std::uint8_t> text_colour);
void DrawPortraitQuoteBubble(int handle, int x, int y, SDL_Surface& target);
void ReleasePortraitQuoteBubble(int handle);
std::string_view GetPortraitQuoteText(int handle);
