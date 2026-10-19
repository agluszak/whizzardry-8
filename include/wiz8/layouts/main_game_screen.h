#include <string>

#include "wiz8/canvas/Font.h"
#pragma once

#include "wiz8/camera_motion.h"
#include "wiz8/geometry.h"
#include "wiz8/layouts/targeting.h"

class stTextureAnim;
class stModelInstance2D;
enum W8TextBoxArrowState : unsigned char;

#include <memory>

/* One animated cursor resource the main-game / camp screens can install. The
   object is an stTextureAnim; size and hotspot are the unsigned shorts
   ApplyCurrentCursor feeds to the mouse-cursor surface helpers. */
struct W8MainGameResourceSlot {
    int image_id;
    std::shared_ptr<stTextureAnim> object;
    unsigned int frame_count;
    unsigned short size_x;
    unsigned short size_y;
    unsigned short hotspot_x;
    unsigned short hotspot_y;
};

/* Main-game chrome mode RefreshMainGameLayout writes into both the level block
   and g_settings. NONE is settings-only: Screens parks it at -1 while
   RefreshMainGameLayout re-raises panels during main-game enter. */
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

/* RequestRedraw requests main-game repaints with this mask. The low
   eight bits select party portraits; combinations remain unsigned mask words. */
enum W8MainGameRedrawFlag {
    W8_MAIN_REDRAW_PORTRAIT_0 = 0x1,
    W8_MAIN_REDRAW_PORTRAIT_1 = 0x2,
    W8_MAIN_REDRAW_PORTRAIT_2 = 0x4,
    W8_MAIN_REDRAW_PORTRAIT_3 = 0x8,
    W8_MAIN_REDRAW_PORTRAIT_4 = 0x10,
    W8_MAIN_REDRAW_PORTRAIT_5 = 0x20,
    W8_MAIN_REDRAW_PORTRAIT_6 = 0x40,
    W8_MAIN_REDRAW_PORTRAIT_7 = 0x80,
    W8_MAIN_REDRAW_PORTRAITS = 0xff,
    W8_MAIN_REDRAW_ROOF_AND_SPELL_ICONS = 0x100,
    W8_MAIN_REDRAW_LAYOUT = 0x200,
    W8_MAIN_REDRAW_RADAR = 0x400,
    W8_MAIN_REDRAW_TEXT_BOX = 0x800,
    W8_MAIN_REDRAW_SUBMENU_BUTTONS = 0x1000,
    W8_MAIN_REDRAW_SUBMENU_PANEL = 0x2000,
    W8_MAIN_REDRAW_FORMATION = 0x4000,
    W8_MAIN_REDRAW_PORTRAIT_PANEL = 0x8000,
    W8_MAIN_REDRAW_MONSTER_LIST = 0x20000,
    W8_MAIN_REDRAW_OPTIONS_BUTTON = 0x40000,
    W8_MAIN_REDRAW_COMBAT_STANCE = 0x80000,
    W8_MAIN_REDRAW_COMBAT_PROGRESS = 0x100000,
    W8_MAIN_REDRAW_CHARACTER_ACTION = 0x200000,
    W8_MAIN_REDRAW_COMBAT_EFFECTS = 0x800000,
    W8_MAIN_REDRAW_CHROME = W8_MAIN_REDRAW_RADAR | W8_MAIN_REDRAW_TEXT_BOX |
        W8_MAIN_REDRAW_SUBMENU_BUTTONS | W8_MAIN_REDRAW_SUBMENU_PANEL | W8_MAIN_REDRAW_FORMATION |
        W8_MAIN_REDRAW_OPTIONS_BUTTON | W8_MAIN_REDRAW_COMBAT_STANCE |
        W8_MAIN_REDRAW_CHARACTER_ACTION
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

static const unsigned int W8_MAIN_REDRAW_FRAME = 0x80000000U;
static const unsigned int W8_MAIN_REDRAW_ALL = 0xffffffffU;

enum W8PortraitAssayHoverMode : unsigned char {
    W8_PORTRAIT_ASSAY_HOVER_NONE = 0,
    W8_PORTRAIT_ASSAY_HOVER_PRIMARY_WEAPON = 1,
    W8_PORTRAIT_ASSAY_HOVER_SECONDARY_WEAPON = 2,
    W8_PORTRAIT_ASSAY_HOVER_TWO_HANDED_WEAPON = 3
};

struct W8LevelRuntimeBlock {
    /* Owned notice/list paint scratch, reused by the monster-list formatter. */
    std::string text_paint_scratch;
    unsigned int redraw_flags;
    W8ViewportMode camera_mode;
    unsigned char portrait_refresh_pending[8];
    int portrait_refresh_image[8];
    bool pick_changed;
    unsigned char action_panel_visible;
    bool formation_board_visible;
    unsigned char radar_map_visible;
    /* Portraits UI mode selected; picks the portrait-hover layout. */
    bool portrait_mode;
    unsigned char text_scroll_drag_idle;
    /* The x origin the mode-6 hover panel anchors the slot's portrait
       column position against. */
    int portrait_hover_x_origin;
    /* Portrait hover-panel y offset RefreshMainGameLayout sets to 0x76 while the
       action panel is down and clears while it is up. */
    int portrait_y_shift;
    /* The applied highlight, and the hover waiting out the tooltip delay. */
    W8PortraitHighlight highlight;
    W8PortraitHighlight pending_highlight;
    int combat_action_hover_party_slot = -1;
    /* PortraitAssaySidebarInput hover zone — 1 upper weapon
       row, 2 lower, 3 assayable item; Screens resets it to 0. */
    W8PortraitAssayHoverMode portrait_assay_hover_mode;
    int held_item_display = -1;
    W8TextBoxArrowState text_content_region;
    W8TextBoxArrowState dialogue_content_region;
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
    /* The dialogue highlight sprite. DrawHighlightOverlay lazily creates it
       from catalog object 0x72 through CreateSpriteFromSurface, and it is released
       through ReleaseObject whenever mode 6 ends or the tracked party
       slots change. */
    stModelInstance2D* highlight_graphic;
    unsigned int world_update_flags;
    unsigned int camera_motion_flags;
    uint64_t character_update_timer;
    uint64_t world_update_timer;
    uint64_t countdown0;
    uint64_t countdown1;
    bool transition_active;
    int highlighted_item = -1;
    int selected_item = -1;
    /* Text box visible; toggled by the keyboard shortcut and raised
       by spell/item/dialogue screens that need it. */
    bool text_box_visible;
    uint64_t tick;
    int pending_level;
    int pending_entry_id;
    int group_list_rows;
    int group_list_width;
    int action_group = -1;
    /* Result of HighlightMonsterAsTarget for the current pick; [0]
       drives the group tint and the target cursor shape. */
    /* The formation board's three stModelInstance2D-family
       sprites - the board art with slot markers baked in, the rotating compass
       needle tracking party_facing against party_heading, and a lazily
       created overlay.  All are produced by CreateSpriteFromSurface in MGSFormation.cpp
       and released through ReleaseObject. */
    stModelInstance2D* formation_board_sprite;
    stModelInstance2D* formation_compass_sprite;
    stModelInstance2D* formation_overlay_sprite;
    unsigned char refresh_combat_panel;
    uint64_t combat_panel_timer;
    short combat_end_notification;
    int text_box_left;
    int text_box_top;
    int text_box_right;
    int text_box_bottom;
    int move_percent;
    int move_percent_shown;
    Font* text_box_font;
    Palette16 palette;
    W8ActionKind selection_kind;
    int pending_action;
    bool selection_settled;
    uint64_t tooltip_since;
    bool tooltip_pending;
    uint64_t countdown3;
    int combat_slot;
    /* The keyboard-action menu is open; set before BuildKeyboardMenu,
       cleared by CloseKeyboardMenu. */
    bool keyboard_menu_open;
    int hover_combat_slot;
    /* Keyboard-menu cursor-left grace countdown armed; expires into
       CloseKeyboardMenu. */
    unsigned char cursor_grace;
    uint64_t countdown4;
    /* PortraitSelectInput right-button hold armed for camp. */
    bool portrait_right_hold_armed;
    unsigned char formation_board_alternate;
    unsigned char radar_map_alternate;
    bool review_transition_active;
    /* The review-screen transition finished; gates its early-out. */
    bool review_transition_done;
    uint64_t countdown5;
};
