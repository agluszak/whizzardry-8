#include "wiz8/local_code/ItemManager.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <fmt/format.h>
#include <glm/gtx/rotate_vector.hpp>

#include "AnimRep.hpp"
#include "Monster.h"
#include "compat/native.h"
#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/trigonometric.hpp"
#include "layouts/gameplay_databases.h"
#include "layouts/item_instance.h"
#include "layouts/item_tables.h"
#include "layouts/main_game_screen.h"
#include "layouts/npc_state.h"
#include "layouts/world.h"
#include "random.h"
#include "soundman.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"
#include "wiz8/containers.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_tables.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/modal_content.h"
#include "wiz8/sr_api.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

class W8DialogBase;

/* The level runtime block, which also carries the interface
   selection the item manager resets. */
#define ITEM_MANAGER_CPP "C:\\Projects\\Wizardry 8\\Local Code\\ItemManager.cpp"

/* 1/360, the half-degree step random item angles are built
   from. */
// GLOBAL: WIZ8 0x005ed7b0
extern const double g_inverse_full_turn_degrees = 1.0 / 360.0;

/* Camera distance inside which inactive world items are activated. */
// GLOBAL: WIZ8 0x005ed7b8
const float g_item_camera_cull_distance = 20000.0f;

/* Pi, the amplitude cursor-driven throw angles are scaled from. */
// GLOBAL: WIZ8 0x005ed7a8
extern const double g_throw_angle_amplitude = 3.141592653589793;

/* The screen-y coefficient in the drop-item yaw. */
// GLOBAL: WIZ8 0x005ebf48
extern const float g_drop_item_yaw_scale = 85.0f;

/* The screen-z coefficient in the drop-item pitch. */
// GLOBAL: WIZ8 0x005ebf4c
extern const float g_drop_item_pitch_scale = 71.0f;

/* The vertical scale of the drop-item direction. */
// GLOBAL: WIZ8 0x005ed7c0
extern const double g_drop_item_vertical_scale = -2500.0;

/* When set, skip activating world items that already carry flag
   bit 0. */
// GLOBAL: WIZ8 0x0064a1cd
bool g_hide_invisible_items = true;

// FUNCTION: WIZ8 0x004f69f0
bool InitializeItemManagerState()
{
    g_status.next_world_item_id = 1;
    gXStatus.item_manager_pending = 0;
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME && g_level_block != 0) {
        g_level_block->selected_item = -1;
    }
    gXStatus.world_items.clear();
    return true;
}

// FUNCTION: WIZ8 0x004f8130
bool ItemHasFlags(const W8WorldItem* item, unsigned int mask)
{
    if (item == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 998, 0);
    }
    return (item->flags & mask) != 0;
}

// FUNCTION: WIZ8 0x004f8170
void SetItemFlags(W8WorldItem* item, unsigned int mask, bool enabled)
{
    if (item == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1004, 0);
    }
    if (enabled) {
        item->flags |= mask;
    } else {
        item->flags &= ~mask;
    }
}

// FUNCTION: WIZ8 0x004f81c0
void SetItemAndEntityFlags(W8WorldItem* item, unsigned int mask, bool enabled)
{
    if (item == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1019, 0);
    }
    if (enabled) {
        item->entity_flags |= mask;
    } else {
        item->entity_flags &= ~mask;
    }
    if (item->p3D != 0) {
        static_cast<W8ItemRep*>(item->p3D->m_pRep)->SetFlags(mask, enabled);
    }
}

// FUNCTION: WIZ8 0x004f8300
int ItemInfoGetNumInGroup(W8WorldItem* item)
{
    if (item == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1115,
                     "Bad ITEM_STRUCT in ItemInfoGetNumInGroup");
    }
    item = item->next;
    int count = 1;
    while (item != 0) {
        item = item->next;
        ++count;
    }
    return count;
}

// FUNCTION: WIZ8 0x004f8340
void ItemInfoAddToGroup(W8WorldItem* group, W8WorldItem* item)
{
    W8WorldItem* tail;

    if (group == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1130, "Bad ITEM_STRUCT in ItemInfoAddToGroup");
    }
    tail = group;
    while (tail->next != 0) {
        tail = tail->next;
    }
    tail->next = item;
    item->next = 0;
}

struct W8ItemLevelScaleRange {
    unsigned int minimum_party_level;
    unsigned int minimum_item_value;
    unsigned int maximum_item_value;
};

// GLOBAL: WIZ8 0x00617CE0
static W8ItemLevelScaleRange g_item_level_scale_ranges[7] = {
    {1, 0, 500},      {6, 50, 1000},    {11, 100, 3000},     {16, 300, 5000},
    {21, 600, 10000}, {26, 800, 20000}, {31, 1000, 1000000},
};

// FUNCTION: WIZ8 0x004f88a0
int FindItemTableByName(const char* name)
{
    int index;

    for (index = 0; index < static_cast<int>(gXStatus.uiItemTablesInDatabase); ++index) {
        if (_stricmp(name, g_item_tables[index].name) == 0) {
            return index;
        }
    }
    return -1;
}

static W8WorldItem* CreateTableItem(int item_id, const srVector3T<float>* position)
{
    W8ItemInstance item;
    W8ItemInstance* item_pointer;
    W8WorldItem* result;

    if (item_id == -1) {
        item_pointer = 0;
    } else {
        ReplaceOrCreateItem(&item, item_id, false, false, false);
        item_pointer = &item;
    }
    result = CreateWorldItem(item_pointer, position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE,
                             false);
    if (result == 0) {
        srAssertFail("pItemInfo", "C:\\Projects\\Wizardry 8\\Local Code\\ItemManager.cpp", 0x18e,
                     0);
    }
    return result;
}

// FUNCTION: WIZ8 0x004f88f0
int GenerateItemsFromTable(std::vector<W8WorldItem*>& output_items, unsigned int table_id,
                           unsigned int maximum_items)
{
    unsigned int party_level = GetAveragePartyLevel();
    srVector3T<float> position;
    std::vector<int> candidates;
    W8ItemTableRecord* table;
    W8ItemTableEntry* entry;
    W8ItemDatabaseRecord* item_record;
    const W8ItemLevelScaleRange* range;
    unsigned int item_value;
    unsigned int total_weight;
    unsigned int selected_count;
    unsigned int random_value;
    int entry_index;
    int position_index;

    table_id &= 0xffff;
    for (entry_index = 0; entry_index < 40; ++entry_index) {
        table = &g_item_tables[table_id];
        if (table->entries[entry_index].selector != 0) {
            item_record = &g_item_records[table->entries[entry_index].item_id];
            if (table->entries[entry_index].weight == 0) {
                output_items.push_back(CreateTableItem(
                    g_item_tables[table_id].entries[entry_index].item_id, &position));
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
        g_item_tables[table_id].level_scaled != 0) {
        candidates.clear();
        for (entry_index = 0; entry_index < 40; ++entry_index) {
            if (g_item_tables[table_id].entries[entry_index].selector != 0) {
                candidates.push_back(entry_index);
            }
        }
    }

    total_weight = 0;
    for (entry_index = 0; entry_index < static_cast<int>(candidates.size()); ++entry_index) {
        total_weight += g_item_tables[table_id].entries[candidates[entry_index]].weight;
    }

    selected_count = 0;
    while (selected_count < maximum_items) {
        if (candidates.empty()) {
            break;
        }
        position_index = Random(static_cast<int>(candidates.size()));
        entry_index = candidates[position_index];
        table = &g_item_tables[table_id];
        entry = &table->entries[entry_index];
        random_value = Random(total_weight);
        if (random_value <= entry->weight) {
            output_items.push_back(CreateTableItem(entry->item_id, &position));
            candidates.erase(candidates.begin() + position_index);
            ++selected_count;
        }
    }

    return static_cast<int>(output_items.size());
}

/* The cursor the iterator below resumes from. */

// GLOBAL: WIZ8 0x00689b54
int g_world_item_cursor;

// FUNCTION: WIZ8 0x004f6cc0
void FreeWorldItemGroup(W8WorldItem* item)
{
    W8WorldItem* next;

    while (item != 0) {
        next = item->next;
        free(item);
        item = next;
    }
}

/* Look an item record up by its internal name. The name sits at 0x8d in the
   record; a record whose name is empty is matched against the display name
   converted down from wide instead. */
// FUNCTION: WIZ8 0x004f8220
int FindItemRecordByName(const char* name)
{
    int index;
    std::string internal_name;

    for (index = 0; index < static_cast<int>(gXStatus.uiItemsInDatabase); ++index) {
        internal_name = g_item_records[index].internal_name;
        if (internal_name[0] == 0) {
            if (_stricmp(g_item_records[index].display_name.c_str(), name) == 0) {
                return index;
            }
        } else if (CompareTextIgnoreAsciiCase(internal_name, name) == 0) {
            return index;
        }
    }
    return -1;
}

/* Walk the world item list, resuming where the last call left off. Restarting
   rewinds; running off the end answers nothing without rewinding. */
// FUNCTION: WIZ8 0x004f82b0
W8WorldItem* GetNextWorldItem(bool restart)
{
    int index;

    if (restart) {
        g_world_item_cursor = 0;
    }
    index = g_world_item_cursor;
    if (static_cast<unsigned int>(index) < gXStatus.world_items.size()) {
        ++g_world_item_cursor;
        return GetWorldItemListEntry(index);
    }
    return 0;
}

/* Take one item out of the group chained onto another. Unlinking the head
   promotes whatever followed it; unlinking anything else just closes the gap.
   Answers the group's head afterwards. */
// FUNCTION: WIZ8 0x004f83a0
W8WorldItem* ItemInfoRemoveFromGroup(W8WorldItem* head, W8WorldItem* item)
{
    W8WorldItem* previous;
    W8WorldItem* scan;

    if (head == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1142,
                     "Bad ITEM_STRUCT in ItemInfoRemoveFromGroup");
    }

    if (head == item) {
        return item;
    }

    scan = head;
    do {
        previous = scan;
        if (previous == 0) {
            break;
        }
        scan = previous->next;
        if (scan == 0) {
            break;
        }
    } while (scan != item);

    if (scan == item) {
        if (item->next != 0) {
            previous->next = item->next;
            item->next = 0;
            return head;
        }
        previous->next = 0;
    }
    item->next = 0;
    return head;
}

// FUNCTION: WIZ8 0x004f8410
W8WorldItem* ItemInfoGroupGetNext(W8WorldItem* item)
{
    if (item == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1178,
                     "Bad ITEM_STRUCT in ItemInfoGroupGetNext");
    }
    return item->next;
}

// FUNCTION: WIZ8 0x004f91e0
bool ItemInfoIsWorldPersistent(const W8WorldItem* item)
{
    if (item == 0) {
        return false;
    }
    return (g_item_records[item->item.iItemNo].flags & W8_ITEM_FLAG_CONTAINER) != 0;
}

// FUNCTION: WIZ8 0x004f9210
W8ItemInstance* CopyWorldItemInstance(const W8WorldItem* item)
{
    W8ItemInstance* copy = static_cast<W8ItemInstance*>(malloc(sizeof(W8ItemInstance)));

    if (copy == 0) {
        return 0;
    }
    *copy = item->item;
    return copy;
}

// FUNCTION: WIZ8 0x004f94a0
void SetWorldItemFalling(W8WorldItem* item, bool enabled)
{
    if (enabled) {
        item->flags |= W8_WORLD_ITEM_FALLING;
    } else {
        item->flags &= ~W8_WORLD_ITEM_FALLING;
    }
}

/* The world item at one list position. Both the bound and the fetch are
   asserted, and the second reports the list it failed on. */
// FUNCTION: WIZ8 0x004f7fe0
W8WorldItem* ItemInfo(unsigned int item_list_index)
{
    W8WorldItem* item;

    if (item_list_index >= gXStatus.world_items.size()) {
        srAssertFail("uiItemListIndex < (UINT32) gXStatus.world_items.size()", ITEM_MANAGER_CPP,
                     961, 0);
    }
    item = GetWorldItemListEntry(item_list_index);
    if (item == 0) {
        srAssertFail("pItemInfo != NULL", ITEM_MANAGER_CPP, 965,
                     fmt::format("ItemInfo: ERROR - missing entry, index {:d}, pList {}",
                                 item_list_index, static_cast<const void*>(&gXStatus.world_items)));
    }
    return item;
}

// FUNCTION: WIZ8 0x004f8060
unsigned int ItemIndex(int runtime_id)
{
    unsigned int index;

    for (index = 0; index < gXStatus.world_items.size(); ++index) {
        if (ItemInfo(index)->runtime_id == runtime_id) {
            return index;
        }
    }
    srAssertFail("FALSE", ITEM_MANAGER_CPP, 992,
                 fmt::format("ItemIndex: ERROR - ItemID {:d} not found", runtime_id));
    return 0;
}

/* The level runtime block, which also carries the interface
   selection the item manager resets. */

// FUNCTION: WIZ8 0x004f8440
int ItemInfoMakeGroupList(W8WorldItem* item, std::vector<W8WorldItem*>& out)
{
    W8WorldItem* next;

    if (item == 0) {
        srAssertFail("pItemInfo", ITEM_MANAGER_CPP, 1185,
                     "Bad ITEM_STRUCT in ItemInfoMakeGroupList");
    }

    out.push_back(item);

    for (next = item->next; next != 0; next = next->next) {
        out.push_back(next);
    }
    return static_cast<int>(out.size());
}

/* The generic 3D model names ActivateItem falls back to when a
   record's internal_name is blank, indexed by unidentified_name_index. */
// GLOBAL: WIZ8 0x00617d34
static char g_item_model_fallback_names[145][0x1e] = {"Dagger",
                                                      "Long Sword",
                                                      "Bipennis",
                                                      "Battle Axe",
                                                      "Flail",
                                                      "Mace",
                                                      "Hammer",
                                                      "Staff",
                                                      "Halberd",
                                                      "Spear",
                                                      "Bo Stick",
                                                      "Bow",
                                                      "Crossbow",
                                                      "Sling",
                                                      "Great Sword",
                                                      "Rapier",
                                                      "Katana",
                                                      "Long Staff",
                                                      "Wand",
                                                      "Magic Stave",
                                                      "Spellbook",
                                                      "Shuriken",
                                                      "Arrows",
                                                      "Bullets",
                                                      "Kite Shield",
                                                      "Basnet",
                                                      "Plate Torso",
                                                      "Plate Leggings",
                                                      "Gauntlets",
                                                      "Sollerets",
                                                      "Amulet",
                                                      "Ring",
                                                      "Blue Potion",
                                                      "Scroll",
                                                      "Powder",
                                                      "Key",
                                                      "Locket",
                                                      "Great Bow",
                                                      "Round Shield",
                                                      "Wizard Cone",
                                                      "Leather Helm",
                                                      "Cuirass",
                                                      "Leather Top",
                                                      "Greaves",
                                                      "Leather Leggings",
                                                      "Gloves",
                                                      "Sandals",
                                                      "Buskins",
                                                      "Boots",
                                                      "Book",
                                                      "Ankh",
                                                      "Ninjato",
                                                      "War Hammer",
                                                      "Flamberge",
                                                      "Bullwhip",
                                                      "Sai",
                                                      "Nunchuka",
                                                      "Glaive",
                                                      "Black Sword",
                                                      "Fire Sword",
                                                      "Upper Robes",
                                                      "Lower Robes",
                                                      "Halter",
                                                      "Skirt",
                                                      "Skullcap",
                                                      "Feathered Cap",
                                                      "Mitre",
                                                      "Breast Plate",
                                                      "Kabuto",
                                                      "Upper Toseido",
                                                      "Lower Toseido",
                                                      "Burgonet",
                                                      "Fur Leggings",
                                                      "Cloak",
                                                      "Mantis Gloves",
                                                      "Mantis Boots",
                                                      "Garland",
                                                      "Cat 'O Nine Tails",
                                                      "Silver Cross",
                                                      "Ruby Talisman",
                                                      "Purple Amulet",
                                                      "Bracelet",
                                                      "Necklace",
                                                      "Quarrels",
                                                      "Jade Figure",
                                                      "Chain Coif",
                                                      "Upper Chain",
                                                      "Lower Chain",
                                                      "Mail Mittens",
                                                      "Chain Hosen",
                                                      "Lance",
                                                      "Armet",
                                                      "Ninja Cowl",
                                                      "Upper Ninja Garb",
                                                      "Lower Ninja Garb",
                                                      "Tabi Boots",
                                                      "Heaume",
                                                      "Box Helm",
                                                      "Diamond Ring",
                                                      "Bag",
                                                      "Red Potion",
                                                      "Green Potion",
                                                      "Purple Potion",
                                                      "Yellow Potion",
                                                      "Power Glove",
                                                      "T'Rang Staff",
                                                      "Bronze Gauntlets",
                                                      "Comm Link",
                                                      "Light Sword",
                                                      "Magic Bag",
                                                      "Phaser",
                                                      "ID Card",
                                                      "Darts",
                                                      "Powder Shot",
                                                      "Musket",
                                                      "Coin",
                                                      "Stone",
                                                      "Stick",
                                                      "Short Sword",
                                                      "UNDEFINED",
                                                      "Bagpipes",
                                                      "Lyre",
                                                      "Lute",
                                                      "Horn",
                                                      "Map",
                                                      "Bracelet",
                                                      "Shuriken",
                                                      "UNDEFINED",
                                                      "Wand",
                                                      "Yellow Potion",
                                                      "Medium Shield",
                                                      "UNDEFINED",
                                                      "UNDEFINED",
                                                      "UNDEFINED",
                                                      "UNDEFINED",
                                                      "UNDEFINED",
                                                      "Saxophone",
                                                      "Violin",
                                                      "Drum",
                                                      "Bullroarer",
                                                      "bomb",
                                                      "emptybottle",
                                                      "stix",
                                                      "rocket",
                                                      "rocketlauncher"};

/* Put one spawned item into the world: load its 3D model from
   Data\Items3D\Bitmaps, drop it at the item's position with a random yaw,
   attach and register it, then scale it by whether the sun reaches it. */
// FUNCTION: WIZ8 0x004f6cf0
void ActivateItem(W8WorldItem* item)
{
    srVector3T<float> position;
    srVector3T<float> sun_position;
    W8ReadLevelInfo info;
    std::string zItemName;
    std::string zItemFullPath;
    std::string name;
    srNode* sun;
    stModelInstance* mesh;

    if (item == 0) {
        srAssertFail("pItemInfo != NULL", ITEM_MANAGER_CPP, 0x1d4, 0);
    }
    if (item->fActive) {
        srAssertFail("!pItemInfo->fActive", ITEM_MANAGER_CPP, 0x1d5, 0);
    }

    info.world = GetWorld();
    info.hFile = 0;
    info.bitmap_folder = "Data\\Items3D\\Bitmaps";

    name = g_item_records[item->item.iItemNo].internal_name;
    if (name.empty()) {
        name =
            g_item_model_fallback_names[g_item_records[item->item.iItemNo].unidentified_name_index];
    }
    zItemName = name;
    if (zItemName.find(".ITM") != std::string::npos) {
        zItemFullPath = fmt::format("{}\\{}", "Data\\Items3D", zItemName);
    } else {
        zItemFullPath = fmt::format("{}\\{}.ITM", "Data\\Items3D", zItemName);
    }
    if (![&]() {
            const auto status = wiz8::file_status(zItemFullPath);
            return status && status->info.type == SDL_PATHTYPE_FILE;
        }()) {
        zItemName = "questionmark";
        zItemFullPath = fmt::format("{}\\{}.ITM", "Data\\Items3D", zItemName);
        if (![&]() {
                const auto status = wiz8::file_status(zItemFullPath);
                return status && status->info.type == SDL_PATHTYPE_FILE;
            }()) {
            srAssertFail("[&]() { const auto status = wiz8::file_status(zItemFullPath); return "
                         "status && status->info.type == SDL_PATHTYPE_FILE; }()",
                         ITEM_MANAGER_CPP, 0x1ed,
                         fmt::format("ActivateItem: ERROR - missing ITM file {}, item {:d}",
                                     zItemFullPath, item->item.iItemNo));
        }
    }

    W8Item* entity = item->p3D;
    bool loaded = LoadItemFromFile(&info, zItemName.c_str(), &entity, false);
    item->p3D = entity;
    if (!loaded) {
        srAssertFail("fSuccess", ITEM_MANAGER_CPP, 0x1f2,
                     fmt::format("ActivateItem: ERROR - ItemRead {} failed", zItemName));
    }
    if (ItemHasFlags(item, W8_WORLD_ITEM_RADAR_BLIP_LIT)) {
        item->p3D->LightRadarBlip();
    }
    position = item->position;
    item->p3D->SetLocation(&position);
    item->p3D->SetYaw(
        static_cast<float>(Random(0x168) * 2 * g_camera_pi * g_inverse_full_turn_degrees));
    static_cast<W8ItemRep*>(item->p3D->m_pRep)->flags |= item->entity_flags;
    item->p3D->AttachMesh(GetWorld());
    AddItemToWorld(GetWorld(), item->p3D);
    item->fActive = true;
    ++gXStatus.item_manager_pending;

    sun = FindWorldNode<srNode>("SUN");
    if (sun != 0) {
        sun_position = glm::vec3(sun->getLocation());
        mesh = static_cast<stModelInstance*>(item->p3D->GetMesh());
        if (mesh != 0) {
            if (g_octree->HasLineOfSight(&position, &sun_position, true)) {
                mesh->light_scale = glm::vec3(1);
            } else {
                mesh->light_scale = glm::vec3(0);
            }
        }
    }
}

/* Take one item out of the world. Its three assertions name the two fields
   they guard - fActive and p3D - and the item keeps its last position and
   entity flags so it can be put back. */
// FUNCTION: WIZ8 0x004f70d0
void DeactivateWorldItem(W8WorldItem* item)
{
    srVector3T<float> position;

    if (item == 0) {
        srAssertFail("pItemInfo != NULL", ITEM_MANAGER_CPP, 554, 0);
    }
    if (!item->fActive) {
        srAssertFail("pItemInfo->fActive", ITEM_MANAGER_CPP, 555, 0);
    }
    if (item->p3D == 0) {
        srAssertFail("pItemInfo->p3D != NULL", ITEM_MANAGER_CPP, 556, 0);
    }

    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME && g_level_block != 0 &&
        g_level_block->selected_item == item->runtime_id) {
        g_level_block->selected_item = -1;
    }

    position = item->p3D->m_pRep->GetLocation();
    item->position = position;
    item->entity_flags = static_cast<W8ItemRep*>(item->p3D->m_pRep)->flags;

    item->p3D->DetachMesh(GetWorld());
    RemoveItemFromWorld(GetWorld(), item->p3D);
    delete item->p3D;
    item->p3D = 0;
    item->fActive = false;
    --gXStatus.item_manager_pending;
}

/* Highlight or un-highlight a world item by runtime id: the rep's highlight
   bit tracks the SetHighlight call so the marker and the flag stay in step. */
// FUNCTION: WIZ8 0x004F71E0
void SetWorldItemHighlight(int runtime_id, bool on)
{
    W8WorldItem* item = ItemInfo(ItemIndex(runtime_id));
    W8Item* world_item = item->p3D;
    if (world_item == 0) {
        srAssertFail("pItem", ITEM_MANAGER_CPP, 627, 0);
    }
    W8ItemRep* rep = static_cast<W8ItemRep*>(world_item->m_pRep);
    if (on) {
        rep->flags |= W8_ITEM_ENTITY_HIGHLIGHT_GREEN;
        world_item->SetHighlight(true);
        return;
    }
    rep->flags &= ~W8_ITEM_ENTITY_HIGHLIGHT_GREEN;
    world_item->SetHighlight(false);
}

/* The runtime id of the nearest active world item the cursor is hovering over
   and whose screen distance stays inside `max_distance`, or -1. The cursor
   coordinates are carried but unused - the hover test is IsSelected. */
// FUNCTION: WIZ8 0x004f7370
int PickNearestItemUnderCursor(int cursor_x, int cursor_y, float max_distance)
{
    int result;
    float best_distance;
    unsigned int index;

    result = -1;
    best_distance = 999999.0f;
    for (index = 0; index < gXStatus.world_items.size(); ++index) {
        W8WorldItem* item = ItemInfo(index);
        float distance;

        if (!item->fActive) {
            continue;
        }
        if (static_cast<W8ItemRep*>(item->p3D->m_pRep)->flags & W8_ITEM_ENTITY_NO_PICKUP) {
            continue;
        }
        if (!item->p3D->IsSelected()) {
            continue;
        }
        distance = ItemDistanceToCamera(GetWorld(), item->p3D);
        if (distance < max_distance && distance < best_distance) {
            result = item->runtime_id;
            best_distance = distance;
        }
    }
    return result;
}

/* Walk every world item: repair invalid sectors, advance falling ones, then
   activate inactive items near the camera and deactivate active ones that have
   drifted beyond the far range. */
// FUNCTION: WIZ8 0x004f7480
void UpdateNearbyWorldItems(void)
{
    srVector3T<float> camera;
    unsigned int index;
    unsigned int count;

    camera = cameraLocation(*GetWorld());
    count = gXStatus.world_items.size();
    for (index = 0; index < count; ++index) {
        W8WorldItem* item = ItemInfo(index);

        if (item->sector_id < -1) {
            item->sector_id = -1;
            SettleWorldItem(item);
        }
        if (ItemHasFlags(item, W8_WORLD_ITEM_FALLING)) {
            AdvanceFallingWorldItem(item);
        }
        if (!item->fActive) {
            if (DistanceBetweenPoints(&item->position, &camera) < g_item_camera_cull_distance) {
                if (g_hide_invisible_items) {
                    if (item == 0) {
                        srAssertFail("pItemInfo != NULL", ITEM_MANAGER_CPP, 0x3e6, 0);
                    }
                    if (ItemHasFlags(item, W8_WORLD_ITEM_HIDDEN)) {
                        goto next_item;
                    }
                }
                ActivateItem(item);
            }
        } else {
            srVector3T<float> location;

            location = item->p3D->m_pRep->GetLocation();
            if (DistanceBetweenPoints(&location, &camera) > g_float_twenty_five_thousand) {
                DeactivateWorldItem(item);
            }
        }
    next_item:
        count = gXStatus.world_items.size();
    }
}

glm::vec3 ItemThrowDirection(const glm::vec2& cursor, const glm::mat3& camera_rotation)
{
    const auto offset = (cursor - g_float_half) * g_float_nine_tenths;
    const float yaw = glm::radians(offset.x * g_drop_item_yaw_scale);
    const float pitch = glm::radians(g_float_five + offset.y * g_drop_item_pitch_scale);
    const auto forward = glm::vec3(0, 0, g_double_twenty_five_hundred);
    return camera_rotation * glm::rotateY(glm::rotateX(forward, pitch), yaw);
}

/* Toss the cursor-held item into the world. The tracked cursor position is
   turned into a 2500-unit direction through the camera rotation, traced to
   the first obstruction, shortened by the item radius, settled onto the
   ground and lifted back up. When the spot is free the held instance becomes
   a world item; otherwise the refusal sound plays. */
// FUNCTION: WIZ8 0x004F7610
void DropHeldItem(int)
{
    glm::mat3 rotation;
    rotation = cameraRotation(*g_world);
    srVector3 direction = ItemThrowDirection(GetCursorScaledPosition(), rotation);

    srVector3T<float> camera = cameraLocation();
    direction += camera;
    g_octree->TraceLineOfSight(&camera, &direction, true, -3, -3, true, 0);

    srVector3T<float> delta = direction - camera;
    delta.SetLength(glm::max(delta.Length() - g_float_one_hundred_twenty_five, g_float_zero));

    srVector3T<float> position = camera + delta;
    position.y = g_octree->settleToGround(position.glmValue(), true, 250).position.y +
                 g_float_one_hundred_twenty_five;
    if (FindNearbyFreePosition(250.0f, &position, true, true) != 0) {
        W8WorldItem* item = CreateWorldItem(&g_status.item_in_hand, &position,
                                            W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE, true);
        if (item == 0) {
            // Retail passes the NULL item pointer as the assert message.
            // reinterpret-ok: pointer-valued assert message argument.
            const char* failed_item = reinterpret_cast<const char*>(item);
            srAssertFail("pItemInfo != NULL", ITEM_MANAGER_CPP, 0x339, failed_item);
        }
        return;
    }
    wiz8::sound::play("Data\\Sound\\Misc\\CantDrop.WAV");
}

/* The item picker returns its remaining items as a group chain. A two-item group returns the picked
   extra to the world at the group's position; a lone item is simply dropped.
   Either way the group's own world entry is found by runtime id, pulled out
   of its sector, deactivated, freed and removed from the list. */
// FUNCTION: WIZ8 0x004f7c50
static void OnItemPickerClosed(W8WorldItem* group)
{
    W8WorldItem* item;
    unsigned int index;

    if (ItemInfoGetNumInGroup(group) == 2) {
        item = ItemInfoGroupGetNext(group);
        item->position = group->position;
        if (item->p3D == 0) {
            ActivateItem(item);
        }
        gXStatus.world_items.push_back(item);
        group->next = 0;
    } else if (ItemInfoGetNumInGroup(group) != 1) {
        return;
    }
    index = ItemIndex(group->runtime_id);
    item = ItemInfo(index);
    if (item->sector_id > -1) {
        RemoveItemFromSector(item->sector_id, item);
    }
    if (item->fActive) {
        DeactivateWorldItem(item);
    }
    FreeWorldItemGroup(item);
    gXStatus.world_items.erase(gXStatus.world_items.begin() + index);
}

/* Interact with one world item by runtime id. Records flagged 0x20 open the
   trigger item-picker instead of being taken; otherwise the item's trigger
   runs, the instance copies into the cursor hand, and the world entry is
   stripped back out of its sector and the list. */
// FUNCTION: WIZ8 0x004f7910
unsigned char InteractWithWorldItem(int runtime_id)
{
    unsigned char result = 1;
    int index = ItemIndex(runtime_id);
    W8WorldItem* item = ItemInfo(index);

    if (item == 0) {
        srAssertFail("pItemInfo != NULL", ITEM_MANAGER_CPP, 857, 0);
    }
    if (g_item_records[item->item.iItemNo].flags & W8_ITEM_FLAG_CONTAINER) {
        W8TriggerItemPickerDialog* dialog = new W8TriggerItemPickerDialog;
        if (dialog != 0) {
            dialog->SetItemGroup(item);
            dialog->on_complete = OnItemPickerClosed;
        }
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        return 1;
    }
    if (item->p3D->trigger != 0) {
        result = RunItemTrigger(item->p3D);
        if (result == 0) {
            return result;
        }
    }
    if ((static_cast<W8ItemRep*>(item->p3D->m_pRep)->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0) {
        CopyItemInstance(&g_status.item_in_hand, &item->item, 0, true);
    }
    index = ItemIndex(runtime_id);
    item = ItemInfo(index);
    if (item->sector_id > -1) {
        RemoveItemFromSector(item->sector_id, item);
    }
    if (item->fActive) {
        DeactivateWorldItem(item);
    }
    FreeWorldItemGroup(item);
    gXStatus.world_items.erase(gXStatus.world_items.begin() + index);
    return result;
}

/* Detach and release all world item groups. */
// FUNCTION: WIZ8 0x004f6a50
unsigned char ReleaseItemLists(void)
{
    wiz8::clear_and_dispose(gXStatus.world_items, [](W8WorldItem* item) {
        if (item->sector_id >= 0) {
            RemoveItemFromSector(item->sector_id, item);
        }
        if (item->fActive) {
            DeactivateWorldItem(item);
        }
        FreeWorldItemGroup(item);
    });
    return 1;
}

/* Whether one world item is close enough to a point to be reached, and in
   sight of the party's eye. The distance is compared before the trace, so a
   far item is never traced to. */
// FUNCTION: WIZ8 0x004f8560
bool IsWorldItemWithinReach(W8Item* owner, const srVector3T<float>* from, float radius)
{
    srVector3T<float> position;
    srVector3T<float> eye;

    position = owner->m_pRep->GetLocation();
    eye = cameraLocation();

    srVector3T<float> delta = position - *from;
    if (delta.Length() < radius) {
        auto bounds = owner->GetCachedLocalBounds();
        bounds.minimum += position.glmValue();
        bounds.maximum += position.glmValue();
        if (targetMarkerVisible(eye.glmValue(), bounds)) {
            return true;
        }
    }
    return false;
}

/* plsItemList index that last satisfied AnyWorldItemVisible. */
// GLOBAL: WIZ8 0x00618E70
static int g_last_visible_world_item = -1;

static bool IsWorldItemMarkerVisible(W8WorldItem* item, const srVector3T<float>& camera)
{
    if (item->p3D != 0) {
        srVector3T<float> position;
        srVector3T<float> eye;
        position = item->p3D->m_pRep->GetLocation();
        eye = cameraLocation();
        srVector3T<float> delta = position - camera;
        if (delta.Length() < static_cast<float>(g_double_twenty_five_hundred)) {
            auto bounds = item->p3D->GetCachedLocalBounds();
            bounds.minimum += position.glmValue();
            bounds.maximum += position.glmValue();
            return targetMarkerVisible(eye.glmValue(), bounds) != 0;
        }
    }
    return false;
}

/* Any live world item visible to the camera within g_double_twenty_five_hundred, resuming
   the scan at the last match. */
// FUNCTION: WIZ8 0x004f8650
bool AnyWorldItemVisible(void)
{
    srVector3T<float> camera;
    int count;
    int index;

    if (g_world == 0 || g_world->camera == 0) {
        return false;
    }
    camera = cameraLocation();
    count = gXStatus.world_items.size();
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

/* Advance one falling world item (flag bit 1) toward the ground for this
   frame. Clears the falling flag once it has landed or the settle misses. */
// FUNCTION: WIZ8 0x004f9240
unsigned char AdvanceFallingWorldItem(W8WorldItem* item)
{
    srVector3T<float> probe;
    bool hit;
    float ground;
    float previous_y;
    float dt;
    int sector;

    if (!ItemHasFlags(item, W8_WORLD_ITEM_FALLING)) {
        return 0;
    }

    previous_y = item->position.y;
    probe.Set(item->position.x, previous_y + g_world_scale, item->position.z);
    dt = g_game_time_accumulator->GetFrameDelta();
    if (g_camera_snap_epsilon < item->vertical_velocity) {
        probe.y = dt * item->vertical_velocity + probe.y;
    }

    const auto ground_probe = g_octree->settleToGround(probe.glmValue(), 1, 250.0f);
    probe = ground_probe.position;
    hit = ground_probe.hit;
    ground = ground_probe.position.y;
    if (!hit || fabs(ground - previous_y) < g_camera_snap_epsilon) {
        item->flags &= ~W8_WORLD_ITEM_FALLING;
        item->vertical_velocity = 0.0f;
        return 0;
    }

    sector = g_octree->current_prop;
    if (sector != item->sector_id) {
        if (item->sector_id >= 0) {
            RemoveItemFromSector(item->sector_id, item);
        }
        if (sector >= 0) {
            AddItemToSector(sector, item);
        }
        item->sector_id = sector;
    }

    if (previous_y <= ground) {
        item->vertical_velocity = (ground - previous_y) / dt;
        probe.y = ground;
    } else {
        item->vertical_velocity = dt * g_navigator_gravity * g_float_half + item->vertical_velocity;
        probe.y = probe.y - dt * item->vertical_velocity;
        if (probe.y < ground) {
            probe.y = ground;
        }
    }

    if (item->p3D != 0) {
        item->p3D->SetLocation(&probe);
    }
    item->position = probe;
    return 1;
}

/* Drop one item onto the ground below where it is. The search starts one world
   unit up so an item already resting does not settle into the floor; landing
   moves it between sectors and clears its saved-marker flag. */
// FUNCTION: WIZ8 0x004f93d0
unsigned char SettleWorldItem(W8WorldItem* item)
{
    srVector3T<float> start;
    bool hit;
    int sector;

    start.Set(item->position.x, item->position.y + g_world_scale, item->position.z);

    item->flags &= ~W8_WORLD_ITEM_FALLING;
    item->vertical_velocity = 0.0f;

    const auto ground_probe = g_octree->settleToGround(start.glmValue(), 1, 250.0f);
    start = ground_probe.position;
    hit = ground_probe.hit;
    if (!hit) {
        return 0;
    }

    sector = g_octree->current_prop;
    if (sector != item->sector_id) {
        if (item->sector_id >= 0) {
            RemoveItemFromSector(item->sector_id, item);
        }
        if (sector >= 0) {
            AddItemToSector(sector, item);
        }
        item->sector_id = sector;
    }
    if (item->p3D != 0) {
        item->p3D->SetLocation(&start);
    }
    item->position = start;
    return 1;
}

/* Rebuild every world item's carried instance from its own item id, which
   re-rolls whatever the record decides rather than keeping what was there. */
// FUNCTION: WIZ8 0x004f94c0
void RebuildAllWorldItemInstances(void)
{
    unsigned int index;
    W8WorldItem* item;

    for (index = 0; index < gXStatus.world_items.size(); ++index) {
        item = ItemInfo(index);
        ReplaceOrCreateItem(&item->item, item->item.iItemNo, false, false, false);
    }
}

// FUNCTION: WIZ8 0x004f6b90
W8WorldItem* CreateWorldItem(W8ItemInstance* item, const srVector3T<float>* position,
                             int entity_flags, bool add_to_world)
{
    W8WorldItem* result = static_cast<W8WorldItem*>(malloc(sizeof(W8WorldItem)));

    if (result == 0) {
        return 0;
    }

    memset(result, 0, sizeof(W8WorldItem));
    EmptyItemRecord(&result->item, 0, true);
    result->runtime_id = g_status.next_world_item_id++;
    result->fActive = false;
    result->p3D = 0;
    result->position = *position;
    result->sector_id = -1;
    SettleWorldItem(result);
    result->entity_flags = entity_flags;

    if (item != 0) {
        CopyItemInstance(&result->item, item, 0, true);
    }
    if (add_to_world) {
        gXStatus.world_items.push_back(result);
    }
    return result;
}

// FUNCTION: WIZ8 0x004f6c50
W8WorldItem* SpawnItem(int item_id, const srVector3T<float>* position, int entity_flags,
                       bool add_to_world)
{
    W8ItemInstance local_item;
    W8ItemInstance* item;
    W8WorldItem* result;

    if (item_id == -1) {
        item = 0;
    } else {
        ReplaceOrCreateItem(&local_item, item_id, false, false, false);
        item = &local_item;
    }

    result = CreateWorldItem(item, position, entity_flags, add_to_world);
    if (result == 0) {
        srAssertFail("pItemInfo", "C:\\Projects\\Wizardry 8\\Local Code\\ItemManager.cpp", 0x18e,
                     0);
    }
    return result;
}

/* Monster loot drop, driven by the death switch in MonsterManager. Non-NPC
   records fire their treasure slots (count gate, Random(100) chance, dice for
   the drop count; type 1 routes the id to GenerateItemsFromTable once per
   roll) and roll the gold dice. NPC-bound monsters empty their state item
   list instead: every zero-weight slot drops outright, then a weighted pick
   rolls item_count_dice items out of what remains; the NPC's gold field
   pays out directly. One drop is adopted into plsItemList at the death
   position; two or more are chained onto a container spawned with
   SpawnItem - 0x23c by default, or value when the caller names one. */
// FUNCTION: WIZ8 0x004f8cb0
void DropMonsterLoot(W8MonsterInfo* monster_info, int value)
{
    std::vector<W8WorldItem*> items;
    if (monster_info != 0) {
        W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
        srVector3T<float> position = monster_info->p3D->GetPosition();
        W8WorldItem* item;
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
                W8MonsterTreasureEntry* entry = &record->treasure.slots[slot];
                if (entry->count != 0 && Random(100) <= entry->chance) {
                    int rolls = RollDice(&entry->dice);
                    for (roll = 0; roll < rolls; ++roll) {
                        if (entry->type == 0) {
                            item = SpawnItem(entry->item_id, &position,
                                             W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE, false);
                            if (item != 0) {
                                items.push_back(item);
                            }
                        } else if (entry->type == 1) {
                            GenerateItemsFromTable(items, entry->item_id, rolls);
                        }
                    }
                }
            }
            gold = RollDice(&record->treasure.gold_dice);
        } else {
            W8NpcState* npc = GetNpcStateForMonsterInfo(monster_info, true);
            if (npc == 0) {
                srAssertFail(
                    "pNPC", ITEM_MANAGER_CPP, 1459,
                    fmt::format("Monster {} marked as NPC with no NPC data", record->name0));
            }
            for (index = 0; index < 40; ++index) {
                if (npc->item_ids[index] != -1 && npc->item_weights[index] == 0) {
                    item = SpawnItem(npc->item_ids[index], &position,
                                     W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE, false);
                    if (item != 0) {
                        items.push_back(item);
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
                    pick = Random(40);
                    if (npc->item_ids[pick] != -1 &&
                        static_cast<short>(Random(total_weight)) <=
                            static_cast<short>(static_cast<signed char>(npc->item_weights[pick]))) {
                        item = SpawnItem(npc->item_ids[pick], &position,
                                         W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE, false);
                        if (item != 0) {
                            items.push_back(item);
                        }
                        ++taken;
                        npc->item_ids[pick] = -1;
                    }
                }
            }
            gold = npc->gold;
        }
        if (gold != 0) {
            AddPartyGold(gold, true);
        }
        if (items.size() == 1) {
            item = items[0];
            gXStatus.world_items.push_back(item);
            item->position = position;
        } else if (items.size() > 1) {
            W8WorldItem* container;
            int item_index;
            if (value == -1) {
                container = SpawnItem(0x23c, &position, W8_ITEM_ENTITY_ROTATE, true);
            } else {
                container =
                    SpawnItem(value, &position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE, true);
            }
            for (item_index = 0; item_index < static_cast<int>(items.size()); ++item_index) {
                ItemInfoAddToGroup(container, items[item_index]);
            }
        }
    }
}
