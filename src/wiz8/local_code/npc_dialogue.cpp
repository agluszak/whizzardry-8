#include <fmt/format.h>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "random.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/integer_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/unicode.h"

/* The NSF dialogue interpreter: picks the next quote, runs a quote's
   immediate entries, resolves player replies and schedules continuations on
   the message queue. Presentation and speech stay in NPC Scripting.cpp and
   the dialogue screen. */

/* Evaluate a kind-3 quote entry: each sub-entry pair is a (fact, expected)
   guard that must all hold, then the entry selects the response - operand0
   directly, randomly in [operand0, operand2] when operand1 is 2, or a
   queued QUOTE_ENTRY continuation line when operand2 is 4. Returns the
   response index, -1 when a guard fails, and -2 after queueing. */
// FUNCTION: WIZ8 0x005251F0
static int SelectNpcQuoteResponse(W8NpcQuoteEntry* entry, int quote_index, int entry_index)
{
    int index;
    unsigned char expected;
    char response_count;

    index = 0;
    if (entry->operand2 == 4 && entry->operand1 != 2) {
        index = 1;
    }
    while (index < entry->sub_entry_count) {
        expected = static_cast<unsigned char>(
            entry->sub_entries[index + 1].operand) /* c-style-cast-ok: packed byte operand */;
        if (GetFact(static_cast<W8FactId>(entry->sub_entries[index].operand)) != expected) {
            return -1;
        }
        index += 2;
    }
    if (entry->operand2 == 4 && entry->operand1 != 2) {
        QueueNpcQuoteEntry(quote_index, entry_index, 0, false);
        return -2;
    }
    if (entry->operand1 == 2) {
        response_count = entry->operand2 - entry->operand0 + 1;
        return Random(response_count) + entry->operand0;
    }
    return entry->operand0;
}

/* Kind-4 entries carry the keyword strings a dialogue keyword can select;
   scan every quote for a subentry whose text
   matches, reporting the entry and subentry indices through the out-pointers. */
// FUNCTION: WIZ8 0x00525E80
int FindNpcScriptQuoteByKeyword(std::string_view keyword, short* entry_index,
                                short* sub_entry_index)
{
    W8NpcScriptFile* script;
    W8NpcScriptQuote* quote;
    W8NpcQuoteEntry* entry;
    std::string text;
    int quote_index;
    int item_index;
    int sub_index;

    script = g_npc_scripting.session.npc->script_file;
    for (quote_index = 0; quote_index < script->quote_count; quote_index++) {
        quote = &script->quotes[quote_index];
        for (item_index = 0; item_index < quote->entry_count; item_index++) {
            entry = &quote->entries[item_index];
            if (entry->kind == W8_NPC_ENTRY_KEYWORD) {
                for (sub_index = 0; sub_index < entry->sub_entry_count; sub_index++) {
                    text = entry->sub_entries[sub_index].text.c_str();
                    if (CompareTextIgnoreAsciiCase(text, keyword) == 0) {
                        if (entry_index != 0) {
                            *entry_index = static_cast<short>(item_index);
                        }
                        if (sub_entry_index != 0) {
                            *sub_entry_index = static_cast<short>(sub_index);
                        }
                        return quote_index;
                    }
                }
            }
        }
    }
    return -1;
}

/* Run one script quote line: stage it as current, and when the last subquote
   is reached first walk the quote's entries - each kind drives facts, NPC
   switches, items, gold, keyword learning or the follow-up quote. Then speak
   the current subquote and queue the continuation line. Quote indices
   'F'..'R' fall outside the scripted world-action dispatch. */
// FUNCTION: WIZ8 0x00525FA0
void RunNpcScriptLine(int script_line, bool force_npc_voice)
{
    W8NpcScriptQuote* quote;
    W8NpcQuoteEntry* entry;
    W8Monster* monster;
    W8NpcState* target;
    signed char faction;
    char response_count;
    int response;
    int entry_index;
    bool finished;

    g_npc_scripting.session.current_quote_index = script_line;
    if (script_line != g_npc_scripting.session.finished_quote_index) {
        g_npc_scripting.session.subquote_index = 0;
    }
    if (script_line < g_npc_scripting.session.npc->script_file->quote_count) {
        quote = &g_npc_scripting.session.npc->script_file->quotes[script_line];
        if (g_npc_scripting.session.subquote_index == quote->subquote_count) {
            g_npc_scripting.session.subquote_index = 0;
        }
        response = -1;
        finished = false;
        if (g_npc_scripting.session.subquote_index == quote->subquote_count - 1 &&
            quote->entry_count != 0) {
            for (entry_index = 0; entry_index < quote->entry_count; entry_index++) {
                entry = &quote->entries[entry_index];
                switch (entry->kind) {
                case W8_NPC_ENTRY_NEXT_QUOTE:
                    if (entry->operand2 == 4) {
                        if (entry->operand1 != 2) {
                            QueueNpcQuoteEntry(script_line, entry_index, 0, false);
                            response = -1;
                            finished = true;
                            break;
                        }
                    } else if (entry->operand1 != 2) {
                        response = entry->operand0;
                        finished = true;
                        break;
                    }
                    response_count = static_cast<char>(entry->operand2 - entry->operand0 + 1);
                    response = Random(response_count) + entry->operand0;
                    finished = true;
                    break;
                case W8_NPC_ENTRY_FACT_CONDITIONAL_QUOTE:
                    response = SelectNpcQuoteResponse(entry, script_line, entry_index);
                    if (response != -1) {
                        finished = true;
                        break;
                    }
                    break;
                case W8_NPC_ENTRY_OPTIONS:
                    g_npc_scripting.reply = W8NpcReplyState::pending;
                    QueueNpcQuoteEntry(script_line, entry_index, 0, false);
                    finished = true;
                    break;
                case W8_NPC_ENTRY_SET_FACT:
                    SetFact(static_cast<W8FactId>(entry->operand0), entry->operand1, false);
                    break;
                case W8_NPC_ENTRY_CLOSE_DIALOGUE:
                    QueueNpcAction(W8NpcCommand::close_dialogue);
                    break;
                case W8_NPC_ENTRY_GIVE_ITEM:
                case W8_NPC_ENTRY_REMOVE_ITEM:
                case W8_NPC_ENTRY_GIVE_GOLD:
                case W8_NPC_ENTRY_GIVE_EXPERIENCE:
                    QueueNpcQuoteEntry(script_line, entry_index, 0, false);
                    break;
                case W8_NPC_ENTRY_MONSTER_SCRIPT_LABEL:
                    monster = GetNpcMonster(g_npc_scripting.session.npc);
                    if (monster != 0 && entry->operand2 != 4 &&
                        !monster->SetScriptLabel(entry->sub_entries[0].text.c_str())) {
                        std::string script_error;
                        script_error =
                            fmt::format("Failed to load script (or failed to find {} in script)",
                                        entry->sub_entries[0].text);
                    }
                    break;
                case W8_NPC_ENTRY_NPC_WORLD_ACTION:
                    if (g_npc_scripting.session.current_quote_index < g_world_action_quote_min ||
                        g_world_action_quote_max < g_npc_scripting.session.current_quote_index) {
                        std::string action_name;
                        BeginScriptedWorldAction();
                        action_name = entry->sub_entries[0].text.c_str() + 4;
                        target = FindNpcStateByName(action_name);
                        if (target != 0) {
                            if (!target->is_grouped) {
                                ClearMainGameTargetState();
                            } else {
                                QueueNpcAction(W8NpcDismiss{static_cast<int>(target->group_index)});
                            }
                        }
                    }
                    break;
                case W8_NPC_ENTRY_CLOSE_AND_RESUME:
                    QueueNpcAction(W8NpcCommand::close_and_resume_npc);
                    finished = true;
                    break;
                case W8_NPC_ENTRY_KEYWORD_INPUT:
                    g_npc_scripting.reply = W8NpcReplyState::pending;
                    QueueNpcQuoteEntry(script_line, entry_index, 0, false);
                    finished = true;
                    break;
                case W8_NPC_ENTRY_SELF_GROUP_ACTION:
                    if (g_npc_scripting.session.current_quote_index < g_world_action_quote_min ||
                        g_world_action_quote_max < g_npc_scripting.session.current_quote_index) {
                        BeginScriptedWorldAction();
                        QueueNpcAction(W8NpcDismiss{
                            static_cast<int>(g_npc_scripting.session.npc->group_index)});
                    }
                    break;
                case W8_NPC_ENTRY_NPC_INTERACTION: {
                    std::string action_name;
                    action_name = entry->sub_entries[0].text.c_str() + 4;
                    target = FindNpcStateByName(action_name);
                    if (target != 0) {
                        ApplyNpcInteraction(target, 4, 0, 0, entry->operand0);
                    }
                } break;
                case W8_NPC_ENTRY_FACTION_CHANGE:
                    faction = FindFactionByName(entry->sub_entries[0].text.c_str());
                    if (faction != -1) {
                        ApplyFactionChange(3, 1, faction, entry->operand0);
                    }
                    break;
                case W8_NPC_ENTRY_PARTY_SPEAKER_EVENT:
                    QueueNpcAction(
                        W8NpcPartySpeakerEvent{static_cast<unsigned int>(entry->operand0)});
                    break;
                case W8_NPC_ENTRY_PERSON_KEYWORD:
                case W8_NPC_ENTRY_ITEM_KEYWORD:
                case W8_NPC_ENTRY_PLACE_KEYWORD:
                case W8_NPC_ENTRY_MISC_KEYWORD:
                case 31:
                case 32:
                case 33:
                case 34:
                    ApplyScriptedDialogueKeyword(entry->kind, entry->sub_entries[0].text.c_str());
                    break;
                }
                if (finished) {
                    break;
                }
            }
        }
        if (g_npc_scripting.dialogue_cancelled) {
            g_npc_scripting.dialogue_cancelled = false;
            return;
        }
        if (response >= 0) {
            QueueNpcScriptLine(response, false, false, false);
        }
        SpeakNpcSubquote(quote, g_npc_scripting.session.subquote_index, false, force_npc_voice);
        g_npc_scripting.session.subquote_index++;
        if (g_npc_scripting.session.current_quote_index == 0) {
            g_npc_scripting.session.npc->greeting_pending = false;
        }
        if (g_npc_scripting.session.subquote_index < quote->subquote_count) {
            QueueNpcScriptLine(g_npc_scripting.session.current_quote_index, false, true, true);
        }
    }
}

static int SessionNpcKind()
{
    const W8NpcState* npc = g_npc_scripting.session.npc;
    return npc != 0 ? npc->record->kind : -1;
}

static void QueueMessageBoxLine(W8MessageBoxLine line, bool prepend)
{
    if (!prepend) {
        g_npc_scripting.message_lines.push_back(std::move(line));
    } else {
        g_npc_scripting.message_lines.push_front(std::move(line));
    }
}

// FUNCTION: WIZ8 0x00528830
void QueueNpcScriptLine(int quote, bool mark_pending, bool prepend, bool suppress_entries)
{
    QueueMessageBoxLine({SessionNpcKind(), W8NpcQueuedQuote{quote, mark_pending, suppress_entries}},
                        prepend);
}

// FUNCTION: WIZ8 0x005289B0
void QueueNpcAction(W8NpcAction action)
{
    QueueMessageBoxLine({SessionNpcKind(), std::move(action)}, false);
}

void QueueNpcPortraitMessage(std::string text, W8QuoteNotice notice)
{
    QueueNpcAction(W8NpcQueuedNotice{std::move(text), notice});
}

/* Resolve the player's reply text to the script line it selects. The current
   quote's option/keyword entries (kinds 5 and 0x13) carry the matchable
   phrases in their sub-entries; a hit consults the quote's kind-6 reply
   entries - role 3 supplies the per-keyword answer list, role 1 the generic
   answer. No keyword match at all falls back to the role-2 entry. -1 when the
   quote has no usable reply. */
// FUNCTION: WIZ8 0x00529300
static int FindNpcReplyQuote(std::string_view text)
{
    W8NpcScriptQuote* quote;
    W8NpcQuoteEntry* entry;
    std::string sub_text;
    int quote_index;
    int index;
    int sub;

    quote_index = g_npc_scripting.session.current_quote_index;
    quote = g_npc_scripting.session.npc->script_file->quotes.data() + quote_index;
    for (index = 0; index < quote->entry_count; ++index) {
        entry = &quote->entries[index];
        if (entry->kind == W8_NPC_ENTRY_OPTIONS || entry->kind == W8_NPC_ENTRY_KEYWORD_INPUT) {
            break;
        }
    }

    if (index < quote->entry_count) {
        for (sub = 0; sub < entry->sub_entry_count; ++sub) {
            sub_text = entry->sub_entries[sub].text;
            if (CompareTextIgnoreAsciiCase(sub_text, text) != 0) {
                continue;
            }
            /* The reply matched an option: a keyword answer, else the generic
               answer. */
            for (index = 0; index < quote->entry_count; ++index) {
                entry = &quote->entries[index];
                if (entry->kind == W8_NPC_ENTRY_REPLY && entry->operand2 == 3 &&
                    entry->sub_entry_count != 0) {
                    for (sub = 0; sub < entry->sub_entry_count; ++sub) {
                        sub_text = entry->sub_entries[sub].text;
                        if (CompareTextIgnoreAsciiCase(sub_text, text) == 0) {
                            return entry->operand0;
                        }
                    }
                }
            }
            for (index = 0; index < quote->entry_count; ++index) {
                entry = &quote->entries[index];
                if (entry->kind == W8_NPC_ENTRY_REPLY && entry->operand2 == 1) {
                    return entry->operand0;
                }
            }
            return -1;
        }
    }

    for (index = 0; index < quote->entry_count; ++index) {
        entry = &quote->entries[index];
        if (entry->kind == W8_NPC_ENTRY_REPLY && entry->operand2 == 2) {
            return entry->operand0;
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x005294c0
std::string GetNpcQuoteText(W8NpcState* npc, int type)
{
    const W8NpcScriptFile* script = npc->script_file;
    if (type < 0 || type >= static_cast<int>(script->quote_count)) {
        return {};
    }
    const auto& record = script->quotes[type];
    if (record.subquotes.empty() || record.subquotes[0].empty()) {
        return {};
    }
    return record.subquotes[0];
}

/* Queue a quote-entry line carrying `continuation_quote`; `prepend` inserts it
   at the front of the queue (continuation subquotes overtake pending lines). */
// FUNCTION: WIZ8 0x00528B50
void QueueNpcQuoteEntry(int quote_index, int entry_index, int continuation_quote, bool prepend)
{
    QueueMessageBoxLine(
        {SessionNpcKind(), W8NpcQueuedEntry{quote_index, entry_index, continuation_quote}},
        prepend);
}

// FUNCTION: WIZ8 0x00529560
void CancelNpcDialogue(void)
{
    g_npc_scripting.dialogue_cancelled = true;
}

/* Run every kind-0x17 consequence entry of the given quote - the reply
   handler's decline path when the party refuses a price offer. */
// FUNCTION: WIZ8 0x00529610
static void RunNpcQuoteDeclineActions(int quote_index)
{
    W8NpcScriptQuote* quote;
    int index;

    quote = g_npc_scripting.session.npc->script_file->quotes.data() + quote_index;
    for (index = 0; index < quote->entry_count; ++index) {
        if (quote->entries[index].kind == W8_NPC_ENTRY_DECLINE_QUOTE) {
            RunNpcScriptLine(quote->entries[index].operand0, false);
        }
    }
}

/* Resolve the player's answer to the pending reply: a typed or chosen reply
   selects its script line; a price check spends the price and runs its quote,
   or runs the quote's decline entries. */
void AnswerNpcReply(std::string_view text)
{
    if (!g_npc_scripting.price_check) {
        const int line = FindNpcReplyQuote(text);
        if (line != -1) {
            QueueNpcScriptLine(line, false, false, false);
        }
    } else {
        const W8NpcPriceCheck check = *g_npc_scripting.price_check;
        if (CompareTextIgnoreAsciiCase(text, g_localized_strings[0x7df]) == 0) {
            if (static_cast<unsigned int>(check.price) > g_status.party_gold) {
                RunNpcScriptLine(0x14, false);
            } else {
                SpendPartyGold(check.price);
                if (check.tells_fact) {
                    TellNpcFact(check.npc, check.quote_index);
                }
                RunNpcScriptLine(check.quote_index, false);
            }
        } else {
            RunNpcQuoteDeclineActions(check.quote_index);
        }
        g_npc_scripting.price_check.reset();
    }
    g_npc_scripting.reply = W8NpcReplyState::none;
}
