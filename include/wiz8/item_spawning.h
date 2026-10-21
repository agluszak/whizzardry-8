#pragma once

#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/world_item.h"
#include "wiz8/entity_link.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/rules/item_instance.h"

class Trigger;

/* Bit masks in W8WorldItem::flags; preserve combinations in unsigned int. */
enum {
    W8_WORLD_ITEM_HIDDEN = 0x01u,
    W8_WORLD_ITEM_FALLING = 0x02u,
    W8_WORLD_ITEM_RADAR_BLIP_LIT = 0x04u
};

/* A world item. Saves store it as wiz8::retail::WorldItem. */
struct W8WorldItem {
    int runtime_id;      /* 0x00 */
    // The scene entity, deactivated before the world record is removed.
    EntityLink<WorldItemEntity> entity;
    /* The level trigger placed with this item. The record keeps it, not the scene entity,
       because the entity is rebuilt each time the item comes back into range. The trigger
       belongs to the same world and outlives the record. */
    Trigger* trigger = nullptr;
    bool fActive;        /* 0x08: assertion-proven pItemInfo->fActive */
    W8ItemInstance item; /* 0x09 */
    glm::vec3 position; /* 0x15 */
    unsigned char unknown_21[4];
    /* Saved world item flag word; level restore clears RADAR_SEEN. */
    int entity_flags;
    unsigned int flags; /* 0x29: W8_WORLD_ITEM_* masks */
    int saved_marker;
    std::vector<std::unique_ptr<W8WorldItem>> contents;
    /* 0x35: vertical velocity while flag bit 1 (falling) is set; cleared on
       ground settle. */
    float vertical_velocity;
    int sector_id; /* 0x39 */
    unsigned char unknown_3d[0x70];
};

std::unique_ptr<W8WorldItem> CreateWorldItem(W8ItemInstance* item, const glm::vec3* position,
                                             int entity_flags);
W8WorldItem* AddWorldItem(std::unique_ptr<W8WorldItem> item);
bool SettleWorldItem(W8WorldItem* item);
std::unique_ptr<W8WorldItem> SpawnItem(int item_id, const glm::vec3* position, int entity_flags);

bool ItemHasFlags(const W8WorldItem* item, unsigned int mask);
void SetItemFlags(W8WorldItem* item, unsigned int mask, bool enabled);
void SetItemAndEntityFlags(W8WorldItem* item, unsigned int mask, bool enabled);
bool ItemInfoIsWorldPersistent(const W8WorldItem* item);
void ActivateItem(W8WorldItem* item);
int FindItemRecordByName(const char* name);
