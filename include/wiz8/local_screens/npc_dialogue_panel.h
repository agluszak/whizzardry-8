#pragma once

#include <bitset>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_items.h"
#include "wiz8/npc_speech.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/ui_layer.h"

struct MainGameScreen;
struct InputEvent;
struct NpcDialogueUi;
struct ScriptContext;
struct W8NpcScriptQuote;
enum class W8NpcSubquoteOutcome : unsigned char;

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

/* W8NpcDialoguePanel::trade_mode - the active tab of the option/trade
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

// An active quote may have no drawable bubble if both layout attempts fail.
// Its input and dismissal payload still share the same lifetime.
struct W8ActiveNpcQuote {
    std::optional<QuoteBubble> bubble;
    SDL_Point origin{};
    W8QuoteNotice notice{};
    UiLayer input{UiLayerOrder::NpcQuoteBubble};

    SDL_Rect bounds() const
    {
        return {origin.x, origin.y, bubble ? bubble->width() : 0, bubble ? bubble->height() : 0};
    }
};

/* The NPC dialogue's interface on the main game: the layouts and their widgets, the trade
   table, the transcript view, the quote bubble and the camera turned toward the speaker.
   The main game owns it (`MainGameScreen::dialogue`); what the conversation itself keeps
   is the game's (`W8NpcInteractionState`). It presents the conversation's quotes,
   portrait messages and voice for its screen's game. */
struct W8NpcDialoguePanel {
    // Out of line, where the dialogue's widgets are a complete type.
    explicit W8NpcDialoguePanel(MainGameScreen& screen);
    ~W8NpcDialoguePanel();
    W8NpcDialoguePanel(const W8NpcDialoguePanel&) = delete;
    W8NpcDialoguePanel& operator=(const W8NpcDialoguePanel&) = delete;

    void handleKeyEvent(const InputEvent* event);
    bool showingQuote() const;
    // Explicit screen discard abandons continuation; suspension retains it.
    void discardSpeech();
    // Finish a message or quote whose time is up, and move the speaker's mouth.
    void updateVoice();
    /* Speak one subquote of the speaker's current quote; `notice_only` posts it to the
       text box instead. */
    W8NpcSubquoteOutcome speakSubquote(const W8NpcScriptQuote* quote, unsigned char subquote_index,
                                       bool notice_only, bool force_npc_voice);
    /* Finish the voice on request. Suppressed while the level-4 gate is up; without
       `force`, ignored within 500 ms of the voice starting. */
    void tryFinishVoice(bool force);
    void showPortraitMessage(std::string_view text, W8QuoteNotice notice = {});
    /* Raise the quote bubble over `text`, with the start-game click when
       `play_sound` is set. */
    void displayQuote(std::string_view text, bool play_sound);
    /* Repost the speaker's current quote after a modal sub-dialog closes, or clear the
       bubble when the speaker or its script went away. */
    void restoreQuoteBubble();
    /* Show the quote bubble over `text`, or dismiss it and deliver what it held back:
       the notice it carried and, at the end of a quote, the pending notice lines. */
    void setQuoteBubbleVisible(bool visible, std::string_view text, const W8NpcScriptQuote* quote,
                               int quote_id, unsigned int font_palette, W8QuoteNotice notice = {},
                               int npc_kind = -1);

    W8NpcSpeech speech;
    std::optional<std::uint64_t> utterance_identity;
    W8NpcTradeMode trade_mode{};
    /* The layout the current one replaced; back-out paths reopen it. */
    W8NpcDialogueLayout previous_dialogue_layout{};
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
    /* Raised while the "Where Is" submit path runs, so the typed text is
       wrapped in the "Where is %s" template instead of a plain keyword lookup. */
    bool where_is_query{};
    std::optional<W8ActiveNpcQuote> active_quote;
    /* The option layout was reached through the Exit/farewell path, so backing
       out of it reopens the topic menu instead of the transcript. */
    bool reopen_topics{};
    int trade_gold{}; /* gold put on the trade table */
    /* The next UpdateNpcDialogueSubMode must reapply the trade_pc_items
       toggle state. */
    bool pending_trade_toggle{};
    /* A layout staged for reopen; the frame update closes the current layout
       and opens this one, then clears it. */
    W8NpcDialogueLayout pending_layout{};
    /* LookAtDialogueNpc aimed the camera at the NPC, so the close restores
       the saved camera angles. */
    bool camera_redirected{};
    float saved_camera_pitch{};
    int trade_quantity{};
    /* How many times the transcript layout has opened this session; the
       first open queues the greeting lines. */
    int transcript_open_count{};
    /* The "PC Items"/"Party Items" pool toggle of the trade layouts. */
    bool trade_pc_items = true;
    /* The main text box is collapsed to a single line while the dialogue is
       fresh; cleared when the next transcript line scrolls. */
    bool text_box_collapsed{};

    // Request a dialogue panel's repaint.
    void requestRepaint(int panel);
    /* Before the draw, add the panels whose transcript or action text changed to
       `panels`; the draw then paints the requested ones. */
    void updatePanels(std::bitset<7>& panels);
    void paintPanels(std::bitset<7> panels) const;
    // Show and expand the transcript panel, without repainting the layout.
    void showTranscript();
    /* A repainted portrait band covers the open transcript: show it again while
       the transcript layout is up, unhidden and not waiting on a reply. */
    void keepTranscriptOverPortraits();

private:
    void paintPanel(int index, SDL_Surface& target, bool force) const;
    const W8NpcDialogue::Utterance* utterance() const;
    void finishPreviousUtterance();
    void finishVoice(std::uint64_t identity, bool resume_script);
    MainGameScreen& screen_;
};
