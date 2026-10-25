#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "wiz8/audio/soundman.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/unicode.h"

constexpr int kWorldActionQuoteMax = 'R';
constexpr int kWorldActionQuoteMin = 'F';

/* The NSF dialogue interpreter half of W8NpcDialogue: picks the next quote,
   runs a quote's immediate entries, resolves player replies and schedules
   continuations on the queue. The queue's frame step lives in
   NPC Scripting.cpp; presentation and speech in npc_dialogue_view.cpp. */

/* Evaluate a kind-3 quote entry: each sub-entry pair is a (fact, expected)
   guard that must all hold, then the entry selects the response - operand0
   directly, randomly in [operand0, operand2] when operand1 is 2, or a
   queued QUOTE_ENTRY continuation line when operand2 is 4. Returns the
   response index, -1 when a guard fails, and -2 after queueing. */
int W8NpcDialogue::SelectResponse(const ScriptContext& context, const W8NpcQuoteEntry* entry,
                                  int quote_index, int entry_index)
{
    int index;
    unsigned char expected;
    signed char response_count;

    index = 0;
    if (entry->operand2 == 4 && entry->operand1 != 2) {
        index = 1;
    }
    while (index < std::ssize(entry->sub_entries)) {
        expected = static_cast<unsigned char>(
            entry->sub_entries[index + 1].operand) /* c-style-cast-ok: packed byte operand */;
        if (context.game.testFact(static_cast<W8FactId>(entry->sub_entries[index].operand)) !=
            expected) {
            return -1;
        }
        index += 2;
    }
    if (entry->operand2 == 4 && entry->operand1 != 2) {
        QueueQuoteEntry(quote_index, entry_index, 0, false);
        return -2;
    }
    if (entry->operand1 == 2) {
        response_count = entry->operand2 - entry->operand0 + 1;
        return context.game.rng.below(response_count) + entry->operand0;
    }
    return entry->operand0;
}

/* Kind-4 entries carry the keyword strings a dialogue keyword can select;
   scan every quote for a subentry whose text
   matches, reporting the entry and subentry indices through the out-pointers. */
int FindNpcScriptQuoteByKeyword(std::string_view keyword, short* entry_index,
                                short* sub_entry_index)
{
    const W8NpcScriptFile* script;
    const W8NpcScriptQuote* quote;
    const W8NpcQuoteEntry* entry;
    std::string text;
    int quote_index;
    int item_index;
    int sub_index;

    script = CurrentGame().npc_dialogue.binding().script.get();
    for (quote_index = 0; quote_index < std::ssize(script->quotes); quote_index++) {
        quote = &script->quotes[quote_index];
        for (item_index = 0; item_index < std::ssize(quote->entries); item_index++) {
            entry = &quote->entries[item_index];
            if (entry->kind == W8_NPC_ENTRY_KEYWORD) {
                for (sub_index = 0; sub_index < std::ssize(entry->sub_entries); sub_index++) {
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
void W8NpcDialogue::Speak(const ScriptContext& context, int script_line, bool force_npc_voice)
{
    const W8NpcScriptQuote* quote;
    const W8NpcQuoteEntry* entry;
    Monster* monster;
    W8NpcState* target;
    signed char faction;
    signed char response_count;
    int response;
    int entry_index;
    bool finished;

    // Facts may replace the NPC script and select a new invocation while this runs.
    const auto cancellation_generation = cancellation_generation_;
    const auto script_owner = session_.binding.script;
    if (!script_owner || script_line < 0) return;
    session_.current_quote_index = script_line;
    if (script_line != session_.finished_quote_index) {
        session_.subquote_index = 0;
    }
    if (script_line < std::ssize(script_owner->quotes)) {
        quote = &script_owner->quotes[script_line];
        if (session_.subquote_index == std::ssize(quote->subquotes)) {
            session_.subquote_index = 0;
        }
        response = -1;
        finished = false;
        if (session_.subquote_index + 1 == std::ssize(quote->subquotes) &&
            !quote->entries.empty()) {
            for (entry_index = 0; entry_index < std::ssize(quote->entries); entry_index++) {
                entry = &quote->entries[entry_index];
                switch (entry->kind) {
                case W8_NPC_ENTRY_NEXT_QUOTE:
                    if (entry->operand2 == 4) {
                        if (entry->operand1 != 2) {
                            QueueQuoteEntry(script_line, entry_index, 0, false);
                            response = -1;
                            finished = true;
                            break;
                        }
                    } else if (entry->operand1 != 2) {
                        response = entry->operand0;
                        finished = true;
                        break;
                    }
                    response_count =
                        static_cast<signed char>(entry->operand2 - entry->operand0 + 1);
                    response = context.game.rng.below(response_count) + entry->operand0;
                    finished = true;
                    break;
                case W8_NPC_ENTRY_FACT_CONDITIONAL_QUOTE:
                    response = SelectResponse(context, entry, script_line, entry_index);
                    if (response != -1) {
                        finished = true;
                        break;
                    }
                    break;
                case W8_NPC_ENTRY_OPTIONS:
                    reply_ = W8NpcReplyState::pending;
                    QueueQuoteEntry(script_line, entry_index, 0, false);
                    finished = true;
                    break;
                case W8_NPC_ENTRY_SET_FACT:
                    context.game.setFact(static_cast<W8FactId>(entry->operand0), entry->operand1);
                    break;
                case W8_NPC_ENTRY_CLOSE_DIALOGUE:
                    Queue(W8NpcCommand::close_dialogue);
                    break;
                case W8_NPC_ENTRY_GIVE_ITEM:
                case W8_NPC_ENTRY_REMOVE_ITEM:
                case W8_NPC_ENTRY_GIVE_GOLD:
                case W8_NPC_ENTRY_GIVE_EXPERIENCE:
                    QueueQuoteEntry(script_line, entry_index, 0, false);
                    break;
                case W8_NPC_ENTRY_MONSTER_SCRIPT_LABEL:
                    monster = GetNpcMonster(session_.npc.get());
                    if (monster != 0 && entry->operand2 != 4 &&
                        !monster->SetScriptLabel(entry->sub_entries[0].text.c_str())) {
                        std::string script_error;
                        script_error =
                            fmt::format("Failed to load script (or failed to find {} in script)",
                                        entry->sub_entries[0].text);
                    }
                    break;
                case W8_NPC_ENTRY_NPC_WORLD_ACTION:
                    if (session_.current_quote_index < kWorldActionQuoteMin ||
                        kWorldActionQuoteMax < session_.current_quote_index) {
                        std::string action_name;
                        BeginScriptedWorldAction();
                        action_name = entry->sub_entries[0].text.c_str() + 4;
                        target = context.story.npcByName(action_name);
                        if (target != 0) {
                            if (!target->is_grouped) {
                                ClearMainGameTargetState();
                            } else {
                                Queue(W8NpcDismiss{static_cast<int>(target->group_index)});
                            }
                        }
                    }
                    break;
                case W8_NPC_ENTRY_CLOSE_AND_RESUME:
                    Queue(W8NpcCommand::close_and_resume_npc);
                    finished = true;
                    break;
                case W8_NPC_ENTRY_KEYWORD_INPUT:
                    reply_ = W8NpcReplyState::pending;
                    QueueQuoteEntry(script_line, entry_index, 0, false);
                    finished = true;
                    break;
                case W8_NPC_ENTRY_SELF_GROUP_ACTION:
                    if (session_.current_quote_index < kWorldActionQuoteMin ||
                        kWorldActionQuoteMax < session_.current_quote_index) {
                        BeginScriptedWorldAction();
                        Queue(W8NpcDismiss{static_cast<int>(session_.npc->group_index)});
                    }
                    break;
                case W8_NPC_ENTRY_NPC_INTERACTION: {
                    std::string action_name;
                    action_name = entry->sub_entries[0].text.c_str() + 4;
                    target = context.story.npcByName(action_name);
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
                    Queue(W8NpcPartySpeakerEvent{static_cast<unsigned int>(entry->operand0)});
                    break;
                case W8_NPC_ENTRY_PERSON_KEYWORD:
                case W8_NPC_ENTRY_ITEM_KEYWORD:
                case W8_NPC_ENTRY_PLACE_KEYWORD:
                case W8_NPC_ENTRY_MISC_KEYWORD:
                case W8_NPC_ENTRY_PERSON_KEYWORD_ALT:
                case W8_NPC_ENTRY_ITEM_KEYWORD_ALT:
                case W8_NPC_ENTRY_PLACE_KEYWORD_ALT:
                case W8_NPC_ENTRY_MISC_KEYWORD_ALT:
                    ApplyScriptedDialogueKeyword(context, entry->kind,
                                                 entry->sub_entries[0].text.c_str());
                    break;
                default:
                    break;
                }
                if (cancellation_generation != cancellation_generation_) {
                    return;
                }
                if (finished) {
                    break;
                }
            }
        }
        if (cancellation_generation != cancellation_generation_) {
            return;
        }
        if (response >= 0) {
            QueueQuote(response, false, false, false);
        }
        switch (SpeakNpcSubquote(context, quote, session_.subquote_index, false, force_npc_voice)) {
        case W8NpcSubquoteOutcome::presented:
            break;
        case W8NpcSubquoteOutcome::unknown:
            QueueQuote(context.game.rng.below(100) < 50 ? 0x1c : 0x1d, false, false, false);
            break;
        case W8NpcSubquoteOutcome::classified:
            QueueQuote(0x1e, false, false, false);
            break;
        }
        session_.subquote_index++;
        if (session_.current_quote_index == 0) {
            session_.npc->greeting_pending = false;
        }
        if (session_.subquote_index < std::ssize(quote->subquotes)) {
            QueueQuote(session_.current_quote_index, false, true, true);
        }
    }
}

void W8NpcDialogue::Push(W8NpcAction action, bool prepend)
{
    W8MessageBoxLine line{session_.npc != 0 ? session_.npc->record->kind : -1, std::move(action)};
    if (!prepend) {
        queue_.push_back(std::move(line));
    } else {
        queue_.push_front(std::move(line));
    }
}

void W8NpcDialogue::QueueQuote(int quote, bool mark_pending, bool prepend, bool suppress_entries)
{
    Push(W8NpcQueuedQuote{session_.binding, session_.subquote_index, quote, mark_pending,
                          suppress_entries},
         prepend);
}

void W8NpcDialogue::Queue(W8NpcAction action)
{
    if (std::holds_alternative<W8NpcScriptNotice>(action) &&
        std::ranges::any_of(queue_, [](const auto& queued) {
            return std::holds_alternative<W8NpcScriptNotice>(queued.action);
        }))
        return;
    Push(std::move(action), false);
}

void W8NpcDialogue::QueuePortraitMessage(std::string text, W8QuoteNotice notice)
{
    Queue(W8NpcQueuedNotice{std::move(text), std::move(notice)});
}

/* Resolve the player's reply text to the script line it selects. The current
   quote's option/keyword entries (kinds 5 and 0x13) carry the matchable
   phrases in their sub-entries; a hit consults the quote's kind-6 reply
   entries - role 3 supplies the per-keyword answer list, role 1 the generic
   answer. No keyword match at all falls back to the role-2 entry. -1 when the
   quote has no usable reply. */
int W8NpcDialogue::FindReplyQuote(std::string_view text) const
{
    const auto& entries = session_.binding.script->quotes[session_.current_quote_index].entries;
    const auto matches = [text](const W8NpcQuoteEntry& entry) {
        return std::ranges::any_of(entry.sub_entries, [text](const auto& sub) {
            return CompareTextIgnoreAsciiCase(sub.text, text) == 0;
        });
    };
    const auto options = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.kind == W8_NPC_ENTRY_OPTIONS || entry.kind == W8_NPC_ENTRY_KEYWORD_INPUT;
    });
    const bool matched = options != entries.end() && matches(*options);
    if (matched) {
        for (const auto& entry : entries) {
            if (entry.kind == W8_NPC_ENTRY_REPLY && entry.operand2 == 3 && matches(entry)) {
                return entry.operand0;
            }
        }
    }
    // A recognized option uses the generic answer; other text uses the fallback.
    for (const auto& entry : entries) {
        if (entry.kind == W8_NPC_ENTRY_REPLY && entry.operand2 == (matched ? 1 : 2)) {
            return entry.operand0;
        }
    }
    return -1;
}

std::string GetNpcQuoteText(W8NpcState* npc, int type)
{
    const W8NpcScriptFile* script = npc->script_file.get();
    if (type < 0 || type >= std::ssize(script->quotes)) return {};
    const auto& record = script->quotes[type];
    if (record.subquotes.empty() || record.subquotes[0].empty()) return {};
    return record.subquotes[0];
}

/* Queue a quote-entry line carrying `continuation_quote`; `prepend` inserts it
   at the front of the queue (continuation subquotes overtake pending lines). */
void W8NpcDialogue::QueueQuoteEntry(int quote_index, int entry_index, int continuation_quote,
                                    bool prepend)
{
    Push(W8NpcQueuedEntry{session_.binding, quote_index, entry_index, continuation_quote}, prepend);
}

void W8NpcDialogue::Cancel()
{
    ++cancellation_generation_;
}

/* Run every kind-0x17 consequence entry of the given quote - the reply
   handler's decline path when the party refuses a price offer. */
void W8NpcDialogue::RunDeclineActions(const ScriptContext& context, int quote_index)
{
    const W8NpcScriptQuote* quote;
    int index;

    const auto script_owner = session_.binding.script;
    quote = script_owner->quotes.data() + quote_index;
    for (index = 0; index < std::ssize(quote->entries); ++index) {
        if (quote->entries[index].kind == W8_NPC_ENTRY_DECLINE_QUOTE) {
            Speak(context, quote->entries[index].operand0, false);
        }
    }
}

/* Resolve the player's answer to the pending reply: a typed or chosen reply
   selects its script line; a price check spends the price and runs its quote,
   or runs the quote's decline entries. */
void W8NpcDialogue::Answer(const ScriptContext& context, std::string_view text)
{
    if (reply_session_) {
        session_ = std::move(*reply_session_);
        reply_session_.reset();
    }
    if (!price_check_) {
        const int line = FindReplyQuote(text);
        if (line != -1) {
            QueueQuote(line, false, false, false);
        }
    } else {
        const W8NpcPriceCheck check = *price_check_;
        if (CompareTextIgnoreAsciiCase(text, Data().strings[0x7df]) == 0) {
            if (static_cast<unsigned int>(check.price) > context.party.gold) {
                Speak(context, 0x14, false);
            } else {
                SpendPartyGold(context.party, check.price);
                if (check.tells_fact) {
                    TellNpcFact(check.npc.get(), check.quote_index);
                }
                Speak(context, check.quote_index, false);
            }
        } else {
            RunDeclineActions(context, check.quote_index);
        }
        price_check_.reset();
    }
    reply_ = W8NpcReplyState::none;
}

const W8NpcDialogue::Utterance* W8NpcDialogue::utterance(std::uint64_t identity) const
{
    return utterance_ && utterance_->identity == identity ? &*utterance_ : nullptr;
}

std::uint64_t W8NpcDialogue::BeginUtterance(Utterance::Phase phase, std::uint64_t now,
                                            std::uint64_t duration)
{
    utterance_ = Utterance{++utterance_identity_,
                           session_.identity,
                           phase,
                           session_.npc,
                           session_.binding,
                           session_.current_quote_index,
                           now,
                           duration};
    return utterance_->identity;
}

std::uint64_t W8NpcDialogue::BeginVoiceQuote()
{
    return BeginUtterance(Utterance::Phase::voice, 0, 0);
}

void W8NpcDialogue::VoiceStarted(std::uint64_t identity, std::uint64_t now, bool voiced,
                                 std::uint64_t unvoiced_ms)
{
    if (!utterance(identity)) return;
    voice_started_at_ = now;
    if (!voiced) {
        utterance_->phase = Utterance::Phase::quote_hold;
        utterance_->hold_started = now;
        utterance_->hold_ms = unvoiced_ms;
    }
}

std::uint64_t W8NpcDialogue::BeginHeldQuote(std::uint64_t now, std::uint64_t duration)
{
    voice_started_at_ = now;
    return BeginUtterance(Utterance::Phase::quote_hold, now, duration);
}

std::uint64_t W8NpcDialogue::BeginPortraitMessage(std::uint64_t now, std::uint64_t duration)
{
    return BeginUtterance(Utterance::Phase::portrait_message, now, duration);
}

bool W8NpcDialogue::UtteranceHoldOver(std::uint64_t identity, std::uint64_t now) const
{
    const auto* pending = utterance(identity);
    return pending && pending->phase != Utterance::Phase::voice &&
           pending->hold_ms < now - pending->hold_started;
}

bool W8NpcDialogue::CanSkipUtterance(std::uint64_t now) const
{
    return now - voice_started_at_ > 500;
}

void W8NpcDialogue::MarkUtteranceFinished(const Utterance& finished)
{
    const auto mark = [&](Session& session) {
        if (session.identity == finished.session) session.finished_quote_index = finished.quote;
    };
    mark(session_);
    if (suspended_) mark(*suspended_);
    if (reply_session_) mark(*reply_session_);
}

void W8NpcDialogue::VoiceFinished(std::uint64_t identity, std::uint64_t now)
{
    const auto* pending = utterance(identity);
    if (!pending || pending->phase != Utterance::Phase::voice) return;
    MarkUtteranceFinished(*pending);
    utterance_->phase = Utterance::Phase::quote_hold;
    utterance_->hold_started = now;
    utterance_->hold_ms = 700;
}

void W8NpcDialogue::FinishUtterance(std::uint64_t identity,
                                    std::function_ref<void(const Utterance&)> release_presentation)
{
    if (!utterance(identity)) return;
    auto finished = std::move(*utterance_);
    utterance_.reset();
    // Notice observers may issue follow-up commands. They must see the completed
    // cursor and no outstanding wait, never a half-finished utterance.
    MarkUtteranceFinished(finished);
    release_presentation(finished);
}

void W8NpcDialogue::AbandonUtterance(std::uint64_t identity)
{
    if (utterance(identity)) utterance_.reset();
}

void W8NpcDialogue::ReplyDialogOpened(std::optional<W8NpcPriceCheck> price_check)
{
    reply_session_ = session_;
    reply_ = W8NpcReplyState::dialog_open;
    if (price_check) {
        price_check_ = std::move(price_check);
    }
}

void W8NpcDialogue::ReplyDialogClosed()
{
    reply_session_.reset();
    reply_ = W8NpcReplyState::none;
}

void W8NpcDialogue::AbandonReply()
{
    reply_session_.reset();
    reply_ = W8NpcReplyState::none;
    price_check_.reset();
}

W8NpcDialogue::~W8NpcDialogue()
{
    if (scene_) wiz8::sound::stop(*scene_);
}

void W8NpcDialogue::BeginScene()
{
    ReleaseScene();
    scene_.emplace();
}

void W8NpcDialogue::BeginSoundScene(std::string_view path)
{
    auto sound = wiz8::sound::play(path, {.on_finished = [this] { ReleaseScene(); }});
    if (!sound) return;
    ReleaseScene();
    scene_ = std::move(sound);
}

void W8NpcDialogue::ReleaseScene()
{
    if (scene_) wiz8::sound::stop(*scene_);
    scene_.reset();
}
