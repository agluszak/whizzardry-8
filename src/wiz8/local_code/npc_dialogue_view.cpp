#include "wiz8/local_code/npc_dialogue_view.h"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/chrono.h> // IWYU pragma: keep - std::tm formatter specialization.
#include <fmt/format.h>
#include <fmt/printf.h>

#include "cursor.h"
#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/npc_speech.h"
#include "wiz8/platform/timer.h"
#include "wiz8/pointer.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/unicode.h"
#include "wiz8/world/level_session.h"

static int g_empty_quote_text_index;

// Dialogue code with no screen of its own presents through the main game's panel.
static W8NpcDialoguePanel* ShownPanel()
{
    auto* screen = MainGame();
    return screen ? &screen->dialogue : nullptr;
}

bool IsNpcScriptSessionActive(void)
{
    const auto* panel = ShownPanel();
    return panel && panel->presenting();
}

bool IsNpcVoicePlaying(void)
{
    const auto* panel = ShownPanel();
    return panel && panel->voicePlaying();
}

void UpdateNpcDialogueVoiceAndCursor(const ScriptContext& context)
{
    auto* panel = ShownPanel();
    if (!panel) return;
    panel->updateVoice();

    if (context.activity.conversation) {
        if (context.game.npc_interaction.dialogue_hidden != 0) {
            ScreenPointer().showSlot(1);
            return;
        }
        if (panel->presentation != W8NpcPresentation::quote) {
            if (!context.game.npc_dialogue.has_queued_actions()) {
                if (ScreenPointer().shownSlot() != W8_CURSOR_TEXT_INPUT) {
                    ScreenPointer().showSlot(W8_CURSOR_NONE);
                    return;
                }
            }
        } else if (!context.activity.scripted_scene) {
            ScreenPointer().showSlot(W8_CURSOR_MAP_LOAD);
        }
    }
}

W8NpcSubquoteOutcome SpeakNpcSubquote(const ScriptContext& context, const W8NpcScriptQuote* quote,
                                      unsigned char subquote_index, bool notice_only,
                                      bool force_npc_voice)
{
    // Without a main game there is no panel to speak into; the quote counts as presented.
    auto* panel = ShownPanel();
    if (!panel) return W8NpcSubquoteOutcome::presented;
    return panel->speakSubquote(context, quote, subquote_index, notice_only, force_npc_voice);
}

void TryFinishNpcVoicePlayback(bool force)
{
    MainGame()->dialogue.tryFinishVoice(force);
}

void ShowNpcPortraitMessage(std::string_view text, W8QuoteNotice notice)
{
    MainGame()->dialogue.showPortraitMessage(text, notice);
}

void RestoreCurrentNpcQuoteBubble(void)
{
    MainGame()->dialogue.restoreQuoteBubble();
}

void DisplayNpcQuote(std::string_view text, bool play_sound)
{
    MainGame()->dialogue.displayQuote(text, play_sound);
}

void W8NpcDialoguePanel::updateVoice()
{
    if (presentation == W8NpcPresentation::portrait_message) {
        if (IsNpcSpeechHoldOver(speech)) finishVoice(true);
    } else if (presentation == W8NpcPresentation::quote) {
        if (!speech.voice_playing && IsNpcSpeechHoldOver(speech)) finishVoice(true);
        W8NpcState* speaker = screen_.game.npc_dialogue.speaker();
        if (speech.voice_playing && !speaker->is_grouped) {
            UpdateNpcMouth(speech, GetNpcMonster(speaker));
        }
    }
}

/* Speak one subquote of the staged quote: build the voice path under
   Data\Sound\<PCs|NPCs>\<stem>\<stem>_<quote>[a..].wav, pick the display and
   plain text (EMPTY/BLANK/UNKNOWN/CLASSIFIED/SOUND are sentinels), start the
   sound with the EOS callback armed, and hang the mouth-gap track on the
   party slot's monster-manager entry or on the dialogue speech for ungrouped
   NPCs. notice_only routes the text through ShowNotice instead. */
W8NpcSubquoteOutcome W8NpcDialoguePanel::speakSubquote(const ScriptContext& context,
                                                       const W8NpcScriptQuote* quote,
                                                       unsigned char subquote_index,
                                                       bool notice_only, bool force_npc_voice)
{
    auto& dialogue = screen_.game.npc_dialogue;
    W8NpcState* const speaker = dialogue.speaker();
    const int current_quote = dialogue.current_quote();
    W8MonsterManagerEntry* entry;
    Monster* monster;
    std::string voice_dir;
    std::string voice_path;
    std::string display_text;
    std::string plain_text;

    static const int s_empty_quote_text_ids[2] = {1867, 1868};

    if (quote->dialogue_alert != 0) {
        voice_path = "Data\\Sound\\npcs\\Dialogue Alert.wav";
        display_text = quote->subquotes[subquote_index];
        if (!notice_only) {
            presentation = W8NpcPresentation::portrait_message;
            setQuoteBubbleVisible(true, display_text, 0, -1, -1);
            HoldNpcSpeech(speech, wiz8::text::grapheme_count(display_text) * 60 + 2000);
            return W8NpcSubquoteOutcome::presented;
        }
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, W8_NOTICE_WRAP_TEXT_BOX);
        return W8NpcSubquoteOutcome::presented;
    }

    if (!dialogue.binding().npc_voice && !force_npc_voice) {
        voice_dir = "PCs";
    } else {
        voice_dir = "npcs";
    }
    voice_path =
        fmt::format("Data\\Sound\\{}\\{}\\{}_{:03d}", voice_dir, dialogue.binding().voice_stem,
                    dialogue.binding().voice_stem, current_quote);
    if (subquote_index > 0) {
        voice_path.push_back(static_cast<char>(subquote_index + 'a' - 1));
    }
    voice_path += ".wav";
    if (quote->subquotes.empty()) {
        display_text = fmt::format(" Missing quote #{:d} for {}.", current_quote,
                                   speaker->record->source_name);
        plain_text = fmt::format(" Missing quote #{:d} for {}.", current_quote,
                                 speaker->record->source_name);
    } else {
        display_text = fmt::format(" \"{}\"", quote->subquotes[subquote_index]);
        plain_text = quote->subquotes[subquote_index];
    }
    if (plain_text.size() == 0) {
        return W8NpcSubquoteOutcome::presented;
    }
    if (!notice_only) {
        if (CompareTextIgnoreAsciiCase(plain_text, "EMPTY") == 0 ||
            CompareTextIgnoreAsciiCase(plain_text, "BLANK") == 0) {
            if (current_quote != 0x67 && current_quote != 0x68 && current_quote != 0x69) {
                return W8NpcSubquoteOutcome::presented;
            }
            plain_text =
                fmt::sprintf(Data().strings[s_empty_quote_text_ids[g_empty_quote_text_index]],
                             speaker->record->source_name);
            ++g_empty_quote_text_index;
            if (g_empty_quote_text_index == 2) {
                g_empty_quote_text_index = 0;
            }
            displayQuote(plain_text, true);
            return W8NpcSubquoteOutcome::presented;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "UNKNOWN") == 0) {
            return W8NpcSubquoteOutcome::unknown;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "CLASSIFIED") == 0) {
            return W8NpcSubquoteOutcome::classified;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "SOUND") != 0 && !speaker->is_grouped) {
            if (!context.activity.conversation && speaker->record->voice_script == 0) {
                display_text = fmt::format("{}: {}", speaker->record->source_name, display_text);
            }
            setQuoteBubbleVisible(true, display_text, 0, current_quote, -1, {},
                                  speaker->partner_index);
        }
        const bool voiced = PlayNpcVoice(speech, voice_path, [this] {
            finishVoice(false);
            presentation = W8NpcPresentation::quote;
            HoldNpcSpeech(speech, 700);
        });
        monster = GetNpcMonster(speaker);
        if (monster != 0) {
            monster->StartTalking(true);
        }
        if (!voiced) {
            HoldNpcSpeech(speech, wiz8::text::grapheme_count(display_text) * 60 + 2000);
        }
        entry = GetNpcGroupEntry(speaker);
        if (entry != 0) {
            screen_.game.speech.SetPortraitEventState(speaker->group_index, true, current_quote,
                                                      display_text, 1);
            screen_.game.speech.BeginPortraitVoice(speaker->group_index, speech.voice_handle,
                                                   voice_path, speech.hold_ms);
            if (voiced) {
                context.party.rows[speaker->group_index].pending_event_type = current_quote;
                entry->pending_event_type = current_quote;
                screen_.game.npc_interaction.last_notice_npc_kind = speaker->partner_index;
            }
        } else if (voiced) {
            speech.gap_track = LoadMouthGapTrack(voice_path);
        }
        presentation = W8NpcPresentation::quote;
        return W8NpcSubquoteOutcome::presented;
    }
    display_text = fmt::format("{}: {}", speaker->record->source_name, display_text);
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, W8_NOTICE_WRAP_TEXT_BOX);
    return W8NpcSubquoteOutcome::presented;
}

void W8NpcDialoguePanel::finishVoice(bool resume_script)
{
    auto& dialogue = screen_.game.npc_dialogue;
    W8NpcState* const speaker = dialogue.speaker();
    const int current_quote = dialogue.current_quote();
    if (presentation == W8NpcPresentation::portrait_message) {
        presentation = W8NpcPresentation::none;
        setQuoteBubbleVisible(false, {}, 0, -1, -1);
        dialogue.SpeechFinished();
        return;
    }
    if (presentation != W8NpcPresentation::quote) {
        return;
    }
    StopNpcVoice(speech);
    Monster* monster = GetNpcMonster(speaker);
    if (monster != 0) {
        monster->StopTalking();
    }
    if (GetNpcGroupEntry(speaker) != 0) {
        screen_.game.speech.FinishPortraitSpeech(speaker->group_index);
        setQuoteBubbleVisible(false, {}, 0, -1, -1);
    }
    presentation = W8NpcPresentation::none;
    dialogue.SpeechFinished();
    if (dialogue.reply() == W8NpcReplyState::none && resume_script) {
        if (speaker != 0 && dialogue.binding().script != 0 &&
            current_quote < std::ssize(dialogue.binding().script->quotes)) {
            setQuoteBubbleVisible(false, {}, &dialogue.binding().script->quotes[current_quote],
                                  current_quote, -1);
            return;
        }
    }
}

void W8NpcDialoguePanel::tryFinishVoice(bool force)
{
    if (screen_.game.status.world_cursor_gate == 0 || GetLoadedLevelID() != 4) {
        if (!force && MonotonicMilliseconds() - speech.voice_started_at <= 500) {
            return;
        }
        finishVoice(true);
    }
}

void W8NpcDialoguePanel::showPortraitMessage(std::string_view text, W8QuoteNotice notice)
{
    presentation = W8NpcPresentation::portrait_message;
    setQuoteBubbleVisible(true, text, 0, -1, -1, notice, -1);
    HoldNpcSpeech(speech, ComputePortraitMessageDuration(text));
}

void W8NpcDialoguePanel::restoreQuoteBubble()
{
    auto& dialogue = screen_.game.npc_dialogue;
    const int current_quote = dialogue.current_quote();
    const W8NpcScriptFile* script_file =
        dialogue.speaker() != 0 ? dialogue.binding().script.get() : 0;
    if (script_file != 0 && current_quote < std::ssize(script_file->quotes)) {
        setQuoteBubbleVisible(false, {}, script_file->quotes.data() + current_quote, current_quote,
                              0xffffffff);
        return;
    }
    setQuoteBubbleVisible(false, {}, 0, -1, 0xffffffff);
}

void W8NpcDialoguePanel::displayQuote(std::string_view text, bool play_sound)
{
    setQuoteBubbleVisible(true, text, 0, -1, 0x47);
    speech.voice_playing = false;
    speech.voice_started_at = MonotonicMilliseconds();
    HoldNpcSpeech(speech, 2000);
    presentation = W8NpcPresentation::quote;
    if (play_sound) {
        wiz8::sound::play("Data\\Sound\\misc\\startgame.wav");
    }
}
