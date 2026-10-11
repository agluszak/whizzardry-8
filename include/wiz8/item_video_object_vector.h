#ifndef WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H
#define WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H

#include <cstddef>
#include <memory>
#include <vector>

struct SGPVObject;

class W8ItemVideoObjectCache {
public:
    W8ItemVideoObjectCache() = default;
    W8ItemVideoObjectCache(const W8ItemVideoObjectCache&) = delete;
    W8ItemVideoObjectCache& operator=(const W8ItemVideoObjectCache&) = delete;
    void Initialize(std::size_t item_count);
    void Clear();
    SGPVObject& Sprites(int item_id);
    std::size_t LoadedImageCount() const;

private:
    std::vector<std::shared_ptr<SGPVObject>> images_;
};

extern W8ItemVideoObjectCache g_item_video_objects;

#endif
