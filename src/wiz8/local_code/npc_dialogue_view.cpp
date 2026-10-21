#include "wiz8/local_code/npc_dialogue_view.h"

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

static void FinishNpcVoicePlayback(MainGameScreen& screen, bool resume_script);

static int g_empty_quote_text_index;

bool IsNpcScriptSessionActive(void)
{
    return MainGame() && MainGame()->dialogue.presentation != W8NpcPresentation::none;
}

bool IsNpcVoicePlaying(void)
{
    return MainGame() && MainGame()->dialogue.speech.voice_playing;
}

void UpdateNpcDialogueVoiceAndCursor(const ScriptContext& context)
{
    if (!MainGame()) return;
    W8NpcState* speaker = CurrentGame().npc_dialogue.speaker();
    if (MainGame()->dialogue.presentation == W8NpcPresentation::portrait_message) {
        if (IsNpcSpeechHoldOver(MainGame()->dialogue.speech)) {
            FinishNpcVoicePlayback(*MainGame(), true);
        }
    } else if (MainGame()->dialogue.presentation == W8NpcPresentation::quote) {
        if (!MainGame()->dialogue.speech.voice_playing &&
            IsNpcSpeechHoldOver(MainGame()->dialogue.speech)) {
            FinishNpcVoicePlayback(*MainGame(), true);
        }
        if (MainGame()->dialogue.speech.voice_playing && !speaker->is_grouped) {
            UpdateNpcMouth(MainGame()->dialogue.speech, GetNpcMonster(speaker));
        }
    }

    if (context.activity.conversation) {
        if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
            ScreenPointer().showSlot(1);
            return;
        }
        if (MainGame()->dialogue.presentation != W8NpcPresentation::quote) {
            if (!CurrentGame().npc_dialogue.has_queued_actions()) {
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

/* Speak one subquote of the staged quote: build the voice path under
   Data\Sound\<PCs|NPCs>\<stem>\<stem>_<quote>[a..].wav, pick the display and
   plain text (EMPTY/BLANK/UNKNOWN/CLASSIFIED/SOUND are sentinels), start the
   sound with the EOS callback armed, and hang the mouth-gap track on the
   party slot's monster-manager entry or on the dialogue speech for ungrouped
   NPCs. notice_only routes the text through ShowNotice instead. */
W8NpcSubquoteOutcome SpeakNpcSubquote(const ScriptContext& context, const W8NpcScriptQuote* quote,
                                      unsigned char subquote_index, bool notice_only,
                                      bool force_npc_voice)
{
    W8NpcState* const speaker = CurrentGame().npc_dialogue.speaker();
    const int current_quote = CurrentGame().npc_dialogue.current_quote();
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
            MainGame()->dialogue.presentation = W8NpcPresentation::portrait_message;
            SetNpcQuoteBubbleVisible(true, display_text, 0, -1, -1);
            HoldNpcSpeech(MainGame()->dialogue.speech,
                          wiz8::text::grapheme_count(display_text) * 60 + 2000);
            return W8NpcSubquoteOutcome::presented;
        }
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, W8_NOTICE_WRAP_TEXT_BOX);
        return W8NpcSubquoteOutcome::presented;
    }

    if (!CurrentGame().npc_dialogue.binding().npc_voice && !force_npc_voice) {
        voice_dir = "PCs";
    } else {
        voice_dir = "npcs";
    }
    voice_path = fmt::format("Data\\Sound\\{}\\{}\\{}_{:03d}", voice_dir,
                             CurrentGame().npc_dialogue.binding().voice_stem,
                             CurrentGame().npc_dialogue.binding().voice_stem, current_quote);
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
            DisplayNpcQuote(plain_text, true);
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
            SetNpcQuoteBubbleVisible(true, display_text, 0, current_quote, -1, {},
                                     speaker->partner_index);
        }
        const bool voiced =
            PlayNpcVoice(MainGame()->dialogue.speech, voice_path, [screen = MainGame()] {
                FinishNpcVoicePlayback(*screen, false);
                screen->dialogue.presentation = W8NpcPresentation::quote;
                HoldNpcSpeech(screen->dialogue.speech, 700);
            });
        monster = GetNpcMonster(speaker);
        if (monster != 0) {
            monster->StartTalking(true);
        }
        if (!voiced) {
            HoldNpcSpeech(MainGame()->dialogue.speech,
                          wiz8::text::grapheme_count(display_text) * 60 + 2000);
        }
        entry = GetNpcGroupEntry(speaker);
        if (entry != 0) {
            SetPartyPortraitEventState(speaker->group_index, true, current_quote, display_text, 1);
            BeginPortraitVoice(speaker->group_index, MainGame()->dialogue.speech.voice_handle,
                               voice_path, MainGame()->dialogue.speech.hold_ms);
            if (voiced) {
                context.party.rows[speaker->group_index].pending_event_type = current_quote;
                entry->pending_event_type = current_quote;
                CurrentGame().npc_interaction.last_notice_npc_kind = speaker->partner_index;
            }
        } else if (voiced) {
            MainGame()->dialogue.speech.gap_track = LoadMouthGapTrack(voice_path);
        }
        MainGame()->dialogue.presentation = W8NpcPresentation::quote;
        return W8NpcSubquoteOutcome::presented;
    }
    display_text = fmt::format("{}: {}", speaker->record->source_name, display_text);
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, W8_NOTICE_WRAP_TEXT_BOX);
    return W8NpcSubquoteOutcome::presented;
}

static void FinishNpcVoicePlayback(MainGameScreen& screen, bool resume_script)
{
    W8NpcState* const speaker = screen.game.npc_dialogue.speaker();
    const int current_quote = screen.game.npc_dialogue.current_quote();
    if (screen.dialogue.presentation == W8NpcPresentation::portrait_message) {
        screen.dialogue.presentation = W8NpcPresentation::none;
        SetNpcQuoteBubbleVisible(false, {}, 0, -1, -1);
        screen.game.npc_dialogue.SpeechFinished();
        return;
    }
    if (screen.dialogue.presentation != W8NpcPresentation::quote) {
        return;
    }
    StopNpcVoice(screen.dialogue.speech);
    Monster* monster = GetNpcMonster(speaker);
    if (monster != 0) {
        monster->StopTalking();
    }
    if (GetNpcGroupEntry(speaker) != 0) {
        FinishPortraitSpeech(speaker->group_index);
        SetNpcQuoteBubbleVisible(false, {}, 0, -1, -1);
    }
    screen.dialogue.presentation = W8NpcPresentation::none;
    screen.game.npc_dialogue.SpeechFinished();
    if (screen.game.npc_dialogue.reply() == W8NpcReplyState::none && resume_script) {
        if (speaker != 0 && screen.game.npc_dialogue.binding().script != 0 &&
            current_quote < screen.game.npc_dialogue.binding().script->quote_count) {
            SetNpcQuoteBubbleVisible(
                false, {}, &screen.game.npc_dialogue.binding().script->quotes[current_quote],
                current_quote, -1);
            return;
        }
    }
}

/* Guarded finish entry point: suppressed outright while the level-4 gate
   flag is set; a non-forced call inside 500 ms of the last script tick is a
   no-op, so the tick path can poll without re-finishing. */
void TryFinishNpcVoicePlayback(const GameStatus& status, bool force)
{
    if (status.world_cursor_gate == 0 || GetLoadedLevelID() != 4) {
        if (!force &&
            MonotonicMilliseconds() - MainGame()->dialogue.speech.voice_started_at <= 500) {
            return;
        }
        FinishNpcVoicePlayback(*MainGame(), true);
    }
}

void ShowNpcPortraitMessage(std::string_view text, W8QuoteNotice notice)
{
    MainGame()->dialogue.presentation = W8NpcPresentation::portrait_message;
    SetNpcQuoteBubbleVisible(true, text, 0, -1, -1, notice, -1);
    HoldNpcSpeech(MainGame()->dialogue.speech, ComputePortraitMessageDuration(text));
}

/* Repost the NPC's current quote bubble after a modal sub-dialog closes, or
   clear it when the dialogue NPC/script went away. */
void RestoreCurrentNpcQuoteBubble(void)
{
    W8NpcState* const speaker = CurrentGame().npc_dialogue.speaker();
    const int current_quote = CurrentGame().npc_dialogue.current_quote();
    const W8NpcScriptFile* script_file =
        speaker != 0 ? CurrentGame().npc_dialogue.binding().script.get() : 0;
    if (script_file != 0 && current_quote < static_cast<int>(script_file->quote_count)) {
        SetNpcQuoteBubbleVisible(false, {}, script_file->quotes.data() + current_quote,
                                 current_quote, 0xffffffff);
        return;
    }
    SetNpcQuoteBubbleVisible(false, {}, 0, -1, 0xffffffff);
}

/* Raise the quote bubble over `text` and, when `play_sound` is set, kick off
   the generic start-game click that accompanies a silent text notice. */
void DisplayNpcQuote(std::string_view text, bool play_sound)
{
    SetNpcQuoteBubbleVisible(true, text, 0, -1, 0x47);
    auto& speech = MainGame()->dialogue.speech;
    speech.voice_playing = false;
    speech.voice_started_at = MonotonicMilliseconds();
    HoldNpcSpeech(speech, 2000);
    MainGame()->dialogue.presentation = W8NpcPresentation::quote;
    if (play_sound) {
        wiz8::sound::play("Data\\Sound\\misc\\startgame.wav");
    }
}
