#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_rect.h>

#include "Font.h"

struct SpriteSheet;
inline constexpr std::string_view DEFAULT_GENERIC_BUTTON_OFF = "Data\\Message Box\\GENBUTN.STI";
inline constexpr std::string_view DEFAULT_GENERIC_BUTTON_ON = "Data\\Message Box\\GENBUTN2.STI";
inline constexpr std::string_view DEFAULT_GENERIC_BUTTON_OFF_HI = "Data\\Message Box\\GENBUTN3.STI";
inline constexpr std::string_view DEFAULT_GENERIC_BUTTON_ON_HI = "Data\\Message Box\\GENBUTN4.STI";

inline constexpr int BUTTON_NO_IMAGE = -1;
inline constexpr std::nullptr_t BUTTON_NO_CALLBACK = nullptr;

struct ButtonGraphics;
struct GenericButtonImages;

using ButtonImage = std::shared_ptr<const ButtonGraphics>;
using GenericButtonImage = std::shared_ptr<GenericButtonImages>;

struct ButtonAppearance {
    ButtonImage quick_image;
    GenericButtonImage generic_image;
    std::string string;
    Font* usFont = nullptr;
    bool fMultiColor = false;
    std::int16_t sForeColor = 0;
    std::int16_t sShadowColor = -1;
};

struct ButtonVisualState {
    bool enabled = true;
    bool pressed = false;
    bool hovered = false;
    // Menu artwork leaves missing states blank and insets enlarged hover sprites.
    bool menu_artwork = false;
};

struct ButtonGraphics {
    static ButtonImage Load(std::string_view filename, std::int32_t grayed, std::int32_t off_normal,
                            std::int32_t off_hilite, std::int32_t on_normal,
                            std::int32_t on_hilite);
    static ButtonImage Load(std::shared_ptr<const SpriteSheet> sprites, std::int32_t grayed,
                            std::int32_t off_normal, std::int32_t off_hilite,
                            std::int32_t on_normal, std::int32_t on_hilite);

    const SpriteSheet& object() const
    {
        return *sprites;
    }

private:
    std::shared_ptr<const SpriteSheet> sprites;

public:
    std::int32_t Grayed = BUTTON_NO_IMAGE;
    std::int32_t OffNormal = BUTTON_NO_IMAGE;
    std::int32_t OffHilite = BUTTON_NO_IMAGE;
    std::int32_t OnNormal = BUTTON_NO_IMAGE;
    std::int32_t OnHilite = BUTTON_NO_IMAGE;
    std::uint32_t MaxWidth = 0;
    std::uint32_t MaxHeight = 0;
};

struct GenericButtonImages {
    std::array<std::shared_ptr<const SpriteSheet>, 6> frames;
    std::uint16_t fill_color = 0;
    std::uint16_t background_frame = 0;
    std::int16_t offset_x = 0;
    std::int16_t offset_y = 0;
};

void DrawButtonAppearance(SDL_Surface& target, const ButtonAppearance& appearance,
                          const SDL_Rect& bounds, const ButtonVisualState& state);
GenericButtonImage DefaultGenericButtonImage();
bool InitializeButtonImageManager();
void ShutdownButtonImageManager();
GenericButtonImage LoadGenericButtonImages(std::string_view gray, std::string_view off_normal,
                                           std::string_view off_hilite, std::string_view on_normal,
                                           std::string_view on_hilite, std::string_view background,
                                           std::int16_t index, std::int16_t offset_x,
                                           std::int16_t offset_y);
