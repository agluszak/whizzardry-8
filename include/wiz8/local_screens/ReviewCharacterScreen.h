#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wiz8/character_skills.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/RCSStatsPage.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

class Game;
class Pointer;

/* The UI orders Other before Accessory, and places the two non-group filters last. */
enum W8CampItemFilterButton {
    W8_CAMP_ITEM_FILTER_BUTTON_HAND = 0,
    W8_CAMP_ITEM_FILTER_BUTTON_BODY = 1,
    W8_CAMP_ITEM_FILTER_BUTTON_OTHER = 2,
    W8_CAMP_ITEM_FILTER_BUTTON_ACCESSORY = 3,
    W8_CAMP_ITEM_FILTER_BUTTON_USABLE = 4,
    W8_CAMP_ITEM_FILTER_BUTTON_UNIDENTIFIED = 5
};

struct W8Character;
struct W8CombatSlot;
struct W8ItemUse;

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

/* The camp's one modal dialog. Opening a dialog retires the open one; the
   pages open theirs here. */
class CampDialogs {
public:
    CampDialogs() = default;
    CampDialogs(const CampDialogs&) = delete;
    CampDialogs& operator=(const CampDialogs&) = delete;
    ~CampDialogs();

    void Display(std::unique_ptr<W8DialogBase> dialog);
    void ShowNotice(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel);
    W8DialogBase* open() const { return dialog.get(); }
    // Runs the open dialog's input; true when it closed.
    bool ProcessInput();

private:
    std::unique_ptr<W8DialogBase> dialog;
};

/* The reviewed character's party slot, or -1 for a character outside the party. */
int CampPartySlot(const W8Character& character);

/* The combat and armor panel beside the items and stats pages. */
class W8CampCharacterInfo {
public:
    W8CampCharacterInfo();
    W8CampCharacterInfo(const W8CampCharacterInfo&) = delete;
    W8CampCharacterInfo& operator=(const W8CampCharacterInfo&) = delete;

    void CreateControls();
    void SetActive(bool enabled);
    // Returns whether the panel was repainted.
    bool Redraw(SDL_Surface& target, const W8Character& character, bool full_redraw);
    void SetCombatView(bool enabled);
    void InvalidateAll() { m_dirty = true; }
    UiLayer input;
    bool m_combat_view = true;
    bool m_active = false;
    bool m_dirty = true;
    int m_backgroundImage = 0;
    std::unique_ptr<W8ButtonWidget> m_combat_tab;
    std::unique_ptr<W8ButtonWidget> m_stats_tab;
    std::array<HitTarget*, 4> m_values{};
    std::array<std::string, 4> m_help;
};

struct CampHeaderButtons {
    UiLayer input{UiLayerOrder::GameplayOverlays};
    std::unique_ptr<W8ButtonWidget> level_up;
    std::unique_ptr<W8ButtonWidget> dismiss;
};
// The page buttons and the exit button, on the bottom panel beside the items
// page's action buttons.
struct CampPageButtons {
    UiLayer input{UiLayerOrder::CampButtons};
    std::array<std::unique_ptr<W8ButtonWidget>, 5> pages;
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
   them. The camp screen owns it and routes input and drawing to it; selecting
   another character and leaving the camp go back to the camp through the two
   callbacks. */
class W8CampItemsPage {
public:
    W8CampItemsPage(Pointer& pointer, CampDialogs& dialogs, W8CampCharacterInfo& character_info,
                    std::function<void(int)> select_character, std::function<void()> leave_camp)
        : pointer(pointer), dialogs(dialogs), character_info(character_info),
          select_character(std::move(select_character)), leave_camp(std::move(leave_camp))
    {
    }
    W8CampItemsPage(const W8CampItemsPage&) = delete;
    W8CampItemsPage& operator=(const W8CampItemsPage&) = delete;

    void CreateInput();
    // Start a camp visit; an identify spell opens the page targeting an item.
    void Reset(W8Character* identifying_character);
    void Activate(W8Character& character);
    void Deactivate();
    void ReviewedCharacterChanged(W8Character& character);
    void Tick();
    /* Cells whose items changed, and the cells and panels invalidated since
       the last draw, repaint with the next camp draw; a full camp repaint
       repaints them all. */
    void BeginRedraw();
    bool RedrawPending() const;
    void InvalidateCells() { cell_redraw.InvalidateAll(); }
    void InvalidateCharacterInfo() { character_info_dirty = true; }
    // What the page asked the camp to repaint since the last call.
    struct CampRepaint {
        bool all = false;
        bool portrait = false;
    };
    CampRepaint TakeCampRepaint() { return std::exchange(camp_repaint, {}); }
    void EndRedraw();
    void DrawControls(bool dialog_open);
    void InvalidateControls() { item_controls.dirty = true; }
    W8CampItemAction action() const { return item_action; }
    std::span<const int> ListedPoolEntries() const
    {
        return std::span{item_list}.first(item_list_count);
    }

    /* The item-action buttons along the bottom panel: enabled only while the
       page is shown; the camp repaints them with its page buttons. */
    void CreateActionButtons();
    void UpdateActionButtons(bool shown);
    bool ActionButtonsDirty() const;
    // The next draw repaints the whole bottom panel, as after an action change.
    bool ActionButtonsInvalidated() const { return action_buttons_invalidated; }
    void RefreshActionButtons();
    void RedrawCampItemsPage(bool full);
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
    void CreateCampItemControls(UiLayer& scroll_input);

private:
    Pointer& pointer;
    CampDialogs& dialogs;
    W8CampCharacterInfo& character_info;
    std::function<void(int)> select_character;
    std::function<void()> leave_camp;
    // The character the page shows, given by the camp.
    W8Character* character = nullptr;
    int PartySlot() const { return CampPartySlot(*character); }
    // The move cursor: the held item under the hand, or the empty hand.
    void ShowHandCursor();
    CampItemControls item_controls;
    UiLayer action_input{UiLayerOrder::CampButtons};
    std::array<std::unique_ptr<W8ButtonWidget>, W8_CAMP_ACTION_BUTTON_COUNT> action_buttons;
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
    // The cells, and the strips behind them, the next draw repaints.
    struct CellRedraw {
        bool backpack_header = false;
        std::bitset<8> backpack;
        bool paper_doll = false;
        std::bitset<12> equipment;
        bool pool_header = false;
        std::bitset<8> pool;
        void InvalidateBackpack()
        {
            backpack_header = true;
            backpack.set();
        }
        void InvalidateEquipment()
        {
            paper_doll = true;
            equipment.set();
        }
        void InvalidatePool()
        {
            pool_header = true;
            pool.set();
        }
        void InvalidateAll()
        {
            InvalidateBackpack();
            InvalidateEquipment();
            InvalidatePool();
        }
        bool BackpackPending() const { return backpack_header || backpack.any(); }
        bool EquipmentPending() const { return paper_doll || equipment.any(); }
        bool PoolPending() const { return pool_header || pool.any(); }
    } cell_redraw;
    CampRepaint camp_repaint;
    // Panels the next draw repaints besides the cells.
    bool character_info_dirty = false;
    bool action_buttons_invalidated = false;
    bool hand_buttons_dirty = false;
    bool filter_buttons_dirty = false;
    unsigned char item_filters[W8_CAMP_ITEM_FILTER_COUNT] = {};
    unsigned int item_scroll = 0;
    /* The displayed item-pool indices - the count and the list of pool
       slots RCSItemsPage.cpp renders. RebuildCampItemList rebuilds it; the pool
       handler reads it through item_scroll. */
    unsigned int item_list_count = 0;
    int item_list[500] = {};
    std::unique_ptr<W8RangeControl> item_range;
    uint64_t item_timer = 0;
    bool item_timer_active = false;
    bool item_timer_expired = false;
    W8CampItemAction item_action = W8_CAMP_ITEM_ACTION_NONE;
    W8CampInfoPage info_page = W8_CAMP_INFO_PAGE_ITEMS;
    /* The camp item icons were drawn while the monster/combat
       timer was enabled; its stop forces a full redraw to drop them. */
    bool item_icons_drawn = false;

    void InvalidateChangedCells();
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
    void UseHeldItemOnItem(W8ItemLocation location);
    void UnequipBothHands();
    void TogglePartyRowFlag();
    void SortCampItemPool();
    void ToggleCampItemFilter(W8CampItemFilterButton tab);
    void ShowCampItemInfo(W8ItemInstance* item);
    W8ItemLocation CampItemAt(int slot, W8ItemStorage storage) const;
    std::string CampItemTooltip(int slot, W8ItemStorage storage);
    void DrawCampItemLabel(W8ItemInstance* item, int left, int top, signed char flag);
    void OpenSplitStackDialog(W8ItemLocation location);
};

/* The stats page: the attribute summary and the scrolling list of effects from conditions and worn items. */
class W8CampStatsPage {
public:
    explicit W8CampStatsPage(W8CampCharacterInfo& character_info) : character_info(character_info)
    {
    }
    W8CampStatsPage(const W8CampStatsPage&) = delete;
    W8CampStatsPage& operator=(const W8CampStatsPage&) = delete;

    void CreateControls(UiLayer& scroll_input);
    void Activate(const W8Character& character);
    void Deactivate();
    void ReviewedCharacterChanged(const W8Character& character);
    void DrawControls();

    void RebuildCampEffectList(const W8Character& character);
    void FilterCampEffectList();
    // The effect list repaints with the next draw.
    void InvalidateEffectList() { effect_list_dirty = true; }
    bool RedrawPending() const { return effect_list_dirty; }
    void EndRedraw() { effect_list_dirty = false; }
    void DrawCampStatsPage(const W8Character& character, bool full);

    std::unique_ptr<W8RangeControl> stats_range;
    std::unique_ptr<W8CampStatsControls> stats_controls;
    /* The stats page's condition/equipment effect list, rebuilt
       by RebuildCampEffectList and refiltered by
       FilterCampEffectList. */
    bool effect_items_only = false; /* true lists equipped items, false conditions/enchantments */
    W8CampEffectFilter effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
    int effect_beneficial_count = 0;
    int effect_detrimental_count = 0;
    int effect_first_visible = 0;
    int effect_last_visible = 0;
    int effect_visible_lines = 0;
    int effect_scroll = 0;
    std::vector<W8CampEffectEntry> effect_list; /* W8CampEffectEntry rows */

private:
    W8CampCharacterInfo& character_info;
    bool effect_list_dirty = false;
    void DrawCampEffectList(const W8Character& character);
};

/* The skills page: one hover region per skill row, grouped into five categories. */
class W8CampSkillsPage {
public:
    explicit W8CampSkillsPage(CampDialogs& dialogs) : dialogs(dialogs) {}
    W8CampSkillsPage(const W8CampSkillsPage&) = delete;
    W8CampSkillsPage& operator=(const W8CampSkillsPage&) = delete;

    void Activate(W8Character& character);
    // Leaving the page forgets which skills improved.
    void Deactivate();
    void ReviewedCharacterChanged(W8Character& character) { this->character = &character; }
    void DrawCampSkillsPage(bool full);

    std::unique_ptr<UiLayer> skill_input;
    int skill_hover_row = -1;

private:
    CampDialogs& dialogs;
    // The character the page shows, given by the camp.
    W8Character* character = nullptr;
    UiResult HandleCampSkillList(const UiPointerEvent& event, W8SkillCategory category);
};

/* The spells page: the six realm lists with their scrollbars and animations, and the resistance bars. */
class W8CampSpellsPage {
public:
    explicit W8CampSpellsPage(CampDialogs& dialogs) : dialogs(dialogs) {}
    W8CampSpellsPage(const W8CampSpellsPage&) = delete;
    W8CampSpellsPage& operator=(const W8CampSpellsPage&) = delete;

    void CreateInput();
    void CreateControls(UiLayer& scroll_input);
    void Activate(W8Character& character);
    void Deactivate();
    void ReviewedCharacterChanged(W8Character& character);
    void Tick();
    void DrawControls();

    bool RedrawPending() const { return realms_dirty.any() || animation_dirty; }
    void EndRedraw()
    {
        realms_dirty.reset();
        animation_dirty = false;
    }
    // Only the realms on the left keep animating under an open dialog.
    void DrawCampSpellPages(bool full, bool dialog_open);

    UiLayer spell_input{UiLayerOrder::CampSpells};
    std::array<HitTarget*, 6> spell_targets{};
    W8LearnedSpellState learned_spells;
    std::array<std::unique_ptr<W8RangeControl>, 6> spell_ranges;
    uint64_t animation_timer = 0;
    unsigned int animation_frames[6] = {};
    int selected_spell_row = 0;

private:
    CampDialogs& dialogs;
    // The character the page shows, given by the camp.
    W8Character* character = nullptr;
    // Realm lists, and the realm animations, the next draw repaints.
    std::bitset<6> realms_dirty;
    bool animation_dirty = false;
    void RefreshCampSpellRanges();
    void DrawCampResistances();
    UiResult HandleCampSpellList(const UiPointerEvent& event, int realm);
    void OpenSpellInfoDialog(unsigned int spell_id);
};

/* The camp: the reviewed character's items, stats, skills and spells pages
   over the party strip. The camp is rebuilt on every visit; after a suspension
   it reviews the character it showed. Each page is given the character it
   shows and the dialogs it opens. */
class CampScreen final : public Screen {
public:
    static constexpr W8ScreenId id = W8_SCREEN_CAMP;

    // The camp is rebuilt on every visit, within one game.
    CampScreen(Game& game, Pointer& pointer) : pointer(pointer), game_(game) {}

    /* Build the pages for `request.character` (null when no character is
       selected); `request.parameter_4` names an identifying character. */
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;
    std::optional<ScreenRequest> rebuildRequest(const ScreenRequest& entered) const override;

    // The next draw repaints the whole screen.
    void InvalidateAll() { full_redraw = true; }
    // The next draw repaints the reviewed character's portrait, vitals and hands.
    void InvalidatePortrait() { header_redraw.portrait = true; }
    // A party member's presentation changed; the camp shows only the reviewed one's.
    void RefreshPartySlot(int party_slot);
    // The party's load changed: the weights and the reviewed portrait.
    void RefreshPartyLoad();

    void DismissSelectedPartyCharacter();
    void SelectCampCharacter(int slot);
    int ReviewedPartySlot() const;

    // The pointer the camp shares with a main game suspended under it.
    Pointer& pointer;
    // Declared first: the open dialog is retired after the pages that opened it.
    CampDialogs dialogs;
    // The time since the camp last stepped the game; the camp is built on every visit.
    LapTimer step_timer;
    // All camp scrollbars share this foreground layer; each control owns its
    // registrations, but cannot disable input belonging to another scrollbar.
    // It outlives the pages' scrollbars.
    UiLayer scroll_input;
    // Shared by the items and stats pages.
    W8CampCharacterInfo character_info;
    W8CampItemsPage items{pointer, dialogs, character_info,
                          [this](int slot) { SelectCampCharacter(slot); },
                          [this] { DismissSelectedPartyCharacter(); }};
    W8CampStatsPage stats{character_info};
    W8CampSkillsPage skills{dialogs};
    W8CampSpellsPage spells{dialogs};
    W8Character* reviewed = nullptr;
    W8CampPage page = W8_CAMP_PAGE_ITEMS;

private:
    Game& game_;
    CampHeaderButtons header_buttons;
    CampPageButtons page_buttons;
    UiLayer main_input{UiLayerOrder::CampMain};
    std::array<HitTarget*, 11> main_targets{};
    W8CampHeaderMode header_mode = W8_CAMP_HEADER_SUMMARY;
    /* 0xd40[0]: mouse is over the reviewed portrait; drives the highlight
       frame and a redraw. */
    bool portrait_hovered = false;

    // The header's parts the next draw repaints.
    struct HeaderRedraw {
        bool background = false;
        bool caption = false;
        bool portrait = false;
        std::bitset<8> party_slots;
        bool any() const { return background || caption || portrait || party_slots.any(); }
    };
    void InvalidateHeader();

    bool full_redraw = false;
    HeaderRedraw header_redraw;
    bool button_panel_dirty = true;

    void CreateCampInput();
    void DrawCampScreen();
    void DrawButtonPanel(bool invalidate);
    void SyncReviewCharInputRegion();
    void SwitchCampPage(W8CampPage page);
    void ActivateCampPage();
    void DeactivateCampPage();
    void DrawCampRegenStats();
    void SetCampHeaderMode(W8CampHeaderMode mode);
    UiResult HandleCampDismissPortrait(const UiPointerEvent& event);
    UiResult HandleCampPortrait(const UiPointerEvent& event, int target_slot);
    UiResult HandleCampCharacterHeader(const UiPointerEvent& event);
    UiResult HandleCampProfessionHistory(const UiPointerEvent& event);
    void RedrawRcsLevelUpPanel();
    void RedrawRcsDismissPanel();
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

/* One Data().strings id per primary attribute row; defined in
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
int CommitPartySlotItemUse(int party_slot, W8ItemUse use, W8CombatSlot* target);

bool IsEquippableItemClass(W8ItemInstance* item);

/* Begin the timed screen fade and run `callback` when it
   finishes; `fade_to_black` selects the alpha ramp. */
void BeginScreenFade(int fade_to_black, int fade_out, int duration, void (*callback)(void),
                     bool fullscreen_scene_last, signed char render_each_tick);
unsigned char UpdateScreenFade(void);
void BeginPartyDeath(void);
void PumpReviewTransition(void);
void DrawPartyDeathScreen(void);
void EndReviewTransition(void);
/* Ending sequence picker run when that fade completes. */
void ShowEndingScreen(void);
void BeginEndgameSequence(void);
/* A notice in the open camp's dialog. */
void ShowCampNoticeLine(std::string_view text, std::function<void(bool)> callback,
                        bool confirmation, bool cancel);
