#ifndef WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H
#define WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H

#include <cstddef>
#include <memory>
#include <vector>

struct SpriteSheet;

class W8ItemVideoObjectCache {
public:
    W8ItemVideoObjectCache() = default;
    W8ItemVideoObjectCache(const W8ItemVideoObjectCache&) = delete;
    W8ItemVideoObjectCache& operator=(const W8ItemVideoObjectCache&) = delete;
    ~W8ItemVideoObjectCache() = default;
    void Initialize(std::size_t item_count);
    void Clear();
    const SpriteSheet& Sprites(int item_id);
    std::shared_ptr<const SpriteSheet> SharedSprites(int item_id);
    std::size_t LoadedImageCount() const;

private:
    std::vector<std::shared_ptr<const SpriteSheet>> images_;
};

extern W8ItemVideoObjectCache g_item_video_objects;

#endif
