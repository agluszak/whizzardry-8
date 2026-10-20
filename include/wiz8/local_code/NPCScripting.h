#pragma once

#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/message_box.h"
#include "wiz8/npc_script_file.h"
struct W8NpcState;
struct W8NpcScriptFile;
struct W8NpcQuoteEntry;
struct W8NpcScriptQuote;
struct W8ItemInstance;
struct W8Character;
struct W8MonsterGroup;
struct W8WorldItem;
class Monster;
class W8DialogBase;
struct ScriptContext;
struct GameStatus;

/* Whether the conversation waits on the player: a queued options, keyword or
   price-check entry is pending, or its reply dialog is open. */
enum class W8NpcReplyState : unsigned char { none, pending, dialog_open };

/* An open price-check dialog: accepting spends price and runs quote_index,
   telling it to npc as a fact unless the entry was an always-price-check. */
struct W8NpcPriceCheck {
    W8NpcState* npc;
    int quote_index;
    int price;
    bool tells_fact;
};

/* Owns conversation progress: the speaker and its quote cursor, the queued
   script work, and whether the player owes a reply. Presentation, screens and
   game systems report what happened through the transitions below; only this
   class decides which script step follows. */
class W8NpcDialogue {
public:
    W8NpcState* speaker() const { return session_.npc; }
    int current_quote() const { return session_.current_quote_index; }
    W8NpcReplyState reply() const { return reply_; }
    const std::optional<W8NpcPriceCheck>& price_check() const { return price_check_; }
    bool has_queued_actions() const { return !queue_.empty(); }
    /* A scripted scene holds the queue until it ends. */
    bool is_held_by_scene() const { return held_by_scene_; }

    /* Make `npc` the speaker. `suspend_current` keeps the current
       conversation, which resumes once the queue drains. */
    void Begin(W8NpcState* npc, bool suspend_current);
    /* One frame: unless a quote is on screen, a scene holds the queue or a
       reply dialog is open, run the next queued action; once the queue
       drains, resume a suspended conversation. */
    void Advance(const ScriptContext& context);
    /* Run script quote `quote` of the speaker now: apply its entries on the
       last subquote and present the current subquote. */
    void Speak(const ScriptContext& context, int quote, bool force_npc_voice);
    /* Abandon the response Speak is assembling (a fact ended the conversation). */
    void Cancel();
    /* The player's answer to the pending reply. */
    void Answer(const ScriptContext& context, std::string_view text);
    /* The current quote or portrait message has left the screen. */
    void SpeechFinished();

    void QueueQuote(int quote, bool mark_pending, bool prepend, bool suppress_entries);
    void Queue(W8NpcAction action);
    /* Queue a portrait message; `notice` posts when its bubble is dismissed. */
    void QueuePortraitMessage(std::string text, W8QuoteNotice notice = {});

    void ReplyDialogOpened(std::optional<W8NpcPriceCheck> price_check);
    void ReplyDialogClosed();
    /* Drop a pending reply and price check without answering. */
    void AbandonReply();
    void HoldForScene();
    void ReleaseScene();

private:
    /* A conversation position. Begin can suspend one and Advance resumes it. */
    struct Session {
        W8NpcState* npc;
        int current_quote_index;
        /* The last quote presentation finished; Speak continues its
           subquotes instead of restarting. */
        int finished_quote_index;
        /* Which subquote of the current quote is next. */
        unsigned char subquote_index;
    };

    void Push(W8NpcAction action, bool prepend);
    void QueueQuoteEntry(int quote, int entry, int continuation_quote, bool prepend);
    void RunNextAction(const ScriptContext& context);
    void RunQuoteEntry(const ScriptContext& context, W8NpcQuoteEntry* entry,
                       int continuation_quote);
    int SelectResponse(const ScriptContext& context, W8NpcQuoteEntry* entry, int quote,
                       int entry_index);
    int FindReplyQuote(std::string_view text) const;
    void RunDeclineActions(const ScriptContext& context, int quote);

    Session session_{};
    std::optional<Session> suspended_;
    W8NpcReplyState reply_ = W8NpcReplyState::none;
    std::optional<W8NpcPriceCheck> price_check_;
    std::list<W8MessageBoxLine> queue_;
    /* Quotes queued with mark_pending since the queue last ran dry. */
    std::vector<int> pending_quotes_;
    bool queue_idle_ = false;
    bool cancelled_ = false;
    bool held_by_scene_ = false;
};

std::string GetNpcScriptRegionName(int region);
void StripNpcKeywordPunctuation(std::string& text);
void TryFinishNpcVoicePlayback(const GameStatus& status, bool force);
int FindNpcScriptQuoteByKeyword(std::string_view keyword, short* entry_index,
                                short* sub_entry_index);
/* What presenting a subquote asks of the conversation: script sentinels
   UNKNOWN and CLASSIFIED stand for the NPC's stock "don't know" and
   "won't say" quotes. */
enum class W8NpcSubquoteOutcome : unsigned char { presented, unknown, classified };
/* Present one subquote of the speaker's current quote (npc_dialogue_view.cpp). */
W8NpcSubquoteOutcome SpeakNpcSubquote(const ScriptContext& context, W8NpcScriptQuote* quote,
                                      unsigned char subquote_index, bool notice_only,
                                      bool force_npc_voice);
/* Add a keyword a quote entry of `kind` teaches to the dialogue keyword lists. */
void ApplyScriptedDialogueKeyword(const ScriptContext& context, W8NpcQuoteEntryKind kind,
                                  const char* text);
/* The first argument is a pointer into a character or party item
   slot - the slot whose address matches is the one removed. */
void RemoveNpcScriptItem(const ScriptContext& context, W8ItemInstance* item, int match_item_id,
                         int item_id);
/* Find the script quote matching an item and whether the NPC consumes it. */
struct W8NpcScriptItemQuote {
    int quote_index;
    bool consumes_item;
};
std::optional<W8NpcScriptItemQuote> FindNpcScriptItemQuote(int item_id);
void UpdateNpcDialogueVoiceAndCursor(const ScriptContext& context);
void SetNpcScriptEventActive(unsigned char value);
/* Resolves NPC-name, named-person, and region keywords to a quote
   id; returns -1 when nothing matches. */
int FindNpcNameOrPlaceQuote(W8NpcState* npc, std::string_view text);
void RestoreCurrentNpcQuoteBubble(void);
/* Show the NPC quote bubble for the formatted line; a nonzero
   second argument also plays the startup jingle. */
void DisplayNpcQuote(std::string_view text, bool play_sound);
/* Advance the dialogue NPC's refusal state - each stage queues a
   different quote until the third, which stays queued. */
void QueueDialogueNpcRefusal(void);
/* The quote audit: every script's quotes through the notice box, with a report of the
   long ones. */
void AuditNpcScriptQuotes(GameStatus& status);
/* The NPC-side rebinding pass; reloads the NPC's .nsf script
   file and rebuilds its runtime bindings. */
void ReloadNpcScriptResources(W8NpcState* npc, Monster* monster);
