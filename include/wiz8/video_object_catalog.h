#ifndef WIZ8_VIDEO_OBJECT_CATALOG_H
#define WIZ8_VIDEO_OBJECT_CATALOG_H
#include <memory>
#include <utility>
#include <variant>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "sprite.h"

class CatalogArtwork {
public:
    static CatalogArtwork Load(int object, int frame = 0);
    void draw(SDL_Surface& target, int image, int x, int y, SpriteDraw options = {}) const;
    void drawAndInvalidate(SDL_Surface& target, int image, int x, int y,
                           SpriteDraw options = {}) const;
    void invalidate(int image, int x, int y, int flags) const;
    SDL_Point size(int image) const;
    SDL_Point position(int image) const;
    short imageOffset() const;
    void drawRect(SDL_Surface& target, const SDL_Rect& source, int x, int y) const;
    std::shared_ptr<const SpriteSheet> spriteSheet() const;
    std::shared_ptr<SDL_Surface> bitmap() const;

private:
    CatalogArtwork(std::shared_ptr<const SpriteSheet> sprites, short image_offset);
    CatalogArtwork(std::shared_ptr<SDL_Surface> bitmap, short image_offset);

    std::variant<std::shared_ptr<const SpriteSheet>, std::shared_ptr<SDL_Surface>> image_;
    short image_offset_ = 0;
};

std::pair<int, int> GetSpriteSize(const SpriteSheet& sprites, int image);
void DrawSpriteAndInvalidate(SDL_Surface& target, const SpriteSheet& sprites, int image,
                             int left, int top, SpriteDraw options = {});

void ReleaseLoadedVideoFrames(void);

#endif
