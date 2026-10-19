#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compat/compiler.h"
#include "wiz8/character_skills.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/equipment_slots.h"
#include "wiz8/item_location.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/RCSStatsPage.h"
#include "wiz8/platform/input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

/* The UI orders Other before Accessory, and places the two non-group filters last. */
enum W8CampItemFilterButton {
    W8_CAMP_ITEM_FILTER_BUTTON_HAND = 0,
    W8_CAMP_ITEM_FILTER_BUTTON_BODY = 1,
    W8_CAMP_ITEM_FILTER_BUTTON_OTHER = 2,
    W8_CAMP_ITEM_FILTER_BUTTON_ACCESSORY = 3,
    W8_CAMP_ITEM_FILTER_BUTTON_USABLE = 4,
    W8_CAMP_ITEM_FILTER_BUTTON_UNIDENTIFIED = 5
};

struct W8ScreenStateRuntime;
struct W8Character;
struct W8CombatSlot;

enum CampMainRegionOffset {
    kCampPartyPortraitRegionOffset = 1,
    kCampCharacterHeaderRegionOffset = 9
};

enum CampInventoryRegionOffset { kCampEquipmentRegionOffset = 8 };

enum W8CampPage {
    W8_CAMP_PAGE_ITEMS = 0,
    W8_CAMP_PAGE_STATS = 1,
    W8_CAMP_PAGE_SKILLS = 2,
    W8_CAMP_PAGE_SPELLS = 3,
    W8_CAMP_PAGE_REGENERATION = 4
};

enum W8CampItemAction : unsigned char {
    W8_CAMP_ITEM_ACTION_NONE = 0,
    W8_CAMP_ITEM_ACTION_MOVE = 1,
    W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL = 2,
    W8_CAMP_ITEM_ACTION_IDENTIFY = 3,
    W8_CAMP_ITEM_ACTION_SPLIT_STACK = 4,
    W8_CAMP_ITEM_ACTION_USE = 5,
    W8_CAMP_ITEM_ACTION_DROP = 6,
    W8_CAMP_ITEM_ACTION_CAST_SPELL = 7,
    W8_CAMP_ITEM_ACTION_USE_ON_ITEM = 8,
    W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER = 9
};

enum W8CampInfoPage : unsigned char {
    W8_CAMP_INFO_PAGE_ITEMS = 0,
    W8_CAMP_INFO_PAGE_CHARACTER = 1
};

enum W8CampHeaderMode { W8_CAMP_HEADER_SUMMARY = 0, W8_CAMP_HEADER_PROFESSION_HISTORY = 1 };

enum W8CampEffectFilter {
    W8_CAMP_EFFECT_FILTER_ALL = 0,
    W8_CAMP_EFFECT_FILTER_BENEFICIAL = 1,
    W8_CAMP_EFFECT_FILTER_DETRIMENTAL = 2
};

/* Stored item-filter order; the four group indices match GetItemEquipSlotGroup. */
enum W8CampItemFilter {
    W8_CAMP_ITEM_FILTER_USABLE = 0,
    W8_CAMP_ITEM_FILTER_UNIDENTIFIED = 1,
    W8_CAMP_ITEM_FILTER_HAND = 2,
    W8_CAMP_ITEM_FILTER_BODY = 3,
    W8_CAMP_ITEM_FILTER_ACCESSORY = 4,
    W8_CAMP_ITEM_FILTER_OTHER = 5,
    W8_CAMP_ITEM_FILTER_COUNT = 6
};

struct W8CampScreenState;

class W8CampCharacterInfo {
    W8CampScreenState& screen;

public:
    explicit W8CampCharacterInfo(W8CampScreenState& screen);
    void SetActive(bool enabled);
    void Redraw(SDL_Surface& target, bool full_redraw);
    void SetCombatView(bool enabled);
    void InvalidateAll() { m_dirty = true; }
    UiLayer input;
    bool m_combat_view = true;
    bool m_active = false;
    bool m_dirty = true;
    int m_backgroundImage = 0;
    std::unique_ptr<W8ButtonWidget> m_combat_tab;
    std::unique_ptr<W8ButtonWidget> m_stats_tab;
    std::array<HitTarget*, 4> m_values;
    std::array<std::string, 4> m_help;
};

/* W8CampScreenState::redraw_flags. Party-slot bits are shifted by the slot
   and realm-spell bits by the realm. */
enum W8CampRedrawFlag {
    W8_CAMP_REDRAW_PARTY_SLOT_FIRST = 0x00000001,
    W8_CAMP_REDRAW_PORTRAIT = 0x00000100,
    W8_CAMP_REDRAW_ITEM_ACTIONS = 0x00001000,
    W8_CAMP_REDRAW_CHARACTER_INFO = 0x00002000,
    W8_CAMP_REDRAW_EFFECT_LIST = 0x00020000,
    W8_CAMP_REDRAW_RESISTANCES = 0x00100000,
    W8_CAMP_REDRAW_REALM_SPELLS_FIRST = 0x00200000,
    W8_CAMP_REDRAW_ACTION_PANEL = 0x08000000,
    W8_CAMP_REDRAW_ALL = 0x0fffffff,
    W8_CAMP_REDRAW_REALM_TABS = 0x10000000
};

/* W8CampScreenState::item_redraw_flags. Cell bits are shifted by the cell. */
enum W8CampItemRedrawFlag {
    W8_CAMP_ITEM_REDRAW_BACKPACK_HEADER = 0x00000001,
    W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST = 0x00000002,
    W8_CAMP_ITEM_REDRAW_BACKPACK = 0x000001ff,
    W8_CAMP_ITEM_REDRAW_PAPER_DOLL = 0x00000200,
    W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST = 0x00000400,
    W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELLS = 0x003ffc00,
    W8_CAMP_ITEM_REDRAW_EQUIPMENT = 0x003ffe00,
    W8_CAMP_ITEM_REDRAW_POOL_HEADER = 0x00400000,
    W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST = 0x00800000,
    W8_CAMP_ITEM_REDRAW_POOL = 0x7fc00000
};

// Suspension destroys these controls; routing retains the screen arguments.
struct CampHeaderButtons {
    UiLayer input{UiLayerOrder::GameplayOverlays};
    std::unique_ptr<W8ButtonWidget> level_up;
    std::unique_ptr<W8ButtonWidget> dismiss;
};
struct CampButtons {
    UiLayer input{UiLayerOrder::CampButtons};
    std::array<std::unique_ptr<W8ButtonWidget>, 5> pages;
    std::array<std::unique_ptr<W8ButtonWidget>, W8_CAMP_ACTION_BUTTON_COUNT> actions;
    bool dirty = true;
};

struct CampItemControls {
    UiLayer input{UiLayerOrder::CampPool};
    UiLayer secondary;
    std::array<std::unique_ptr<W8ButtonWidget>, 2> actions;
    std::array<std::unique_ptr<W8ButtonWidget>, 7> filters;
    std::array<std::unique_ptr<W8ButtonWidget>, 2> tabs;
    std::array<HitTarget*, 7> stats;
    std::array<HitTarget*, 4> info;
    HitTarget* help;
    std::string help_text;
    bool dirty = true;
};

/* The items page: the reviewed character's equipment and backpack cells, the party
   pool list and its filters, the item actions, and the character-info panel beside
   them. The camp screen owns it and routes input and drawing to it. */
class W8CampItemsPage {
public:
    explicit W8CampItemsPage(W8CampScreenState& screen) : screen(screen) {}
    W8CampItemsPage(const W8CampItemsPage&) = delete;
    W8CampItemsPage& operator=(const W8CampItemsPage&) = delete;

    void CreateInput();
    // Start a camp visit; an identify spell opens the page targeting an item.
    void Reset(W8Character* identifying_character);
    void Activate();
    void Deactivate();
    void ReviewedCharacterChanged();
    void Tick();
    // Cells whose items changed repaint with the next camp draw.
    void BeginRedraw();
    bool RedrawPending() const { return item_redraw_flags != 0; }
    void InvalidateAll() { item_redraw_flags = 0xffffffff; }
    void EndRedraw() { item_redraw_flags = 0; }
    void DrawControls(bool dialog_open);
    void InvalidateControls() { item_controls.dirty = true; }
    W8CampItemAction action() const { return item_action; }
    std::span<const int> ListedPoolEntries() const
    {
        return std::span{item_list}.first(item_list_count);
    }

    void RefreshCampItemActions(bool invalidate);
    void RedrawCampItemsPage();
    unsigned int ChangedItemCells();
    void RebuildCampItemList();
    bool ResolvePendingCampCharacter(bool force);
    void SetCampInfoPageMode(W8CampInfoPage mode);
    void ReportCastResult(int party_slot);
    void TargetCharacterWithHeldItem(unsigned int uiTargetChar);
    void UpdateItemCursorForState(bool alternate, W8ItemInstance* item, int slot);
    UiResult HandleCampItem(const UiPointerEvent& event, int slot, W8ItemStorage storage);
    void DrawCampItemIcons();
    void EnableCampActionButtons();
    void DisableCampActionButtons();
    void RefreshCampActionPanel(bool invalidate);
    void UpdateCampItemFilters();
    void DisableItemsRealmTabs();
    void RefreshItemsTabPanel(bool invalidate);
    void EnableCampSecondaryPanel();
    void DisableCampSecondaryPanel();
    void RefreshCampSecondaryPanel(bool invalidate);
    void SelectCampItemAction(W8CampItemActionButton button);
    void SetCampItemActionMode(W8CampItemAction mode);
    void CreateCampItemControls();

private:
    W8CampScreenState& screen;
    CampItemControls item_controls;
    // What the item cells last showed. Any inventory change, whatever its source,
    // repaints the cells that show it.
    struct ShownInventory {
        const W8Character* character = nullptr;
        std::array<W8ItemInstance, 8> backpack{};
        std::array<W8ItemInstance, 12> equipment{};
        std::vector<W8ItemInstance> pool;
    } shown_inventory;
    struct PendingItemAction {
        int caster_slot = -1;
        W8Character* identifying_character = nullptr;
    } pending_item_action;
    UiLayer inventory_input{UiLayerOrder::CampInventory};
    UiLayer pool_input{UiLayerOrder::CampPool};
    std::array<HitTarget*, 20> inventory_targets{};
    std::array<HitTarget*, 8> pool_targets{};
    unsigned int item_redraw_flags;
    unsigned char item_filters[W8_CAMP_ITEM_FILTER_COUNT];
    unsigned int item_scroll;
    /* The displayed item-pool indices - the count and the list of pool
       slots RCSItemsPage.cpp renders. RebuildCampItemList rebuilds it; the pool
       handler reads it through item_scroll. */
    unsigned int item_list_count;
    int item_list[500];
    std::unique_ptr<W8RangeControl> item_range;
    uint64_t item_timer;
    bool item_timer_active;
    bool item_timer_expired;
    W8CampItemAction item_action;
    W8CampInfoPage info_page;
    /* The camp item icons were drawn while the monster/combat
       timer was enabled; its stop forces a full redraw to drop them. */
    bool item_icons_drawn;

    void DrawCampCharacterInfo();
    void DrawCampBackpackItems();
    void DrawCampEquipmentItems();
    void DrawCampItemPool();
    void DrawCampItemQuantity(W8ItemInstance* item, int left, int top, int width);
    void ClearOtherCampItemGroupFilters(W8CampItemFilter filter);
    void SelectPendingCampCharacter();
    void HandleCampItemClick(W8ItemLocation location);
    void TakeItemUnitToHand(W8ItemLocation source);
    void MarkCampCharacterPending(W8ItemInstance* item);
    void OpenItemInfoDialog(W8ItemInstance* item);
    void OpenAttributeInfoDialog(W8Attribute attribute);
    void OpenSecondaryAttributeInfoDialog(unsigned int uiIndex);
    void IdentifyAndOpenItemInfo(W8ItemInstance* item);
    void DropHeldCampItem();
    void UseCampItem(W8ItemLocation location);
    void MergeItemStacksWithHeld(W8ItemLocation location);
    void UseHeldItemOnItem(W8ItemInstance* item);
    void UnequipBothHands();
    void TogglePartyRowFlag();
    void SortCampItemPool();
    void ToggleCampItemFilter(W8CampItemFilterButton tab);
    void ShowCampItemInfo(W8ItemInstance* item);
    W8ItemLocation CampItemAt(int slot, W8ItemStorage storage) const;
    std::string CampItemTooltip(int slot, W8ItemStorage storage);
    void DrawCampItemLabel(W8ItemInstance* item, int left, int top, char flag);
    void OpenSplitStackDialog(W8ItemLocation location);
};

/* The stats page: the attribute summary and the scrolling list of effects from conditions and worn items. */
class W8CampStatsPage {
public:
    explicit W8CampStatsPage(W8CampScreenState& screen) : screen(screen) {}
    W8CampStatsPage(const W8CampStatsPage&) = delete;
    W8CampStatsPage& operator=(const W8CampStatsPage&) = delete;

    void CreateControls();
    void Activate();
    void Deactivate();
    void ReviewedCharacterChanged();
    void DrawControls();

    void RebuildCampEffectList();
    void FilterCampEffectList();
    void DrawCampStatsPage();
    unsigned int CountEquipItemBenefits(int slot);
    unsigned int CountEquipItemPenalties(int slot);
    void DrawCampEffectList();
    void DrawCampEffectEntry(W8CampEffectEntry* entry, int* line_out);

    std::unique_ptr<W8RangeControl> stats_range;
    std::unique_ptr<W8CampStatsControls> stats_controls;
    /* The stats page's condition/equipment effect list, rebuilt
       by RebuildCampEffectList and refiltered by
       FilterCampEffectList. */
    bool effect_items_only; /* true lists equipped items, false conditions/enchantments */
    W8CampEffectFilter effect_filter; /* 0 all, 1 beneficial only, 2 detrimental only */
    int effect_beneficial_count;
    int effect_detrimental_count;
    int effect_first_visible;
    int effect_last_visible;
    int effect_visible_lines;
    int effect_scroll;
    std::vector<W8CampEffectEntry> effect_list; /* W8CampEffectEntry rows */

private:
    W8CampScreenState& screen;
};

/* The skills page: one hover region per skill row, grouped into five categories. */
class W8CampSkillsPage {
public:
    explicit W8CampSkillsPage(W8CampScreenState& screen) : screen(screen) {}
    W8CampSkillsPage(const W8CampSkillsPage&) = delete;
    W8CampSkillsPage& operator=(const W8CampSkillsPage&) = delete;

    void CreateCampSkillRegions();
    void DisableCampSkillRegions();
    void DrawCampSkillsPage();

    std::unique_ptr<UiLayer> skill_input;
    int skill_hover_row = -1;

private:
    W8CampScreenState& screen;
    UiResult HandleCampSkillList(const UiPointerEvent& event, W8SkillCategory category);
};

/* The spells page: the six realm lists with their scrollbars and animations, and the resistance bars. */
class W8CampSpellsPage {
public:
    explicit W8CampSpellsPage(W8CampScreenState& screen) : screen(screen) {}
    W8CampSpellsPage(const W8CampSpellsPage&) = delete;
    W8CampSpellsPage& operator=(const W8CampSpellsPage&) = delete;

    void CreateInput();
    void CreateControls();
    void Activate();
    void Deactivate();
    void ReviewedCharacterChanged();
    void Tick();
    void DrawControls();

    void DrawCampSpellPages();

    UiLayer spell_input{UiLayerOrder::CampSpells};
    std::array<HitTarget*, 6> spell_targets{};
    W8LearnedSpellState learned_spells;
    std::array<std::unique_ptr<W8RangeControl>, 6> spell_ranges;
    uint64_t animation_timer;
    unsigned int animation_frames[6];
    int selected_spell_row;

private:
    W8CampScreenState& screen;
    void SetCampSpellRangesEnabled(bool enable);
    void RefreshCampSpellRanges();
    void DrawCampResistances();
    UiResult HandleCampSpellList(const UiPointerEvent& event, int realm);
    void OpenSpellInfoDialog(unsigned int spell_id);
};

struct W8CampScreenState {
    bool Enter(const W8ScreenStateRuntime& request);
    void Frame();
    void ShowNotice(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel);

    void DeactivateCampPage();
    void RedrawRcsLevelUpPanel();
    void RedrawRcsDismissPanel();

    void DisplayCampDialog(std::unique_ptr<W8DialogBase> dialog);
    void DismissSelectedPartyCharacter();
    void SelectCampCharacter(int slot);

    W8CampItemsPage items{*this};
    W8CampStatsPage stats{*this};
    W8CampSkillsPage skills{*this};
    W8CampSpellsPage spells{*this};
    // Shared by the items and stats pages.
    std::unique_ptr<W8CampCharacterInfo> character_info;
    CampHeaderButtons header_buttons;
    CampButtons camp_buttons;
    W8Character* reviewed = nullptr;
    int ReviewedPartySlot() const;
    W8CampPage page;
    UiLayer main_input{UiLayerOrder::CampMain};
    // All camp scrollbars share this foreground layer; each control owns its
    // registrations, but cannot disable input belonging to another scrollbar.
    UiLayer scroll_input;
    std::array<HitTarget*, 11> main_targets{};
    unsigned int redraw_flags;
    W8CampHeaderMode header_mode;
    /* 0xd40[0]: mouse is over the reviewed portrait; drives the highlight
       frame and a redraw. */
    bool portrait_hovered = false;
    std::unique_ptr<W8DialogBase> dialog;

    W8CampScreenState() = default;
    ~W8CampScreenState();
    W8CampScreenState(const W8CampScreenState&) = delete;
    W8CampScreenState& operator=(const W8CampScreenState&) = delete;

private:
    void CreateCampInput();
    void DrawCampScreen();
    void SyncReviewCharInputRegion();
    void SwitchCampPage(W8CampPage page);
    void ActivateCampPage();
    void DrawCampRegenStats();
    void SetCampHeaderMode(W8CampHeaderMode mode);
    UiResult HandleCampDismissPortrait(const UiPointerEvent& event);
    UiResult HandleCampPortrait(const UiPointerEvent& event, int target_slot);
    UiResult HandleCampCharacterHeader(const UiPointerEvent& event);
    UiResult HandleCampProfessionHistory(const UiPointerEvent& event);
    void UpdateRcsLevelUpPanel();
    void UpdateRcsDismissPanel();
    void DrawCampHeader();
    void DrawCampVitals();
    void DrawCampHand(W8EquipSlot slot, int hand_index, int top, int count_center);
    void DrawCampHands();
    void SelectCampPageButton(int button, W8CampPage page);
    void OpenLevelUpCharacterScreen();
    void ShowDismissCharacterDialog();
    void OnDismissCharacterDialogClosed(bool accepted);
    void CreateCampHeaderButtons();
    void CreateCampButtonPanel();
};

extern std::unique_ptr<W8CampScreenState> g_camp_screen;

/* One g_localized_strings id per primary attribute row; defined in
   ReviewCharacterScreen.cpp, drawn by RCSStatsPage.cpp's stats page. */
extern int g_attribute_label_ids[7];

/* The twelve camp-screen regions the layout rules do not cover,
   given as explicit rectangles. The region initializer reads the first four
   fields; the trailing five are never touched there and stay positional. */
struct W8CampScreenRegion {
    int x;
    int y;
    int width;
    int height;
    int frame;              /* catalog frame for the slot border */
    int unidentified_frame; /* overlay frame while the item is unidentified */
    int label_x;            /* item label left */
    int label_y;            /* item label top */
    int label_flag;         /* label draw flag */
};

extern W8CampScreenRegion g_camp_screen_regions[12];
/* Load-category font-palette selectors indexed by
   W8Character::load_category. */
extern int g_load_category_palettes[5];
/* The three portrait catalog ids per race used by the camp
   party strip. */
extern int g_race_portrait_images[0x30];

/* Whether an item click fits the character's remaining action allowance in
   combat. */
bool IsCampActionAllowed(int party_slot);
int CommitPartySlotSpell(int party_slot, int spell_id, int power_level, W8CombatSlot* target);
int CommitPartySlotItemUse(int party_slot, W8ItemInstance* item, W8CombatSlot* target);

bool CampScreenEnter(void);
void CampScreenFrame(void);
bool CampScreenLeave(int leaving);

bool IsEquippableItemClass(W8ItemInstance* item);

/* Begin the timed screen fade and run `callback` when it
   finishes; `fade_to_black` selects the alpha ramp. */
void BeginScreenFade(int fade_to_black, int fade_out, int duration, void (*callback)(void),
                     bool fullscreen_scene_last, char render_each_tick);
unsigned char UpdateScreenFade(void);
void BeginPartyDeath(void);
void PumpReviewTransition(void);
void DrawPartyDeathScreen(void);
void EndReviewTransition(void);
/* Ending sequence picker run when that fade completes. */
void ShowEndingScreen(void);
void BeginEndgameSequence(void);
/* Camp and main-game notice dialogs ShowNoticeLine forwards into. */
void ShowCampNoticeLine(std::string_view text, std::function<void(bool)> callback,
                        bool confirmation, bool cancel);
