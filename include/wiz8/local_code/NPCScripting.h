#pragma once

#include "wiz8/audio_resources.h"

#include <optional>
#include <string_view>
std::string GetNpcScriptRegionName(int region);
void StripNpcKeywordPunctuation(std::string& text);

#include "wiz8/mouth_gap.h"
#include "wiz8/message_box.h"
#include <list>
#include <vector>
#include <algorithm>


struct W8NpcState;
struct W8NpcScriptFile;
struct W8NpcQuoteEntry;
struct W8NpcScriptQuote;
struct W8ItemInstance;
struct W8Character;
struct W8MonsterGroup;
struct W8WorldItem;
class W8Monster;
class W8DialogBase;

/* The conversation being spoken: its speaker, script and position. Starting
   a nested conversation can suspend it; it resumes once the queue drains. */
struct W8NpcDialogueSession {
    W8NpcState* npc;
    W8NpcScriptFile* script_file;
    int current_quote_index;
    int finished_quote_index;
    /* Which subquote of the current quote is next; reset when a new quote
       is staged or the cursor runs past subquote_count. */
    unsigned char subquote_index;
    /* A quote is being presented - voice is playing or the text/EOS
       hold is counting down. Cleared by FinishNpcVoicePlayback. */
    bool quote_active;
};

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

struct W8NpcScriptingState {
    W8NpcDialogueSession session;
    std::optional<W8NpcDialogueSession> suspended_session;
    W8NpcReplyState reply;
    std::optional<W8NpcPriceCheck> price_check;
    /* The quote's voice sound actually started; gates wiz8::sound::stop and the
       mouth-gap cleanup in FinishNpcVoicePlayback. */
    bool voice_playing;
    wiz8::sound::Playback voice_handle;
    unsigned int message_duration_ms;
    uint64_t message_started_at;
    std::list<W8MessageBoxLine> message_lines;
    std::vector<int> pending_script_values;
    W8MouthGapTrack gap_track;
    uint64_t last_tick;
    /* Latched by CancelNpcDialogue; the response loop checks it at
       entries_done and abandons the pending response. */
    bool dialogue_cancelled;
    bool portrait_message_active;
    bool scripted_scene_active;
    bool sedexus_release_pending;
    bool sedexus_capture_pending;
    bool sedexus_capture_active;
    bool stopping_voice_playback;
};

extern W8NpcScriptingState g_npc_scripting;
extern bool g_message_queue_idle;
/* Scripted portrait-pick / cutscene gate PortraitSelectRegionEvent
   and EndScriptedPortraitPick clear. */

void TryFinishNpcVoicePlayback(bool force);
int FindNpcScriptQuoteByKeyword(std::string_view keyword, short* entry_index, short* sub_entry_index);
void RunNpcScriptLine(int script_line, bool force_npc_voice);
/* Resolve the player's answer to the pending reply and clear it. */
void AnswerNpcReply(std::string_view text);
/* Speak one subquote of the current quote (NPC Scripting.cpp). */
void SpeakNpcSubquote(W8NpcScriptQuote* quote, unsigned char subquote_index, bool notice_only,
                      bool force_npc_voice);
/* Add a keyword a quote entry of `kind` teaches to the dialogue keyword lists. */
void ApplyScriptedDialogueKeyword(int kind, const char* text);
void ProcessMessageBoxQueue(void);
/* Execute a queued quote entry's deferred effect; the
   continuation quote is handed to the modal-dialog kinds (5/0x13 force -1). */
void ProcessNpcQuoteEntry(W8NpcQuoteEntry* entry, int continuation_quote);
/* Build a W8_NPC_MSG_QUOTE_ENTRY line for `entry` carrying
   `continuation_quote`; prepend != 0 inserts it at the queue front. */
void QueueNpcQuoteEntry(int quote_index, int entry_index, int continuation_quote, bool prepend);
/* The first argument is a pointer into a character or party item
   slot - the slot whose address matches is the one removed. */
void RemoveNpcScriptItem(W8ItemInstance* item, int match_item_id, int item_id);
/* Find the script quote matching an item and whether the NPC consumes it. */
struct W8NpcScriptItemQuote {
    int quote_index;
    bool consumes_item;
};
std::optional<W8NpcScriptItemQuote> FindNpcScriptItemQuote(int item_id);
void ResolveSedexusCapture(void);
void NpcScriptHenchmanArrives(W8Monster* monster);
void NpcScriptHenchmanDeparted(W8Monster* monster);
void NpcScriptSavantHackDone(W8Monster* monster);
void NpcScriptTurnToBook(void);
void NpcScriptEndgameScreen(void);
void OnNpcTravelConfirmationClosed(W8DialogBase* dialog);
void UpdateNpcDialogueVoiceAndCursor(void);
void ProcessNpcScriptingFrame(void);
void SetNpcScriptEventActive(unsigned char value);
bool IsSedexusCaptureActive(void);
void QueueNpcMessageLine(W8NpcMessageKind kind, int argument);
/* Resolves NPC-name, named-person, and region keywords to a quote
   id; returns -1 when nothing matches. */
int FindNpcNameOrPlaceQuote(W8NpcState* npc, std::string_view text);
int FindNpcReplyQuote(std::string_view text);
void RestoreCurrentNpcQuoteBubble(void);
void RunNpcQuoteDeclineActions(int quote_index);
void QueueNpcScriptLine(int quote, bool mark_pending, bool prepend, bool suppress_entries);
void BeginNpcScriptedScene(void);
void SetScriptedSceneActive(void);
void ClearScriptedSceneActive(void);
/* End a scripted portrait pick against the chosen party slot. */
void EndScriptedPortraitPick(int party_slot);
void BeginSedexusCapture(void);
void CancelNpcDialogue(void);
/* Show the NPC quote bubble for the formatted line; a nonzero
   second argument also plays the startup jingle. */
void DisplayNpcQuote(std::string_view text, bool play_sound);
/* Advance the dialogue NPC's refusal state - each stage queues a
   different quote until the third, which stays queued. */
void QueueDialogueNpcRefusal(void);
void AuditNpcScriptQuotes(void);
/* The NPC-side rebinding pass; reloads the NPC's .nsf script
   file and rebuilds its runtime bindings. */
void ReloadNpcScriptResources(W8NpcState* npc);
