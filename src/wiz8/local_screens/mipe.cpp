#include <memory>
#include <SDL3/SDL_rect.h>
#include <fmt/format.h>
#include <string>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include <variant>
#include <cstring>
#include <string_view>

#include "wiz8/fonts.h"
#include "wiz8/local_screens/mipe.h"
#include "input.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/float_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_tables.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/sr_api.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"
#include "dice.h"
#include "layouts/gameplay_databases.h"
#include "layouts/main_game_screen.h"
#include "layouts/world.h"

// The editor reuses its category view for four concrete borrowed row types.
using W8MipeCategoryEntry = std::variant<std::monostate, W8MipeMonsterEntry*, W8ItemDatabaseRecord*,
                                         W8EncounterTableRuntime*, W8ItemTableRecord*>;

template <class T>
static T* MipeCategoryValue(const std::vector<W8MipeCategoryEntry>* rows, int index)
{
    if (!rows || index < 0 || index >= static_cast<int>(rows->size()))
        return nullptr;
    const auto* value = std::get_if<T*>(&(*rows)[index]);
    return value ? *value : nullptr;
}

static std::string_view MipeCategoryName(const std::vector<W8MipeCategoryEntry>* rows, int index)
{
    if (auto* monster = MipeCategoryValue<W8MipeMonsterEntry>(rows, index))
        return monster->name;
    if (auto* item = MipeCategoryValue<W8ItemDatabaseRecord>(rows, index))
        return item->display_name;
    return {};
}

#define MIPE_CPP __FILE__

/* Local Screens\mipe.cpp. The MIPE state is a diagnostic monster/item
   selection panel used while the debug flag is active. */

// GLOBAL: WIZ8 0x0068f0fe
static bool g_mipe_mongen_visible;

// GLOBAL: WIZ8 0x0068f108
int g_mipe_mode;

/* How many monsters/items the next Enter in the create modes spawns. */
// GLOBAL: WIZ8 0x0068f10c
int g_mipe_count;

// GLOBAL: WIZ8 0x0068f110
static short g_mipe_item_index;

// GLOBAL: WIZ8 0x0068f0fc
bool g_debug_monster_cycle;

// GLOBAL: WIZ8 0x0068f100
std::unique_ptr<W8MipeState> g_mipe_state;

// GLOBAL: WIZ8 0x0068f112
static short g_mipe_monster_index;

// GLOBAL: WIZ8 0x0068f114
static unsigned char g_mipe_category;

// GLOBAL: WIZ8 0x0068f118
static int g_mipe_table_row;

// GLOBAL: WIZ8 0x0068f120
int g_mipe_table_base;

// GLOBAL: WIZ8 0x0068f124
static std::vector<W8MipeMonsterEntry> g_mipe_monster_entries;

// GLOBAL: WIZ8 0x0068f12c
static W8WorldCursorNode* g_mipe_cube;

/* 0x0064A1CC: 'C' toggles between single-monster ("Choosing: One") and
   whole-group ("Choosing: Group") selection in UpdateMipeSelection. */
// GLOBAL: WIZ8 0x0064a1cc
static bool g_mipe_choose_group = true;

int FindCategoryItemTable(unsigned int category, int ordinal);
static void ShowMipeNamedCategory(std::vector<W8MipeCategoryEntry>* list,
                                  const unsigned short* name_ids);

/* The MIPE toggle: leaving the panel restores the level flags and combat
   state and tears the editor state down; entering it clears combat flags,
   prints the mode menu and builds the monster entry list. */
static void ShowMipeMenu(std::string_view title, std::initializer_list<std::string_view> rows)
{
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, title);
    for (auto row : rows) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, row);
    }
}

static void ShowMipeMainMenu()
{
    ShowMipeMenu("What would you like to do?", {
        "1) Create a monster.",
        "2) Create an item.",
        "3) Edit object(s).",
        "4) Monster Generators.",
        "5) Select object(s).",
        "6) Handle triggers."});
}

void ToggleMipePanel(void)
{
    if (g_mipe_active) {
        g_level_block->text_box_visible = true;
        g_level_block->mipe_editing = false;
        ResetEditorStatusLine(-1);
        if (gXStatus.fCombatMode) {
            SelectTextBox(1);
        }
        ReleaseWorldCursor();
        g_mipe_active = false;
        g_mipe_menu_active = true;
        g_debug_monster_cycle = false;
        if (g_mipe_cube != 0) {
            SetWorldCursorNodeColorComponents(g_mipe_cube, 0.0f, 0.0f, 0.5f);
        }
        for (unsigned int monster_list_index = 0; monster_list_index < gXStatus.monsters.size(); ++monster_list_index) {
            W8MonsterInfo* monster = GetMonsterListEntry(monster_list_index);
            if (monster != 0 && monster->fInCombat) {
                SetMonsterHighlight(0, monster->location_id, false);
            }
        }
        g_mipe_monster_entries.clear();
        g_mipe_state.reset();
        SetWorldCursorNodesVisible(g_mipe_trigger_display);
        return;
    }

    g_level_block->text_box_visible = false;
    g_level_block->mipe_editing = true;
    SelectTextBox(0);
    ResetEditorStatusLine(-1);
    g_mipe_active = true;
    ShowMipeMainMenu();
    g_mipe_mode = 0;
    InitializeWorldCursor();
    g_mipe_active = true;
    g_mipe_menu_active = false;
    g_mipe_cube = 0;
    std::vector<W8MonsterRecord> records(gXStatus.uiMonstersInDatabase);
    LoadMonsterDatabaseRange(0, gXStatus.uiMonstersInDatabase - 1, records.data());
    g_mipe_monster_entries.clear();
    g_mipe_monster_entries.reserve(records.size());
    for (unsigned int index = 0; index < gXStatus.uiMonstersInDatabase; ++index) {
        W8MipeMonsterEntry& entry = g_mipe_monster_entries.emplace_back();
        entry.name = records[index].name0;
        entry.kind = records[index].kind;
        entry.selectable = records[index].deleted == 0 && records[index].editor_index == -1;
    }

    g_mipe_state = std::make_unique<W8MipeState>();
    g_mipe_state->drag_anchor.SetZero();
    g_mipe_state->monster_ids.reserve(10);

    g_mipe_state->selecting = 0;
    g_mipe_state->value_34 = 1.0f;
    g_mipe_state->speed_step = 0.020000000f;
    g_mipe_state->edit_selection = -1;
    g_mipe_state->waypoints.reserve(10);

    for (int cursor_index = 0, count = GetWorldCursorNodeCount(); cursor_index < count;
         ++cursor_index) {
        W8WorldCursorNode* node = GetWorldCursorNode(cursor_index);
        SetWorldCursorNodeColorComponents(node, 0.0f, 0.0f, 0.5f);
        RefreshWorldCursorNodeLabel(node);
    }

    int selection = g_mipe_table_row + g_mipe_table_base;
    /* Both scans below compare their index against the database count as
       signed, which is this file's established reading of those counts - see
       the static_cast<int> on the same two members throughout. The retail
       agrees: 0x0057D9E3 cmp esi,eax; jl and 0x0057DA1C test esi,esi; jle
       for the monsters, 0x0057DA5A cmp eax,esi; jl for the items. */
    int monster_index = 0;
    int visible = 0;
    while (monster_index < static_cast<int>(gXStatus.uiMonstersInDatabase)) {
        W8MipeMonsterEntry* entry = &g_mipe_monster_entries[monster_index];
        if (entry->kind == g_mipe_category && entry->selectable) {
            if (visible == selection) {
                break;
            }
            ++visible;
        }
        ++monster_index;
    }
    if (monster_index == static_cast<int>(gXStatus.uiMonstersInDatabase)) {
        g_mipe_monster_index = 0;
    } else {
        W8MonsterRecord record;
        LoadMonsterDatabaseRecord(monster_index, &record);
        g_mipe_monster_index = record.editor_index;
    }

    int item_index = 0;
    visible = 0;
    while (item_index < static_cast<int>(gXStatus.uiItemsInDatabase)) {
        if (g_item_records[item_index].equip_class == g_mipe_category &&
            g_item_records[item_index].editor_excluded == 0) {
            if (visible == selection) {
                break;
            }
            ++visible;
        }
        ++item_index;
    }
    if (item_index == static_cast<int>(gXStatus.uiItemsInDatabase)) {
        item_index = 0;
    }
    g_mipe_item_index = static_cast<short>(item_index);
}

// GLOBAL: WIZ8 0x0068f11c
static std::unique_ptr<std::vector<W8MipeCategoryEntry>> g_mipe_category_list;

/* 'H' in the item-create mode: spawned items get the hidden flag. */
// GLOBAL: WIZ8 0x0068f128
static bool g_mipe_item_hidden;

// GLOBAL: WIZ8 0x0068f130
static int g_mipe_cube_param;

// GLOBAL: WIZ8 0x0068f134
static int g_mipe_scale_plane;

/* Lock/trap kind names for the selected trigger's lock_type, printed by the
   prop-edit menu and the locks & traps editor. */
// GLOBAL: WIZ8 0x0064a1d0
static const char* g_lock_type_names[4] = {"None", "Pickable Lock", "Trap", "Key Lock"};

/* Mode-1 status: monster being placed, count being accumulated and the
   creation method. */
static void ShowMipeMonsterStatus(void)
{
    std::string name;
    const char* method;

    name = g_mipe_monster_entries[g_mipe_monster_index].name;
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Type # to specify how many, then ENTER,");
    ShowNotice(W8_FONT_PALETTE_PINK, "OR type C to change what monster to place.  ");
    ShowNotice(W8_FONT_PALETTE_PINK, "OR type O to edit the creation method.      ");
    ShowNotice(W8_FONT_PALETTE_BLUE, fmt::format("How many: {:d}", g_mipe_count));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    if (g_mipe_state->creation_method == 0) {
        method = "Creation Method: Exact #";
    } else if (g_mipe_state->creation_method == 1) {
        method = "Creation Method: Placeholder";
    } else {
        if (g_mipe_state->creation_method != 2) {
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("Monster:   {}        ", name));
            return;
        }
        method = "Creation Method: Selection";
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, method);
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("Monster:   {}        ", name));
}

/* Mode-2 status: item being placed, count and the hidden/visible toggles. */
static void ShowMipeItemStatus(void)
{
    std::string name;
    const char* line;
    unsigned int palette;

    name = g_item_records[g_mipe_item_index].display_name;
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Type # to specify how many, then ENTER,");
    ShowNotice(W8_FONT_PALETTE_PINK, "OR type C to change what item to place.  ");
    ShowNotice(W8_FONT_PALETTE_BLUE, fmt::format("How many: {:d}", g_mipe_count));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    if (!g_hide_invisible_items) {
        line = "A - All invisible items  will be blue.";
        palette = 3;
    } else {
        line = "A - All invisible items will be hidden.";
        palette = 8;
    }
    ShowNotice(palette, line);
    if (!g_mipe_item_hidden) {
        line = "H - Item will be visible.";
        palette = 5;
    } else {
        line = "H - Item will be hidden.";
        palette = 8;
    }
    ShowNotice(palette, line);
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("Item:   {}        ", name));
}

/* Six visible rows of `list` starting at the table base; the selected row is
   palette 3, the rest 0xf, and short lists blank the remainder. */
void ShowMipeTableRows(std::vector<W8MipeCategoryEntry>* list)
{
    std::string_view entry;
    int row;
    unsigned int palette;

    if (list != 0) {
        for (row = 0; row < 6; ++row) {
            entry = MipeCategoryName(list, g_mipe_table_base + row);
            if (entry.empty()) {
                ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
            } else {
                palette = row == g_mipe_table_row ? 3 : 0xf;
                ShowNotice(palette, fmt::format("    {}", entry));
            }
        }
    }
}

/* Mode-9 menu: pick a monster or item to edit, showing the one/group choice
   the 'C' key toggles. */
static void ShowMipeSelectionMenu(const char* title)
{
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, title);
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Type C to change how to choose.");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    if (g_mipe_choose_group) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Choosing: Group");
        return;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Choosing: One");
}

void ShowMipeChooseMenu(void)
{
    ShowMipeSelectionMenu("Choose monster or item to edit.");
}

/* Mode-5 menu: which kind of object to edit. */
void ShowMipeEditMenu(void)
{
    ShowMipeMenu("What do you want to edit?.", {
        "1) Monster",
        "2) Prop",
        "3) Item",
        "4) Move object(s)"});
}

static void ShowMipeMonsterEditMenu()
{
    ShowMipeMenu("Edit Monster", {
        "1) Slow <<",
        "2) Fast >>",
        "3) Set Speed",
        "4) Assign Path",
        "5) Next Cycle",
        "6) Direction"});
}

/* Mode-0xd menu: prop field-editing choices (locks & traps, treasure table). */
void ShowMipePropMenu(void)
{
    ShowMipeMenu("Choose how you want to edit prop.", {
        "1) Locks & Traps",
        "2) Treasure Table",
        "3) .",
        "4) .",
        "5) .",
        "6) ."});
}

/* Mode-0xc menu: trigger actions plus the selected trigger's kind and the
   armed "selecting" notice. */
static void ShowMipeTriggerMenu(void)
{
    const char* status;
    int kind;

    ShowMipeMenu("Select what you want to do.", {
        "1) Create trigger.",
        "2) Delete trigger.",
        "3) ",
        "4) Select trigger."});
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, !g_mipe_trigger_display
                                             ? "5) Toggle trigger display [now off]."
                                             : "5) Toggle trigger display [now on].");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "6) Volume Triggers.");
    if (g_mipe_state->trigger == 0) {
        if (g_mipe_state->selecting == 0) {
            return;
        }
        status = "---> SELECTING TRIGGER <---";
    } else {
        kind = g_mipe_state->trigger->trigger_kind;
        if (kind == 1) {
            status = "Trigger is Switch";
            if (g_mipe_state->selecting == 0) {
                ShowNotice(W8_FONT_PALETTE_WHITE, status);
                return;
            }
        } else if (kind == 2) {
            status = "Trigger is Invisible";
            if (g_mipe_state->selecting == 0) {
                ShowNotice(W8_FONT_PALETTE_WHITE, status);
                return;
            }
        } else {
            status = "Trigger Type Unknown";
            if (g_mipe_state->selecting == 0) {
                ShowNotice(W8_FONT_PALETTE_TEXT_BOX, status);
                return;
            }
        }
    }
    ShowNotice(W8_FONT_PALETTE_BLUE, status);
}

/* The status line block the ','/'.' and 'k'/'l' handlers repaint. */
void ShowMonsterSpeedStatus(void)
{
    W8PathAI* path;
    float speed;

    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Type ',' to decrease speed, '.' to increase.");
    ShowNotice(W8_FONT_PALETTE_PINK, "Type 'k' to decrease increment, 'l' to increase.");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("Increment: {:g}", g_mipe_state->speed_step));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    if (g_mipe_state->monster == 0) {
        ShowNotice(W8_FONT_PALETTE_WHITE, "No monster available.");
        return;
    }
    path = static_cast<W8PathAI*>(MonsterGetAIRecord(g_mipe_state->monster));
    if (path != 0 && path->kind == W8_AI_RECORD_PATH) {
        speed = path->speed;
    } else {
        if (g_mipe_state->monster == 0) {
            ShowNotice(W8_FONT_PALETTE_WHITE, "Monster has no path AI.");
            return;
        }
        speed = MonsterGetNavigatorMovementScale(g_mipe_state->monster);
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("Current speed: {:g}", speed));
}

/* The "Parameters" pane for the selected volume cube. */
void ShowCubeParameters(void)
{
    ResetEditorStatusLine(-1);
    if (g_mipe_cube == 0) {
        ShowNotice(W8_FONT_PALETTE_PINK, "No cube selected.");
        return;
    }
    ShowNotice(W8_FONT_PALETTE_PINK, "Parameters:");
    if (g_mipe_cube_param == 0) {
        ShowNotice(W8_FONT_PALETTE_RED,
                   fmt::format("Message: {:d}", GetWorldCursorNodeParameter(g_mipe_cube, 0)));
    } else {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("Message: {:d}", GetWorldCursorNodeParameter(g_mipe_cube, 0)));
    }
    if (g_mipe_cube_param == 1) {
        ShowNotice(W8_FONT_PALETTE_RED,
                   fmt::format("Search: {:d}", GetWorldCursorNodeParameter(g_mipe_cube, 1)));
    } else {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("Search: {:d}", GetWorldCursorNodeParameter(g_mipe_cube, 1)));
    }
    if (g_mipe_cube_param == 2) {
        ShowNotice(W8_FONT_PALETTE_RED,
                   fmt::format("Function: {:d}", GetWorldCursorNodeParameter(g_mipe_cube, 2)));
        return;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
               fmt::format("Function: {:d}", GetWorldCursorNodeParameter(g_mipe_cube, 2)));
}

/* The monster generator top menu plus its selection status line. */
void ShowMonsterGeneratorStatus(void)
{
    const char* state;

    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK,
               fmt::format("Monster Generators ({:d})", GetMonsterGeneratorCount()));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "1) Create 2) Delete");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "3) Edit   4) Select");
    state = "On";
    if (!g_mipe_mongen_visible) {
        state = "Off";
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("5) Toggle Display [{}]", state));
    state = "Off";
    if (g_generator_save_flag == 0) {
        state = "On";
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("6) Toggle Active  [{}]", state));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    if (g_mipe_state->generator != 0) {
        if (g_mipe_state->selecting != 0) {
            ShowNotice(W8_FONT_PALETTE_BLUE, "<--- MOUSE OVER MONGEN --->");
            return;
        }
        ShowNotice(W8_FONT_PALETTE_WHITE, "MONGEN selected");
        return;
    }
    if (g_mipe_state->selecting != 0) {
        ShowNotice(W8_FONT_PALETTE_BLUE, "---> SELECTING MONGEN <---");
        return;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
}

/* The "Edit Monster Generator" pane. */
void ShowMonsterGeneratorEditor(void)
{
    MonGen* generator;
    W8EncounterTableRuntime* table;
    const char* state;
    int chance;
    short interval;
    int color;
    const char* interval_kind;

    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Edit Monster Generator");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("1) Name: {}", g_mipe_state->generator->name));
    if (g_mipe_state->generator->encounter_table_index == -1) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "2) Table: Not Selected");
    } else {
        table = GetEncounterTable(g_mipe_state->generator->encounter_table_index);
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("2) Table: {}", table->name));
    }
    state = "On";
    if (g_mipe_state->generator->generation_enabled == 0) {
        state = "Off";
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("3) Toggle Active [{}]", state));
    generator = g_mipe_state->generator;
    if ((generator->flags >> 3 & 1) == 0) {
        interval = generator->custom_interval_seconds;
        chance = generator->custom_spawn_chance;
        interval_kind = "Custom";
        color = 5;
    } else {
        chance = g_generator_interval_min;
        interval_kind = "Default";
        color = 8;
        interval = g_generator_default_interval;
    }
    ShowNotice(color, fmt::format(" {}: {:d} (4-/5+) chance every {:d} (6-/7+) s", interval_kind,
                                  chance, interval));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "8) to toggle chance default");
}

template <class Entry> static void ShowMipeNamedTableRows(std::vector<W8MipeCategoryEntry>* list)
{
    if (list != 0) {
        for (int row = 0; row < 6; ++row) {
            Entry* entry = MipeCategoryValue<Entry>(list, g_mipe_table_base + row);
            if (entry == 0) {
                ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
            } else {
                unsigned int palette = row == g_mipe_table_row ? 3 : 0xf;
                ShowNotice(palette, fmt::format("    {}", entry->name));
            }
        }
    }
}

/* Mode-0x17 menu: current encounter-table category plus its six rows. */
void ShowMipeEncounterCategory(void)
{
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK,
               fmt::format("Category: {}", g_encounter_names[g_mipe_category & 0xff]));
    ShowMipeNamedTableRows<W8EncounterTableRuntime>(g_mipe_category_list.get());
}

/* Mode-0x1d menu: current item-table category plus its six rows. */
void ShowMipeItemTableCategory(void)
{
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK,
               fmt::format("Category: {}", g_item_table_category_names[g_mipe_category & 0xff]));
    ShowMipeNamedTableRows<W8ItemTableRecord>(g_mipe_category_list.get());
}

/* Mode-1 key handler: digit keys build the spawn count, Enter creates the
   selected monster group at the world cursor, 'C' jumps to the monster
   category picker and 'O' cycles the creation method. */
static unsigned int FindMipeVisibleMonster(int visible)
{
    int index;
    unsigned int monster_index;
    W8MipeMonsterEntry* entry;
    index = 0;
    monster_index = 0;
    if (0 < static_cast<int>(gXStatus.uiMonstersInDatabase)) {
        do {
            entry = &g_mipe_monster_entries[monster_index];
            if (entry->kind == g_mipe_category && entry->selectable) {
                if (index == visible) {
                    break;
                }
                ++index;
            }
            ++monster_index;
        } while (static_cast<int>(monster_index) < static_cast<int>(gXStatus.uiMonstersInDatabase));
    }
    return monster_index;
}

unsigned char HandleMipeMonsterCreateKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    W8MipeMonsterEntry* entry;
    W8MonsterGroup* monster_group;
    int index;
    int visible;
    unsigned int monster_index;
    srVector3T<float> anchor;
    srVector3T<float> formation;
    W8MonsterRecord record;

    if (!g_mipe_category_list) {
        g_mipe_category_list = std::make_unique<std::vector<W8MipeCategoryEntry>>();
        list = g_mipe_category_list.get();
        for (index = 0; index < static_cast<int>(gXStatus.uiMonstersInDatabase); ++index) {
            entry = &g_mipe_monster_entries[index];
            if (entry->kind == g_mipe_category && entry->selectable) {
                list->emplace_back(entry);
            }
        }
    }
    list = g_mipe_category_list.get();
    switch (key) {
    case 8:
        g_mipe_count /= 10;
        ShowMipeMonsterStatus();
        return 1;
    default:
        return 0;
    case 0xd:
        break;
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        g_mipe_count =
            static_cast<int>(static_cast<char>(static_cast<char>(key) - 0x30)) + g_mipe_count * 10;
        [[fallthrough]];
    case 0x20:
        ShowMipeMonsterStatus();
        return 1;
    case 0x43:
        g_mipe_count = 1;
        g_mipe_mode = 6;
        ShowMipeNamedCategory(list, g_special_category_name_ids);
        return 1;
    case 0x4f:
        ++g_mipe_state->creation_method;
        if (2 < g_mipe_state->creation_method) {
            g_mipe_state->creation_method = 0;
        }
        ShowMipeMonsterStatus();
        return 1;
    }
    if (g_mipe_count != 0) {
        visible = g_mipe_table_row + g_mipe_table_base;
        monster_index = FindMipeVisibleMonster(visible);
        if (monster_index == gXStatus.uiMonstersInDatabase) {
            record.record_id = 0;
        } else {
            LoadMonsterDatabaseRecord(monster_index, &record);
        }
        GetWorldCursorAnchor(&anchor);
        monster_group =
            CreateGroup(static_cast<unsigned int>(static_cast<unsigned short>(record.record_id)),
                        static_cast<unsigned int>(g_mipe_count), &anchor, true, true, true);
        if (monster_group == 0) {
            g_mipe_count = 0;
            return 1;
        }
        formation = anchor;
        SetMonsterGroupFormation(monster_group, &formation);
        monster_group->group_state[0x6d] = g_mipe_state->creation_method;
    }
    return 1;
}

static unsigned int FindMipeVisibleItem(int visible)
{
    int found = 0;
    unsigned int item_index;
    for (item_index = 0; static_cast<int>(item_index) < static_cast<int>(gXStatus.uiItemsInDatabase); ++item_index) {
        if (g_item_records[item_index].equip_class == g_mipe_category &&
            g_item_records[item_index].editor_excluded == 0) {
            if (found == visible) {
                break;
            }
            ++found;
        }
    }
    return item_index;
}

/* Six-row picker navigation is shared by items, monsters, encounter tables
   and treasure tables. A stationary Up still requests a redraw; exhausted
   Down/Page Down and Page Up at the first page do not. */
static bool MoveMipeTableSelection(unsigned short key)
{
    switch (key) {
    case 0x26:
        if (g_mipe_table_row == 0) {
            if (g_mipe_table_base != 0) {
                --g_mipe_table_base;
            }
        } else {
            --g_mipe_table_row;
        }
        return true;
    case 0x21:
        if (g_mipe_table_base == 0) {
            return false;
        }
        g_mipe_table_base -= 6;
        if (g_mipe_table_base < 0) {
            g_mipe_table_base = 0;
        }
        return true;
    case 0x28:
        if (g_mipe_table_row < 5 &&
            g_mipe_table_base + g_mipe_table_row <
                static_cast<int>((g_mipe_category_list ? g_mipe_category_list->size() : 0) - 1)) {
            ++g_mipe_table_row;
            return true;
        }
        if (static_cast<int>((g_mipe_category_list ? g_mipe_category_list->size() : 0) - 1) <=
            g_mipe_table_base + g_mipe_table_row) {
            return false;
        }
        ++g_mipe_table_base;
        return true;
    case 0x22:
        if (g_mipe_table_base + g_mipe_table_row <
            static_cast<int>((g_mipe_category_list ? g_mipe_category_list->size() : 0) - 6)) {
            g_mipe_table_base += 6;
            return true;
        }
        if (static_cast<int>((g_mipe_category_list ? g_mipe_category_list->size() : 0)) <=
            g_mipe_table_base + g_mipe_table_row) {
            return false;
        }
        g_mipe_table_base += 6;
        g_mipe_table_row = 0;
        if (static_cast<int>((g_mipe_category_list ? g_mipe_category_list->size() : 0)) <=
            g_mipe_table_base) {
            g_mipe_table_base =
                static_cast<int>((g_mipe_category_list ? g_mipe_category_list->size() : 0));
            --g_mipe_table_base;
        }
        return true;
    }
    return false;
}

static void ShowMipeNamedCategory(std::vector<W8MipeCategoryEntry>* list,
                                  const unsigned short* name_ids)
{
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK,
               fmt::format("Category: {}", g_localized_strings[name_ids[g_mipe_category & 0xff]]));
    ShowMipeTableRows(list);
}

static void RebuildMipeItemCategory(std::vector<W8MipeCategoryEntry>* list)
{
    if (g_mipe_category_list != 0) {
        g_mipe_category_list->clear();
        for (int index = 0; index < static_cast<int>(gXStatus.uiItemsInDatabase); ++index) {
            W8ItemDatabaseRecord* entry = &g_item_records[index];
            if (entry->equip_class == g_mipe_category && entry->editor_excluded == 0) {
                list->emplace_back(entry);
            }
        }
    }
}

static void RebuildMipeMonsterCategory(std::vector<W8MipeCategoryEntry>* list)
{
    if (g_mipe_category_list != 0) {
        g_mipe_category_list->clear();
        for (int index = 0; index < static_cast<int>(gXStatus.uiMonstersInDatabase); ++index) {
            W8MipeMonsterEntry* entry = &g_mipe_monster_entries[index];
            if (entry->kind == g_mipe_category && entry->selectable) {
                list->emplace_back(entry);
            }
        }
    }
}

/* Mode-7 key handler: the item-category picker. Enter resolves the highlighted
   record back to a database index and drops into item-create mode; the arrows
   page and walk the six-row view and cycle categories. */
void HandleMipeItemCategoryKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    unsigned int item_index;

    list = g_mipe_category_list.get();
    switch (key) {
    case 0xd:
        item_index = FindMipeVisibleItem(g_mipe_table_base + g_mipe_table_row);
        if (item_index == gXStatus.uiItemsInDatabase) {
            item_index = 0;
        }
        g_mipe_item_index = static_cast<short>(item_index);
        ShowMipeItemStatus();
        g_mipe_mode = 2;
        return;
    case 0x25:
        if (g_mipe_category == 0) {
            return;
        }
        --g_mipe_category;
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        RebuildMipeItemCategory(list);
        break;
    case 0x27:
        if (0x18 < g_mipe_category) {
            return;
        }
        ++g_mipe_category;
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        RebuildMipeItemCategory(list);
        break;
    case 0x21:
    case 0x22:
    case 0x26:
    case 0x28:
        if (MoveMipeTableSelection(key)) {
            ShowMipeNamedCategory(g_mipe_category_list.get(), g_equip_class_name_ids);
        }
        return;
    case 0x20:
        break;
    default:
        return;
    }
    list = g_mipe_category_list.get();
    ShowMipeNamedCategory(list, g_equip_class_name_ids);
}

/* Mode-2 key handler: digits accumulate the count, Enter spawns that many of
   the selected item at the world cursor, 'A' flips the invisible-item display
   and activates/deactivates the flagged items, 'C' opens the item category
   picker and 'H' toggles the spawned item's hidden flag. */
unsigned char HandleMipeItemCreateKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    W8WorldItem* spawned;
    W8WorldItem* world_item;
    int index;
    unsigned int item_index;
    unsigned int spawned_index;
    bool show_invisible;
    srVector3T<float> anchor;

    if (!g_mipe_category_list) {
        g_mipe_category_list = std::make_unique<std::vector<W8MipeCategoryEntry>>();
        list = g_mipe_category_list.get();
        for (index = 0; index < static_cast<int>(gXStatus.uiItemsInDatabase); ++index) {
            if (g_item_records[index].equip_class == g_mipe_category &&
                g_item_records[index].editor_excluded == 0) {
                list->emplace_back(&g_item_records[index]);
            }
        }
    }
    list = g_mipe_category_list.get();
    switch (key) {
    case 0x20:
        ShowMipeItemStatus();
        break;
    case 0x43:
        g_mipe_count = 0;
        g_mipe_mode = 7;
        ShowMipeNamedCategory(list, g_equip_class_name_ids);
        return 1;
    case 8:
        g_mipe_count /= 10;
        ShowMipeItemStatus();
        break;
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        g_mipe_count =
            static_cast<int>(static_cast<char>(static_cast<char>(key) - 0x30)) + g_mipe_count * 10;
        ShowMipeItemStatus();
        break;
    case 0x48:
        g_mipe_item_hidden = !g_mipe_item_hidden;
        ShowMipeItemStatus();
        break;
    case 0x41:
        show_invisible = !g_hide_invisible_items;
        g_hide_invisible_items = show_invisible;
        for (item_index = 0; item_index < gXStatus.world_items.size(); ++item_index) {
            world_item = ItemInfo(item_index);
            if (ItemHasFlags(world_item, W8_WORLD_ITEM_HIDDEN)) {
                if (show_invisible) {
                    if (world_item->fActive) {
                        DeactivateWorldItem(world_item);
                    }
                } else if (!world_item->fActive) {
                    ActivateItem(world_item);
                }
            }
        }
        ShowMipeItemStatus();
        return 1;
    case 0xd:
        item_index = FindMipeVisibleItem(g_mipe_table_base + g_mipe_table_row);
        if (item_index == gXStatus.uiItemsInDatabase) {
            item_index = 0;
        }
        GetWorldCursorAnchor(&anchor);
        for (spawned_index = 0; spawned_index < static_cast<unsigned int>(g_mipe_count);
             ++spawned_index) {
            int flags = W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE;
            if (g_mipe_item_hidden) {
                flags =
                    W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE | W8_ITEM_ENTITY_HIGHLIGHT_BLUE;
            }
            spawned = SpawnItem(item_index & 0xffff, &anchor, flags, true);
            if (g_mipe_item_hidden) {
                SetItemFlags(spawned, W8_WORLD_ITEM_HIDDEN, true);
                RegisterSearchableWorldItem(spawned);
            }
        }
        return 1;
    default:
        return 0;
    }
    return 1;
}

/* Mode-6 key handler: the monster-category picker. Enter resolves the
   highlighted entry to a monster record, seeds the spawn count from the
   creation method and drops back to mode 1; the arrows page/walk rows and
   cycle through non-empty categories. */
void HandleMipeMonsterCategoryKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    int index;
    int wraps;
    unsigned int monster_index;
    W8MonsterRecord record;
    W8MonsterRecord selected;

    list = g_mipe_category_list.get();
    switch (key) {
    case 0xd:
        index = g_mipe_table_base + g_mipe_table_row;
        monster_index = FindMipeVisibleMonster(index);
        if (monster_index == gXStatus.uiMonstersInDatabase) {
            record.record_id = 0;
        } else {
            LoadMonsterDatabaseRecord(monster_index, &record);
        }
        g_mipe_monster_index = record.record_id;
        if (g_mipe_state->creation_method != 1) {
            if (g_mipe_state->creation_method == 2) {
                LoadMonsterDatabaseRecord(static_cast<int>(record.record_id), &selected);
                g_mipe_count = selected.group_size_dice.Maximum();
            }
            ShowMipeMonsterStatus();
            g_mipe_mode = 1;
            return;
        }
        g_mipe_count = 1;
        ShowMipeMonsterStatus();
        g_mipe_mode = 1;
        return;
    case 0x25:
        wraps = 0;
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        do {
            list = g_mipe_category_list.get();
            if (g_mipe_category == 0) {
                g_mipe_category = 0x20;
                ++wraps;
            } else {
                --g_mipe_category;
            }
            RebuildMipeMonsterCategory(list);
        } while ((!g_mipe_category_list || g_mipe_category_list->empty()) && wraps < 2);
        break;
    case 0x27:
        wraps = 0;
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        do {
            list = g_mipe_category_list.get();
            if (g_mipe_category < 0x20) {
                ++g_mipe_category;
            } else {
                g_mipe_category = 0;
                ++wraps;
            }
            RebuildMipeMonsterCategory(list);
        } while ((!g_mipe_category_list || g_mipe_category_list->empty()) && wraps < 2);
        break;
    case 0x21:
    case 0x22:
    case 0x26:
    case 0x28:
        if (MoveMipeTableSelection(key)) {
            ShowMipeNamedCategory(list, g_special_category_name_ids);
        }
        return;
    case 0x20:
        break;
    default:
        return;
    }
    list = g_mipe_category_list.get();
    ShowMipeNamedCategory(list, g_special_category_name_ids);
}

/* '1' halves the selected monster's animation scale, '2' doubles it, '3'
   arms speed editing, '4' sends it to the world cursor, '5' advances it one
   subcycle. */
void HandleMonsterDebugKey(unsigned short key)
{
    W8Monster* monster;
    W8MonsterInfo* info;
    W8World* world;
    signed char cycle;
    float scale;
    srVector3T<float> position;
    srVector3T<float> location;

    switch (key) {
    case 0x31:
        monster = GetMonsterByLocationID(g_mipe_state->SelectedMonsterLocationId());
        if (monster != 0) {
            scale = monster->GetCurrentAnimationScale();
            monster->SetCurrentAnimationScale(scale * g_float_half);
            return;
        }
        break;
    case 0x32:
        monster = GetMonsterByLocationID(g_mipe_state->SelectedMonsterLocationId());
        if (monster != 0) {
            scale = monster->GetCurrentAnimationScale();
            monster->SetCurrentAnimationScale(scale + scale);
            return;
        }
        break;
    case 0x33:
        if (static_cast<unsigned int>(g_mipe_state->monster_ids.size()) == 1) {
            g_mipe_mode = 0xb;
            ShowMonsterSpeedStatus();
            return;
        }
        break;
    case 0x34:
        if (static_cast<unsigned int>(g_mipe_state->monster_ids.size()) == 1) {
            info = FindMonster(g_mipe_state->SelectedMonsterLocationId());
            if (info == 0 || info->p3D == 0) {
                srAssertFail("pMonsterInfo && pMonsterInfo->p3D", MIPE_CPP, 0x6e5, 0);
            }
            monster = info->p3D;
            GetWorldCursorPosition(&position);
            location = MonsterGetLocation(monster);
            MonsterAddPathPoint(monster, &position);
            MonsterSetAnimating(monster, true);
            MonsterSetCycle(monster, 4);
            MonsterSetPathLooping(monster, 1);
            g_mipe_state->monster = monster;
            return;
        }
        break;
    case 0x35:
        if (static_cast<unsigned int>(g_mipe_state->monster_ids.size()) == 1) {
            info = FindMonster(g_mipe_state->SelectedMonsterLocationId());
            if (info == 0) {
                srAssertFail("pMonsterInfo", MIPE_CPP, 0x705, 0);
            }
            monster = info->p3D;
            if (monster == 0) {
                srAssertFail("pMonster", MIPE_CPP, 0x707, 0);
            }
            cycle = monster->m_pRep->current_subcycle;
            if (static_cast<char>(monster->m_pRep->GetNumSubsPerCycle(-1)) - 1 <= cycle) {
                cycle = monster->m_pRep->current_cycle + 1;
                do {
                    if (0x1a < cycle) {
                        goto scan_wrap;
                    }
                    do {
                        if (monster->m_pRep->GetNumSubsPerCycle(cycle) != 0) {
                            monster->SetCycle(cycle);
                            monster->SetForcedSubcycle(0);
                            monster->runtime_flags |= W8_MONSTER_KEEP_SUBCYCLE;
                            goto cycle_done;
                        }
                    scan_wrap:
                        ++cycle;
                    } while (cycle < 0x1b);
                    cycle = 0;
                } while (true);
            }
            monster->SetForcedSubcycle(cycle + 1);
        cycle_done:
            monster->SetSubCycle(0);
            monster->m_pRep->pending_subcycle = 0;
            world = GetWorld();
            location = cameraLocation(*world);
            position = location;
            monster->SelectLOD(&position);
            monster->ReplacePath(0);
        }
    }
}

/* The ',', '.', 'k' and 'l' keys while speed editing the debug monster. */
void AdjustMonsterSpeed(unsigned short key)
{
    W8PathAI* path;
    float speed;
    float factor;

    factor = 2.0f;
    if (g_mipe_state->monster == 0) {
        return;
    }
    path = static_cast<W8PathAI*>(MonsterGetAIRecord(g_mipe_state->monster));
    if (path == 0) {
        factor = 1.0f;
        speed = MonsterGetNavigatorMovementScale(g_mipe_state->monster);
    } else {
        if (path->kind != W8_AI_RECORD_PATH) {
            return;
        }
        speed = path->speed;
        speed += speed;
    }
    switch (key) {
    case 0xbc:
        speed = speed - factor * g_mipe_state->speed_step;
        if (path != 0) {
            path->speed = speed;
        }
        MonsterSetNavigatorMovementScale(g_mipe_state->monster, speed);
        break;
    case 0xbe:
        speed = factor * g_mipe_state->speed_step + speed;
        if (path != 0) {
            path->speed = speed;
        }
        MonsterSetNavigatorMovementScale(g_mipe_state->monster, speed);
        break;
    case 0x4c:
        g_mipe_state->speed_step = g_mipe_state->speed_step + g_camera_transition_epsilon;
        break;
    case 0x4b:
        g_mipe_state->speed_step = g_mipe_state->speed_step - g_camera_transition_epsilon;
        if (g_mipe_state->speed_step < static_cast<float>(g_double_one_thousandth)) {
            g_mipe_state->speed_step = 0.001f;
        }
        break;
    default:
        goto speed_done;
    }
    ShowMonsterSpeedStatus();
speed_done:
    if (path != 0 && path->speed < g_float_one_ten_thousandth) {
        path->speed = 0.0001f;
    }
}

static void ShowMipeWaypointStatus()
{
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Type 'C' to create a waypoint.");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_BLUE,
               fmt::format("Laying down waypoint {:d}", g_mipe_state->waypoint_count));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Type X to delete last waypoint.");
}

static void ShowMipeLockTrapStatus()
{
    Trigger* trigger;
    int key_id;
    std::string key_name;

    trigger = g_mipe_state->prop->GetTrigger();
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Edit Locks & Traps");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
               fmt::format("1) Type: {}", g_lock_type_names[trigger->lock_state.lock_type]));
    key_id = trigger->lock_state.key_id;
    if (key_id < 0) {
        key_name = "";
    } else {
        key_name = g_item_records[key_id].display_name;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("2) Key Id: ({:d}) {}", key_id, key_name));
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
               fmt::format(" Difficulty (3+/4-): {:d}", trigger->lock_state.difficulty));
}

/* 'C' drops a waypoint monster at the cursor, 'X' removes the newest one. */
void HandleWaypointKey(unsigned short key)
{
    W8Monster* monster;
    W8World* world;
    srVector3T<float> position;
    W8GrCycleLoadContext context;

    if (key == 0x43) {
        monster = 0;
        context.directory = "Data\\Monsters";
        context.world = GetWorld();
        LoadMonsterCycle(&context, "waypoint", &monster, -1, 1);
        GetWorldCursorPosition(&position);
        monster->SetCyclePosition(&position);
        world = GetWorld();
        AddMonsterToWorld(world, monster);
        world = GetWorld();
        UpdateCycleRepresentation(monster, world);
        ++g_mipe_state->waypoint_count;
        g_mipe_state->waypoints.push_back(monster);
        ShowMipeWaypointStatus();
    } else if (key == 0x58) {
        if (g_mipe_state) {
            unsigned int count = static_cast<unsigned int>(g_mipe_state->waypoints.size());
            if (count != 0) {
                monster = g_mipe_state->waypoints.back();
                if (monster != 0) {
                    g_mipe_state->waypoints.pop_back();
                    world = GetWorld();
                    RemoveMonsterFromWorldList(world, monster);
                    world = GetWorld();
                    DetachMonsterRepresentation(monster, world);
                    g_mipe_state->waypoint_count = g_mipe_state->waypoint_count - 1;
                }
            }
        }
        ShowMipeWaypointStatus();
    }
}

/* Mode-0xd key handler: scans the world's props for the picked one with a
   trigger, stores it as the edit target and opens the locks & traps ('1') or
   treasure-table ('2') editors. */
void HandleMipePropEditKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    W8World* world;
    W8Prop* prop;
    Trigger* trigger;
    W8ItemTableRecord* table;
    int index;
    int count;
    int table_index;

    g_mipe_count = 0;
    world = GetWorld();
    count = WorldGetPropCount(world);
    index = 0;
    if (0 < count) {
        while (true) {
            world = GetWorld();
            prop = WorldGetPropAt(world, index);
            trigger = prop->GetTrigger();
            if (trigger != 0 && prop->IsPickedProp(g_world)) {
                break;
            }
            ++index;
            if (count <= index) {
                return;
            }
        }
        if (prop != 0) {
            g_mipe_state->prop = prop;
            if (key == 0x31) {
                g_mipe_mode = 0x1b;
                ShowMipeLockTrapStatus();
            } else if (key == 0x32) {
                trigger = prop->GetTrigger();
                table_index = FindItemTableByName(trigger->inline_action_data);
                if (g_mipe_category_list == 0) {
                    g_mipe_category_list = std::make_unique<std::vector<W8MipeCategoryEntry>>();
                }
                list = g_mipe_category_list.get();
                if (table_index < 0) {
                    g_mipe_category = 0;
                    g_mipe_table_base = 0;
                    g_mipe_table_row = 0;
                    if (g_mipe_category_list != 0) {
                        g_mipe_category_list->clear();
                        for (index = 0; index < static_cast<int>(gXStatus.uiItemTablesInDatabase); ++index) {
                            table = &g_item_tables[index];
                            if (table->category_id == 0) {
                                list->emplace_back(table);
                            }
                        }
                    }
                } else {
                    unsigned char category = g_item_tables[table_index].category_id;
                    g_mipe_category = category;
                    if (g_mipe_category_list != 0) {
                        g_mipe_category_list->clear();
                        for (index = 0; index < static_cast<int>(gXStatus.uiItemTablesInDatabase); ++index) {
                            table = &g_item_tables[index];
                            if (table->category_id == static_cast<unsigned int>(category)) {
                                list->emplace_back(table);
                            }
                        }
                    }
                    for (index = 0;
                         index < static_cast<int>(
                                     (g_mipe_category_list ? g_mipe_category_list->size() : 0));
                         ++index) {
                        table = MipeCategoryValue<W8ItemTableRecord>(g_mipe_category_list.get(), index);
                        if (table == &g_item_tables[table_index]) {
                            break;
                        }
                    }
                    g_mipe_table_base = (index / 6) * 6;
                    g_mipe_table_row = index % 6;
                }
                g_mipe_menu_active = false;
                g_mipe_mode = 0x1d;
                ShowMipeItemTableCategory();
                return;
            }
        }
    }
}

/* Shared volume-cube menu expanded in editor key/mouse handlers. The
   helper name is descriptive; no original standalone spelling survives. */
static void ShowMipeCubeMenu()
{
    ShowMipeMenu("Choose an action:", {
        "1) Create cube.",
        "2) Delete cube.",
        "3) Edit cube parameters.",
        "4) Move cube.",
        "5) Scale cube.",
        "6) Select cube."});
}

/* The volume cube top menu: create, delete, edit parameters, move, scale and
   select. */
int HandleCubeMenuKey(unsigned int key)
{
    const char* prompt;
    srVector3T<float> position;
    srVector3T<float> anchor;
    std::string name;
    int serial;
    char scale_planes[4];

    switch (key & 0xffff) {
    case 0x20:
        ShowMipeCubeMenu();
        return 1;
    default:
        return 0;
    case 0x31:
        g_mipe_cube = CreateWorldCursorCube();
        GetCameraForwardPointCopy(2500.0f, &position);
        position.y = probeGround(position.glmValue()).position.y;
        MoveWorldCursorNode(g_mipe_cube, &position);
        AttachWorldCursorNode(g_mipe_cube, true);
        SetWorldCursorNodeColorComponents(g_mipe_cube, 0.0f, 1.0f, 0.0f);
        serial = gXStatus.mipe_cube_serial;
        ++gXStatus.mipe_cube_serial;
        name = fmt::format("Cube{:03d}", serial);
        SetWorldCursorNodeName(g_mipe_cube, name.c_str());
        return 1;
    case 0x32:
        if (g_mipe_cube != 0) {
            DestroyWorldCursorCube(g_mipe_cube);
            g_mipe_cube = 0;
        }
        return 1;
    case 0x33:
        if (g_mipe_cube != 0) {
            g_mipe_mode = 0x10;
            g_mipe_menu_active = false;
            ShowCubeParameters();
        }
        return 1;
    case 0x34:
        if (g_mipe_cube == 0) {
            return 1;
        }
        g_mipe_mode = 0x11;
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Move Volume Trigger.");
        if (g_mipe_cube == 0) {
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Click on trigger to move.");
        } else {
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Move trigger. Hold down SHIFT to");
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "change elevation.");
        }
        g_mipe_menu_active = false;
        ShowWorldCursor();
        WarpSystemCursor(0x140, 0xf0);
        GetWorldCursorAnchor(&anchor);
        MoveWorldCursorNode(g_mipe_cube, &anchor);
        g_mipe_state->dragging = true;
        g_mipe_menu_active = false;
        return 1;
    case 0x35:
        if (g_mipe_cube == 0) {
            return 1;
        }
        g_mipe_mode = 0x12;
        scale_planes[0] = 'X';
        scale_planes[1] = 'Y';
        scale_planes[2] = 'Z';
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Scale Volume Trigger.");
        if (g_mipe_cube != 0) {
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                       fmt::format("Scaling in the {:c} plane. ",
                                   static_cast<int>(scale_planes[g_mipe_scale_plane])));
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Press X/Y/Z to change plane.");
            g_mipe_menu_active = false;
            return 1;
        }
        prompt = "Click on trigger to scale.";
        break;
    case 0x36:
        g_mipe_mode = 0x13;
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Select cube:");
        if (g_mipe_cube == 0) {
            prompt = "Click on a cube to select it.";
        } else {
            prompt = "Click on another cube to select it.";
        }
        break;
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, prompt);
    g_mipe_menu_active = false;
    return 1;
}

/* The digit keys editing the selected cube parameter, plus the arrow keys
   stepping between Message, Search and Function. */
int HandleCubeParameterKey(unsigned int key)
{
    int value;

    switch (key & 0xffff) {
    case 0x20:
        ShowMipeCubeMenu();
        return 1;
    case 0x26:
        if (0 < g_mipe_cube_param) {
            --g_mipe_cube_param;
            ShowCubeParameters();
            return 1;
        }
        break;
    case 0x28:
        if (g_mipe_cube_param < 2) {
            ++g_mipe_cube_param;
            ShowCubeParameters();
            return 1;
        }
        break;
    case 8:
        if (g_mipe_cube != 0) {
            value = GetWorldCursorNodeParameter(g_mipe_cube, g_mipe_cube_param);
            SetWorldCursorNodeParameter(g_mipe_cube, g_mipe_cube_param, value / 10);
            DrawWorldCursorNodeLabel(g_mipe_cube);
            ShowCubeParameters();
            return 1;
        }
        break;
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        if (g_mipe_cube != 0 &&
            (value = GetWorldCursorNodeParameter(g_mipe_cube, g_mipe_cube_param), value < 99999)) {
            value = GetWorldCursorNodeParameter(g_mipe_cube, g_mipe_cube_param);
            SetWorldCursorNodeParameter(g_mipe_cube, g_mipe_cube_param,
                                        ((key & 0xffff) - 0x30) + value * 10);
            DrawWorldCursorNodeLabel(g_mipe_cube);
            ShowCubeParameters();
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/* The scale editor's keys: arrows resize the current plane and X/Y/Z pick the
   plane. */
int HandleCubeScaleKey(unsigned short key)
{
    char scale_planes[8];

    switch (key) {
    case 0x20:
        scale_planes[0] = 'X';
        scale_planes[1] = 'Y';
        scale_planes[2] = 'Z';
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Scale Volume Trigger.");
        if (g_mipe_cube == 0) {
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Click on trigger to scale.");
            return 1;
        }
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("Scaling in the {:c} plane. ",
                               static_cast<int>(scale_planes[g_mipe_scale_plane])));
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Press X/Y/Z to change plane.");
        return 1;
    default:
        return 0;
    case 0x26:
        if (g_mipe_cube != 0) {
            if (g_mipe_scale_plane == 0) {
                ScaleWorldCursorNodeX(g_mipe_cube, 1.1);
                return 1;
            }
            if (g_mipe_scale_plane == 1) {
                ScaleWorldCursorNodeY(g_mipe_cube, 1.1);
                return 1;
            }
            if (g_mipe_scale_plane == 2) {
                ScaleWorldCursorNodeZ(g_mipe_cube, 1.1);
                return 1;
            }
        }
        break;
    case 0x28:
        if (g_mipe_cube != 0) {
            if (g_mipe_scale_plane == 0) {
                ScaleWorldCursorNodeX(g_mipe_cube, 0.9);
                return 1;
            }
            if (g_mipe_scale_plane == 1) {
                ScaleWorldCursorNodeY(g_mipe_cube, 0.9);
                return 1;
            }
            if (g_mipe_scale_plane == 2) {
                ScaleWorldCursorNodeZ(g_mipe_cube, 0.9);
                return 1;
            }
        }
        break;
    case 0x58:
        g_mipe_scale_plane = 0;
        return 1;
    case 0x59:
        g_mipe_scale_plane = 1;
        return 1;
    case 0x5a:
        g_mipe_scale_plane = 2;
        break;
    }
    return 1;
}

/* The monster generator top-menu keys. */
static int HandleMonsterGeneratorKey(unsigned short key)
{
    MonGen* generator;
    MonGen* other;
    srVector3T<float> anchor;
    srVector3T<float> position;
    std::string name;
    int index;
    int count;

    switch (key) {
    case 0x20:
        break;
    default:
        return 0;
    case 0x31:
        GetWorldCursorAnchor(&anchor);
        count = GetMonsterGeneratorCount();
        for (index = 0; index < count; ++index) {
            GetMonsterGenerator(index)->SetActive(1, 0);
        }
        g_mipe_mongen_visible = true;
        ShowMonsterGeneratorStatus();
        generator = new MonGen();
        if (generator == 0) {
            srAssertFail("pMongen", MIPE_CPP, 0xa24,
                         "mipe.cpp: Out of memory creating monster generator");
        }
        position = anchor;
        generator->SetState(&position);
        generator->Reload(1, false);
        generator->Reset();
        name = fmt::format("MonGen{:03d}", gXStatus.saved_encounter_budget++);
        generator->SetName(name.c_str());
        AddMonsterGenerator(generator);
        for (index = 0; index < GetMonsterGeneratorCount(); ++index) {
            other = GetMonsterGenerator(index);
            if (other->marker_item != 0) {
                static_cast<W8ItemRep*>(other->marker_item->m_pRep)->SetFlags(0x10, false);
                other->marker_item->SetHighlight(false);
            }
        }
        g_mipe_state->generator = generator;
        if (generator->marker_item != 0) {
            static_cast<W8ItemRep*>(generator->marker_item->m_pRep)->SetFlags(0x10, true);
            generator->marker_item->SetHighlight(true);
        }
        return 1;
    case 0x32:
        if (g_mipe_state->generator != 0) {
            RemoveMonsterGenerator(g_mipe_state->generator);
            g_mipe_state->generator = 0;
            ShowMonsterGeneratorStatus();
        }
        return 1;
    case 0x33:
        if (g_mipe_state->generator != 0) {
            g_mipe_mode = 0x16;
            ShowMonsterGeneratorEditor();
        }
        return 1;
    case 0x34:
        g_mipe_state->selecting = 1;
        HideWorldCursor();
        ShowMonsterGeneratorStatus();
        return 1;
    case 0x35: {
        bool visible = !g_mipe_mongen_visible;
        count = GetMonsterGeneratorCount();
        for (index = 0; index < count; ++index) {
            GetMonsterGenerator(index)->SetActive(visible, 0);
        }
        g_mipe_mongen_visible = visible;
        ShowMonsterGeneratorStatus();
        ShowMonsterGeneratorStatus();
        return 1;
    }
    case 0x36:
        g_generator_save_flag = g_generator_save_flag == 0;
        break;
    }
    ShowMonsterGeneratorStatus();
    return 1;
}

/* The edit-menu keys for the selected monster generator: name entry, table
   picker, active toggle, per-generator or shared chance and interval, and the
   shared-interval flag. */
static void RebuildMipeEncounterCategory(std::vector<W8MipeCategoryEntry>* list)
{
    list->clear();
    int count = static_cast<int>(g_encounter_tables.size());
    for (int index = 0; index < count; ++index) {
        W8EncounterTableRuntime* entry = GetEncounterTable(index);
        if (entry->category == static_cast<unsigned int>(g_mipe_category)) {
            list->emplace_back(entry);
        }
    }
}

int HandleMonsterGeneratorEditKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    W8EncounterTableRuntime* table;
    W8EncounterTableRuntime* entry;
    MonGen* generator;
    int current_index;
    int found;
    int slot;

    if (!g_mipe_category_list) {
        g_mipe_category_list = std::make_unique<std::vector<W8MipeCategoryEntry>>();
        RebuildMipeEncounterCategory(g_mipe_category_list.get());
    }
    switch (key) {
    case 0x20:
        ShowMonsterGeneratorStatus();
        return 1;
    default:
        return 0;
    case 0x31:
        g_mipe_mode = 0x19;
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Enter the name for this generator:");
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, g_mipe_state->generator->name);
        return 1;
    case 0x32:
        current_index = g_mipe_state->generator->encounter_table_index;
        if (current_index < 0) {
            g_mipe_table_base = 0;
            g_mipe_table_row = 0;
        } else {
            table = GetEncounterTable(current_index);
            list = g_mipe_category_list.get();
            g_mipe_category = static_cast<unsigned char>(table->category);
            if (g_mipe_category_list != 0) {
                RebuildMipeEncounterCategory(list);
            }
            for (found = 0; found < static_cast<int>(
                                        (g_mipe_category_list ? g_mipe_category_list->size() : 0));
                 ++found) {
                table = MipeCategoryValue<W8EncounterTableRuntime>(g_mipe_category_list.get(), found);
                entry = GetEncounterTable(current_index);
                if (table == entry) {
                    break;
                }
            }
            g_mipe_table_base = (found / 6) * 6;
            g_mipe_table_row = found % 6;
        }
        g_mipe_mode = 0x17;
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK,
                   fmt::format("Category: {}", g_encounter_names[g_mipe_category & 0xff]));
        if (g_mipe_category_list == 0) {
            return 1;
        }
        for (slot = 0; slot < 6; ++slot) {
            entry = MipeCategoryValue<W8EncounterTableRuntime>(g_mipe_category_list.get(),
                                                               g_mipe_table_base + slot);
            if (entry == 0) {
                ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "");
            } else {
                ShowNotice(slot == g_mipe_table_row ? 3 : 0xf, fmt::format("    {}", entry->name));
            }
        }
        return 1;
    case 0x33:
        g_mipe_state->generator->generation_enabled =
            g_mipe_state->generator->generation_enabled == 0;
        ShowMonsterGeneratorEditor();
        return 1;
    case 0x34:
        generator = g_mipe_state->generator;
        if ((generator->flags >> 3 & 1) == 0) {
            if ('\0' < generator->custom_spawn_chance) {
                generator->custom_spawn_chance -= 10;
                ShowMonsterGeneratorEditor();
                return 1;
            }
        } else if ('\0' < static_cast<char>(g_generator_interval_min)) {
            g_generator_interval_min = static_cast<short>(
                static_cast<char>(static_cast<char>(g_generator_interval_min) - 10));
            ShowMonsterGeneratorEditor();
            return 1;
        }
        break;
    case 0x35:
        generator = g_mipe_state->generator;
        if ((generator->flags >> 3 & 1) == 0) {
            if (generator->custom_spawn_chance < 'd') {
                generator->custom_spawn_chance = generator->custom_spawn_chance + '\n';
                ShowMonsterGeneratorEditor();
                return 1;
            }
        } else if (static_cast<char>(g_generator_interval_min) < 'd') {
            g_generator_interval_min = static_cast<short>(
                static_cast<char>(static_cast<char>(g_generator_interval_min) + '\n'));
            ShowMonsterGeneratorEditor();
            return 1;
        }
        break;
    case 0x36:
        generator = g_mipe_state->generator;
        if ((generator->flags >> 3 & 1) == 0) {
            if (0 < generator->custom_interval_seconds) {
                generator->custom_interval_seconds -= 10;
                g_mipe_state->generator->Reset();
                ShowMonsterGeneratorEditor();
                return 1;
            }
        } else if (0 < g_generator_default_interval) {
            g_generator_default_interval -= 10;
            ShowMonsterGeneratorEditor();
            return 1;
        }
        break;
    case 0x37:
        generator = g_mipe_state->generator;
        if ((generator->flags >> 3 & 1) == 0) {
            generator->custom_interval_seconds += 10;
            g_mipe_state->generator->Reset();
            ShowMonsterGeneratorEditor();
            return 1;
        }
        g_generator_default_interval += 10;
        ShowMonsterGeneratorEditor();
        return 1;
    case 0x38:
        generator = g_mipe_state->generator;
        if ((generator->flags >> 3 & 1) != 0) {
            generator->flags &= 0xfffffff7;
            ShowMonsterGeneratorEditor();
            return 1;
        }
        generator->flags |= 8;
        break;
    }
    ShowMonsterGeneratorEditor();
    return 1;
}

/* Mode-0x17 key handler: the generator's encounter-table picker. Enter binds
   the highlighted table to the selected generator; the arrows page/walk the
   rows and cycle through the categories. */
void HandleMipeGeneratorTableKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    W8EncounterTableRuntime* entry;
    int index;
    int found;
    int wraps;
    int count;
    unsigned int table_index;

    count = static_cast<int>(g_encounter_tables.size());
    switch (key) {
    case 0xd:
        if (g_mipe_state->generator != 0) {
            found = 0;
            table_index = 0;
            index = g_mipe_table_base + g_mipe_table_row;
            if (0 < static_cast<int>(g_encounter_tables.size())) {
                unsigned int category = g_mipe_category & 0xff;
                do {
                    entry = GetEncounterTable(table_index);
                    if (entry->category == category) {
                        if (found == index) {
                            break;
                        }
                        ++found;
                    }
                    ++table_index;
                } while (static_cast<int>(table_index) < count);
            }
            g_mipe_state->generator->SetEncounterTable(table_index & 0xffff);
        }
        ShowMonsterGeneratorEditor();
        g_mipe_mode = 0x16;
        return;
    case 0x20:
        ShowMipeEncounterCategory();
        return;
    case 0x21:
    case 0x22:
    case 0x26:
    case 0x28:
        if (MoveMipeTableSelection(key)) {
            ShowMipeEncounterCategory();
        }
        return;
    case 0x25:
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        wraps = 0;
        do {
            list = g_mipe_category_list.get();
            unsigned char category;
            if (g_mipe_category == 0) {
                g_mipe_category = static_cast<unsigned char>(g_encounter_names.size());
                ++wraps;
            }
            category = g_mipe_category - 1;
            g_mipe_category = category;
            if (g_mipe_category_list != 0) {
                g_mipe_category_list->clear();
                count = static_cast<int>(g_encounter_tables.size());
                for (index = 0; index < count; ++index) {
                    entry = GetEncounterTable(index);
                    if (entry->category == static_cast<unsigned int>(category)) {
                        list->emplace_back(entry);
                    }
                }
            }
        } while ((!g_mipe_category_list || g_mipe_category_list->empty()) && wraps < 2);
        ShowMipeEncounterCategory();
        return;
    case 0x27:
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        wraps = 0;
        do {
            list = g_mipe_category_list.get();
            if ((g_mipe_category & 0xff) < static_cast<int>(g_encounter_names.size()) - 1) {
                ++g_mipe_category;
            } else {
                ++wraps;
                g_mipe_category = 0;
            }
            if (g_mipe_category_list != 0) {
                RebuildMipeEncounterCategory(list);
            }
        } while ((!g_mipe_category_list || g_mipe_category_list->empty()) && wraps < 2);
        ShowMipeEncounterCategory();
        return;
    }
}

/* Character entry for the generator name, capped at the 32-byte field. */
void EditMonsterGeneratorName(unsigned short key)
{
    std::string name;
    int length;

    name = g_mipe_state->generator->name;
    length = name.size();
    if (key == 8) {
        if (0 < length) {
            name[length - 1] = '\0';
        }
    } else if ((((0x2f < key) && (key < 0x3a)) || ((0x40 < key) && (key < 0x5b))) &&
               (length < 0x1f)) {
        if (!IsKeyHeld(VK_SHIFT)) {
            key += 0x20;
        }
        name[length] = static_cast<char>(key);
        name[length + 1] = '\0';
    }
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Enter the name for this generator:");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, g_mipe_state->generator->name);
}

/* Mode-0x1b key handler: the selected prop trigger's locks & traps editor.
   '1' cycles the lock type and re-rolls the pin state, '2' enters key-id
   input, '3'/'4' bump the difficulty, and every path repaints the menu. */
void HandleMipeLockTrapKey(unsigned short key)
{
    Trigger* trigger;
    Trigger* action_trigger;
    W8TriggerActionData* action;
    W8LockState* lock_state;
    int key_id;
    bool pending;

    trigger = g_mipe_state->prop->GetTrigger();
    lock_state = &trigger->lock_state;
    action_trigger = g_mipe_state->prop->GetTrigger();
    action = action_trigger->m_pActionData;
    if (action == 0 || action->type != W8_TRIGGER_PAYLOAD_DOOR) {
        action = 0;
    }
    switch (key) {
    case 0x31:
        ++lock_state->lock_type;
        if (3 < lock_state->lock_type) {
            lock_state->lock_type = 0;
        }
        lock_state->Reset();
        if (action == 0) {
            if (lock_state->lock_type == 3) {
                key_id = trigger->lock_state.key_id;
                g_mipe_state->prop->GetTrigger()->required_item_id = key_id;
            } else {
                g_mipe_state->prop->GetTrigger()->required_item_id = 0xffffffff;
            }
        } else {
            if (lock_state->lock_type == 0 || trigger->lock_state.device_state.completed != 0) {
                pending = false;
            } else {
                pending = true;
            }
            static_cast<W8DoorTriggerActionData*>(action)->locked = pending;
            static_cast<W8DoorTriggerActionData*>(action)->item =
                static_cast<short>(trigger->lock_state.key_id);
        }
        break;
    case 0x32:
        g_mipe_mode = 0x1c;
        trigger = g_mipe_state->prop->GetTrigger();
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Enter Key ID:");
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("{:d}", trigger->lock_state.key_id));
        return;
    case 0x33:
        ++trigger->lock_state.difficulty;
        if (10 < trigger->lock_state.difficulty) {
            trigger->lock_state.difficulty = 10;
        }
        break;
    case 0x34:
        --trigger->lock_state.difficulty;
        if (trigger->lock_state.difficulty < 0) {
            trigger->lock_state.difficulty = 0;
        }
    }
    ShowMipeLockTrapStatus();
}

/* The digit keys editing the selected prop trigger's key id. */
void EditTriggerKeyID(unsigned int key)
{
    Trigger* trigger;
    W8TriggerActionData* action;
    int key_id;
    bool pending;

    trigger = g_mipe_state->prop->GetTrigger();
    action = g_mipe_state->prop->GetTrigger()->m_pActionData;
    if (action == 0 || action->type != W8_TRIGGER_PAYLOAD_DOOR) {
        action = 0;
    }
    key_id = trigger->lock_state.key_id;
    switch (key & 0xffff) {
    case 8:
        key_id /= 10;
        break;
    case 0x2d:
        key_id = -1;
        break;
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        key_id = ((key & 0xffff) - 0x30) + key_id * 10;
        break;
    }
    trigger->lock_state.key_id = key_id;
    if (action == 0) {
        if (trigger->lock_state.lock_type == 3) {
            g_mipe_state->prop->GetTrigger()->required_item_id = key_id;
        } else {
            g_mipe_state->prop->GetTrigger()->required_item_id = 0xffffffff;
        }
    } else {
        if (trigger->lock_state.lock_type == 0 || trigger->lock_state.device_state.completed != 0) {
            pending = false;
        } else {
            pending = true;
        }
        static_cast<W8DoorTriggerActionData*>(action)->locked = pending;
        static_cast<W8DoorTriggerActionData*>(action)->item =
            static_cast<short>(trigger->lock_state.key_id);
    }
    trigger = g_mipe_state->prop->GetTrigger();
    ResetEditorStatusLine(-1);
    ShowNotice(W8_FONT_PALETTE_PINK, "Enter Key ID:");
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("{:d}", trigger->lock_state.key_id));
}

/* Mode-0x1d key handler: the prop trigger's treasure-table picker. Enter
   copies the highlighted table's name into the trigger's inline action data
   and returns to the prop menu; the arrows page/walk rows and cycle the item
   table categories. */
void HandleMipeItemTableKey(unsigned short key)
{
    std::vector<W8MipeCategoryEntry>* list;
    Trigger* trigger;
    W8ItemTableRecord* table;
    unsigned int table_index;
    int wraps;
    int index;

    switch (key) {
    case 0xd:
        if (g_mipe_state->prop != 0) {
            table_index =
                FindCategoryItemTable(g_mipe_category & 0xff, g_mipe_table_base + g_mipe_table_row);
            table = &g_item_tables[table_index & 0xffff];
            trigger = g_mipe_state->prop->GetTrigger();
            strcpy(trigger->inline_action_data, table->name);
            trigger->items_generated = 0;
        }
        ShowMipePropMenu();
        g_mipe_mode = 0xd;
        return;
    case 0x20:
        ShowMipeItemTableCategory();
        return;
    case 0x21:
    case 0x22:
    case 0x26:
    case 0x28:
        if (MoveMipeTableSelection(key)) {
            ShowMipeItemTableCategory();
        }
        return;
    case 0x25:
        wraps = 0;
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        do {
            unsigned char category;
            list = g_mipe_category_list.get();
            category = g_mipe_category;
            if (g_mipe_category == 0) {
                ++wraps;
                category = static_cast<unsigned char>(gXStatus.uiItemTableCategories);
            }
            g_mipe_category = category - 1;
            if (g_mipe_category_list != 0) {
                g_mipe_category_list->clear();
                for (index = 0; index < static_cast<int>(gXStatus.uiItemTablesInDatabase); ++index) {
                    table = &g_item_tables[index];
                    if (table->category_id == static_cast<unsigned int>(category - 1)) {
                        list->emplace_back(table);
                    }
                }
            }
        } while ((!g_mipe_category_list || g_mipe_category_list->empty()) && wraps < 2);
        ShowMipeItemTableCategory();
        return;
    case 0x27:
        wraps = 0;
        g_mipe_table_row = 0;
        g_mipe_table_base = 0;
        do {
            list = g_mipe_category_list.get();
            if ((g_mipe_category & 0xff) < gXStatus.uiItemTableCategories - 1) {
                ++g_mipe_category;
            } else {
                ++wraps;
                g_mipe_category = 0;
            }
            if (g_mipe_category_list != 0) {
                g_mipe_category_list->clear();
                for (index = 0; index < static_cast<int>(gXStatus.uiItemTablesInDatabase); ++index) {
                    table = &g_item_tables[index];
                    if (table->category_id == static_cast<unsigned int>(g_mipe_category & 0xff)) {
                        list->emplace_back(table);
                    }
                }
            }
        } while ((!g_mipe_category_list || g_mipe_category_list->empty()) && wraps < 2);
        ShowMipeItemTableCategory();
        return;
    }
}

/* The MIPE master key handler, fed input atoms while the panel is up. The
   early Escape block unwinds the mode stack, the combat-timer shortcut keeps
   only 'V'/'X' live, and the mode switch routes everything else into the
   per-mode handlers above. */
unsigned char HandleMipeKey(const InputEvent* event)
{
    bool handled;
    unsigned short key;
    InputEventType event_type;
    Trigger* trigger;
    W8Item* rep_item;
    int shown;

    handled = false;
    if (g_mipe_state->dragging) {
        return 1;
    }
    key = static_cast<unsigned short>(event->key);
    if (key == 0x1b) {
        if (event->type == InputEventType::KeyUp || event->type == InputEventType::KeyRepeat) {
            g_mipe_count = 0;
            if (g_mipe_mode == 6) {
                g_mipe_mode = 1;
                ShowMipeMonsterStatus();
            } else if (g_mipe_mode == 7) {
                g_mipe_mode = 2;
                ShowMipeItemStatus();
            } else if (g_mipe_mode == 2 || g_mipe_mode == 1) {
                g_mipe_mode = 0;
                if (g_mipe_category_list != 0) {
                    g_mipe_category_list.reset();
                    
                }
            } else {
                if (g_mipe_mode == 5) {
                    g_mipe_mode = 0;
                } else {
                    if (g_mipe_mode == 8) {
                        g_mipe_mode = 5;
                        ShowMipeEditMenu();
                        g_debug_monster_cycle = false;
                        return 1;
                    }
                    if (g_mipe_mode == 0xd) {
                        g_mipe_mode = 5;
                        ShowWorldCursor();
                        ShowMipeEditMenu();
                    } else if (g_mipe_mode == 0xe) {
                        g_mipe_mode = 0xd;
                        HandleMipeEditPropKey(0x1b);
                        ShowMipePropMenu();
                    } else if (g_mipe_mode == 3) {
                        g_mipe_mode = 8;
                        ShowMipeMonsterEditMenu();
                    } else if (g_mipe_mode == 9) {
                        ShowWorldCursor();
                        g_mipe_mode = 0;
                        g_mipe_state->selecting = 0;
                    } else {
                        if (g_mipe_mode != 4) {
                            if (g_mipe_mode == 0xb) {
                                g_mipe_mode = 8;
                                g_mipe_state->selecting = 0;
                                ShowMipeMonsterEditMenu();
                            } else if (g_mipe_mode == 0xc) {
                                if (g_mipe_state->selecting == 0) {
                                    g_mipe_mode = 0;
                                    trigger = g_mipe_state->trigger;
                                    if (trigger != 0 &&
                                        (rep_item = trigger->rep_item, rep_item != 0)) {
                                        rep_item->SetHighlight(false);
                                    }
                                    g_mipe_state->trigger = 0;
                                } else {
                                    ShowWorldCursor();
                                    g_mipe_state->selecting = 0;
                                    ShowMipeTriggerMenu();
                                }
                            } else if (g_mipe_mode == 0x15) {
                                if (g_mipe_state->selecting == 0) {
                                    g_mipe_mode = 0;
                                    if (g_mipe_state->generator != 0 &&
                                        g_mipe_state->generator->marker_item != 0) {
                                        g_mipe_state->generator->marker_item->SetHighlight(false);
                                    }
                                    g_mipe_state->generator = 0;
                                    if (g_mipe_category_list != 0) {
                                        g_mipe_category_list.reset();
                                        
                                    }
                                } else {
                                    ShowWorldCursor();
                                    g_mipe_state->selecting = 0;
                                    ShowMonsterGeneratorStatus();
                                }
                            } else if (g_mipe_mode == 0x16) {
                                g_mipe_state->selecting = 0;
                                g_mipe_mode = 0x15;
                                ShowMonsterGeneratorStatus();
                            } else if (g_mipe_mode == 0x17) {
                                g_mipe_state->selecting = 0;
                                g_mipe_mode = 0x16;
                                ShowMonsterGeneratorEditor();
                            } else if (g_mipe_mode == 0xf) {
                                if (g_mipe_state->selecting == 0) {
                                    ShowMipeTriggerMenu();
                                    SetWorldCursorNodesVisible(g_mipe_trigger_display);
                                    g_mipe_mode = 0xc;
                                    g_mipe_menu_active = false;
                                } else {
                                    ShowWorldCursor();
                                    g_mipe_state->selecting = 0;
                                    ShowMipeCubeMenu();
                                }
                            } else {
                                if (g_mipe_mode != 0x10) {
                                    if (g_mipe_mode == 0x11) {
                                        g_mipe_mode = 0xf;
                                        g_mipe_state->selecting = 0;
                                        HideWorldCursor();
                                        g_mipe_state->dragging = false;
                                        g_mipe_menu_active = true;
                                        ShowMipeCubeMenu();
                                        return 1;
                                    }
                                    if (g_mipe_mode == 0x12 || g_mipe_mode == 0x13) {
                                        g_mipe_mode = 0xf;
                                        ShowMipeCubeMenu();
                                        g_mipe_state->selecting = 0;
                                    } else if (g_mipe_mode == 0x18 || g_mipe_mode == 0x1a) {
                                        g_mipe_mode = 0x15;
                                        ShowWorldCursor();
                                        ShowMonsterGeneratorStatus();
                                    } else if (g_mipe_mode == 0x19) {
                                        g_mipe_mode = 0x16;
                                        ShowWorldCursor();
                                        ShowMonsterGeneratorEditor();
                                    } else if (g_mipe_mode == 0x1b) {
                                        g_mipe_mode = 0xd;
                                        ShowMipePropMenu();
                                    } else if (g_mipe_mode == 0x1c) {
                                        g_mipe_mode = 0x1b;
                                        ShowMipeLockTrapStatus();
                                    } else {
                                        if (g_mipe_mode == 0x1d) {
                                            g_mipe_mode = 0xd;
                                            ShowMipePropMenu();
                                            g_mipe_menu_active = true;
                                            if (g_mipe_category_list != 0) {
                                                g_mipe_category_list.reset();
                                                
                                            }
                                            return 1;
                                        }
                                        if (g_mipe_mode < 1) {
                                            g_mipe_state->monster = 0;
                                            ToggleMipePanel();
                                            return 1;
                                        }
                                        g_mipe_mode = 0;
                                    }
                                } else {
                                    g_mipe_mode = 0xf;
                                    ShowMipeCubeMenu();
                                    g_mipe_menu_active = true;
                                }
                            }
                        } else {
                            ShowWorldCursor();
                            g_mipe_mode = 0;
                            g_mipe_state->selecting = 0;
                        }
                    }
                }
            }
            handled = true;
        }
    }
    event_type = event->type;
    if (event_type == InputEventType::KeyDown) {
        switch (key) {
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
            if (g_mipe_mode != 6 && g_mipe_mode != 7 && g_mipe_mode != 0xf && g_mipe_mode != 0x12) {
                return handled;
            }
            return 1;
        default:
            return handled;
        case 8:
        case 0xd:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0xbc:
        case 0xbe:
            return 1;
        }
    }
    if (event_type != InputEventType::KeyUp && event_type != InputEventType::KeyRepeat) {
        return handled;
    }
    if (IsKeyHeld(VK_CONTROL)) {
        if (key == 0x56) {
            HideWorldCursor();
            g_mipe_mode = 0xf;
            g_mipe_menu_active = true;
            ShowMipeCubeMenu();
            SetWorldCursorNodesVisible(true);
            return handled;
        }
        if (key != 0x58) {
            return handled;
        }
        ToggleMipePanel();
        return handled;
    }
    switch (g_mipe_mode) {
    case 0:
        switch (key) {
        case 0x30:
            ShowMipeMainMenu();
            g_mipe_mode = 0;
            return handled;
        case 0x31:
            g_mipe_count = 0;
            ShowMipeMonsterStatus();
            g_mipe_mode = 1;
            return handled;
        case 0x32:
            g_mipe_count = 0;
            ShowMipeItemStatus();
            g_mipe_mode = 2;
            return handled;
        case 0x33:
            ShowMipeEditMenu();
            g_mipe_mode = 5;
            return handled;
        case 0x35:
            ShowMipeChooseMenu();
            HideWorldCursor();
            g_mipe_state->selecting = 1;
            g_mipe_mode = 9;
            return handled;
        case 0x36:
            ShowMipeTriggerMenu();
            g_mipe_mode = 0xc;
            return handled;
        case 0x34:
            ShowMonsterGeneratorStatus();
            g_mipe_mode = 0x15;
            return handled;
        default:
            break;
        }
        break;
    case 1:
        return HandleMipeMonsterCreateKey(key);
    case 2:
        return HandleMipeItemCreateKey(key);
    case 3:
        if (key == 0x35) {
            g_mipe_mode = 10;
            g_mipe_state->waypoint_count = 0;
            ShowMipeWaypointStatus();
            return handled;
        }
        break;
    case 5:
        if (key == 0x31) {
            g_mipe_count = 0;
            g_mipe_mode = 8;
            ShowMipeMonsterEditMenu();
            g_debug_monster_cycle = true;
            return handled;
        }
        if (key == 0x32) {
            g_mipe_count = 0;
            g_mipe_mode = 0xd;
            HideWorldCursor();
            ShowMipePropMenu();
            return handled;
        }
        if (key == 0x34) {
            ShowMipeSelectionMenu("MOVE IT!!");
            g_mipe_mode = 4;
            g_mipe_state->selecting = 1;
            HideWorldCursor();
            return handled;
        }
        break;
    case 4:
        if (key == 0x43) {
            g_mipe_choose_group = !g_mipe_choose_group;
            if (g_mipe_state) {
                g_mipe_state->monster_ids.clear();
            }
            ShowMipeSelectionMenu("MOVE IT!!");
            return handled;
        }
        break;
    case 6:
        HandleMipeMonsterCategoryKey(key);
        return handled;
    case 7:
        HandleMipeItemCategoryKey(key);
        return handled;
    case 8:
        HandleMonsterDebugKey(key);
        return handled;
    case 0xd:
        HandleMipePropEditKey(key);
        return handled;
    case 0xe:
        HandleMipeEditPropKey(key);
        return handled;
    case 9:
        if (key == 0x43) {
            g_mipe_choose_group = !g_mipe_choose_group;
            if (g_mipe_state) {
                g_mipe_state->monster_ids.clear();
            }
            ShowMipeSelectionMenu("Choose monster or item to edit.");
            return handled;
        }
        break;
    case 10:
        HandleWaypointKey(key);
        return handled;
    case 0xb:
        AdjustMonsterSpeed(key);
        return handled;
    case 0xc:
        switch (key) {
        case 0x20:
            ShowMipeTriggerMenu();
            return handled;
        case 0x35:
            g_mipe_trigger_display = !g_mipe_trigger_display;
            SetWorldCursorNodesVisible(g_mipe_trigger_display);
            ShowMipeTriggerMenu();
            return handled;
        case 0x36:
            g_mipe_mode = 0xf;
            HideWorldCursor();
            ShowMipeCubeMenu();
            g_mipe_menu_active = true;
            SetWorldCursorNodesVisible(true);
            return handled;
        }
        break;
    case 0xf:
        HandleCubeMenuKey(key);
        return handled;
    case 0x10:
        HandleCubeParameterKey(key);
        return handled;
    case 0x12:
        HandleCubeScaleKey(key);
        return handled;
    case 0x14:
        if (key == 0x31) {
            ShowMonsterGeneratorStatus();
            g_mipe_mode = 0x15;
            return handled;
        }
        break;
    case 0x15:
        HandleMonsterGeneratorKey(key);
        return handled;
    case 0x16:
        HandleMonsterGeneratorEditKey(key);
        return handled;
    case 0x17:
        HandleMipeGeneratorTableKey(key);
        return handled;
    case 0x18:
        switch (key) {
        case 0x20:
            ResetEditorStatusLine(-1);
            ShowNotice(W8_FONT_PALETTE_PINK, "Enter the limit for random encounters:");
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("{:d}", g_random_encounter_limit));
            return handled;
        case 8:
            g_random_encounter_limit /= 10;
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
            g_random_encounter_limit = (key - 0x30) + g_random_encounter_limit * 10;
            break;
        default:
            return handled;
        }
        ResetEditorStatusLine(-1);
        ShowNotice(W8_FONT_PALETTE_PINK, "Enter the limit for random encounters:");
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("{:d}", g_random_encounter_limit));
        return handled;
    case 0x19:
        EditMonsterGeneratorName(key);
        return handled;
    case 0x1a:
        switch (key) {
        case 0x20:
            ResetEditorStatusLine(-1);
            ShowNotice(W8_FONT_PALETTE_PINK, "Enter the encounter culling time (sec):");
            shown = g_encounter_culling_time_seconds;
            break;
        case 8:
            g_encounter_culling_time_seconds /= 10;
            ResetEditorStatusLine(-1);
            ShowNotice(W8_FONT_PALETTE_PINK, "Enter the encounter culling time (sec):");
            shown = g_encounter_culling_time_seconds;
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
            g_encounter_culling_time_seconds = (key - 0x30) + g_encounter_culling_time_seconds * 10;
            ResetEditorStatusLine(-1);
            ShowNotice(W8_FONT_PALETTE_PINK, "Enter the encounter culling time (sec):");
            ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                       fmt::format("{:d}", g_encounter_culling_time_seconds));
            return handled;
        default:
            return handled;
        }
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, fmt::format("{:d}", shown));
        return handled;
    case 0x1b:
        HandleMipeLockTrapKey(key);
        return handled;
    case 0x1c:
        EditTriggerKeyID(key);
        return handled;
    case 0x1d:
        HandleMipeItemTableKey(key);
        return handled;
    }
    return handled;
}

bool IsMipeActive(void)
{
    return g_mipe_active;
}
bool IsMipeMenuActive(void)
{
    return g_mipe_menu_active;
}

/* Index of the `ordinal`-th item table in `category`, or the table count when
   fewer than `ordinal` tables match. */
int FindCategoryItemTable(unsigned int category, int ordinal)
{
    int found = 0;
    int index;

    for (index = 0; index < static_cast<int>(gXStatus.uiItemTablesInDatabase); ++index) {
        if (g_item_tables[index].category_id == (category & 0xff)) {
            if (found == ordinal) {
                return index;
            }
            ++found;
        }
    }
    return index;
}

/* Per-tick world-view pick while selecting is armed. In mode 0x15 it re-picks
   the nearest camera-close generator marker whose item is selected, refreshes
   marker highlights and the generator status; otherwise it picks the monster
   under the cursor. The group-choose flag collects every active monster of the
   pick's group into monster_ids; the single-monster path keeps at most one. */
void UpdateMipeSelection(void)
{
    SDL_Point point;
    MonGen* picked;
    W8MonsterInfo* info;
    W8Item* marker;
    float best_distance;
    int group_id;
    int location_id;
    int index;

    if (g_mipe_state->selecting == 0) {
        return;
    }
    SGPMouseGetPos(&point);
    if (g_mipe_mode == 0x15) {
        picked = 0;
        best_distance = 999999.0f;
        for (index = 0; index < GetMonsterGeneratorCount(); ++index) {
            MonGen* entry = GetMonsterGenerator(index);
            float distance;

            marker = entry->marker_item;
            if (marker == 0 || !marker->IsSelected()) {
                continue;
            }
            distance = marker->DistanceToCamera(GetWorld());
            if (distance < best_distance) {
                best_distance = distance;
                picked = entry;
            }
        }
        for (index = 0; index < GetMonsterGeneratorCount(); ++index) {
            marker = GetMonsterGenerator(index)->marker_item;
            if (marker != 0) {
                static_cast<W8ItemRep*>(marker->m_pRep)->SetFlags(0x10, false);
                marker->SetHighlight(false);
            }
        }
        if (picked != 0) {
            marker = picked->marker_item;
            if (marker != 0) {
                static_cast<W8ItemRep*>(marker->m_pRep)->SetFlags(0x10, true);
                marker->SetHighlight(true);
            }
        }
        g_mipe_state->generator = picked;
        ShowMonsterGeneratorStatus();
    }
    location_id = PickNearestMonsterUnderCursor(point.x, point.y);
    if (g_mipe_choose_group) {
        if (location_id == -1) {
            group_id = W8_MIPE_NO_GROUP;
        } else {
            group_id = FindMonster(location_id)
                           ->monster_group_id;
        }
        if (group_id == g_mipe_state->selected_group_id) {
            return;
        }
        for (index = 0; index < static_cast<int>(g_mipe_state->monster_ids.size()); ++index) {
            int listed = g_mipe_state->monster_ids[index];
            info = FindMonster(listed);
            if (info->fActive) {
                SetMonsterHighlight(0, listed, false);
            }
        }
        g_mipe_state->monster_ids.clear();
        for (index = 0;
             index <
             static_cast<int>(gXStatus.monsters.size());
             ++index) {
            info = GetMonsterListEntry(index);
            if (info->fActive && info->monster_group_id == group_id) {
                SetMonsterHighlight(0, info->location_id, true);
                g_mipe_state->monster_ids.push_back(info->location_id);
            }
        }
        if (static_cast<unsigned int>(g_mipe_state->monster_ids.size()) == 1) {
            info = FindMonster(location_id);
            if (info == 0) {
                srAssertFail("pMonsterInfo", MIPE_CPP, 0x10ea, 0);
            }
            g_mipe_state->monster = info->p3D;
        }
        g_mipe_state->selected_group_id = group_id;
        SetWorldCursorGroupId(group_id);
        return;
    }
    if (location_id != -1 && std::ranges::contains(g_mipe_state->monster_ids, location_id)) {
        return;
    }
    if (!g_mipe_state->monster_ids.empty()) {
        int old_id = g_mipe_state->SelectedMonsterLocationId();
        g_mipe_state->monster_ids.clear();
        SetMonsterHighlight(0, old_id, false);
    }
    if (location_id == -1) {
        return;
    }
    SetMonsterHighlight(0, location_id, true);
    g_mipe_state->monster_ids.push_back(location_id);
    info = FindMonster(location_id);
    if (info == 0) {
        srAssertFail("pMonsterInfo", MIPE_CPP, 0x110e, 0);
    }
    g_mipe_state->monster = info->p3D;
    SetWorldCursorGroupId(info->monster_group_id);
}

/* Drag the selected monsters, or the selected trigger, by the world cursor's
   delta from the drag anchor. */
void DragSelectionWithCursor(void)
{
    srVector3T<float> cursor;
    srVector3T<float> position;
    srVector3T<float> moved;
    W8MonsterInfo* info;
    int index;

    GetWorldCursorPosition(&cursor);
    if (g_mipe_state->trigger != 0) {
        g_mipe_state->trigger->GetPosition(&position);
        moved.Set(cursor.x - g_mipe_state->drag_anchor.x + position.x,
                  cursor.y - g_mipe_state->drag_anchor.y + position.y,
                  cursor.z - g_mipe_state->drag_anchor.z + position.z);
        g_mipe_state->trigger->SetPosition(&moved);
    } else {
        for (index = 0; index < static_cast<int>(g_mipe_state->monster_ids.size()); ++index) {
            info = FindMonster(g_mipe_state->monster_ids[index]);
            position = MonsterGetLocalLocation(info->p3D);
            moved.Set(cursor.x - g_mipe_state->drag_anchor.x + position.x,
                      cursor.y - g_mipe_state->drag_anchor.y + position.y,
                      cursor.z - g_mipe_state->drag_anchor.z + position.z);
            info->p3D->SetCyclePosition(&moved);
        }
    }
    g_mipe_state->drag_anchor = cursor;
}

/* The world-view share of MIPE input: left-down starts a cube drag in mode 4
   or picks another cube in mode 0x13, left-up drops the drag and un-highlights
   the trigger's item, right-up leaves cube selection for the action menu, and
   mouse motion drags or tracks the cube. Returns whether it consumed the
   event. */
bool MipeWorldViewEvent(InputEventType event, const SDL_Point* point)
{
    bool result;

    result = false;
    if (!g_mipe_state) {
        return false;
    }
    switch (event) {
    case InputEventType::LeftButtonDown:
        if (g_mipe_mode == 4) {
            result = true;
            g_mipe_state->dragging = true;
            ShowWorldCursor();
            GetWorldCursorPosition(&g_mipe_state->drag_anchor);
            WarpSystemCursor(0x140, 0xf0);
            g_mipe_state->trigger = 0;
        } else if (g_mipe_mode == 0x13) {
            if (g_mipe_cube != 0) {
                SetWorldCursorNodeColorComponents(g_mipe_cube, 0.0f, 0.0f, 0.5f);
                RefreshWorldCursorNodeLabel(g_mipe_cube);
            }
            g_mipe_cube = PickWorldCursorNodeAtScreenPoint(point->x, point->y);
            if (g_mipe_cube != 0) {
                SetWorldCursorNodeColorComponents(g_mipe_cube, 0.0f, 1.0f, 0.0f);
                RefreshWorldCursorNodeLabel(g_mipe_cube);
            }
            ResetEditorStatusLine(-1);
            ShowNotice(W8_FONT_PALETTE_PINK, "Select cube:");
            if (g_mipe_cube == 0) {
                ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Click on a cube to select it.");
            } else {
                ShowNotice(W8_FONT_PALETTE_TEXT_BOX, "Click on another cube to select it.");
            }
        }
        break;
    case InputEventType::LeftButtonUp:
        if (g_mipe_mode == 4) {
            DragSelectionWithCursor();
            result = true;
            g_mipe_state->dragging = false;
            HideWorldCursor();
            if (g_mipe_state->trigger != 0) {
                Trigger* trigger = g_mipe_state->trigger;
                W8Item* item;

                trigger->flags &= ~0x20u;
                item = trigger->rep_item;
                if ((trigger->flags & W8_TRIGGER_ON) != 0 && item != 0) {
                    static_cast<W8ItemRep*>(item->m_pRep)->SetFlags(0x10, false);
                    item->SetHighlight(false);
                }
                g_mipe_state->trigger = 0;
            }
        }
        break;
    case InputEventType::RightButtonUp:
        if (g_mipe_mode == 0x11) {
            g_mipe_mode = 0xf;
            g_mipe_state->selecting = 0;
            HideWorldCursor();
            g_mipe_state->dragging = false;
            g_mipe_menu_active = true;
            ShowMipeCubeMenu();
        }
        break;
    case InputEventType::MouseMove:
        if (g_mipe_state->dragging) {
            if (g_mipe_mode == 4) {
                DragSelectionWithCursor();
                result = true;
            } else if (g_mipe_mode == 0x11) {
                result = true;
                if (g_mipe_cube != 0) {
                    srVector3T<float> position;

                    GetWorldCursorAnchor(&position);
                    MoveWorldCursorNode(g_mipe_cube, &position);
                }
            } else if (g_mipe_mode == 0x12) {
                result = true;
            }
        }
        break;
    default:
        break;
    }
    return result;
}

/* Monster-generator index that last satisfied
   AnyMonsterGeneratorMarkerWithinReach. */
// GLOBAL: WIZ8 0x0064A1E0
static int g_last_reachable_mongen_marker = -1;

/* Any monster-generator marker item within reach of the camera (radius
   250000), resuming the scan at the last match. */
bool AnyMonsterGeneratorMarkerWithinReach(void)
{
    srVector3T<float> camera;
    int count;
    int index;
    MonGen* generator;
    W8Item* marker;

    if (g_world == 0 || g_world->camera == 0) {
        return false;
    }
    camera = cameraLocation();
    count = GetMonsterGeneratorCount();
    if (0 <= g_last_reachable_mongen_marker && g_last_reachable_mongen_marker < count) {
        generator = GetMonsterGenerator(g_last_reachable_mongen_marker);
        if (generator == 0) {
            marker = 0;
        } else {
            marker = generator->marker_item;
        }
        if (marker != 0 && IsWorldItemWithinReach(marker, &camera, 250000.0f)) {
            return true;
        }
    }
    for (index = 0; index < count; ++index) {
        generator = GetMonsterGenerator(index);
        if (generator == 0) {
            marker = 0;
        } else {
            marker = generator->marker_item;
        }
        if (marker != 0 && IsWorldItemWithinReach(marker, &camera, 250000.0f)) {
            g_last_reachable_mongen_marker = index;
            return true;
        }
    }
    return false;
}
