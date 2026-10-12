#pragma once

#include <cstdint>
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"

struct W8Item;
struct W8WorldItem;
struct W8ItemInstance;
struct W8MonsterInfo;
template <class T> class srVector3T;

/* When set, world items flagged invisible are not activated;
   MIPE's item-create 'A' key toggles it ("hidden" versus "blue"). */
extern bool g_hide_invisible_items;

W8WorldItem* ItemInfo(std::uint32_t item_list_index);
void DeactivateWorldItem(W8WorldItem* item);
std::uint32_t ItemIndex(std::int32_t runtime_id);
void SetWorldItemHighlight(std::int32_t runtime_id, bool on);
std::int32_t PickNearestItemUnderCursor(std::int32_t cursor_x, std::int32_t cursor_y, float max_distance);
std::uint8_t InteractWithWorldItem(std::int32_t runtime_id);
W8ItemInstance* CopyWorldItemInstance(const W8WorldItem* item);
void SetWorldItemFalling(W8WorldItem* item, bool enabled);
void RebuildAllWorldItemInstances(void);

bool InitializeItemManagerState();

bool ReleaseItemLists(void);

extern std::int32_t g_world_item_cursor;

void DropHeldItem(std::int32_t);
void UpdateNearbyWorldItems(void);
bool AdvanceFallingWorldItem(W8WorldItem* item);
bool IsWorldItemWithinReach(W8Item* owner, const srVector3T<float>* from, float radius);
bool AnyWorldItemVisible(void);
void DropMonsterLoot(W8MonsterInfo* monster_info, std::int32_t value);

/* World-space throw vector from normalized screen coordinates. */
glm::vec3 ItemThrowDirection(const glm::vec2& cursor, const glm::mat3& camera_rotation);
