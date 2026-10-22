#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "wiz8/canvas/Font.h"
#pragma once

#include "wiz8/camera_motion.h"
#include "wiz8/design_rect.h"
#include "wiz8/geometry.h"
#include "wiz8/rules/targeting.h"

class Texture;

enum W8TextBoxArrowState : unsigned char {
    kTextBoxArrowUninitialized = 0,
    kTextBoxArrowNone = 0xff,
    kUpArrowDisabled = 0x56,
    kUpArrowEnabled = 0x57,
    kUpArrowPressed = 0x58,
    kDownArrowDisabled = 0x59,
    kDownArrowEnabled = 0x5a,
    kDownArrowPressed = 0x5b,
};
static_assert(sizeof(W8TextBoxArrowState) == sizeof(unsigned char));

#include <memory>

#include "wiz8/render/compositor.h"

/* One animated cursor resource the main-game / camp screens can install. The
   object is an Texture; size and hotspot are the unsigned shorts
   the pointer feeds to the mouse-cursor surface helpers. */
struct W8MainGameResourceSlot {
    int image_id;
    std::shared_ptr<Texture> object;
    unsigned int frame_count;
    unsigned short size_x;
    unsigned short size_y;
    unsigned short hotspot_x;
    unsigned short hotspot_y;
};

/* The main game's chrome mode: the player's preference in g_settings, and the mode
   the applied MainGameLayout shows. */
enum W8MainUiMode {
    W8_MAIN_UI_MODE_NONE = -1,
    W8_MAIN_UI_MODE_PORTRAITS = 0,
    W8_MAIN_UI_MODE_FORMATION = 1,
    W8_MAIN_UI_MODE_RADAR = 2
};

enum W8ViewportMode : unsigned char {
    W8_VIEWPORT_MODE_FULL = 0,
    W8_VIEWPORT_MODE_FULL_WITH_BOTTOM_PANEL = 1,
    W8_VIEWPORT_MODE_CENTERED_WITH_BOTTOM_PANEL = 2,
    W8_VIEWPORT_MODE_CENTERED_COMPACT = 3,
    W8_VIEWPORT_MODE_CENTERED_FULL_HEIGHT = 4,
    W8_VIEWPORT_MODE_CENTERED_TALL = 5,
    W8_VIEWPORT_MODE_FULL_WIDTH_TALL = 6,
    W8_VIEWPORT_MODE_UNINITIALIZED = 7
};

struct W8GameSettings;

/* The main game's panel layout, decided at once from the open interaction and the
   player's preferences, and applied by MainGameScreen::refreshLayout. */
struct MainGameLayout {
    /* The layout for the player's preferences while a spell or item picker, lock or
       trap (`picker`) or an NPC dialogue is open or not. An interaction overrides the
       preferred layout without changing the preference. */
    static MainGameLayout Decide(const W8GameSettings& settings, bool picker, bool npc_dialogue);

    // The mode shown: the preferred one unless an interaction overrides it.
    W8MainUiMode mode = W8_MAIN_UI_MODE_FORMATION;
    /* A spell or item picker, a lock or trap, or an NPC dialogue owns the bottom panel
       and hides the formation board and radar map. */
    bool interaction = false;
    bool formation_board = false;
    bool radar_map = false;
    // The bottom panel is up: the action bar, or the interaction's own panel.
    bool bottom_panel = false;
    W8ViewportMode viewport = W8_VIEWPORT_MODE_UNINITIALIZED;

    bool operator==(const MainGameLayout&) const = default;
    // The x origin the portrait hover panel anchors the slot's portrait column against.
    int PortraitHoverXOrigin() const { return mode == W8_MAIN_UI_MODE_PORTRAITS ? 0 : 0x69; }
    // The portrait hover panel drops by this much while the bottom panel is down.
    int PortraitYShift() const { return bottom_panel ? 0 : 0x76; }
};

/* What the main game's next draw repaints, part by part, as the camp's header and pages
   record theirs. Each draw repaints the recorded parts and clears the record. */
struct MainGameRepaint {
    // Clear the screen and repaint every part.
    bool everything = false;
    /* A slot's complete portrait panel: the band, its vitals, its condition icon and, in
       combat, its side-strip portrait. Each finer request below is part of this one. */
    std::bitset<8> portraits;
    // Complete vitals panels.
    std::bitset<8> portrait_bars;
    /* The widgets painted around the repaint pass, which do not start it: the portrait's
       next animation frame, the condition or enchantment icon after its hover or icon
       changed, and the combat side-strip portrait after its hover or status changed. */
    std::bitset<8> portrait_frames;
    std::bitset<8> condition_icons;
    std::bitset<8> combat_portraits;
    /* The bottom panels of the active mode: spell casting (3), item use (3) or NPC
       dialogue (7). Panel 1 is the text box. */
    std::bitset<7> mode_panels;
    bool roof_and_spell_icons = false;
    // The chrome's layout changed: the bottom panels and the buttons around them repaint.
    bool layout = false;
    bool radar = false;
    bool text_box = false;
    bool submenu_buttons = false;
    bool submenu_panel = false;
    bool formation = false;
    // The world view's frame: the prompt, the movement panel or the combat effects.
    bool portrait_panel = false;
    // The plate shown beside a portrait, laid out by the presentation update.
    bool portrait_plate = false;
    // Where a dismissed plate was, cleared before anything repaints over it.
    std::optional<DesignRect> plate_erase;
    bool monster_list = false;
    // The open keyboard menu, formation panel and combat movement panel.
    bool keyboard_menu = false;
    bool formation_panel = false;
    bool movement_panel = false;
    bool options_button = false;
    bool combat_stance = false;
    bool combat_progress = false;
    bool character_action = false;
    bool combat_effects = false;
    // The debug video inspector, refreshed every half second; painted after the panels.
    bool video_inspector = false;

    // A party slot's portrait; no slot (-1) asks for none.
    void portrait(int slot)
    {
        if (slot >= 0 && slot < 8) portraits.set(static_cast<std::size_t>(slot));
    }
    bool any() const
    {
        return everything || portraits.any() || portrait_bars.any() || mode_panels.any() ||
               portrait_plate || plate_erase || roof_and_spell_icons || layout || radar ||
               text_box || submenu_buttons || submenu_panel || formation || portrait_panel ||
               monster_list || keyboard_menu || formation_panel || movement_panel ||
               options_button || combat_stance || combat_progress || character_action ||
               combat_effects;
    }
};

/* What a delayed portrait hover highlights. The value also indexes the
   matching help string (24 + kind). */
enum W8PortraitHighlightKind {
    W8_HIGHLIGHT_NONE = -1,
    W8_HIGHLIGHT_TARGET = 0,
    W8_HIGHLIGHT_CONDITION = 1,
    W8_HIGHLIGHT_ENCHANTMENT = 2,
    W8_HIGHLIGHT_NAME = 3,
    W8_HIGHLIGHT_VITALS = 4,
    W8_HIGHLIGHT_ASSAY = 5,
    /* Formation board marker; applying it also refreshes the board. */
    W8_HIGHLIGHT_FORMATION_BOARD = 6,
    W8_HIGHLIGHT_FORMATION = 7
};

/* One highlighted party slot and why it is highlighted. */
struct W8PortraitHighlight {
    W8PortraitHighlightKind kind = W8_HIGHLIGHT_NONE;
    int slot = -1;

    int SlotFor(W8PortraitHighlightKind wanted) const { return kind == wanted ? slot : -1; }
    bool IsFormation() const
    {
        return kind == W8_HIGHLIGHT_FORMATION_BOARD || kind == W8_HIGHLIGHT_FORMATION;
    }
    int FormationSlot() const { return IsFormation() ? slot : -1; }
    /* The target and formation highlights light the whole portrait. */
    bool LightsPortrait(int party_slot) const
    {
        return slot == party_slot && (kind == W8_HIGHLIGHT_TARGET || IsFormation());
    }
};

enum W8PortraitAssayHoverMode : unsigned char {
    W8_PORTRAIT_ASSAY_HOVER_NONE = 0,
    W8_PORTRAIT_ASSAY_HOVER_PRIMARY_WEAPON = 1,
    W8_PORTRAIT_ASSAY_HOVER_SECONDARY_WEAPON = 2,
    W8_PORTRAIT_ASSAY_HOVER_TWO_HANDED_WEAPON = 3
};

/* The plate beside a portrait: the held condition or enchantment orb's list,
   the vitals, or the condition highlight's conditions and enchantments. */
enum class PortraitPlate : unsigned char { Conditions, Enchantments, Vitals, Status };
struct ShownPortraitPlate {
    PortraitPlate kind;
    int slot;
    int rows;
};

struct W8LevelRuntimeBlock {
    /* Owned notice/list paint scratch, reused by the monster-list formatter. */
    std::string text_paint_scratch;
    MainGameRepaint repaint;
    unsigned char portrait_refresh_pending[8];
    int portrait_refresh_image[8];
    unsigned char text_scroll_drag_idle;
    /* The applied highlight, and the hover waiting out the tooltip delay. */
    W8PortraitHighlight highlight;
    W8PortraitHighlight pending_highlight;
    int combat_action_hover_party_slot = -1;
    /* PortraitAssaySidebarInput hover zone — 1 upper weapon
       row, 2 lower, 3 assayable item; Screens resets it to 0. */
    W8PortraitAssayHoverMode portrait_assay_hover_mode;
    int held_item_display = -1;
    W8TextBoxArrowState text_content_region = kTextBoxArrowNone;
    W8TextBoxArrowState dialogue_content_region = kTextBoxArrowNone;
    unsigned int text_lines[12];
    /* Wrapped-line starts for hover and selection highlighting.
       NPC dialogue uses box 2 for the hovered row and selected trade item. */
    int hovered_text_lines[4];
    int selected_text_lines[4];
    int portrait_overlay_party_slot = -1;
    /* Party slot whose condition / enchantment orb is held or
       drag-hovering; PortraitConditionOrbInput and
       PortraitEnchantmentOrbInput own them (-1 while idle). */
    int condition_orb_party_slot = -1;
    int enchantment_orb_party_slot = -1;
    int condition_highlight_party_slot = -1;
    uint64_t clock;
    unsigned char portrait_flash;
    int dialogue_x;
    unsigned int dialogue_y; /* ClearSurfaceRect's unsigned top/bottom */
    unsigned int dialogue_height;
    /* First content row's y; the highlight sprite hangs off it at highlight_row * 0x12. */
    int dialogue_row_y;
    int dialogue_text_x;
    int dialogue_text_width;
    int dialogue_width;
    int highlight_row = -1;
    // The plate the dialogue rectangle holds while the highlight overlay mode is up.
    std::optional<ShownPortraitPlate> shown_plate;
    /* The dialogue highlight sprite, made from catalog object 0x72 when a plate
       is laid out and released when the plate is dismissed. */
    std::unique_ptr<OverlayQuad> highlight_graphic;
    unsigned int world_update_flags;
    unsigned int camera_motion_flags;
    uint64_t countdown0;
    uint64_t countdown1;
    int highlighted_item = -1;
    int selected_item = -1;
    /* Text box visible; toggled by the keyboard shortcut and raised
       by spell/item/dialogue screens that need it. */
    bool text_box_visible;
    // When the video inspector was last refreshed.
    uint64_t tick;
    int group_list_rows;
    int group_list_width;
    int action_group = -1;
    /* Result of HighlightMonsterAsTarget for the current pick; [0]
       drives the group tint and the target cursor shape. */
    /* The formation board's three overlay
       sprites - the board art with slot markers baked in, the rotating compass
       needle tracking party_facing against party_heading, and a lazily
       created overlay.  All are produced by makeOverlayFromSurface in MGSFormation.cpp
       and owned by this runtime block. */
    std::unique_ptr<OverlayQuad> formation_board_sprite;
    std::unique_ptr<OverlayQuad> formation_compass_sprite;
    std::unique_ptr<OverlayQuad> formation_overlay_sprite;
    unsigned char refresh_combat_panel;
    uint64_t combat_panel_timer;
    // The submenu panel that is open, by menu id.
    std::optional<short> open_submenu;
    int text_box_left;
    int text_box_top;
    int text_box_right;
    int text_box_bottom;
    // The party's remaining move, in percent, as the gauge last showed it.
    int move_percent_shown;
    Font* text_box_font;
    Palette16 palette;
    uint64_t tooltip_since;
    bool tooltip_pending;
    uint64_t countdown3;
    int combat_slot;
    int hover_combat_slot;
    /* Keyboard-menu cursor-left grace countdown armed. */
    unsigned char cursor_grace;
    uint64_t countdown4;
    /* PortraitSelectInput right-button hold armed for camp. */
    bool portrait_right_hold_armed;
    unsigned char formation_board_alternate;
    /* The party-death or ending screen is up, from its first fade until the
       closing fade's EndReviewTransition. */
    bool review_transition_active;
    /* A BeginScreenFade ramp is in progress; the main game's frame advances it. */
    bool screen_fade_running;
    uint64_t countdown5;
};
