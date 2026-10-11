#ifndef WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H
#define WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H

#include <cstddef>
#include <optional>
#include <vector>

class W8ItemVideoObjectCache {
public:
    W8ItemVideoObjectCache() = default;
    W8ItemVideoObjectCache(const W8ItemVideoObjectCache&) = delete;
    W8ItemVideoObjectCache& operator=(const W8ItemVideoObjectCache&) = delete;
    void Initialize(std::size_t item_count);
    void Clear();
    int GetOrCreateVideoObject(int item_id);
    std::size_t LoadedImageCount() const;

private:
    std::vector<std::optional<int>> images_;
};

extern W8ItemVideoObjectCache g_item_video_objects;

#endif
