#include "wiz8/local_code/ItemManager.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <glm/common.hpp> // NOLINT(misc-include-cleaner)
#include <glm/geometric.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/trigonometric.hpp> // NOLINT(misc-include-cleaner)

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/npc_state.h"
#include "wiz8/cursor.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_tables.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

constexpr float kDropItemPitchScale = 71.0f;
constexpr float kDropItemYawScale = 85.0f;

class W8DialogBase;

// FUNCTION: WIZ8 0x004f8130
bool ItemHasFlags(const W8WorldItem* item, unsigned int mask)
{
    assert(item != 0);
    return (item->flags & mask) != 0;
}

void SetItemFlags(W8WorldItem* item, unsigned int mask, bool enabled)
{
    assert(item != 0);
    if (enabled) {
        item->flags |= mask;
    } else {
        item->flags &= ~mask;
    }
}

void SetItemAndEntityFlags(W8WorldItem* item, unsigned int mask, bool enabled)
{
    assert(item != 0);
    if (enabled) {
        item->entity_flags |= mask;
    } else {
        item->entity_flags &= ~mask;
    }
    if (item->entity) {
        item->entity->SetFlags(mask, enabled);
    }
}

struct W8ItemLevelScaleRange {
    unsigned int minimum_party_level;
    unsigned int minimum_item_value;
    unsigned int maximum_item_value;
};

static W8ItemLevelScaleRange g_item_level_scale_ranges[7] = {
    {1, 0, 500},      {6, 50, 1000},    {11, 100, 3000},     {16, 300, 5000},
    {21, 600, 10000}, {26, 800, 20000}, {31, 1000, 1000000},
};

int FindItemTableByName(const char* name)
{
    int index;

    for (index = 0; index < static_cast<int>(Data().item_tables.size()); ++index) {
        if (SDL_strcasecmp(name, Data().item_tables[index].name) == 0) {
            return index;
        }
    }
    return -1;
}

int GenerateItemsFromTable(std::vector<std::unique_ptr<W8WorldItem>>& output_items,
                           unsigned int table_id, unsigned int maximum_items,
                           const glm::vec3& position)
{
    unsigned int party_level = CurrentGame().party.averageLevel();
    std::vector<int> candidates;
    const W8ItemTableRecord* table;
    const W8ItemTableEntry* entry;
    const W8ItemDatabaseRecord* item_record;
    const W8ItemLevelScaleRange* range;
    unsigned int item_value;
    unsigned int total_weight;
    unsigned int selected_count;
    unsigned int random_value;
    int entry_index;
    int position_index;

    table_id &= 0xffff;
    for (entry_index = 0; entry_index < 40; ++entry_index) {
        table = &Data().item_tables[table_id];
        if (table->entries[entry_index].selector != 0) {
            item_record = &Data().items[table->entries[entry_index].item_id];
            if (table->entries[entry_index].weight == 0) {
                output_items.push_back(CurrentGame().spawnWorldItem(
                    CurrentGame().level.loaded(),
                    Data().item_tables[table_id].entries[entry_index].item_id, &position,
                    W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            } else if (table->level_scaled == 0) {
                candidates.push_back(entry_index);
            } else {
                item_value = item_record->value;
                for (range = g_item_level_scale_ranges; range < g_item_level_scale_ranges + 7;
                     ++range) {
                    if (range->minimum_party_level <= party_level &&
                        range->minimum_item_value <= item_value &&
                        item_value < range->maximum_item_value) {
                        candidates.push_back(entry_index);
                        break;
                    }
                }
            }
        }
    }

    if (static_cast<int>(candidates.size()) < static_cast<int>(maximum_items) &&
        Data().item_tables[table_id].level_scaled != 0) {
        candidates.clear();
        for (entry_index = 0; entry_index < 40; ++entry_index) {
            if (Data().item_tables[table_id].entries[entry_index].selector != 0) {
                candidates.push_back(entry_index);
            }
        }
    }

    total_weight = 0;
    for (entry_index = 0; entry_index < static_cast<int>(candidates.size()); ++entry_index) {
        total_weight += Data().item_tables[table_id].entries[candidates[entry_index]].weight;
    }

    selected_count = 0;
    while (selected_count < maximum_items) {
        if (candidates.empty()) {
            break;
        }
        position_index = CurrentGame().rng.below(static_cast<int>(candidates.size()));
        entry_index = candidates[position_index];
        table = &Data().item_tables[table_id];
        entry = &table->entries[entry_index];
        random_value = CurrentGame().rng.below(total_weight);
        if (random_value <= entry->weight) {
            output_items.push_back(CurrentGame().spawnWorldItem(
                CurrentGame().level.loaded(), entry->item_id, &position,
                W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            candidates.erase(candidates.begin() + position_index);
            ++selected_count;
        }
    }

    return static_cast<int>(output_items.size());
}

/* Look an item record up by its internal name. The name sits at 0x8d in the
   record; a record whose name is empty is matched against the display name
   converted down from wide instead. */
int FindItemRecordByName(const GameData& data, const char* name)
{
    int index;
    std::string internal_name;

    for (index = 0; index < static_cast<int>(data.items.size()); ++index) {
        internal_name = data.items[index].internal_name;
        if (internal_name[0] == 0) {
            if (SDL_strcasecmp(data.items[index].display_name.c_str(), name) == 0) {
                return index;
            }
        } else if (CompareTextIgnoreAsciiCase(internal_name, name) == 0) {
            return index;
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x004f91e0
bool ItemInfoIsWorldPersistent(const W8WorldItem* item)
{
    if (item == 0) {
        return false;
    }
    return (Data().items[item->item.iItemNo].flags & W8_ITEM_FLAG_CONTAINER) != 0;
}

void SetWorldItemFalling(W8WorldItem* item, bool enabled)
{
    if (enabled) {
        item->flags |= W8_WORLD_ITEM_FALLING;
    } else {
        item->flags &= ~W8_WORLD_ITEM_FALLING;
    }
}

/* The world item at one list position. */
W8WorldItem* ItemInfo(unsigned int item_list_index)
{
    W8WorldItem* item;

    assert(item_list_index < CurrentLevelPopulation().world_items.size());
    item = GetWorldItemListEntry(item_list_index);
    assert(item != 0);
    return item;
}

unsigned int ItemIndex(int runtime_id)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().world_items.size(); ++index) {
        if (ItemInfo(index)->runtime_id == runtime_id) {
            return index;
        }
    }
    assert(false && "unknown world item id");
    return 0;
}

/* The runtime id of the nearest active world item the cursor is hovering over
   and whose screen distance stays inside `max_distance`, or -1. The cursor
   coordinates are carried but unused - the hover test is IsSelected. */
int PickNearestItemUnderCursor(int cursor_x, int cursor_y, float max_distance)
{
    int result;
    float best_distance;
    unsigned int index;

    result = -1;
    best_distance = 999999.0f;
    for (index = 0; index < CurrentLevelPopulation().world_items.size(); ++index) {
        W8WorldItem* item = ItemInfo(index);
        float distance;

        if (!item->entity) {
            continue;
        }
        if (item->entity->flags & W8_ITEM_ENTITY_NO_PICKUP) {
            continue;
        }
        if (GetWorld()->selectedModel() != item->entity->GetMesh()) {
            continue;
        }
        distance = ItemDistanceToCamera(*item->entity);
        if (distance < max_distance && distance < best_distance) {
            result = item->runtime_id;
            best_distance = distance;
        }
    }
    return result;
}

glm::vec3 ItemThrowDirection(const glm::vec2& cursor, const glm::mat3& camera_rotation)
{
    const auto offset = (cursor - 0.5f) * 0.9f;
    const float yaw = glm::radians(offset.x * kDropItemYawScale);
    const float pitch = glm::radians(5.0f + offset.y * kDropItemPitchScale);
    const auto forward = glm::vec3(0, 0, 2500.0);
    return camera_rotation * glm::rotateY(glm::rotateX(forward, pitch), yaw);
}

/* Toss the cursor-held item into the world. The tracked cursor position is
   turned into a 2500-unit direction through the camera rotation, traced to
   the first obstruction, shortened by the item radius, settled onto the
   ground and lifted back up. When the spot is free the held instance becomes
   a world item; otherwise the refusal sound plays. Without a loaded level there is no
   drop position. */
std::optional<glm::vec3> FindItemDropPosition()
{
    auto* const index = GetWorldSpatialIndex();
    if (!index) return std::nullopt;
    glm::mat3 rotation;
    rotation = GetWorld()->party.rotation();
    glm::vec3 direction = ItemThrowDirection(GetCursorScaledPosition(), rotation);

    glm::vec3 camera = cameraLocation();
    direction += camera;
    if (const auto hit = index->traceSight(camera, direction)) direction = hit->point;

    glm::vec3 delta = direction - camera;
    delta = glm::dot(delta, delta) == 0
                ? delta
                : glm::normalize(delta) *
                      static_cast<float>(glm::max(glm::length(delta) - 125.0f, 0.0f));

    glm::vec3 position = camera + delta;
    position.y = index->settleToGround(position, true, 250).position.y + 125.0f;
    if (FindNearbyFreePosition(250.0f, &position, true, true) != 0) return position;
    return std::nullopt;
}

/* The item picker returns its remaining items as group contents. A two-item group returns the picked
   extra to the world at the group's position; a lone item is simply dropped.
   Either way the group's own world entry is found by runtime id, pulled out
   of its sector, deactivated, freed and removed from the list. */
static void OnItemPickerClosed(W8WorldItem* group)
{
    W8WorldItem* item;
    unsigned int index;

    if (group->contents.size() == 1) {
        item = group->contents.front().get();
        item->position = group->position;
        if (!item->entity) {
            CurrentGame().level.loaded().activateItem(Data(), CurrentGame().rng, item);
        }
        CurrentGame().level.loaded().adoptItem(std::move(group->contents.front()));
        group->contents.clear();
    } else if (!group->contents.empty()) {
        return;
    }
    index = ItemIndex(group->runtime_id);
    item = ItemInfo(index);
    if (item->sector_id > -1) {
        CurrentGame().level.loaded().removeItemFromSector(item->sector_id, item);
    }
    if (item->entity) {
        item->deactivate(CurrentGame().events);
    }
    CurrentLevelPopulation().world_items.erase(CurrentLevelPopulation().world_items.begin() +
                                               index);
}

/* Interact with one world item by runtime id. Records flagged 0x20 open the
   trigger item-picker instead of being taken; otherwise the item's trigger
   runs, the instance copies into the cursor hand, and the world entry is
   stripped back out of its sector and the list. */
unsigned char InteractWithWorldItem(int runtime_id)
{
    unsigned char result = 1;
    int index = ItemIndex(runtime_id);
    W8WorldItem* item = ItemInfo(index);

    assert(item != 0);
    if (Data().items[item->item.iItemNo].flags & W8_ITEM_FLAG_CONTAINER) {
        auto dialog = std::make_unique<W8TriggerItemPickerDialog>();
        dialog->SetItemGroup(item);
        dialog->on_complete = OnItemPickerClosed;
        OpenModal(std::move(dialog));
        return 1;
    }
    if (Trigger* trigger = item->trigger) {
        trigger->Run(-1);
        if (trigger->action_state == W8TriggerActionState::inactive ||
            trigger->action_state == W8TriggerActionState::fallback_action) {
            return 0;
        }
    }
    if ((item->entity->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0) {
        CurrentGame().inventory.hold(item->item);
    }
    index = ItemIndex(runtime_id);
    item = ItemInfo(index);
    if (item->sector_id > -1) {
        CurrentGame().level.loaded().removeItemFromSector(item->sector_id, item);
    }
    if (item->entity) {
        item->deactivate(CurrentGame().events);
    }
    CurrentLevelPopulation().world_items.erase(CurrentLevelPopulation().world_items.begin() +
                                               index);
    return result;
}

/* Detach and release all world item groups. */
void ReleaseItemLists()
{
    for (const auto& owned : CurrentLevelPopulation().world_items) {
        auto* item = owned.get();
        if (item->sector_id >= 0)
            CurrentGame().level.loaded().removeItemFromSector(item->sector_id, item);
        if (item->entity) item->deactivate(CurrentGame().events);
    }
    CurrentLevelPopulation().world_items.clear();
}

/* plsItemList index that last satisfied AnyWorldItemVisible. */
static int g_last_visible_world_item = -1;

static bool IsWorldItemMarkerVisible(W8WorldItem* item, const glm::vec3& camera)
{
    if (item->entity) {
        glm::vec3 position;
        glm::vec3 eye;
        position = item->entity->GetLocation();
        eye = cameraLocation();
        glm::vec3 delta = position - camera;
        if (glm::length(delta) < static_cast<float>(2500.0)) {
            auto bounds = item->entity->GetCachedLocalBounds();
            bounds.minimum += position;
            bounds.maximum += position;
            return targetMarkerVisible(eye, bounds) != 0;
        }
    }
    return false;
}

/* Any live world item visible to the camera within 2500.0, resuming
   the scan at the last match. */
bool AnyWorldItemVisible(void)
{
    glm::vec3 camera;
    std::ptrdiff_t count;
    int index;

    if (GetWorld() == 0 || GetWorld()->camera == 0) {
        return false;
    }
    camera = cameraLocation();
    count = CurrentLevelPopulation().world_items.size();
    if (0 <= g_last_visible_world_item && g_last_visible_world_item < count) {
        W8WorldItem* item = GetWorldItemListEntry(g_last_visible_world_item);
        if (IsWorldItemMarkerVisible(item, camera)) {
            return true;
        }
    }
    for (index = 0; index < count; ++index) {
        W8WorldItem* item = GetWorldItemListEntry(index);

        if (IsWorldItemMarkerVisible(item, camera)) {
            g_last_visible_world_item = index;
            return true;
        }
    }
    return false;
}

/* Rebuild every world item's carried instance from its own item id, which
   re-rolls whatever the record decides rather than keeping what was there. */
void RebuildAllWorldItemInstances(void)
{
    unsigned int index;
    W8WorldItem* item;

    for (index = 0; index < CurrentLevelPopulation().world_items.size(); ++index) {
        item = ItemInfo(index);
        item->item = MakeItem(Data(), CurrentGame().rng, item->item.iItemNo, false, false, false);
    }
}

std::unique_ptr<W8WorldItem> Game::createWorldItem(World& world, W8ItemInstance* item,
                                                   const glm::vec3* position, int entity_flags)
{
    auto result = std::make_unique<W8WorldItem>();
    result->item = W8ItemInstance{.iItemNo = -1};
    result->runtime_id = status.next_world_item_id++;
    result->position = *position;
    result->sector_id = -1;
    world.settleItem(result.get());
    result->entity_flags = entity_flags;
    if (item) result->item = std::exchange(*item, W8ItemInstance{.iItemNo = -1});
    return result;
}

std::unique_ptr<W8WorldItem> Game::spawnWorldItem(World& world, int item_id,
                                                  const glm::vec3* position, int entity_flags)
{
    if (item_id == -1) return createWorldItem(world, nullptr, position, entity_flags);
    auto item = MakeItem(data, rng, item_id, false, false, false);
    return createWorldItem(world, &item, position, entity_flags);
}

/* Monster loot drop, driven by the death switch in MonsterManager. Non-NPC
   records fire their treasure slots (count gate, Random(100) chance, dice for
   the drop count; type 1 routes the id to GenerateItemsFromTable once per
   roll) and roll the gold dice. NPC-bound monsters empty their state item
   list instead: every zero-weight slot drops outright, then a weighted pick
   rolls item_count_dice items out of what remains; the NPC's gold field
   pays out directly. One drop is adopted into plsItemList at the death
   position; two or more are owned by a container spawned with
   spawnWorldItem - 0x23c by default, or value when the caller names one. */
void DropMonsterLoot(W8MonsterInfo* monster_info, int value)
{
    std::vector<std::unique_ptr<W8WorldItem>> items;
    if (monster_info != 0) {
        const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
        glm::vec3 position = monster_info->entity->nav.GetPosition();
        std::unique_ptr<W8WorldItem> item;
        int gold = 0;
        int slot;
        int roll;
        int index;
        unsigned int remaining;
        int total_weight;
        unsigned int take;
        unsigned int taken;
        int pick;
        if ((record->flags & W8_MONSTER_FLAG_NPC) == 0) {
            for (slot = 0; slot < 8; ++slot) {
                const W8MonsterTreasureEntry* entry = &record->treasure.slots[slot];
                if (entry->count != 0 && CurrentGame().rng.below(100) <= entry->chance) {
                    int rolls = RollDice(&entry->dice);
                    for (roll = 0; roll < rolls; ++roll) {
                        if (entry->type == 0) {
                            item = CurrentGame().spawnWorldItem(
                                CurrentGame().level.loaded(), entry->item_id, &position,
                                W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE);
                            if (item != 0) {
                                items.push_back(std::move(item));
                            }
                        } else if (entry->type == 1) {
                            GenerateItemsFromTable(items, entry->item_id, rolls, position);
                        }
                    }
                }
            }
            gold = RollDice(&record->treasure.gold_dice);
        } else {
            W8NpcState* npc = GetNpcStateForMonsterInfo(monster_info, true);
            if (npc == 0)
                throw std::runtime_error(
                    fmt::format("NPC monster {} has no NPC state", monster_info->monster_species));
            for (index = 0; index < 40; ++index) {
                if (npc->item_ids[index] != -1 && npc->item_weights[index] == 0) {
                    item = CurrentGame().spawnWorldItem(
                        CurrentGame().level.loaded(), npc->item_ids[index], &position,
                        W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE);
                    if (item != 0) {
                        items.push_back(std::move(item));
                    }
                    npc->item_ids[index] = -1;
                }
            }
            remaining = 0;
            for (index = 0; index < 40; ++index) {
                if (npc->item_ids[index] != -1) {
                    ++remaining;
                }
            }
            if (remaining != 0) {
                total_weight = 0;
                for (index = 0; index < 40; ++index) {
                    if (npc->item_ids[index] != -1) {
                        total_weight += static_cast<signed char>(npc->item_weights[index]);
                    }
                }
                take = RollDice(&npc->item_count_dice);
                if (remaining < take) {
                    take = remaining;
                }
                taken = 0;
                while (taken < take) {
                    pick = CurrentGame().rng.below(40);
                    if (npc->item_ids[pick] != -1 &&
                        static_cast<short>(CurrentGame().rng.below(total_weight)) <=
                            static_cast<short>(static_cast<signed char>(npc->item_weights[pick]))) {
                        item = CurrentGame().spawnWorldItem(
                            CurrentGame().level.loaded(), npc->item_ids[pick], &position,
                            W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE);
                        if (item != 0) {
                            items.push_back(std::move(item));
                        }
                        ++taken;
                        npc->item_ids[pick] = -1;
                    }
                }
            }
            gold = npc->gold;
        }
        if (gold != 0) {
            AddPartyGold(CurrentGame(), gold, true);
        }
        if (items.size() == 1) {
            auto* dropped = CurrentGame().level.loaded().adoptItem(std::move(items[0]));
            dropped->position = position;
        } else if (items.size() > 1) {
            W8WorldItem* container;
            if (value == -1) {
                container = CurrentGame().level.loaded().adoptItem(CurrentGame().spawnWorldItem(
                    CurrentGame().level.loaded(), 0x23c, &position, W8_ITEM_ENTITY_ROTATE));
            } else {
                container = CurrentGame().level.loaded().adoptItem(
                    CurrentGame().spawnWorldItem(CurrentGame().level.loaded(), value, &position,
                                                 W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            }
            container->contents = std::move(items);
        }
    }
}

W8WorldItem* GetWorldItemListEntry(int index)
{
    if (!GetWorld() || index < 0 ||
        std::cmp_greater_equal(index, CurrentLevelPopulation().world_items.size())) {
        return nullptr;
    }
    return CurrentLevelPopulation().world_items[index].get();
}
