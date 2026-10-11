#ifndef WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H
#define WIZ8_ITEM_VIDEO_OBJECT_VECTOR_H

#include <memory>

struct W8ItemVideoObjectEntry {
    bool initialized;
    int video_object;

    W8ItemVideoObjectEntry();
    ~W8ItemVideoObjectEntry() = default;
};

class W8ItemVideoObjectCache {
public:
    W8ItemVideoObjectCache() : capacity(0), loaded_count(0) {}
    ~W8ItemVideoObjectCache() = default;
    void Initialize(int capacity);
    void Clear();
    int GetOrCreateVideoObject(int item_id);

    /* The methods share this receiver, including the counter at +8.
       A nested srArray identity is unproved: retail teardown leaves capacity
       untouched, unlike the canonical srArray destructor. */
    std::unique_ptr<W8ItemVideoObjectEntry[]> data;
    int capacity;
    int loaded_count;
};

extern W8ItemVideoObjectCache g_item_video_objects;

#endif
