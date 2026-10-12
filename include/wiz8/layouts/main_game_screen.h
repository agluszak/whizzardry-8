#include <cstdint>
#include "Font.h"
#include <string>
#include <vector>
#pragma once

#include "wiz8/camera_motion.h"

#include "wiz8/geometry.h"
#include "wiz8/layouts/targeting.h"

class stTextureAnim;
class stModelInstance2D;

#include <cstddef>
#include <memory>

/* One animated cursor resource the main-game / camp screens can install. The
   object is an stTextureAnim; size and hotspot are the unsigned shorts
   ApplyCurrentCursor feeds to the mouse-cursor surface helpers. */
struct W8MainGameResourceSlot {
    std::int32_t image_id;             /* Historical offset 0x00: catalog image / YOffset key */
    std::shared_ptr<stTextureAnim> object;    /* Historical offset 0x04: loaded cursor anim */
    std::uint32_t frame_count; /* Historical offset 0x08: frames appended into the anim */
    std::uint16_t size_x;    /* Historical offset 0x0c */
    std::uint16_t size_y;    /* Historical offset 0x0e */
    std::uint16_t hotspot_x; /* Historical offset 0x10 */
    std::uint16_t hotspot_y; /* Historical offset 0x12 */
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

static const std::uint32_t W8_MAIN_REDRAW_FRAME = 0x80000000U;
static const std::uint32_t W8_MAIN_REDRAW_ALL = 0xffffffffU;

/* The dormant typed-dialogue input state hung off the level block: a plain
   heap object, not an srClass derivative. */
struct W8DialogueTextState {
    std::string text;                 /* Historical offset 0x00: owned editable buffer */
    std::string first_line_prefix;    /* Historical offset 0x04: owned, measured before line one */
    void (*completion_callback)(); /* Historical offset 0x08: invoked before the input closes */
    std::vector<std::size_t> line_offsets{0};    /* Historical offset 0x0c: owned offsets into text */
    std::uint32_t line_count;       /* Historical offset 0x10 */
    std::int32_t notice_channel;            /* Historical offset 0x1c: passed to the completed notice */
    std::int16_t text_box;                /* Historical offset 0x20: text box whose scroll is replaced */
    std::int16_t unknown_22;              /* Historical offset 0x22: never read */
    std::uint32_t wrap_width;       /* Historical offset 0x24: pixel budget and notice width */
    std::size_t cursor;           /* Historical offset 0x28: insertion point in text */
    bool cursor_dirty;    /* Historical offset 0x2c: repaint just the dialogue cursor */
    bool dirty;                    /* Historical offset 0x2d: cursor/text redraw pending */
    std::uint8_t padding_2e[2];
    std::uint32_t saved_scroll_line; /* Historical offset 0x30: restored when input closes */
};

struct W8LevelRuntimeBlock {
    /* Owned notice/list paint scratch, reused by the monster-list formatter. */
    std::string text_paint_scratch;
    bool message_box_pending; /* Historical offset 0x0f0: message box lived last frame; render deferred */
    std::uint8_t flags[3];
    std::uint32_t redraw_flags; /* Historical offset 0x0f4 */
    /* Snapshot of redraw_flags taken before the two redraw passes; the second
       pass consumes this copy while the live word may still change. */
    std::uint32_t saved_redraw_flags;           /* Historical offset 0x0f8 */
    W8MainUiMode main_ui_mode;                 /* Historical offset 0x0fc: portraits / formation / radar */
    std::int32_t camera_mode;                           /* Historical offset 0x100 */
    std::uint32_t hover_region;                 /* Historical offset 0x104 */
    bool portrait_strip_dirty;                 /* Historical offset 0x108: portrait strip needs a redraw */
    bool portrait_refresh_pending[8]; /* Historical offset 0x109 */
    std::uint8_t padding_111[3];
    std::int32_t portrait_refresh_image[8]; /* Historical offset 0x114 */
    std::int32_t portrait_refresh_mode[8];  /* Historical offset 0x134 */
    bool pick_changed;
    std::uint8_t action_panel_visible;
    bool formation_board_visible; /* Historical offset 0x156: formation board shown */
    std::uint8_t radar_map_visible;
    /* Historical offset 0x158: portraits UI mode selected; picks the portrait-hover layout. */
    std::uint8_t portrait_mode;
    std::uint8_t text_scroll_drag_idle; /* Historical offset 0x159: cleared while thumb is dragged */
    std::uint8_t padding_15a[2];
    /* Historical offset 0x15c: the x origin the mode-6 hover panel anchors the slot's portrait
       column position against. */
    std::int32_t portrait_hover_x_origin;
    /* Historical offset 0x160/0x164/0x168: layout offsets ApplyMainGameModeFlag writes when the
       action panel or portrait chrome is down (0x76/6 and 0x69/6); cleared to
       zero while the matching panel is up. */
    std::int32_t portrait_y_shift;
    std::int32_t portrait_layout_inset;
    std::int32_t action_panel_layout_inset;
    std::int32_t highlight_override;             /* Historical offset 0x16c */
    std::int32_t condition_hover_party_slot;     /* Historical offset 0x170 */
    std::int32_t enchantment_hover_party_slot;   /* Historical offset 0x174 */
    std::int32_t name_hover_party_slot;          /* Historical offset 0x178 */
    std::int32_t vitals_hover_party_slot;        /* Historical offset 0x17c */
    std::int32_t combat_action_hover_party_slot; /* Historical offset 0x180 */
    std::int32_t assay_hover_party_slot;         /* Historical offset 0x184 */
    /* Historical offset 0x188: PortraitAssaySidebarRegionEvent hover zone — 1 upper weapon
       row, 2 lower, 3 assayable item; Screens resets it to 0. */
    std::int32_t portrait_assay_hover_mode;
    std::int32_t formation_highlight_party_slot; /* Historical offset 0x18c */
    std::int32_t held_item_display;              /* Historical offset 0x190 */
    std::int32_t value_194;
    std::int32_t value_198;
    std::int32_t value_19c;
    std::int32_t text_content_region;
    std::int32_t dialogue_content_region;
    std::uint32_t text_lines[12]; /* Historical offset 0x1a8 */
    /* Historical offset 0x1d8/0x1e8: wrapped-line starts for hover and selection highlighting.
       NPC dialogue uses box 2 for the hovered row and selected trade item. */
    std::int32_t hovered_text_lines[4];
    std::int32_t selected_text_lines[4];
    bool dialogue_text_input_open;
    std::uint8_t padding_1f9[3];
    /* Never set in this build; the editor path that consumes it is dormant. */
    std::unique_ptr<W8DialogueTextState> dialogue_text_input;
    std::int32_t portrait_overlay_party_slot; /* Historical offset 0x200: -1 while untracked */
    /* Historical offset 0x204 / 0x208: party slot whose condition / enchantment orb is held or
       drag-hovering; PortraitConditionOrbRegionEvent and
       PortraitEnchantmentOrbRegionEvent own them (-1 while idle). */
    std::int32_t condition_orb_party_slot;
    std::int32_t enchantment_orb_party_slot;
    std::int32_t condition_highlight_party_slot; /* Historical offset 0x20c: -1 while untracked */
    bool flag3;                         /* Historical offset 0x210 */
    std::uint8_t padding_211[3];
    std::uint64_t clock;                  /* Historical offset 0x214 */
    std::uint8_t portrait_flash; /* Historical offset 0x218: 500ms highlight pulse on clock */
    std::uint8_t padding_219[3];
    /* Historical offset 0x21c: content row count captured by the mode-6 hover overlay draw. */
    std::int32_t hover_overlay_row_count;
    std::int32_t dialogue_x;
    std::uint32_t dialogue_y; /* ClearSurfaceRect's unsigned top/bottom */
    std::uint32_t dialogue_height;
    std::int32_t dialogue_row_y;      /* Historical offset 0x22c: first content row's y; the highlight
                                  sprite hangs off it at highlight_row * 0x12 */
    std::int32_t dialogue_text_x;     /* Historical offset 0x230: panel left + 9 */
    std::int32_t dialogue_text_width; /* Historical offset 0x234: panel width - 0x12 */
    std::int32_t dialogue_width;
    std::int32_t highlight_row; /* Historical offset 0x23c: -1 none; the content row the highlight sits on */
    /* The dialogue highlight sprite. DrawHighlightOverlay lazily creates it
       from catalog object 0x72 through CreateSpriteFromSurface, and it is released
       through ReleaseObject whenever mode 6 ends or the tracked party
       slots change. */
    stModelInstance2D* highlight_graphic; /* Historical offset 0x240 */
    std::uint32_t world_update_flags;      /* Historical offset 0x244 */
    std::uint32_t camera_motion_flags;     /* Historical offset 0x248 */
    /* Historical offset 0x24c: mouselook debug overlay; draws the pending pitch/yaw readout. */
    std::uint8_t mouselook_debug;
    /* Historical offset 0x24d: video inspector overlay enabled when no modal mode owns input. */
    bool inspector_enabled;
    std::uint8_t padding_24e[2];
    std::uint64_t character_update_timer; /* Historical offset 0x250 */
    std::uint64_t world_update_timer;     /* Historical offset 0x254 */
    std::uint64_t countdown0;             /* Historical offset 0x258 */
    std::uint64_t countdown1;             /* Historical offset 0x25c */
    bool transition_active;       /* Historical offset 0x260 */
    bool transition_pending;      /* Historical offset 0x261 */
    std::uint8_t padding_262[2];
    std::int32_t highlighted_item;
    std::int32_t selected_item;
    std::uint64_t countdown2; /* Historical offset 0x26c */
    bool flag4;
    /* Historical offset 0x271: text box visible; toggled by the keyboard shortcut and raised
       by spell/item/dialogue screens that need it. */
    bool text_box_visible;
    bool mipe_editing; /* Historical offset 0x272: MIPE edit session owns the hidden action panel */
    std::uint8_t padding_273;
    std::uint64_t tick; /* Historical offset 0x274 */
    std::int32_t value_278;     /* Historical offset 0x278 */
    std::int32_t pending_level;
    std::int32_t pending_entry_id;
    std::int32_t group_list_rows;  /* Historical offset 0x284 */
    std::int32_t group_list_width; /* Historical offset 0x288 */
    std::int32_t action_group;     /* Historical offset 0x28c */
    /* Historical offset 0x290: result of HighlightMonsterAsTarget for the current pick; [0]
       drives the group tint and the target cursor shape. */
    std::uint8_t target_highlight_ok[0x10];
    /* Historical offset 0x2a0..0x2a8: the formation board's three stModelInstance2D-family
       sprites - the board art with slot markers baked in, the rotating compass
       needle tracking party_facing against party_heading, and a lazily
       created overlay.  All are produced by CreateSpriteFromSurface in MGSFormation.cpp
       and released through ReleaseObject. */
    stModelInstance2D* formation_board_sprite;
    stModelInstance2D* formation_compass_sprite;
    stModelInstance2D* formation_overlay_sprite;
    std::int32_t value_2ac; /* Historical offset 0x2ac */
    std::int32_t value_2b0; /* Historical offset 0x2b0 */
    std::int32_t value_2b4; /* Historical offset 0x2b4 */
    std::uint8_t unknown_2b8[8];
    std::uint8_t refresh_combat_panel;
    std::uint8_t padding_2c1[3];
    std::uint64_t combat_panel_timer;
    std::uint8_t refresh_party_panel;
    std::uint8_t padding_2c9;
    std::int16_t combat_end_notification;
    std::int32_t text_box_left;
    std::int32_t text_box_top;
    std::int32_t text_box_right;
    std::int32_t text_box_bottom;
    std::int32_t move_percent;
    std::int32_t move_percent_shown;
    std::uint8_t unknown_2e4[4];
    Font* text_box_font;       /* Historical offset 0x2e8: g_wiz_text_font_secondary while the main text box is live */
    Palette16 palette; /* Historical offset 0x2ec */
    W8ActionKind selection_kind;
    std::int32_t pending_action; /* Historical offset 0x2f4 */
    bool selection_settled;
    std::uint8_t padding_2f9[3];
    std::uint64_t tooltip_since;
    bool tooltip_pending;
    std::uint8_t padding_301[3];
    std::int32_t tooltip_subject;
    std::int32_t tooltip_kind;
    std::uint64_t countdown3; /* Historical offset 0x30c */
    std::int32_t combat_slot;  /* Historical offset 0x310 */
    /* Historical offset 0x314: the keyboard-action menu is open; set before BuildKeyboardMenu,
       cleared by CloseKeyboardMenu. */
    bool keyboard_menu_open;
    std::uint8_t padding_315[3];
    std::int32_t hover_combat_slot; /* Historical offset 0x318 */
    /* Historical offset 0x31c: keyboard-menu cursor-left grace countdown armed; expires into
       CloseKeyboardMenu. */
    std::uint8_t cursor_grace;
    std::uint8_t padding_31d[3];
    std::uint64_t countdown4; /* Historical offset 0x320: portrait right-hold arm clock */
    /* Historical offset 0x324: PortraitSelectRegionEvent right-button hold armed for camp. */
    bool portrait_right_hold_armed;
    std::uint8_t formation_board_alternate; /* Historical offset 0x325: highlighted board art while hovered */
    std::uint8_t radar_map_alternate;       /* Historical offset 0x326: radar uses alternate frame art */
    bool review_transition_active;           /* Historical offset 0x327: set while leaving into review */
    /* Historical offset 0x328: the review-screen transition finished; gates its early-out. */
    bool review_transition_done;
    std::uint8_t padding_329[3];
    std::uint64_t countdown5;
};
