#pragma once

#include <cstdint>
#include <string_view>
#include <memory>
#include <vector>

#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/ControlSelection.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/compat/compiler.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/ControlsRect.h"
#include "input.h"
#include "layouts/gameplay_databases.h"

class W8DialogBase;
class W8CharacterScreen;
class W8CharacterPageEntry;

class W8CharacterPageEntryListener {
public:
    virtual void AdjustEntry(W8CharacterPageEntry* entry, std::int32_t delta) = 0;
    virtual void ShowEntryInfo(W8CharacterPageEntry* entry) = 0;
};

/* One value row shared by the skills and attributes pages. */
class W8CharacterPageEntry : public W8TextControl::Listener {
public:
    W8CharacterPageEntry(Controls* owner, std::int32_t x, std::int32_t y, bool compact);
    virtual ~W8CharacterPageEntry()
    {
        delete m_label;
        delete m_first_text;
        delete m_second_text;
    }
    void SetContent(std::uint32_t id, std::string_view label, std::uint32_t* first, std::int32_t* second,
                    std::int32_t* third, std::int32_t help_id);
    void SetEnabled(bool enabled);
    void SetIncrementAllowed(bool allowed);
    void Redraw();
    void SetLabelFontState(std::int32_t state);
    void MarkDirty();
    void UpdateButtons();
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl* control) override;
    void SetHelpActive(bool active);

    W8CharacterPageEntryListener* m_listener;
    W8TextControl* m_increment;
    W8TextControl* m_decrement;
    W8TextControl* m_help;
    W8TextBuffer* m_label;
    W8TextBuffer* m_first_text;
    W8TextBuffer* m_second_text;
    std::uint32_t* m_first;
    std::int32_t* m_second;
    std::int32_t* m_third;
    std::uint32_t m_id;
    std::int32_t m_x;
    std::int32_t m_y;
    bool m_draw_background;
    bool m_dirty;
    bool m_enabled;
    bool m_increment_allowed;
};

/* Common base of the four character screen pages. */
class W8CharacterPage : public Controls {
public:
    W8CharacterPage(CatalogArtwork background);
    virtual ~W8CharacterPage();
    virtual void Invalidate(const W8ControlsRect* rect) override;
    virtual void Redraw() override;
    virtual void SetCharacter(W8Character* character, W8CharacterCreationState* creation_state,
                              std::int32_t mode);
    virtual void Activate() = 0;
    virtual void Deactivate() = 0;
    virtual void Accept() = 0;
    virtual void GetNavigationState(bool* next_enabled, bool* exit_enabled) = 0;
    virtual void HandleInput(InputEvent* input);
    virtual void Refresh();
    virtual void Prepare();
    std::vector<std::unique_ptr<W8CharacterPageEntry>> m_entries;
    W8CharacterScreen* m_screen;
    W8Character* m_character;
    W8CharacterCreationState* m_creation_state;
    std::int32_t m_mode;
    bool m_prepared;
    bool m_dirty;
    std::uint8_t pad_06e[2];

    void AddEntry(W8CharacterPageEntry* entry);
};

class W8CharacterStatsRow;

/* One record of the three stats-page row tables (profession, race, sex).
   0x00 is the catalogue object id, 0x04/0x08 its two images, 0x0C the name
   message id and 0x0E the selectable flag. */
struct W8CharacterStatsRecord {
    /* Catalog object id resolved when drawing the selected row. */
    std::uint32_t object;
    /* Catalog image ids drawn while the control is enabled and
       disabled respectively. */
    std::int32_t image_enabled;
    std::int32_t image_disabled;
    std::uint16_t name_id;
    std::uint8_t enabled;
    std::uint8_t pad_0f;
};

/* The stats value control: it carries the record currently shown and the
   record to fall back to when the character has none. */
// VTABLE: WIZ8 0x005ef6b0 W8CharacterStatsValue
class W8CharacterStatsValue : public W8TextControl {
public:
    W8CharacterStatsValue(Controls* owner, std::int32_t x, std::int32_t y,
                          const W8CharacterStatsRecord* default_record);
    virtual void Redraw(bool full_redraw) override;
    virtual void OnRightButtonUp(std::int32_t event) override;
    void SetRecord(const W8CharacterStatsRecord* record);

    const W8CharacterStatsRecord* m_record;
    const W8CharacterStatsRecord* m_default_record;
};

/* Stats-row callbacks. The row itself raises slots 0 and 3; the page's input
   handling raises slots 1 and 2. */
class W8CharacterStatsRowListener {
public:
    virtual void OnRowValueChanged(W8CharacterStatsRow* row, std::int32_t value) = 0;
    virtual void OnRowExpanded(W8CharacterStatsRow* row) = 0;
    virtual void OnRowCollapsed(W8CharacterStatsRow* row) = 0;
    virtual void OnRowInfoRequested(W8CharacterStatsRow* row, std::int32_t value) = 0;
};

/* A stats-page value row: a decrement arrow, an increment arrow and a value
   control whose current index indexes the row's 0x10-byte record table. */
class W8CharacterStatsRow final : public W8TextControl::Listener {
public:
    W8CharacterStatsRow();
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl* control) override;
    void Initialize(Controls* owner, std::int32_t x, std::int32_t y, std::int32_t count,
                    const W8CharacterStatsRecord* table,
                    const W8CharacterStatsRecord* default_record, std::int32_t help_first, std::int32_t help_second,
                    std::int32_t help_value);
    /* Descriptive names for the collapse operations expanded in row/page input. */
    void Collapse();
    void CollapseIfNotHovered();
    void Invalidate();
    void BuildSubpanel();
    void SetValue(std::int32_t index);

    std::int32_t m_index; /* current stat index */
    std::uint16_t m_count;
    std::uint8_t pad_00a[2];
    std::int32_t m_x;
    std::int32_t m_y;
    const W8CharacterStatsRecord* m_table;
    W8TextControl* m_decrement;
    W8TextControl* m_increment;
    W8CharacterStatsValue* m_value_control;
    std::unique_ptr<Controls> m_subpanel;
    // Owned by m_subpanel.
    std::vector<W8TextControl*> m_subpanel_entries;
    W8CharacterStatsRowListener* m_listener;
};

// VTABLE: WIZ8 0x005ef778 W8CharacterPage
class W8CharacterStatsPage : public W8CharacterPage,
                             public W8CharacterStatsRowListener,
                             public W8CharacterPageEntryListener,
                             public W8TextControl::Listener {
public:
    W8CharacterStatsPage() : W8CharacterPage(CatalogArtwork::Load(0x104)) {}
    virtual ~W8CharacterStatsPage() override;
    virtual void Redraw() override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*, std::int32_t) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void HandleInput(InputEvent*) override;
    virtual void Refresh() override;
    virtual void Prepare() override;
    virtual void OnRowValueChanged(W8CharacterStatsRow* row, std::int32_t value) override;
    virtual void OnRowExpanded(W8CharacterStatsRow* row) override;
    virtual void OnRowCollapsed(W8CharacterStatsRow* row) override;
    virtual void OnRowInfoRequested(W8CharacterStatsRow* row, std::int32_t value) override;
    virtual void AdjustEntry(W8CharacterPageEntry*, std::int32_t) override;
    virtual void ShowEntryInfo(W8CharacterPageEntry*) override;
    virtual void OnPrimary(W8TextControl*) override;
    virtual void OnSecondary(W8TextControl*) override;

private:
    void UpdateRowValues();
    void SetRowControlsActive(W8CharacterStatsRow* row, bool active);

public:
    W8CharacterStatsRow* m_profession_row;
    W8CharacterStatsRow* m_race_row;
    W8CharacterStatsRow* m_gender_row;
    bool nav_next_state;
    bool m_rows_initialized;
    std::uint8_t pad_08a[2];
    W8TextControl* m_attribute_controls[5];
};

struct W8CharacterSpellEntry {
    W8SpellRealm realm;
    std::uint32_t spell;
    bool fSelectable;
    bool selected;
};

class W8CharacterSpellListListener {
public:
    virtual void SelectSpell(std::uint32_t entry) = 0;
    virtual void ShowSpellInfo(std::uint32_t entry) = 0;
};

class W8CharacterSpellList;

class W8CharacterSpellsPage : public W8CharacterPage, public W8CharacterSpellListListener {
public:
    W8CharacterSpellsPage() : W8CharacterPage(CatalogArtwork::Load(0x109)), anim_timer(0.05f, 1)
    {
        for (std::int32_t realm = 0; realm < 6; ++realm) {
            m_realms[realm] = 0;
        }
    }
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*, std::int32_t) override;
    virtual void Redraw() override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void Refresh() override;
    virtual void SelectSpell(std::uint32_t entry) override;
    virtual void ShowSpellInfo(std::uint32_t entry) override;

private:
    void UpdateSpellLists();
    W8CharacterSpellList* m_realms[6];
    W8CharacterSpellEntry m_SpellData[114];
    W8GameTimer anim_timer;
    std::uint32_t m_animation_frames[6];
    std::uint32_t m_last_selected;
};

class W8CharacterSkillsPage : public W8CharacterPage, public W8CharacterPageEntryListener {
public:
    W8CharacterSkillsPage() : W8CharacterPage(CatalogArtwork::Load(0x108)) {}
    virtual void Redraw() override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*, std::int32_t) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void AdjustEntry(W8CharacterPageEntry*, std::int32_t) override;
    virtual void ShowEntryInfo(W8CharacterPageEntry*) override;
    virtual void Refresh() override;

private:
    void UpdateEntries();
    bool m_force_redraw;
    bool m_show_fifth_category;
    bool nav_next_state;
};

class W8CharacterPersonalityPage : public W8CharacterPage,
                                   public W8ControlSelectionListener,
                                   public W8TextControl::Listener {
public:
    W8CharacterPersonalityPage()
        : W8CharacterPage(CatalogArtwork::Load(0x105)), anim_timer(0.4f, 1), m_animation_active(0)
    {
    }
    virtual void Redraw() override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*, std::int32_t) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void HandleInput(InputEvent*) override;
    virtual void Refresh() override;
    virtual void OnSelectionChanged(W8ControlSelection*, std::int32_t) override;
    virtual void OnPrimary(W8TextControl*) override;

private:
    W8TextControl* m_control4;
    W8TextControl* m_control5;
    W8TextControl* m_control6;
    W8TextControl* m_control7;
    W8TextControl* m_randomize;
    W8ControlSelection m_personality_selection;
    W8ControlSelection m_voice_selection;
    W8GameTimer anim_timer;
    std::int32_t m_animation_frame;
    bool m_animation_active;
    bool m_description_dirty;
    bool m_portrait_dirty;
};

W8CharacterStatsPage* CreateCharacterStatsPage();
W8CharacterSpellsPage* CreateCharacterSpellsPage();
W8CharacterSkillsPage* CreateCharacterSkillsPage();
W8CharacterPersonalityPage* CreateCharacterPersonalityPage();

/* Realm animation records shared by the stats page's resistance icons and the
   spells page's realm list. The stats page reads only initial_frame, through
   the realm-1 offset the spells page's array lays out. */
struct W8SpellRealmAnimation {
    std::int32_t image;
    std::uint32_t frame_count;
    std::uint32_t initial_frame;
};
extern W8SpellRealmAnimation g_spell_realm_animations[6];

/* The six realm-icon object ids; the definition is the GLOBAL in
   CGSStatsPage.cpp. The camp screen's character block reuses them. */
extern std::int32_t g_character_resistance_images[6];

/* One message id per character trait, indexed by trait id. */
extern std::uint16_t g_character_trait_name_ids[0x20];

/* Attribute-entry and skill-name message id tables shared by the stats and
   skills pages. */
extern std::uint16_t g_character_description_first_ids[22];
extern std::uint16_t g_character_skill_name_ids[84];
/* One row per gender, the third entry the possessive the item
   notices print. */
extern std::uint16_t g_gender_name_message_rows[4][4];
/* One message id per final-page personality slot; the array's terminating zero
   keeps its ten-entry extent. */
extern std::uint16_t g_personality_message_ids[10];

/* Skill-availability bookkeeping raised by RefreshCharacterSkillAvailability while the character
   screen is open: adjust the named page-2 entry and refresh that page. */
void ResetCharacterScreenSkill(W8Skill skill_id);
void RefundCharacterScreenSkill(W8Skill skill_id);

/* Used by the pages to raise the screen-owned dialogs and to query the
   current character. */
class W8CharacterPageHost {
public:
    virtual void UpdateNavigation(W8CharacterPage* page) = 0;
    virtual void ShowSpellInfo(std::int32_t value) = 0;
    virtual void ShowProfessionInfo(W8Profession profession) = 0;
    virtual void ShowRaceInfo(W8Race race) = 0;
    virtual void ShowPrimaryAttributeInfo(W8Attribute attribute) = 0;
    virtual void ShowSecondaryAttributeInfo(std::uint32_t attribute) = 0;
    virtual void ShowSkillInfo(W8Skill value) = 0;
    virtual void ShowDescription(std::int32_t first, std::int32_t second) = 0;
    virtual void ShowCharacterSummary() = 0;
    virtual bool HasDialog() = 0;
    virtual W8Character* GetOriginalCharacter() = 0;
};

class W8CharacterScreen final : public W8CharacterPageHost, public W8TextControl::Listener {
public:
    W8CharacterScreen(std::int32_t mode, W8Character* character);
    ~W8CharacterScreen();
    void BuildControls();
    void UpdateDialog();
    void AdvancePage(bool forward);
    void SelectPage(std::int32_t index);
    void SyncCharacterForPage(std::int32_t index);
    bool CommitCharacter();
    void DrawHeader();
    void ShowMessage(std::string_view text, std::int32_t confirmation, std::int32_t response);
    void HandleDialogResult(std::int32_t response, std::uint8_t accepted);
    bool ValidateName();

    virtual void UpdateNavigation(W8CharacterPage* page) override;
    virtual void ShowSpellInfo(std::int32_t value) override;
    virtual void ShowProfessionInfo(W8Profession profession) override;
    virtual void ShowRaceInfo(W8Race race) override;
    virtual void ShowPrimaryAttributeInfo(W8Attribute attribute) override;
    virtual void ShowSecondaryAttributeInfo(std::uint32_t attribute) override;
    virtual void ShowSkillInfo(W8Skill value) override;
    virtual void ShowDescription(std::int32_t first, std::int32_t second) override;
    virtual void ShowCharacterSummary() override;
    virtual bool HasDialog() override;
    virtual W8Character* GetOriginalCharacter() override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl* control) override;

    std::int32_t m_mode;
    std::int32_t m_page_index;
    bool m_header_dirty;
    std::uint8_t pad_011[3];
    W8Character* m_original;
    W8Character m_character;
    std::uint8_t pad_187a[2];
    W8CharacterCreationState m_creation_state;
    bool m_block_advance;
    bool m_confirm_profession;
    bool m_force_transition;
    std::uint8_t pad_1aef;
    std::unique_ptr<Controls> m_controls;
    W8TextControl* m_previous;
    W8TextControl* m_next;
    W8TextControl* m_exit;
    W8TextControl* m_accept;
    W8TextControl* m_reset;
    bool m_page_enabled[4];
    std::unique_ptr<W8CharacterPage> m_pages[4];
    W8DialogBase* m_dialog;
    std::uint32_t m_dialog_response;
    bool m_capture_dialog_result;
    std::uint8_t pad_1b25[3];
};

extern std::unique_ptr<W8CharacterScreen> g_character_screen;

/* Per-profession message indexes. */
extern std::uint16_t g_profession_name_message_ids[32];
extern std::uint16_t g_race_name_message_ids[W8_RACE_COUNT];
/* Per-profession level-name message indexes, one row per profession for the
   level bands. */
extern std::uint16_t g_profession_level_name_message_ids[15][9];

/* Refresh the character-screen response when a party slot changes. */
void RefreshCharacterScreenPartySlot(std::uint32_t party_slot);
std::uint8_t CharacterScreenEnter(void);
void CharacterScreenFrame(void);
std::uint8_t CharacterScreenLeave(std::int32_t leaving);
extern char g_dash[];
