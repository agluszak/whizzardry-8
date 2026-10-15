#include <string>

#include "Font.h"
#pragma once

#include "wiz8/camera_motion.h"
#include "wiz8/geometry.h"
#include "wiz8/layouts/targeting.h"

class stTextureAnim;
class stModelInstance2D;

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

/* Main-game chrome mode ApplyMainGameModeFlag writes into both the level block
   and g_settings. NONE is settings-only: Screens parks it at -1 while
   ApplyMainGameModeFlag re-raises panels during main-game enter. */
enum W8MainUiMode {
    W8_MAIN_UI_MODE_NONE = -1,
    W8_MAIN_UI_MODE_PORTRAITS = 0,
    W8_MAIN_UI_MODE_FORMATION = 1,
    W8_MAIN_UI_MODE_RADAR = 2
};

/* RequestRedraw and the two main-game paint passes share this mask. The low
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
                            W8_MAIN_REDRAW_SUBMENU_BUTTONS | W8_MAIN_REDRAW_SUBMENU_PANEL |
                            W8_MAIN_REDRAW_FORMATION | W8_MAIN_REDRAW_OPTIONS_BUTTON |
                            W8_MAIN_REDRAW_COMBAT_STANCE | W8_MAIN_REDRAW_CHARACTER_ACTION
};

static const unsigned int W8_MAIN_REDRAW_FRAME = 0x80000000U;
static const unsigned int W8_MAIN_REDRAW_ALL = 0xffffffffU;

struct W8LevelRuntimeBlock {
    /* Owned notice/list paint scratch, reused by the monster-list formatter. */
    std::string text_paint_scratch;
    unsigned char flags[3];
    unsigned int redraw_flags;
    /* Snapshot of redraw_flags taken before the two redraw passes; the second
       pass consumes this copy while the live word may still change. */
    unsigned int saved_redraw_flags;
    W8MainUiMode main_ui_mode;
    int camera_mode;
    bool portrait_strip_dirty;
    unsigned char portrait_refresh_pending[8];
    unsigned char padding_111[3];
    int portrait_refresh_image[8];
    int portrait_refresh_mode[8];
    bool pick_changed;
    unsigned char action_panel_visible;
    bool formation_board_visible;
    unsigned char radar_map_visible;
    /* Historical offset 0x158: portraits UI mode selected; picks the portrait-hover layout. */
    unsigned char portrait_mode;
    unsigned char text_scroll_drag_idle;
    unsigned char padding_15a[2];
    /* Historical offset 0x15c: the x origin the mode-6 hover panel anchors the slot's portrait
       column position against. */
    int portrait_hover_x_origin;
    /* Historical offset 0x160/0x164/0x168: layout offsets ApplyMainGameModeFlag writes when the
       action panel or portrait chrome is down (0x76/6 and 0x69/6); cleared to
       zero while the matching panel is up. */
    int portrait_y_shift;
    int portrait_layout_inset;
    int action_panel_layout_inset;
    int highlight_override;
    int condition_hover_party_slot;
    int enchantment_hover_party_slot;
    int name_hover_party_slot;
    int vitals_hover_party_slot;
    int combat_action_hover_party_slot;
    int assay_hover_party_slot;
    /* Historical offset 0x188: PortraitAssaySidebarInput hover zone — 1 upper weapon
       row, 2 lower, 3 assayable item; Screens resets it to 0. */
    int portrait_assay_hover_mode;
    int formation_highlight_party_slot;
    int held_item_display;
    int value_194;
    int value_198;
    int value_19c;
    int text_content_region;
    int dialogue_content_region;
    unsigned int text_lines[12];
    /* Historical offset 0x1d8/0x1e8: wrapped-line starts for hover and selection highlighting.
       NPC dialogue uses box 2 for the hovered row and selected trade item. */
    int hovered_text_lines[4];
    int selected_text_lines[4];
    int portrait_overlay_party_slot;
    /* Historical offset 0x204 / 0x208: party slot whose condition / enchantment orb is held or
       drag-hovering; PortraitConditionOrbInput and
       PortraitEnchantmentOrbInput own them (-1 while idle). */
    int condition_orb_party_slot;
    int enchantment_orb_party_slot;
    int condition_highlight_party_slot;
    bool flag3;
    unsigned char padding_211[3];
    uint64_t clock;
    unsigned char portrait_flash;
    unsigned char padding_219[3];
    /* Historical offset 0x21c: content row count captured by the mode-6 hover overlay draw. */
    int hover_overlay_row_count;
    int dialogue_x;
    unsigned int dialogue_y; /* ClearSurfaceRect's unsigned top/bottom */
    unsigned int dialogue_height;
    int dialogue_row_y; /* Historical offset 0x22c: first content row's y; the highlight
                                  sprite hangs off it at highlight_row * 0x12 */
    int dialogue_text_x;
    int dialogue_text_width;
    int dialogue_width;
    int highlight_row;
    /* The dialogue highlight sprite. DrawHighlightOverlay lazily creates it
       from catalog object 0x72 through CreateSpriteFromSurface, and it is released
       through ReleaseObject whenever mode 6 ends or the tracked party
       slots change. */
    stModelInstance2D* highlight_graphic;
    unsigned int world_update_flags;
    unsigned int camera_motion_flags;
    /* Historical offset 0x24c: mouselook debug overlay; draws the pending pitch/yaw readout. */
    unsigned char mouselook_debug;
    /* Historical offset 0x24d: video inspector overlay enabled when no modal mode owns input. */
    bool inspector_enabled;
    unsigned char padding_24e[2];
    uint64_t character_update_timer;
    uint64_t world_update_timer;
    uint64_t countdown0;
    uint64_t countdown1;
    bool transition_active;
    bool transition_pending;
    unsigned char padding_262[2];
    int highlighted_item;
    int selected_item;
    uint64_t countdown2;
    bool flag4;
    /* Historical offset 0x271: text box visible; toggled by the keyboard shortcut and raised
       by spell/item/dialogue screens that need it. */
    bool text_box_visible;
    bool mipe_editing;
    unsigned char padding_273;
    uint64_t tick;
    int value_278;
    int pending_level;
    int pending_entry_id;
    int group_list_rows;
    int group_list_width;
    int action_group;
    /* Historical offset 0x290: result of HighlightMonsterAsTarget for the current pick; [0]
       drives the group tint and the target cursor shape. */
    unsigned char target_highlight_ok[0x10];
    /* Historical offset 0x2a0..0x2a8: the formation board's three stModelInstance2D-family
       sprites - the board art with slot markers baked in, the rotating compass
       needle tracking party_facing against party_heading, and a lazily
       created overlay.  All are produced by CreateSpriteFromSurface in MGSFormation.cpp
       and released through ReleaseObject. */
    stModelInstance2D* formation_board_sprite;
    stModelInstance2D* formation_compass_sprite;
    stModelInstance2D* formation_overlay_sprite;
    int value_2ac;
    int value_2b0;
    int value_2b4;
    unsigned char unknown_2b8[8];
    unsigned char refresh_combat_panel;
    unsigned char padding_2c1[3];
    uint64_t combat_panel_timer;
    unsigned char refresh_party_panel;
    unsigned char padding_2c9;
    short combat_end_notification;
    int text_box_left;
    int text_box_top;
    int text_box_right;
    int text_box_bottom;
    int move_percent;
    int move_percent_shown;
    unsigned char unknown_2e4[4];
    Font* text_box_font;
    Palette16 palette;
    W8ActionKind selection_kind;
    int pending_action;
    bool selection_settled;
    unsigned char padding_2f9[3];
    uint64_t tooltip_since;
    bool tooltip_pending;
    unsigned char padding_301[3];
    int tooltip_subject;
    int tooltip_kind;
    uint64_t countdown3;
    int combat_slot;
    /* Historical offset 0x314: the keyboard-action menu is open; set before BuildKeyboardMenu,
       cleared by CloseKeyboardMenu. */
    bool keyboard_menu_open;
    unsigned char padding_315[3];
    int hover_combat_slot;
    /* Historical offset 0x31c: keyboard-menu cursor-left grace countdown armed; expires into
       CloseKeyboardMenu. */
    unsigned char cursor_grace;
    unsigned char padding_31d[3];
    uint64_t countdown4;
    /* Historical offset 0x324: PortraitSelectInput right-button hold armed for camp. */
    bool portrait_right_hold_armed;
    unsigned char formation_board_alternate;
    unsigned char radar_map_alternate;
    bool review_transition_active;
    /* Historical offset 0x328: the review-screen transition finished; gates its early-out. */
    bool review_transition_done;
    unsigned char padding_329[3];
    uint64_t countdown5;
};
