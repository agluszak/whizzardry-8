#include "wiz8/dialog_code/ButtonAppearance.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/image_assets.h"
#include "wiz8/canvas/sprite.h"

static GenericButtonImage default_generic_image;

GenericButtonImage DefaultGenericButtonImage()
{
    return default_generic_image;
}

static bool DrawQuickButton(SDL_Surface& target, const ButtonAppearance* appearance,
                            const SDL_Rect& bounds, const ButtonVisualState& state);
static bool DrawGenericButton(SDL_Surface& target, const ButtonAppearance* appearance,
                              const SDL_Rect& bounds, const ButtonVisualState& state);
static void DrawTextOnButton(SDL_Surface& target, const ButtonAppearance* appearance,
                             const SDL_Rect& bounds, const ButtonVisualState& state);

ButtonImage ButtonGraphics::Load(std::string_view filename, std::int32_t grayed,
                                 std::int32_t off_normal, std::int32_t off_hilite,
                                 std::int32_t on_normal, std::int32_t on_hilite)
{
    if (filename.empty()) return {};
    return Load(g_image_assets.sprites(filename), grayed, off_normal, off_hilite, on_normal,
                on_hilite);
}

ButtonImage ButtonGraphics::Load(std::shared_ptr<const SpriteSheet> sprites, std::int32_t grayed,
                                 std::int32_t off_normal, std::int32_t off_hilite,
                                 std::int32_t on_normal, std::int32_t on_hilite)
{
    const std::array frames{grayed, off_normal, off_hilite, on_normal, on_hilite};
    if (!sprites || std::all_of(frames.begin(), frames.end(),
                                [](int frame) { return frame == BUTTON_NO_IMAGE; }))
        return {};

    auto image = std::make_shared<ButtonGraphics>();
    image->sprites = std::move(sprites);
    for (const int index : frames) {
        if (index == BUTTON_NO_IMAGE) continue;
        if (index < 0 || static_cast<std::size_t>(index) >= image->object().frames.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid button frame %d", index);
            return {};
        }
        const auto& frame = image->object().frames[index];
        image->MaxWidth = std::max(
            image->MaxWidth, static_cast<std::uint32_t>(std::max(0, frame.width + frame.offset.x)));
        image->MaxHeight =
            std::max(image->MaxHeight,
                     static_cast<std::uint32_t>(std::max(0, frame.height + frame.offset.y)));
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
                                       std::int16_t background_frame, std::int16_t offset_x,
                                       std::int16_t offset_y, bool optional_highlights = false)
{
    if (filenames[0].empty() || filenames[1].empty()) return {};
    auto image = std::make_shared<GenericButtonImages>();
    for (std::size_t index = 0; index < filenames.size(); ++index) {
        if (filenames[index].empty()) continue;
        auto& object = image->frames[index];
        object = g_image_assets.sprites(filenames[index]);
        const bool valid =
            object &&
            (index == 5 ? background_frame >= 0 &&
                              static_cast<std::size_t>(background_frame) < object->frames.size()
                        : object->frames.size() >= 9 && object->frames[0].width > 0 &&
                              object->frames[0].height > 0);
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
    default_generic_image = LoadGenericGraphics({DEFAULT_GENERIC_BUTTON_OFF,
                                                 DEFAULT_GENERIC_BUTTON_ON,
                                                 {},
                                                 DEFAULT_GENERIC_BUTTON_OFF_HI,
                                                 DEFAULT_GENERIC_BUTTON_ON_HI,
                                                 {}},
                                                0, 0, 0, true);
    return default_generic_image != nullptr;
}

GenericButtonImage LoadGenericButtonImages(std::string_view gray, std::string_view off_normal,
                                           std::string_view off_hilite, std::string_view on_normal,
                                           std::string_view on_hilite, std::string_view background,
                                           std::int16_t index, std::int16_t offset_x,
                                           std::int16_t offset_y)
{
    return LoadGenericGraphics({off_normal, on_normal, gray, off_hilite, on_hilite, background},
                               index, offset_x, offset_y);
}

void ShutdownButtonImageManager(void)
{
    default_generic_image.reset();
}

void DrawButtonAppearance(SDL_Surface& target, const ButtonAppearance& appearance,
                          const SDL_Rect& bounds, const ButtonVisualState& state)
{
    const bool overlay = appearance.quick_image
                             ? DrawQuickButton(target, &appearance, bounds, state)
                             : DrawGenericButton(target, &appearance, bounds, state);
    if (!appearance.string.empty()) DrawTextOnButton(target, &appearance, bounds, state);
    if (overlay) {
        if (appearance.string.empty())
            HatchRect(target, bounds);
        else
            ShadowRect(target, bounds);
    }
}

static bool DrawQuickButton(SDL_Surface& target, const ButtonAppearance* b, const SDL_Rect& bounds,
                            const ButtonVisualState& state)
{
    bool overlay = false;
    if (!b->quick_image) return overlay;
    const auto& image = *b->quick_image;
    const bool pressed = state.pressed;
    std::int32_t frame = state.menu_artwork ? BUTTON_NO_IMAGE : 0;
    if (state.enabled) {
        const auto normal = pressed ? image.OnNormal : image.OffNormal;
        const auto hilite = pressed ? image.OnHilite : image.OffHilite;
        if (state.hovered && hilite != BUTTON_NO_IMAGE)
            frame = hilite;
        else if (normal != BUTTON_NO_IMAGE)
            frame = normal;
    } else if (image.Grayed != BUTTON_NO_IMAGE) {
        frame = image.Grayed;
    } else {
        frame = image.OffNormal;
        overlay = true;
    }
    if (frame != BUTTON_NO_IMAGE) {
        SDL_Point origin{bounds.x, bounds.y};
        if (state.menu_artwork && state.hovered) {
            const auto& sprite = image.object().frames[frame];
            origin.x += std::max(0, sprite.width - bounds.w) / 2;
            origin.y += std::max(0, sprite.height - bounds.h) / 2;
        }
        DrawSprite(target, image.object(), static_cast<std::size_t>(frame), origin.x, origin.y);
    }
    return overlay;
}

static void DrawTextOnButton(SDL_Surface& target, const ButtonAppearance* b, const SDL_Rect& bounds,
                             const ButtonVisualState& state)
{
    SDL_Rect clip{bounds.x + 3, bounds.y + 2, bounds.w - 6, bounds.h - 4};
    SDL_Rect previous;
    SDL_GetSurfaceClipRect(&target, &previous);
    if (!SDL_GetRectIntersection(&clip, &previous, &clip)) return;
    ScopedClipRect clipping(target, clip);
    SDL_Point origin{bounds.x + 3 + (bounds.w - 6 - b->usFont->textWidth(b->string)) / 2,
                     bounds.y + 1 + (bounds.h - b->usFont->height()) / 2};
    if (state.pressed) {
        ++origin.x;
        ++origin.y;
    }
    TextOptions options;
    // Palette entry 0 is the font's colour key; a zero background leaves the button art visible.
    options.colors.background = 0;
    options.colors.foreground = b->usFont->color(static_cast<std::uint8_t>(b->sForeColor));
    if (b->sShadowColor != -1)
        options.colors.shadow = b->usFont->color(static_cast<std::uint8_t>(b->sShadowColor));
    if (options.colors.shadow == 0 && b->sShadowColor > 0) options.colors.shadow = 1;
    options.mono = !b->fMultiColor;
    DrawText(target, *b->usFont, origin, b->string, options);
}

static bool DrawGenericButton(SDL_Surface& target, const ButtonAppearance* b,
                              const SDL_Rect& bounds, const ButtonVisualState& state)
{
    if (!b->generic_image) return false;
    const auto& image = *b->generic_image;
    const bool overlay = !state.enabled && !image.frames[2];
    const int normal = state.pressed ? 1 : 0;
    const int hover = state.pressed ? 4 : 3;
    const int frame = !state.enabled                         ? (image.frames[2] ? 2 : 0)
                      : state.hovered && image.frames[hover] ? hover
                                                             : normal;
    const auto& sprites = *image.frames[frame];
    const int width = sprites.frames[0].width;
    const int height = sprites.frames[0].height;
    const int columns = bounds.w / width;
    const int rows = bounds.h / height;
    const int right = bounds.x + bounds.w - width;
    const int bottom = bounds.y + bounds.h - height;
    SDL_FillSurfaceRect(&target, &bounds, image.fill_color);
    if (image.frames[5]) {
        const int inset = state.pressed ? 1 : 0;
        TileSprite(target, {bounds.x + inset, bounds.y + inset, bounds.w - inset, bounds.h - inset},
                   *image.frames[5], image.background_frame, {image.offset_x, image.offset_y});
    }
    for (int column = 0; column < columns; ++column) {
        DrawSprite(target, sprites, column == 0 ? 0 : 1, bounds.x + column * width, bounds.y);
        DrawSprite(target, sprites, column == 0 ? 5 : 6, bounds.x + column * width, bottom);
    }
    DrawSprite(target, sprites, 2, right, bounds.y);
    DrawSprite(target, sprites, 7, right, bottom);
    if (bounds.h % height != 0) {
        DrawSprite(target, sprites, 3, bounds.x, bottom - height);
        DrawSprite(target, sprites, 4, right, bottom - height);
    }
    for (int row = 1; row < rows - 1; ++row) {
        DrawSprite(target, sprites, 3, bounds.x, bounds.y + row * height);
        DrawSprite(target, sprites, 4, right, bounds.y + row * height);
    }
    return overlay;
}
