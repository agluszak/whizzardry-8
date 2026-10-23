#pragma once

#include <optional>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"

struct WorldItemEntity;
struct W8WorldItem;
struct W8ItemInstance;
struct W8MonsterInfo;

/* When set, world items flagged invisible are not activated;
   MIPE's item-create 'A' key toggles it ("hidden" versus "blue"). */
extern bool g_hide_invisible_items;

int PickNearestItemUnderCursor(int cursor_x, int cursor_y, float max_distance);
unsigned char InteractWithWorldItem(int runtime_id);
void SetWorldItemFalling(W8WorldItem* item, bool enabled);
void RebuildAllWorldItemInstances(void);

std::optional<glm::vec3> FindItemDropPosition();
void DropHeldItem(int);
void UpdateNearbyWorldItems(void);
bool AnyWorldItemVisible(void);
void DropMonsterLoot(W8MonsterInfo* monster_info, int value);

/* World-space throw vector from normalized screen coordinates. */
glm::vec3 ItemThrowDirection(const glm::vec2& cursor, const glm::mat3& camera_rotation);
