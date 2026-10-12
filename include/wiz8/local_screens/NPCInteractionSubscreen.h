#include <cstdint>
#include "Font.h"
#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <string_view>
#include <string>

void SyncNpcDialogueTranscriptScrollButtons();
class ScreenRegionSet;
extern ScreenRegionSet g_npc_dialogue_transcript_panel_regions;
extern ScreenRegionSet g_npc_dialogue_trade_panel_regions;
extern ScreenRegionSet g_npc_dialogue_options_panel_regions;
extern ScreenRegionSet g_npc_quote_bubble_regions;
enum NpcDialogueTranscriptRegionOffset {
    kNpcDialogueMiscButtonRegionOffset = 11,
};
enum NpcDialogueTradeRegionOffset {
    kNpcDialogueText35RegionOffset = 11,
};
void ShowNpcQuoteBubbleRegion(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2);
void HideNpcQuoteBubbleRegion();

#include "input.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/message_box.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/xstatus.h"
#include "wiz8/dialog_code/PortraitQuote.h"

namespace wiz8 { class File; }

/* Usability filters and the exclusive equipment categories in the trade UI.
   The category bits use the same positions as W8ItemEquipSlotGroup. */
enum W8NpcTradeFilter {
    W8_NPC_TRADE_USABLE_BY_CHARACTER = 0x01,
    W8_NPC_TRADE_HAND = 0x04,
    W8_NPC_TRADE_BODY = 0x08,
    W8_NPC_TRADE_ACCESSORY = 0x10,
    W8_NPC_TRADE_OTHER = 0x20,
    W8_NPC_TRADE_CATEGORY_MASK = 0x3c,
    W8_NPC_TRADE_USABLE_BY_PARTY = 0x40
};

class W8DialogBase;
class W8Widget;
struct W8ControlsRect;
struct W8NpcQuoteEntry;
struct W8NpcScriptQuote;
struct W8NpcState;
struct W8Region;

/* The panel hosting the six option buttons. SetEnabled keeps them inactive
   unless the main-text-box dialogue layout is up, and Redraw substitutes
   m_main_text_box_image for m_backgroundImage in that layout. */
// VTABLE: WIZ8 0x005ee9f0
class W8NpcDialogueOptionsPanel : public Controls {
public:
    W8NpcDialogueOptionsPanel(std::int32_t left, std::int32_t top, std::int32_t new_right, std::int32_t new_bottom,
                              std::optional<CatalogArtwork> background, std::int32_t background_image)
        : Controls(left, top, new_right, new_bottom, std::move(background), background_image)
    {
        m_main_text_box_image = 0x11;
    }
    virtual void SetEnabled(bool enable) override;
    virtual void Redraw() override;

    std::int32_t m_main_text_box_image;
};

/* The typed-dialogue panel. Enabling it starts text-input scheme 1 and
   installs the input field; Redraw draws the input frame one row lower while
   where_is_query is raised. */
// VTABLE: WIZ8 0x005ee9e4
class W8NpcTypedDialoguePanel : public Controls {
public:
    W8NpcTypedDialoguePanel(std::int32_t left, std::int32_t top, std::int32_t new_right, std::int32_t new_bottom,
                            std::optional<CatalogArtwork> background, std::int32_t background_image)
        : Controls(left, top, new_right, new_bottom, std::move(background), background_image)
    {
    }
    virtual void SetEnabled(bool enable) override;
    virtual void Redraw() override;
};

struct W8PendingNoticeLine {
    std::string text;
    std::int32_t npc_kind;
};

/* An NPC script notice waiting for DispatchPendingNpcScriptNotice. */
struct W8PendingNotice {
    W8NpcState* npc;
    W8ItemInstance item;
    std::int32_t line;
    bool flag;
    std::uint8_t force;
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

/* Transcript keyword categories stored in
   W8NpcInteractionState::dialogue_category_filter and on each
   W8DialogTextEntry::m_category. The five filter buttons spell them
   "Items", "People", "Places", "Misc" and "All". A keyword is classified by
   scanning the item records, the NPC records plus the
   named-monster table and the fixed place-name string table, in that
   order. -1 shows every category. */
enum W8DialogueCategory {
    W8_DIALOGUE_CATEGORY_ALL = -1,
    W8_DIALOGUE_CATEGORY_ITEMS = 0,
    W8_DIALOGUE_CATEGORY_PEOPLE = 1,
    W8_DIALOGUE_CATEGORY_PLACES = 2,
    W8_DIALOGUE_CATEGORY_MISC = 3
};

/* One saved transcript keyword. */
struct W8DialogueTranscriptRecord {
    std::string text;
    std::int8_t category;
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
    std::int16_t value_000; /* cleared while the dialogue opens */
    std::uint8_t unknown_002[0xee];
    W8MainUiMode saved_main_ui_mode; /* the pre-dialogue display mode */
    /* The dialogue's leading speaker: the occupied row whose character leads
       skill 0x16. */
    std::int32_t dialogue_speaker;
    std::int32_t target_location_id;
    W8NpcDialogueLayout dialogue_layout;
    W8NpcTradeMode trade_mode;
    /* The layout the current one replaced; back-out paths reopen it. */
    W8NpcDialogueLayout previous_dialogue_layout;
    W8ItemInstance* trade_item;
    W8Widget* dialogue_controls[39]; /* indexed by region callback id */
    Controls* dialogue_panels[7];
    /* The cursor position of the last mouse event the region handler acted
       on; repeat events at the same point are dropped. */
    std::int32_t last_mouse_x;
    std::int32_t last_mouse_y;
    /* The text-box line the mouse/wheel handlers last selected, -1 for none. */
    std::int32_t hovered_text_line;
    std::int32_t trade_filter; /* W8NpcTradeFilter bits */
    W8NpcState* dialogue_npc;
    std::int8_t dialogue_category_filter; /* W8DialogueCategory */
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
    std::int16_t quote_x;
    std::int16_t quote_y;
    std::int16_t quote_width;
    std::int16_t quote_height;
    bool quote_visible;
    std::vector<W8PendingNoticeLine*> pending_notice_lines;
    /* The whole dialogue UI is parked (right-click hide) and the world input
       path is live until it is shown again. */
    std::uint8_t dialogue_hidden;
    /* The option layout was reached through the Exit/farewell path, so backing
       out of it reopens the topic menu instead of the transcript. */
    bool reopen_topics;
    std::int32_t trade_gold; /* gold put on the trade table */
    /* g_settings.main_ui_mode saved while the NPC dialogue is suppressed. */
    W8MainUiMode saved_mode;
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
    float saved_camera_yaw;
    W8QuoteNotice quote_notice;
    /* A modal W8NpcDialog is up over the dialogue; transcript word clicks,
       layout keys other than Escape and layout leave paths bail. */
    bool modal_dialog_open;
    bool farewell_queued; /* a refusal/farewell line was queued for the exit path */
    /* The dialogue session runs as queued script lines without the
       interactive panel. */
    bool scripted_dialogue;
    std::int32_t trade_quantity;
    std::int32_t selected_trade_row; /* -1 for none */
    /* How many times the transcript layout has opened this session; the
       first open queues the greeting lines. */
    std::int32_t transcript_open_count;
    /* The "PC Items"/"Party Items" pool toggle of the trade layouts. */
    bool trade_pc_items;
    /* The main text box is collapsed to a single line while the dialogue is
       fresh; cleared when the next transcript line scrolls. */
    bool text_box_collapsed;
    bool dialogue_panel_hidden;
    std::int32_t last_notice_npc_kind;
};

extern W8NpcInteractionState* g_npc_interaction_state;

void ForwardNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, std::int32_t line, bool suppress);
void QueueNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, std::int32_t line, bool suppress,
                          std::uint8_t arg);
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
void AddDialogueTranscriptKeyword(std::string_view name, std::int8_t category);
void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              std::int32_t quote_id, std::uint32_t font_palette);
void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              std::int32_t quote_id, std::uint32_t font_palette,
                              W8QuoteNotice notice, std::int32_t npc_kind);
void DrawNpcQuoteBubble(void);
void LookAtDialogueNpc(void);
void CloseNpcDialogueLayout(void);
void OpenNpcDialogueTranscriptLayout(void);
void DispatchPendingNpcScriptNotice(void);
bool CanOpenNpcDialogue(void);
bool IsNpcDialogueTextInputActive(void);
bool IsNpcDialogueTextBoxActive(void);
bool SetNpcDialoguePanelVisible(bool value);
bool ProcessPendingEvent(void);
void SyncDialogueNpcStateAndMarkPending(void);
/* A script notice is staged in g_pending_notice */
extern bool g_pending_notice_queued;
void SyncNpcServiceButtons(std::int32_t party_slot);
/* Forward mouse events to W8NpcInteractionState control slots indexed by
   callback_id in dialogue_controls (id 0x27 is ignored). */
bool MainScreenControlRegionEvent(const InputEvent* event, struct W8Region* region);
void SwitchNpcDialogueLayout(W8NpcDialogueLayout layout);
void BeginNpcDialogue(W8NpcState* npc, W8ItemInstance* item, std::int32_t quote, std::uint8_t flags,
                      std::uint8_t force);
void SelectNpcDialogueSpeaker(W8NpcState* npc, std::int32_t flags);
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
void OpenNpcDialog(W8NpcQuoteEntry* request, std::int32_t aux_data);
/* Learn one keyword into the dialogue transcript. category -1
   auto-classifies the text against items, NPC/named-monster names and the
   place-name table; a nonzero play_chime rings the keyword chime. */
void AddNpcDialogueKeyword(std::string text, std::int8_t category, std::int32_t play_chime);
void ClearNpcDialogueTranscript(void);
void CloseNpcDialogueForCamp(void);
void UpdateNpcDialogueSubMode(void);
/* Restate the five transcript category buttons so only the
   active dialogue_category_filter's button shows its secondary state. */
void SyncDialogueCategoryButtons(void);
void EndNpcDialogueSession(bool skip_exit_actions);

// FUNCTION: WIZ8 0x00576b80
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
bool IsPortraitObscuredByNpcDialogue(std::uint32_t party_slot);
void RecordLevelEntryDialogueState(void);
std::uint8_t IsNpcDialogueCursorActive(void);
/* Forward a portrait pick into an active NPC dialogue. */
void TryNpcDialoguePickpocket(std::int32_t party_slot);
std::string ShortenTextToWidth(std::string_view text, std::uint32_t width, Font& font);
bool NpcQuoteBubbleRegionEvent(const InputEvent* event, W8Region* region);
void SetDialogueFieldKeyword(std::string keyword, bool append);
void ActivateNpcDialoguePanels(bool active);
bool HasNpcDialogueDirtyPanels(void);
bool NpcDialogueTextBoxRegionEvent(const InputEvent* event, W8Region* region);
void NpcDialogueTextBoxWheelAt(std::int16_t x, std::uint16_t y, bool flag);
/* True when an NPC quote/portrait session is active: finish voice playback and
   report that the click was consumed. */
bool FinishNpcVoiceIfSessionActive(void);
void UpdateNpcTradeSelection(std::int32_t index, std::int32_t, std::int32_t);
bool NpcTradeItemAllowed(W8ItemInstance* item);
void EnableNpcTradeFilterButtons(void);
void HandleNpcDialogueKeyEvent(const InputEvent* event);
std::uint8_t LoadNpcDialogueTranscript(wiz8::File& file);
std::uint8_t SaveNpcDialogueTranscript(wiz8::File& file);
void HandleNpcDialogueItemChoice(void);
void RefreshNpcTradePartyGold(void);
void RefreshNpcTradePrice(void);
