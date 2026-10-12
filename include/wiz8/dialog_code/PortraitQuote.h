#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct SDL_Surface;

/* An NPC or party quote rendered into a framed bubble. Copies share the
   immutable rendered surface. */
class QuoteBubble {
public:
    /* Returns nullopt when `text` does not fit a bubble at most `max_width` wide. */
    static std::optional<QuoteBubble> layout(std::string_view text, std::int32_t max_width,
                                             std::optional<std::uint8_t> text_colour);

    void draw(SDL_Surface& target, std::int32_t x, std::int32_t y) const;
    std::int32_t width() const;
    std::int32_t height() const;
    std::string_view text() const { return text_; }

private:
    QuoteBubble(std::shared_ptr<SDL_Surface> surface, std::string_view text);

    std::shared_ptr<SDL_Surface> surface_;
    std::string text_;
};
