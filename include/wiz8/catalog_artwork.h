#ifndef WIZ8_CATALOG_ARTWORK_H
#define WIZ8_CATALOG_ARTWORK_H
#include <memory>
#include <utility>
#include <variant>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "sprite.h"

// Whether a bitmap draw skips transparent (zero) pixels; sprites carry their own transparency.
enum class BitmapKey : bool { Opaque, Transparent };

class CatalogArtwork {
public:
    static CatalogArtwork Load(int object, int frame = 0);
    void draw(SDL_Surface& target, int image, int x, int y, SpriteDraw options = {},
              BitmapKey key = BitmapKey::Transparent) const;
    void drawAndInvalidate(SDL_Surface& target, int image, int x, int y, SpriteDraw options = {},
                           BitmapKey key = BitmapKey::Transparent) const;
    void invalidate(int image, int x, int y, int flags) const;
    SDL_Point size(int image) const;
    SDL_Point position(int image) const;
    int imageOffset() const;
    void drawRect(SDL_Surface& target, const SDL_Rect& source, int x, int y) const;
    std::shared_ptr<const SpriteSheet> spriteSheet() const;
    std::shared_ptr<SDL_Surface> bitmap() const;

private:
    CatalogArtwork(std::shared_ptr<const SpriteSheet> sprites, int image_offset);
    CatalogArtwork(std::shared_ptr<SDL_Surface> bitmap, int image_offset);

    std::variant<std::shared_ptr<const SpriteSheet>, std::shared_ptr<SDL_Surface>> image_;
    int image_offset_ = 0;
};

std::pair<int, int> GetSpriteSize(const SpriteSheet& sprites, int image);
void DrawSpriteAndInvalidate(SDL_Surface& target, const SpriteSheet& sprites, int image,
                             int left, int top, SpriteDraw options = {});

void ReleaseCatalogArtwork();

#endif
