#pragma once
struct W8Chunk;
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/canvas/Font.h"

void SyncNpcDialogueTranscriptScrollButtons();
void SetNpcDialogueInputEnabled(int group, bool enabled);
void ShowNpcQuoteBubbleRegion(int x1, int y1, int x2, int y2);
void HideNpcQuoteBubbleRegion();

#include <array>
#include <functional>

#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_items.h"
#include "wiz8/platform/input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

namespace wiz8 {
class File;
}

/* Usability filters and the exclusive equipment categories in the trade UI.
   The category bits use the same positions as W8ItemEquipSlotGroup. */
enum W8NpcTradeFilter : int {
    W8_NPC_TRADE_FILTER_NONE = 0,
    W8_NPC_TRADE_USABLE_BY_CHARACTER = 0x01,
    W8_NPC_TRADE_HAND = 0x04,
    W8_NPC_TRADE_BODY = 0x08,
    W8_NPC_TRADE_ACCESSORY = 0x10,
    W8_NPC_TRADE_OTHER = 0x20,
    W8_NPC_TRADE_CATEGORY_MASK = 0x3c,
    W8_NPC_TRADE_USABLE_BY_PARTY = 0x40
};

class W8DialogBase;
struct W8ControlsRect;
struct W8NpcQuoteEntry;
struct W8NpcScriptQuote;
struct W8NpcState;

class W8NpcDialogueTextController;
struct SpriteSheet;

// Fixed NPC dialogue composition: seven painted areas and their local actions.
// No visual parents, inherited hooks or arbitrary widget containers.
struct NpcDialogueUi {
    ~NpcDialogueUi();
    UiLayer transcript_input{UiLayerOrder::NpcTranscript};
    UiLayer trade_input{UiLayerOrder::NpcTrade};
    UiLayer options_input{UiLayerOrder::NpcOptions};
    struct Panel {
        SDL_Point origin;
        int background;
        bool active = false, dirty = true;
    };
    std::array<Panel, 7> panels{{{{0x17, 0x166}, 0},
                                 {{0xa4, 0x166}, 1},
                                 {{0x1dc, 0x11b}, 2},
                                 {{0x1dc, 0x12f}, 5},
                                 {{0x1dc, 0x166}, 7},
                                 {{0x1dc, 0x166}, 6},
                                 {{0x1dc, 0x166}, 13}}};
    struct Action {
        int panel = -1;
        SDL_Rect bounds{};
        std::shared_ptr<const SpriteSheet> sprites;
        int image_offset = 0;
        std::array<int, 5> frames{-1, -1, -1, -1, -1};
        HitTarget* target = nullptr;
        W8TextBuffer text;
        ButtonInteraction interaction;
        ButtonPolicy policy{.input_rule = ButtonInputRule::PrimaryPressedState};
        std::function<void()> primary, secondary, down;
        bool visible = true, enabled = true, dirty = true;
    };
    std::array<Action, 39> actions;
    std::shared_ptr<const SpriteSheet> item_preview;
    std::unique_ptr<W8NpcDialogueTextController> transcript;
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
extern W8PendingNotice g_pending_notice;

/* W8NpcInteractionState::trade_mode - the active tab of the option/trade
   layouts. The StringData.DAT captions the tabs and headers load are
   "Give", "Sell", "Buy" and "Shoplift": mode 2 lists the party's items
   with the purse as row zero, mode 3 the character/party sell pools, and
   modes 4/5 the NPC's own inventory. 0 while no tab is selected. */
enum W8NpcTradeMode {
    W8_NPC_TRADE_NONE = 0,
    W8_NPC_TRADE_GIVE = 2,
    W8_NPC_TRADE_SELL = 3,
    W8_NPC_TRADE_BUY = 4,
    W8_NPC_TRADE_SHOPLIFT = 5
};

/* One saved transcript keyword. */
struct W8DialogueTranscriptRecord {
    std::string text;
    W8DialogueCategory category;
};

enum W8NpcDialogueControlSlot {
    W8_NPC_CONTROL_NPC_NAME = 0,
    W8_NPC_CONTROL_TEXT_1 = 1,
    W8_NPC_CONTROL_TEXT_2 = 2,
    W8_NPC_CONTROL_TEXT_3 = 3,
    W8_NPC_CONTROL_TEXT_4 = 4,
    W8_NPC_CONTROL_TEXT_5 = 5,
    W8_NPC_CONTROL_TEXT_6 = 6,
    W8_NPC_CONTROL_TEXT_7 = 7,
    W8_NPC_CONTROL_WIDGET = 8,
    W8_NPC_CONTROL_SCROLL = 9,
    W8_NPC_CONTROL_SCROLL_UP_BUTTON = 10,
    W8_NPC_CONTROL_SCROLL_DOWN_BUTTON = 11,
    W8_NPC_CONTROL_TEXT_12 = 12,
    W8_NPC_CONTROL_TEXT_13 = 13,
    W8_NPC_CONTROL_SORT_BUTTON = 15,
    W8_NPC_CONTROL_PEOPLE_BUTTON = 17,
    W8_NPC_CONTROL_PLACES_BUTTON = 18,
    W8_NPC_CONTROL_ITEMS_BUTTON = 19,
    W8_NPC_CONTROL_MISC_BUTTON = 20,
    W8_NPC_CONTROL_ALL_BUTTON = 21,
    W8_NPC_CONTROL_TEXT_22 = 22,
    W8_NPC_CONTROL_TEXT_23 = 23,
    W8_NPC_CONTROL_TEXT_24 = 24,
    W8_NPC_CONTROL_OPTION_BUTTONS = 25,
    W8_NPC_CONTROL_TEXT_31 = 31,
    W8_NPC_CONTROL_TEXT_33 = 33,
    W8_NPC_CONTROL_TEXT_34 = 34,
    W8_NPC_CONTROL_TEXT_35 = 35,
    W8_NPC_CONTROL_TEXT_36 = 36,
    W8_NPC_CONTROL_TEXT_37 = 37,
    W8_NPC_CONTROL_TEXT_38 = 38,
};

struct W8NpcInteractionState {
    short value_000; /* cleared while the dialogue opens */
    unsigned char unknown_002[0xee];
    /* The dialogue's leading speaker: the occupied row whose character leads
       skill 0x16. */
    int dialogue_speaker;
    int target_location_id;
    W8NpcDialogueLayout dialogue_layout;
    W8NpcTradeMode trade_mode;
    /* The layout the current one replaced; back-out paths reopen it. */
    W8NpcDialogueLayout previous_dialogue_layout;
    // Borrowed preview only; confirmation resolves trade_selection.
    W8ItemInstance* trade_item;
    std::vector<W8TradeSelection> trade_rows;
    std::optional<W8TradeSelection> trade_selection;
    std::unique_ptr<NpcDialogueUi> ui;
    /* The cursor position of the last mouse event the region handler acted
       on; repeat events at the same point are dropped. */
    int last_mouse_x;
    int last_mouse_y;
    /* The text-box line the mouse/wheel handlers last selected, -1 for none. */
    int hovered_text_line;
    W8NpcTradeFilter trade_filter = W8_NPC_TRADE_FILTER_NONE;
    W8NpcState* dialogue_npc;
    W8DialogueCategory dialogue_category_filter = W8_DIALOGUE_CATEGORY_ALL;
    /* Raised while the "Where Is" submit path runs, so the typed text is
       wrapped in the "Where is %s" template instead of a plain keyword lookup. */
    bool where_is_query;
    std::vector<std::unique_ptr<W8DialogueTranscriptRecord>> dialogue_transcript;
    bool transcript_sorted;
    /* The item a pending NPC notice carries; copied from g_pending_notice
       when the dialogue opens. */
    W8ItemInstance pending_item;
    /* pending_item came in through the cursor item; dialogue close returns it
       to the hand and the consume path drains its stack. */
    bool held_item_pending;
    std::optional<QuoteBubble> quote_bubble;
    short quote_x;
    short quote_y;
    short quote_width;
    short quote_height;
    bool quote_visible;
    std::vector<W8PendingNoticeLine> pending_notice_lines;
    /* The whole dialogue UI is parked (right-click hide) and the world input
       path is live until it is shown again. */
    unsigned char dialogue_hidden;
    /* The option layout was reached through the Exit/farewell path, so backing
       out of it reopens the topic menu instead of the transcript. */
    bool reopen_topics;
    int trade_gold; /* gold put on the trade table */
    /* g_settings.main_ui_mode saved while the NPC dialogue is suppressed. */
    /* The next UpdateNpcDialogueSubMode must reapply the trade_pc_items
       toggle state. */
    bool pending_trade_toggle;
    /* A layout staged for reopen; the frame update closes the current layout
       and opens this one, then clears it. */
    W8NpcDialogueLayout pending_layout;
    /* Cleared when the dialogue ends on a refusal/abrupt dismissal, in which
       case the close path queues a delayed party reaction event. */
    bool suppress_parting_reaction;
    /* LookAtDialogueNpc aimed the camera at the NPC, so the close restores
       the saved camera angles. */
    bool camera_redirected;
    float saved_camera_pitch;
    W8QuoteNotice quote_notice;
    /* A modal W8NpcDialog is up over the dialogue; transcript word clicks,
       layout keys other than Escape and layout leave paths bail. */
    bool modal_dialog_open;
    /* The dialogue session runs as queued script lines without the
       interactive panel. */
    bool scripted_dialogue;
    int trade_quantity;
    int selected_trade_row; /* -1 for none */
    /* How many times the transcript layout has opened this session; the
       first open queues the greeting lines. */
    int transcript_open_count;
    /* The "PC Items"/"Party Items" pool toggle of the trade layouts. */
    bool trade_pc_items;
    /* The main text box is collapsed to a single line while the dialogue is
       fresh; cleared when the next transcript line scrolls. */
    bool text_box_collapsed;
    bool dialogue_panel_hidden;
    int last_notice_npc_kind;
};

extern W8NpcInteractionState* g_npc_interaction_state;

void ForwardNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress);
void QueueNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress,
                          unsigned char arg);
void FlushPendingNoticeLines(void);
/* Zero W8NpcInteractionState, write its reset values, and reload the
   keyword lists through the loader below. */
void ResetMainScreenStateBlock(void);
/* The dialogue keyword tables, one file list per language;
   element zero is English_Keywords.txt and element one the translated list.
   A file list holds one line list per line, and a line list one malloc'd wide
   word per '/'-separated field. */
using KeywordFile = std::vector<std::vector<std::string>>;
extern std::vector<KeywordFile> g_keyword_lists;
/* Both files are loaded and the tables are usable. Raised once the
   second file loads and lowered whenever the tables are released. */
extern bool g_keyword_lists_loaded;
/* Replace the keyword lists with the contents of
   Data\Strings\English_Keywords.txt and Data\Strings\translated_Keywords.txt. */
void ReloadKeywordLists(void);
/* Clear the loaded keyword tables. */
void ClearKeywordLists(void);
/* Copy the next '/'-terminated field out of a keyword line into
   the caller's buffer and return the cursor past it, or null at the end. */

void SyncDialogueNpcState(void);
/* Store a transcript keyword, inferring its category when category is -1. */
void AddDialogueTranscriptKeyword(std::string_view name, W8DialogueCategory category);
void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              int quote_id, unsigned int font_palette);
void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              int quote_id, unsigned int font_palette, W8QuoteNotice notice,
                              int npc_kind);
void DrawNpcQuoteBubble(void);
void LookAtDialogueNpc(void);
void CloseNpcDialogueLayout(void);
void OpenNpcDialogueTranscriptLayout(void);
void DispatchPendingNpcScriptNotice(void);
bool IsNpcDialogueTextInputActive(void);
bool IsNpcDialogueTextBoxActive(void);
unsigned char SetNpcDialoguePanelVisible(unsigned char value);
bool ProcessPendingEvent(void);
void SyncDialogueNpcStateAndMarkPending(void);
/* A script notice is staged in g_pending_notice */
extern bool g_pending_notice_queued;
void SyncNpcServiceButtons(int party_slot);
void SwitchNpcDialogueLayout(W8NpcDialogueLayout layout);
void BeginNpcDialogue(W8NpcState* npc, W8ItemInstance* item, int quote, unsigned char flags,
                      unsigned char force);
void SelectNpcDialogueSpeaker(W8NpcState* npc, int flags);
void InvalidateMainGameActionPanelRect(const W8ControlsRect* rect);
void CloseNpcDialogueOptionLayout(void);
void ShowNpcDialogueTopicMenu(void);
std::string TranslateDialogueKeyword(std::string_view source);
void ResetNpcDialogueItemEditor(void);
/* While NPC script deferral holds character events, drain Escape / click so
   the open dialogue layout can dismiss without the normal input path. */
void DrainNpcDialogueDeferralInput(void);
/* When world_cursor_gate is set, discard queued input after a mouse-position hook so
   the world-cursor gate does not process stale events. */
void FlushInputWhileWorldCursorGate(void);
void HandleNpcDialogueInput(void);
void OpenNpcDialog(W8NpcQuoteEntry* request, int aux_data);
/* Learn one keyword into the dialogue transcript. category -1
   auto-classifies the text against items, NPC/named-monster names and the
   place-name table; a nonzero play_chime rings the keyword chime. */
void AddNpcDialogueKeyword(std::string text, W8DialogueCategory category, int play_chime);
void ClearNpcDialogueTranscript(void);
void CloseNpcDialogueForCamp(void);
void UpdateNpcDialogueSubMode(void);
/* Restate the five transcript category buttons so only the
   active dialogue_category_filter's button shows its secondary state. */
void SyncDialogueCategoryButtons(void);
void EndNpcDialogueSession(bool skip_exit_actions);

inline void CloseNpcDialogueIfActive(void)
{
    if (gXStatus.fNpcDialogueMode) {
        EndNpcDialogueSession(false);
    }
}
/* Whether an open NPC dialogue transcript covers the party slot's portrait:
   dialogue mode up, scripted_dialogue clear, the controller enabled, and its top
   edge above the slot's band. Portrait and character-update paths skip the
   covered rows through this. */
bool IsPortraitObscuredByNpcDialogue(unsigned int party_slot);
void RecordLevelEntryDialogueState(void);
unsigned char IsNpcDialogueCursorActive(void);
/* Forward a portrait pick into an active NPC dialogue. */
void TryNpcDialoguePickpocket(int party_slot);
std::string ShortenTextToWidth(std::string_view text, unsigned int width, Font& font);
void SetDialogueFieldKeyword(std::string keyword, bool append);
void ActivateNpcDialoguePanels(bool active);
bool HasNpcDialogueDirtyPanels(void);
UiResult HandleNpcTextBox(const UiPointerEvent& event);
void NpcDialogueTextBoxWheelAt(short x, unsigned short y, bool flag);
/* True when an NPC quote/portrait session is active: finish voice playback and
   report that the click was consumed. */
bool FinishNpcVoiceIfSessionActive(void);
void UpdateNpcTradeSelection(int index, int, int);
bool NpcTradeItemAllowed(W8ItemInstance* item);
void EnableNpcTradeFilterButtons(void);
void HandleNpcDialogueKeyEvent(const InputEvent* event);
unsigned char LoadNpcDialogueTranscript(W8Chunk& file);
unsigned char SaveNpcDialogueTranscript(wiz8::File& file);
void HandleNpcDialogueItemChoice(void);
void RefreshNpcTradePartyGold(void);
void RefreshNpcTradePrice(void);
