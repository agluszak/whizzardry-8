#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_items.h"
#include "wiz8/rules/item_instance.h"

struct NpcDialogueUi;
struct W8NpcState;

/* Usability filters and the exclusive equipment categories in the trade UI.
   The category bits use the same positions as W8ItemEquipSlotGroup. */
enum W8NpcTradeFilter : unsigned char {
    W8_NPC_TRADE_FILTER_NONE = 0,
    W8_NPC_TRADE_USABLE_BY_CHARACTER = 0x01,
    W8_NPC_TRADE_HAND = 0x04,
    W8_NPC_TRADE_BODY = 0x08,
    W8_NPC_TRADE_ACCESSORY = 0x10,
    W8_NPC_TRADE_OTHER = 0x20,
    W8_NPC_TRADE_CATEGORY_MASK = 0x3c,
    W8_NPC_TRADE_USABLE_BY_PARTY = 0x40
};

struct W8PendingNoticeLine {
    std::string text;
    int npc_kind;
};

/* An NPC script notice waiting for DispatchPendingNpcScriptNotice. */
struct W8PendingNotice {
    W8NpcState* npc;
    W8ItemInstance item;
    int line;
    bool flag;
    unsigned char force;
};

/* W8NpcInteractionState::trade_mode - the active tab of the option/trade
   layouts. The StringData.DAT captions the tabs and headers load are
   "Give", "Sell", "Buy" and "Shoplift": mode 2 lists the party's items
   with the purse as row zero, mode 3 the character/party sell pools, and
   modes 4/5 the NPC's own inventory. 0 while no tab is selected. */
enum W8NpcTradeMode : unsigned char {
    W8_NPC_TRADE_NONE = 0,
    W8_NPC_TRADE_GIVE = 2,
    W8_NPC_TRADE_SELL = 3,
    W8_NPC_TRADE_BUY = 4,
    W8_NPC_TRADE_SHOPLIFT = 5
};

/* The NPC conversation's interface state for one play-through: the dialogue layout and
   trade tables, the transcript the save keeps, the quote bubble and the staged script
   notice. The game owns it (`Game::npc_interaction`), so a new game starts it afresh.
   Transitional: most of it is presentation the main game screen should own. */
struct W8NpcInteractionState {
    // Out of line, where the dialogue's interface is a complete type.
    W8NpcInteractionState();
    ~W8NpcInteractionState();
    W8NpcInteractionState(const W8NpcInteractionState&) = delete;
    W8NpcInteractionState& operator=(const W8NpcInteractionState&) = delete;

    short value_000{}; /* cleared while the dialogue opens */
    unsigned char unknown_002[0xee]{};
    /* The dialogue's leading speaker: the occupied row whose character leads
       skill 0x16. */
    int dialogue_speaker{};
    int target_location_id{};
    W8NpcDialogueLayout dialogue_layout{};
    W8NpcTradeMode trade_mode{};
    /* The layout the current one replaced; back-out paths reopen it. */
    W8NpcDialogueLayout previous_dialogue_layout{};
    // Borrowed preview only; confirmation resolves trade_selection.
    W8ItemInstance* trade_item{};
    std::vector<W8TradeSelection> trade_rows{};
    std::optional<W8TradeSelection> trade_selection{};
    std::unique_ptr<NpcDialogueUi> ui{};
    /* The cursor position of the last mouse event the region handler acted
       on; repeat events at the same point are dropped. */
    int last_mouse_x{};
    int last_mouse_y{};
    /* The text-box line the mouse/wheel handlers last selected, -1 for none. */
    int hovered_text_line{};
    W8NpcTradeFilter trade_filter = W8_NPC_TRADE_FILTER_NONE;
    W8NpcState* dialogue_npc{};
    W8DialogueCategory dialogue_category_filter = W8_DIALOGUE_CATEGORY_ALL;
    /* Raised while the "Where Is" submit path runs, so the typed text is
       wrapped in the "Where is %s" template instead of a plain keyword lookup. */
    bool where_is_query{};
    bool transcript_sorted{};
    /* The item a pending NPC notice carries; copied from pending_notice
       when the dialogue opens. */
    W8ItemInstance pending_item{};
    /* pending_item came in through the cursor item; dialogue close returns it
       to the hand and the consume path drains its stack. */
    bool held_item_pending{};
    std::optional<QuoteBubble> quote_bubble{};
    short quote_x{};
    short quote_y{};
    short quote_width{};
    short quote_height{};
    bool quote_visible{};
    std::vector<W8PendingNoticeLine> pending_notice_lines{};
    /* The whole dialogue UI is parked (right-click hide) and the world input
       path is live until it is shown again. */
    unsigned char dialogue_hidden{};
    /* The option layout was reached through the Exit/farewell path, so backing
       out of it reopens the topic menu instead of the transcript. */
    bool reopen_topics{};
    int trade_gold{}; /* gold put on the trade table */
    /* g_settings.main_ui_mode saved while the NPC dialogue is suppressed. */
    /* The next UpdateNpcDialogueSubMode must reapply the trade_pc_items
       toggle state. */
    bool pending_trade_toggle{};
    /* A layout staged for reopen; the frame update closes the current layout
       and opens this one, then clears it. */
    W8NpcDialogueLayout pending_layout{};
    /* Cleared when the dialogue ends on a refusal/abrupt dismissal, in which
       case the close path queues a delayed party reaction event. */
    bool suppress_parting_reaction{};
    /* LookAtDialogueNpc aimed the camera at the NPC, so the close restores
       the saved camera angles. */
    bool camera_redirected{};
    float saved_camera_pitch{};
    W8QuoteNotice quote_notice{};
    /* A modal W8NpcDialog is up over the dialogue; transcript word clicks,
       layout keys other than Escape and layout leave paths bail. */
    bool modal_dialog_open{};
    /* The dialogue session runs as queued script lines without the
       interactive panel. */
    bool scripted_dialogue{};
    int trade_quantity{};
    int selected_trade_row = -1; /* -1 for none */
    /* How many times the transcript layout has opened this session; the
       first open queues the greeting lines. */
    int transcript_open_count{};
    /* The "PC Items"/"Party Items" pool toggle of the trade layouts. */
    bool trade_pc_items = true;
    /* The main text box is collapsed to a single line while the dialogue is
       fresh; cleared when the next transcript line scrolls. */
    bool text_box_collapsed{};
    bool dialogue_panel_hidden{};
    int last_notice_npc_kind{};
    // The script notice staged for DispatchPendingNpcScriptNotice, while queued.
    W8PendingNotice pending_notice{};
    bool pending_notice_queued = false;
};
