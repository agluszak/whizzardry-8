#pragma once

struct WorldItemEntity;
struct W8WorldItem;
struct W8ItemInstance;
struct W8MonsterInfo;

/* When set, world items flagged invisible are not activated;
   MIPE's item-create 'A' key toggles it ("hidden" versus "blue"). */

W8WorldItem* ItemInfo(unsigned int item_list_index);
unsigned int ItemIndex(int runtime_id);

int PickNearestItemUnderCursor(int cursor_x, int cursor_y, float max_distance);
unsigned char InteractWithWorldItem(int runtime_id);
void SetWorldItemFalling(W8WorldItem* item, bool enabled);
void RebuildAllWorldItemInstances(void);

void ReleaseItemLists();

bool AnyWorldItemVisible(void);
void DropMonsterLoot(W8MonsterInfo* monster_info, int value);
