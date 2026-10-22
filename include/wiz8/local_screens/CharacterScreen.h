#pragma once
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/data/records.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/character.h"
#include "wiz8/screen.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_layer.h"

class Game;

class W8DialogBase;
class CharacterScreen;
class W8CharacterPageEntry;

enum class W8CharacterPortraitAction : int {
    previous_group = 0,
    next_group = 1,
    previous_portrait = 2,
    next_portrait = 3,
    show_summary = 4,
};

/* One value row shared by the skills and attributes pages. */
class W8CharacterPageEntry {
public:
    W8CharacterPageEntry(UiLayer& input, int x, int y, bool compact);
    ~W8CharacterPageEntry();
    void SetContent(unsigned int id, std::string_view label, unsigned int* first, int* second,
                    int* third, int help_id);
    void SetEnabled(bool enabled);
    void SetIncrementAllowed(bool allowed);
    void Redraw(SDL_Surface& target);
    void SetLabelFontState(int state);
    void MarkDirty();
    void UpdateButtons();
    void SetHelpActive(bool active);
    std::function<void(int)> on_adjust;
    std::function<void()> on_info;

    UiLayer& m_input;
    std::unique_ptr<W8ButtonWidget> m_increment;
    std::unique_ptr<W8ButtonWidget> m_decrement;
    HitTarget* m_help;
    W8TextBuffer m_label;
    W8TextBuffer m_first_text;
    W8TextBuffer m_second_text;
    unsigned int* m_first;
    int* m_second;
    int* m_third;
    unsigned int m_id;
    int m_x;
    int m_y;
    bool m_draw_background;
    bool m_dirty;
    bool m_enabled;
    bool m_increment_allowed;
};

/* Common base of the four character screen pages. */
class W8CharacterPage {
public:
    UiLayer input;
    CatalogArtwork background;
    static constexpr SDL_Rect bounds{0xc3, 0x2b, 445, 406};
    bool m_active = false;
    bool m_dirty = true;
    W8CharacterPage(CatalogArtwork background);
    void SetActive(bool active);
    virtual ~W8CharacterPage();
    virtual void InvalidateAll();
    virtual void Redraw(SDL_Surface& target, bool full_redraw);
    virtual void SetCharacter(W8Character* character, W8CharacterCreationState* creation_state,
                              W8CharacterScreenMode mode);
    virtual void Activate() = 0;
    virtual void Deactivate() = 0;
    virtual void Accept() = 0;
    virtual void GetNavigationState(bool* next_enabled, bool* exit_enabled) = 0;
    virtual void HandleInput(const InputEvent* input);
    virtual void Refresh();
    virtual void Prepare();
    std::vector<std::unique_ptr<W8CharacterPageEntry>> m_entries;
    CharacterScreen* m_screen;
    W8Character* m_character;
    W8CharacterCreationState* m_creation_state;
    W8CharacterScreenMode m_mode;
    bool m_prepared;
    bool m_contentDirty; /* page-specific content needs repainting */

    // Add a value row at (x, y) in this page's input layer.
    W8CharacterPageEntry& AddEntry(int x, int y, bool compact);
};

class W8CharacterStatsRow;

/* One record of the three stats-page row tables (profession, race, sex).
   0x00 is the catalogue object id, 0x04/0x08 its two images, 0x0C the name
   message id and 0x0E the selectable flag. */
struct W8CharacterStatsRecord {
    /* Catalog object id resolved when drawing the selected row. */
    unsigned int object;
    /* Catalog image ids drawn while the control is enabled and
       disabled respectively. */
    int image_enabled;
    int image_disabled;
    unsigned short name_id;
    unsigned char enabled;
    unsigned char pad_0f;
};

class W8CharacterStatsPage;

class W8CharacterStatsRow {
public:
    W8CharacterStatsRow(W8CharacterStatsPage& owner, int x, int y, int count,
                        const W8CharacterStatsRecord* table,
                        const W8CharacterStatsRecord* default_record, int help_first,
                        int help_second, int help_value);
    ~W8CharacterStatsRow();
    void Collapse();
    void CollapseIfNotHovered();
    void Invalidate() { m_dirty = true; }
    void BuildSubpanel();
    void SetValue(int index);
    void SetEnabled(bool enabled);
    void Draw(SDL_Surface& target, bool force);
    void DrawChoices(SDL_Surface& target);
    void Step(int direction);

    int m_index = -1;
    int m_count;
    int m_x;
    int m_y;
    const W8CharacterStatsRecord* m_table;
    const W8CharacterStatsRecord* m_default_record;
    W8CharacterStatsPage& m_owner;
    std::unique_ptr<W8ButtonWidget> m_decrement;
    std::unique_ptr<W8ButtonWidget> m_increment;
    HitTarget* m_value;
    std::unique_ptr<UiLayer> m_choices;
    std::vector<HitTarget*> m_choice_targets;
    bool m_enabled = true;
    bool m_dirty = true;
};

class W8CharacterStatsPage : public W8CharacterPage {
public:
    W8CharacterStatsPage() : W8CharacterPage(CatalogArtwork::Load(0x104)) {}
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*,
                              W8CharacterScreenMode) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void HandleInput(const InputEvent*) override;
    virtual void Refresh() override;
    virtual void Prepare() override;
    void OnRowValueChanged(W8CharacterStatsRow* row, int value);
    void OnRowExpanded(W8CharacterStatsRow* row);
    void OnRowCollapsed(W8CharacterStatsRow* row);
    void OnRowInfoRequested(W8CharacterStatsRow* row, int value);
    void AdjustEntry(W8CharacterPageEntry*, int);
    void ShowEntryInfo(W8CharacterPageEntry*);

private:
    void UpdateRowValues();
    void SetRowControlsActive(W8CharacterStatsRow* row, bool active);

public:
    std::unique_ptr<W8CharacterStatsRow> m_profession_row;
    std::unique_ptr<W8CharacterStatsRow> m_race_row;
    std::unique_ptr<W8CharacterStatsRow> m_gender_row;
    bool nav_next_state;
    bool m_rows_initialized;
    unsigned char pad_08a[2];
    HitTarget* m_attribute_controls[5];
};

struct W8CharacterSpellEntry {
    W8SpellRealm realm;
    unsigned int spell;
    bool fSelectable;
    bool selected;
};

class W8CharacterSpellList;

class W8CharacterSpellsPage : public W8CharacterPage {
public:
    W8CharacterSpellsPage();
    ~W8CharacterSpellsPage() override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*,
                              W8CharacterScreenMode) override;
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void Refresh() override;
    void SelectSpell(unsigned int entry);
    void ShowSpellInfo(unsigned int entry);

private:
    void UpdateSpellLists();
    std::unique_ptr<W8CharacterSpellList> m_realms[6];
    W8CharacterSpellEntry m_SpellData[114];
    W8GameTimer anim_timer;
    unsigned int m_animation_frames[6];
    unsigned int m_last_selected;
};

class W8CharacterSkillsPage : public W8CharacterPage {
public:
    W8CharacterSkillsPage() : W8CharacterPage(CatalogArtwork::Load(0x108)) {}
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*,
                              W8CharacterScreenMode) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    void AdjustEntry(W8CharacterPageEntry*, int);
    void ShowEntryInfo(W8CharacterPageEntry*);
    virtual void Refresh() override;

private:
    void UpdateEntries();
    bool m_force_redraw;
    bool m_show_fifth_category;
    bool nav_next_state;
};

class W8CharacterPersonalityPage : public W8CharacterPage {
public:
    W8CharacterPersonalityPage()
        : W8CharacterPage(CatalogArtwork::Load(0x105)), anim_timer(0.4f, 1), m_animation_active(0)
    {
    }
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void SetCharacter(W8Character*, W8CharacterCreationState*,
                              W8CharacterScreenMode) override;
    virtual void Activate() override;
    virtual void Deactivate() override;
    virtual void Accept() override;
    virtual void GetNavigationState(bool*, bool*) override;
    virtual void HandleInput(const InputEvent*) override;
    virtual void Refresh() override;
    void ChangePortrait(W8CharacterPortraitAction action);

private:
    std::array<std::unique_ptr<W8ButtonWidget>, 5> m_buttons;
    std::array<HitTarget*, 11> m_choices{};
    bool m_choices_dirty = true;
    W8GameTimer anim_timer;
    int m_animation_frame;
    bool m_animation_active;
    bool m_description_dirty;
    bool m_portrait_dirty;
    std::optional<TextFieldGroup> m_text_fields;
};

std::unique_ptr<W8CharacterPage> CreateCharacterStatsPage();
std::unique_ptr<W8CharacterPage> CreateCharacterSpellsPage();
std::unique_ptr<W8CharacterPage> CreateCharacterSkillsPage();
std::unique_ptr<W8CharacterPage> CreateCharacterPersonalityPage();

/* Realm animation records shared by the stats page's resistance icons and the
   spells page's realm list. The stats page reads only initial_frame, through
   the realm-1 offset the spells page's array lays out. */
struct W8SpellRealmAnimation {
    int image;
    unsigned int frame_count;
    unsigned int initial_frame;
};
extern W8SpellRealmAnimation g_spell_realm_animations[6];

/* The six realm-icon object ids; the definition is the GLOBAL in
   CGSStatsPage.cpp. The camp screen's character block reuses them. */
extern int g_character_resistance_images[6];

/* One message id per character trait, indexed by trait id. */
extern unsigned short g_character_trait_name_ids[0x20];

/* One message id per final-page personality slot; the array's terminating zero
   keeps its ten-entry extent. */
extern unsigned short g_personality_message_ids[10];

/* Creation, editing and level-up of one character, page by page. The working
   copy and pages are rebuilt on every visit. */
class CharacterScreen final : public Screen {
public:
    static constexpr W8ScreenId id = W8_SCREEN_CHARACTER;

    // The screen is rebuilt on every visit, within one game.
    explicit CharacterScreen(Game& game);
    ~CharacterScreen() override;
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;
    std::optional<ScreenRequest> rebuildRequest(const ScreenRequest& entered) const override
    {
        return entered;
    }

    /* A skill became available or unavailable while the screen is current
       (SkillAvailabilityChanged): adjust its allocation and refresh the skills
       page. */
    void SkillAvailabilityChanged(W8Skill skill, bool available);

    void BuildControls();
    void UpdateDialog();
    void AdvancePage(bool forward);
    void SelectPage(int index);
    void SyncCharacterForPage(int index);
    bool CommitCharacter();
    void DrawHeader();
    void ShowMessage(std::string_view text, int confirmation, int response);
    void HandleDialogResult(int response, unsigned char accepted);
    bool ValidateName();

    void UpdateNavigation(W8CharacterPage* page);
    void ShowSpellInfo(int value);
    void ShowProfessionInfo(W8Profession profession);
    void ShowRaceInfo(W8Race race);
    void ShowPrimaryAttributeInfo(W8Attribute attribute);
    void ShowSecondaryAttributeInfo(unsigned int attribute);
    void ShowSkillInfo(W8Skill value);
    void ShowDescription(int first, int second);
    void ShowCharacterSummary();
    bool HasDialog();
    W8Character* GetOriginalCharacter();
    void DrawNavigation(SDL_Surface& target);

    Game& m_game;
    W8CharacterScreenMode m_mode = W8_CHARACTER_CREATE;
    int m_page_index = -1;
    bool m_header_dirty = false;
    W8Character* m_original = nullptr;
    W8Character m_character;
    W8CharacterCreationState m_creation_state;
    bool m_block_advance = false;
    bool m_confirm_profession = false;
    bool m_force_transition = false;
    UiLayer m_input;
    bool m_navigation_dirty = true;
    std::unique_ptr<W8ButtonWidget> m_previous;
    std::unique_ptr<W8ButtonWidget> m_next;
    std::unique_ptr<W8ButtonWidget> m_exit;
    std::unique_ptr<W8ButtonWidget> m_accept;
    std::unique_ptr<W8ButtonWidget> m_reset;
    bool m_page_enabled[4] = {};
    std::unique_ptr<W8CharacterPage> m_pages[4];
    std::unique_ptr<W8DialogBase> m_dialog;
    unsigned int m_dialog_response = 0;
    bool m_capture_dialog_result = false;
};

/* Per-profession message indexes. */
extern unsigned short g_profession_name_message_ids[32];
extern unsigned short g_race_name_message_ids[W8_RACE_COUNT];
/* Per-profession level-name message indexes, one row per profession for the
   level bands. */
extern unsigned short g_profession_level_name_message_ids[15][9];

extern char g_dash[];
