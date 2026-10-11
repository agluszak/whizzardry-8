#include <SDL3/SDL_log.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "Font.h"
#include "image.h"
#include "image_assets.h"
#include "sprite.h"

#define FONT_MCOLOR_BLACK 0

// GLOBAL: WIZ8 0x005ff824
bool gfRenderHilights = true;

static GenericButtonImage default_generic_image;

GenericButtonImage DefaultGenericButtonImage()
{
    return default_generic_image;
}



static std::int8_t DisabledOverlayStyle(const ButtonAppearance& appearance,
                                       const ButtonVisualState& state);
static std::int8_t DrawQuickButton(SDL_Surface& target, const ButtonAppearance* appearance,
                                   const ButtonVisualState& state);
static std::int8_t DrawGenericButton(SDL_Surface& target, const ButtonAppearance* appearance,
                                     const ButtonVisualState& state);
static void DrawTextOnButton(SDL_Surface& target, const ButtonAppearance* appearance,
                             const ButtonVisualState& state);
static void DrawHatchOnButton(SDL_Surface& target, const ButtonAppearance* appearance);
static void DrawShadeOnButton(SDL_Surface& target, const ButtonAppearance* appearance);

ButtonImage ButtonGraphics::Load(std::string_view filename, std::int32_t grayed, std::int32_t off_normal,
                                 std::int32_t off_hilite, std::int32_t on_normal, std::int32_t on_hilite)
{
    const std::array frames{grayed, off_normal, off_hilite, on_normal, on_hilite};
    if (filename.empty() || std::all_of(frames.begin(), frames.end(),
                                      [](int frame) { return frame == BUTTON_NO_IMAGE; }))
        return {};

    auto image = std::make_shared<ButtonGraphics>();
    image->sprites = g_image_assets.sprites(filename);
    if (!image->sprites)
        return {};

    image->MaxWidth = image->MaxHeight = 0;
    for (const int index : frames) {
        if (index == BUTTON_NO_IMAGE)
            continue;
        if (index < 0 ||
            static_cast<std::size_t>(index) >= image->object().frames.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid button frame %d in %s",
                         index, std::string(filename).c_str());
            return {};
        }
        const auto& frame = image->object().frames[index];
        image->MaxWidth = std::max(image->MaxWidth,
            static_cast<std::uint32_t>(std::max(0, frame.usWidth + frame.sOffsetX)));
        image->MaxHeight = std::max(image->MaxHeight,
            static_cast<std::uint32_t>(std::max(0, frame.usHeight + frame.sOffsetY)));
    }
    image->Grayed = grayed;
    image->OffNormal = off_normal;
    image->OffHilite = off_hilite;
    image->OnNormal = on_normal;
    image->OnHilite = on_hilite;
    return image;
}

ButtonImage ButtonGraphics::Load(std::shared_ptr<const SpriteSheet> sprites,
                                 std::int32_t grayed, std::int32_t off_normal,
                                 std::int32_t off_hilite, std::int32_t on_normal,
                                 std::int32_t on_hilite)
{
    const std::array frames{grayed, off_normal, off_hilite, on_normal, on_hilite};
    if (!sprites || std::all_of(frames.begin(), frames.end(),
                                [](int frame) { return frame == BUTTON_NO_IMAGE; }))
        return {};

    auto image = std::make_shared<ButtonGraphics>();
    image->sprites = std::move(sprites);
    for (const int index : frames) {
        if (index == BUTTON_NO_IMAGE)
            continue;
        if (index < 0 || static_cast<std::size_t>(index) >= image->object().frames.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid button frame %d", index);
            return {};
        }
        const auto& frame = image->object().frames[index];
        image->MaxWidth = std::max(image->MaxWidth,
            static_cast<std::uint32_t>(std::max(0, frame.usWidth + frame.sOffsetX)));
        image->MaxHeight = std::max(image->MaxHeight,
            static_cast<std::uint32_t>(std::max(0, frame.usHeight + frame.sOffsetY)));
    }
    image->Grayed = grayed;
    image->OffNormal = off_normal;
    image->OffHilite = off_hilite;
    image->OnNormal = on_normal;
    image->OnHilite = on_hilite;
    return image;
}

namespace {
GenericButtonImage LoadGenericGraphics(const std::array<std::string_view, 6>& filenames,
                                       std::int16_t background_frame, std::int16_t offset_x, std::int16_t offset_y,
                                       bool optional_highlights = false)
{
    if (filenames[0].empty() || filenames[1].empty()) return {};
    auto image = std::make_shared<GenericButtonImages>();
    for (std::size_t index = 0; index < filenames.size(); ++index) {
        if (filenames[index].empty()) continue;
        auto& object = image->frames[index];
        object = g_image_assets.sprites(filenames[index]);
        const bool valid = object && (index == 5
            ? background_frame >= 0 &&
                  static_cast<std::size_t>(background_frame) < object->frames.size()
            : object->frames.size() >= 9 && object->frames[0].usWidth > 0 &&
              object->frames[0].usHeight > 0);
        if (!valid) {
            if (optional_highlights && (index == 3 || index == 4)) {
                object.reset();
                continue;
            }
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid generic button graphics: %s",
                         std::string(filenames[index]).c_str());
            return {};
        }
    }
    const auto pixel = image->frames[0]->frames[8].paletteIndexAt(0, 0);
    if (!pixel) return {};
    image->fill_color = image->frames[0]->colors[*pixel];
    image->background_frame = background_frame;
    image->offset_x = offset_x;
    image->offset_y = offset_y;
    return image;
}
}

bool InitializeButtonImageManager()
{
    default_generic_image = LoadGenericGraphics(
        {DEFAULT_GENERIC_BUTTON_OFF, DEFAULT_GENERIC_BUTTON_ON, {},
         DEFAULT_GENERIC_BUTTON_OFF_HI, DEFAULT_GENERIC_BUTTON_ON_HI, {}}, 0, 0, 0, true);
    return default_generic_image != nullptr;
}

// FUNCTION: WIZ8 0x0040cb70
GenericButtonImage LoadGenericButtonImages(std::string_view gray, std::string_view off_normal,
                                          std::string_view off_hilite, std::string_view on_normal,
                                          std::string_view on_hilite, std::string_view background,
                                          std::int16_t index, std::int16_t offset_x, std::int16_t offset_y)
{
    return LoadGenericGraphics({off_normal, on_normal, gray, off_hilite, on_hilite, background},
                                index, offset_x, offset_y);
}

// FUNCTION: WIZ8 0x0040ce60
void ShutdownButtonImageManager(void)
{
    default_generic_image.reset();
}

static std::int8_t DisabledOverlayStyle(const ButtonAppearance& button, const ButtonVisualState& state)
{
    if (button.bDisabledStyle == DISABLED_STYLE_DEFAULT)
        return !state.checkbox && !button.string.empty()
            ? DISABLED_STYLE_SHADED : DISABLED_STYLE_HATCHED;
    return button.bDisabledStyle;
}

void DrawButtonAppearance(SDL_Surface& target, const ButtonAppearance& appearance,
                          const ButtonVisualState& state)
{
    const auto* b = &appearance;
    const auto disabled_style = b->quick_image ? DrawQuickButton(target, b, state) : DrawGenericButton(target, b, state);
    if (!b->string.empty()) DrawTextOnButton(target, b, state);
    if (disabled_style == DISABLED_STYLE_HATCHED) DrawHatchOnButton(target, b);
    else if (disabled_style == DISABLED_STYLE_SHADED) DrawShadeOnButton(target, b);
}

// FUNCTION: WIZ8 0x0040e060
static std::int8_t DrawQuickButton(SDL_Surface& target, const ButtonAppearance* b,
                                   const ButtonVisualState& state)
{
    std::int8_t disabled_style = DISABLED_STYLE_NONE;
    if (!b->quick_image) return disabled_style;
    const auto& image = *b->quick_image;
    const bool checkbox = state.checkbox;
    const bool pressed = state.pressed;
    std::int32_t frame = 0;
    if (state.enabled) {
        const bool highlighted = (state.hovered) && gfRenderHilights &&
                                 (!checkbox || state.mouse_down);
        const auto normal = pressed ? image.OnNormal : image.OffNormal;
        const auto hilite = pressed ? image.OnHilite : image.OffHilite;
        if (highlighted && hilite != BUTTON_NO_IMAGE) frame = hilite;
        else if (normal != BUTTON_NO_IMAGE) frame = normal;
    } else if (image.Grayed != BUTTON_NO_IMAGE) {
        frame = image.Grayed;
    } else {
        frame = checkbox ? (pressed ? image.OnHilite : image.OffHilite) : image.OffNormal;
        disabled_style = DisabledOverlayStyle(*b, state);
    }
    if (frame != BUTTON_NO_IMAGE)
        DrawSprite(target, image.object(), static_cast<std::size_t>(frame),
                   b->bounds.x, b->bounds.y);
    return disabled_style;
}

static void DrawHatchOnButton(SDL_Surface& target, const ButtonAppearance* b)
{
    const auto& bounds = b->bounds;
    const SDL_Rect area{bounds.x, bounds.y, bounds.w, bounds.h};
    HatchRect(target, area);
}

static void DrawShadeOnButton(SDL_Surface& target, const ButtonAppearance* b)
{
    const auto& bounds = b->bounds;
    const SDL_Rect area{bounds.x, bounds.y, bounds.w, bounds.h};
    ShadowRect(target, area);
}

// FUNCTION: WIZ8 0x0040e5d0
static void DrawTextOnButton(SDL_Surface& target, const ButtonAppearance* b,
                             const ButtonVisualState& state)
{
    std::int32_t text_x, text_y, button_width, button_height, text_origin_x, text_origin_y;
    SDL_Rect text_clip, previous_clip;
    const auto& bounds = b->bounds;

    // If this button actually has a string to print
    if (!b->string.empty()) {
        // Get the width and height of this button
        button_width = bounds.w;
        button_height = bounds.h;

        // Compute the viewable area on this button
        text_clip = {bounds.x + 3, bounds.y + 2, button_width - 6, button_height - 4};

        // Get the starting coordinates to print
        text_origin_x = text_clip.x;
        text_origin_y = text_clip.y;

        // Get the current clipping area and clip the viewable area to it
        SDL_GetSurfaceClipRect(&target, &previous_clip);
        if (!SDL_GetRectIntersection(&text_clip, &previous_clip, &text_clip))
            return;
        ScopedClipRect clipping(target, text_clip);

        // Compute the coordinates to center the text
        if (b->bTextYOffset == -1) {
            text_y = ((button_height - b->usFont->height()) / 2) + text_origin_y - 1;
        } else {
            text_y = bounds.y + b->bTextYOffset;
        }
        if (b->bTextXOffset == -1) {
            switch (b->bJustification) {
            case BUTTON_TEXT_LEFT:
                text_x = text_origin_x + 3;
                break;
            case BUTTON_TEXT_RIGHT:
                text_x =
                    (text_clip.x + text_clip.w) - b->usFont->textWidth(b->string) - 3;
                break;
            case BUTTON_TEXT_CENTER:
            default:
                text_x = (((button_width - 6) - b->usFont->textWidth(b->string)) / 2) +
                         text_origin_x;
                break;
            }
        } else {
            text_x = bounds.x + b->bTextXOffset;
        }

        // Set the printing font to the button text font
        TextOptions options;
        options.colors.background = b->usFont->color(FONT_MCOLOR_BLACK);
        options.colors.foreground = b->usFont->color(static_cast<std::uint8_t>(b->sForeColor));
        if (b->sShadowColor != -1)
            options.colors.shadow = b->usFont->color(static_cast<std::uint8_t>(b->sShadowColor));
        if (options.colors.shadow == 0 && b->sShadowColor > 0)
            options.colors.shadow = 1;
        //Override the colors if necessary.
        if (state.enabled && state.hovered &&
            b->sForeColorHilited != -1) {
            options.colors.foreground =
                b->usFont->color(static_cast<std::uint8_t>(b->sForeColorHilited));
        } else if (state.pressed && b->sForeColorDown != -1) {
            options.colors.foreground =
                b->usFont->color(static_cast<std::uint8_t>(b->sForeColorDown));
        }
        if (state.enabled && state.hovered &&
            b->sShadowColorHilited != -1) {
            options.colors.shadow =
                b->usFont->color(static_cast<std::uint8_t>(b->sShadowColorHilited));
        } else if (state.pressed && b->sShadowColorDown != -1) {
            options.colors.shadow =
                b->usFont->color(static_cast<std::uint8_t>(b->sShadowColorDown));
        }
        if (options.colors.shadow == 0 &&
            ((state.enabled && state.hovered && b->sShadowColorHilited > 0) ||
             (state.pressed && b->sShadowColorDown > 0) || b->sShadowColor > 0))
            options.colors.shadow = 1;
        if (state.pressed &&
            b->fShiftText) { // Was the button clicked on? if so, move the text slightly for the illusion
            // that the text moved into the screen.
            text_x++;
            text_y++;
        }
        options.mono = !b->fMultiColor;
        DrawText(target, *b->usFont, {text_x, text_y}, b->string, options);
        // Restore the old text printing settings
    }
}

// FUNCTION: WIZ8 0x0040e890
static std::int8_t DrawGenericButton(SDL_Surface& target, const ButtonAppearance* b,
                                     const ButtonVisualState& state)
{
    std::int8_t disabled_style = DISABLED_STYLE_NONE;
    if (!b->generic_image) return disabled_style;
    std::int32_t NumChunksWide, NumChunksHigh, cx, cy, width, height, hremain, wremain;
    std::int32_t q, ImgNum, ox, oy;
    std::int32_t iBorderHeight, iBorderWidth;
    std::size_t picture_index = 0;

    // Select the graphics to use depending on the current state of the button
    if (state.enabled) {
        if (state.pressed) {
            if ((state.hovered) &&
                (b->generic_image->frames[4] != nullptr) && gfRenderHilights)
                picture_index = 4;
            else
                picture_index = 1;
        } else {
            if ((state.hovered) &&
                (b->generic_image->frames[3] != nullptr) && gfRenderHilights)
                picture_index = 3;
            else
                picture_index = 0;
        }
    } else if (b->generic_image->frames[2])
        picture_index = 2;
    else {
        picture_index = 0;
        disabled_style = DisabledOverlayStyle(*b, state);
    }

    const auto& BPic = *b->generic_image->frames[picture_index];
    const auto& first_frame = BPic.frames[0];
    iBorderHeight = first_frame.usHeight;
    iBorderWidth = first_frame.usWidth;

    // Compute the number of button "chunks" needed to be blitted
    width = b->bounds.w;
    height = b->bounds.h;
    NumChunksWide = width / iBorderWidth;
    NumChunksHigh = height / iBorderHeight;
    hremain = height % iBorderHeight;
    wremain = width % iBorderWidth;

    cx = (b->bounds.x + ((NumChunksWide - 1) * iBorderWidth) + wremain);
    cy = (b->bounds.y + ((NumChunksHigh - 1) * iBorderHeight) + hremain);

    const SDL_Rect button_area{b->bounds.x, b->bounds.y, b->bounds.w, b->bounds.h};
    SDL_FillSurfaceRect(&target, &button_area, b->generic_image->fill_color);

    // If there is a background image, fill the button's area with it
    if (b->generic_image->frames[5] != nullptr) {
        ox = oy = 0;
        // if the button was clicked on, adjust the background image so that we get
        // the illusion that it is sunk into the screen.
        if (state.pressed)
            ox = oy = 1;

        const SDL_Rect background_area{b->bounds.x + ox, b->bounds.y + oy,
                                       b->bounds.w - ox, b->bounds.h - oy};
        TileSprite(target, background_area, *b->generic_image->frames[5],
                   b->generic_image->background_frame,
                   {b->generic_image->offset_x, b->generic_image->offset_y});
    }

    const auto draw_frame = [&](std::size_t frame, int x, int y) {
        DrawSprite(target, BPic, frame, x, y);
    };

    // Draw the button's borders and corners (horizontally)
    for (q = 0; q < NumChunksWide; q++) {
        if (q == 0)
            ImgNum = 0;
        else
            ImgNum = 1;

        draw_frame(static_cast<std::size_t>(ImgNum),
                   b->bounds.x + q * iBorderWidth, b->bounds.y);

        if (q == 0)
            ImgNum = 5;
        else
            ImgNum = 6;

        draw_frame(static_cast<std::size_t>(ImgNum), b->bounds.x + q * iBorderWidth, cy);
    }
    // Blit the right side corners
    draw_frame(2, cx, b->bounds.y);
    draw_frame(7, cx, cy);
    // Draw the vertical members of the button's borders
    NumChunksHigh--;

    if (hremain != 0) {
        q = NumChunksHigh;
        const int y = b->bounds.y + q * iBorderHeight - (iBorderHeight - hremain);
        draw_frame(3, b->bounds.x, y);
        draw_frame(4, cx, y);
    }

    for (q = 1; q < NumChunksHigh; q++) {
        const int y = b->bounds.y + q * iBorderHeight;
        draw_frame(3, b->bounds.x, y);
        draw_frame(4, cx, y);
    }

    return disabled_style;
}
