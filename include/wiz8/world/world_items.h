#pragma once

#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"

class Rng;
struct World;
struct W8WorldItem;
struct W8ItemInstance;

// The item records lying in one world and their placement in it: resting on the
// ground, listed by the collidable prop beneath them, and with a scene entity while
// they are near the camera. A world being loaded owns its items before it is published.
class WorldItems {
public:
    explicit WorldItems(World& world);
    ~WorldItems();
    WorldItems(const WorldItems&) = delete;
    WorldItems& operator=(const WorldItems&) = delete;

    const std::vector<std::unique_ptr<W8WorldItem>>& records() const { return items_; }
    W8WorldItem* find(int runtime_id) const;

    // Creation uses the live game or candidate build inputs without retaining them.
    // A record holding `item` (taken from it; none when null), settled below `position`.
    std::unique_ptr<W8WorldItem> create(int& next_item_id, W8ItemInstance* item,
                                        const glm::vec3& position, int entity_flags);
    // A record holding a newly rolled instance of `item_id`.
    std::unique_ptr<W8WorldItem> spawn(Rng& rng, int& next_item_id, int item_id,
                                       const glm::vec3& position, int entity_flags);
    W8WorldItem& add(std::unique_ptr<W8WorldItem> item);
    // Install restored records, which are not yet in a sector or the scene.
    void restore(std::vector<std::unique_ptr<W8WorldItem>> items);
    // Take a record out of its sector and the scene, then release it.
    void remove(int runtime_id);
    void clear();

    void activate(Rng& rng, W8WorldItem& item);
    void deactivate(W8WorldItem& item);
    // Drop the item onto the ground below it; false when there is no ground.
    bool settle(W8WorldItem& item);
    // Advance a falling item by one frame; false once it has landed.
    bool fall(W8WorldItem& item, float frame_seconds);

private:
    void moveToSector(W8WorldItem& item, int sector);
    void unplace(W8WorldItem& item);

    World& world_;
    std::vector<std::unique_ptr<W8WorldItem>> items_;
};
