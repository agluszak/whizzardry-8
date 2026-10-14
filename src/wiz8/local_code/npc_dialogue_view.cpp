#include "wiz8/local_code/npc_dialogue_view.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/chrono.h> // IWYU pragma: keep - std::tm formatter specialization.
#include <fmt/format.h>
#include <fmt/printf.h>

#include "audio_resources.h"
#include "compat/native.h"
#include "cursor.h"
#include "layouts/screen_state.h"
#include "local_code/MonsterManager.h"
#include "random.h"
#include "soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/message_box.h"
#include "wiz8/mouth_gap.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/npc_speech.h"
#include "wiz8/unicode.h"
#include "wiz8/xstatus.h"

static void FinishNpcVoicePlayback(bool resume_script);

static int g_empty_quote_text_index;

void ShowNpcScriptNoticeBubble(std::string_view text)
{
    SetNpcQuoteBubbleVisible(true, text, 0, -1, 0x47);
    g_npc_scripting.speech.voice_playing = false;
    g_npc_scripting.speech.voice_started_at = w8_get_ticks();
    HoldNpcSpeech(g_npc_scripting.speech, 2000);
}

// FUNCTION: WIZ8 0x00524DA0
void UpdateNpcDialogueVoiceAndCursor(void)
{
    if (g_npc_scripting.portrait_message_active) {
        if (IsNpcSpeechHoldOver(g_npc_scripting.speech)) {
            FinishNpcVoicePlayback(true);
        }
    } else if (g_npc_scripting.session.quote_active) {
        if (!g_npc_scripting.speech.voice_playing && IsNpcSpeechHoldOver(g_npc_scripting.speech)) {
            FinishNpcVoicePlayback(true);
        }
        if (g_npc_scripting.speech.voice_playing && !g_npc_scripting.session.npc->is_grouped) {
            UpdateNpcMouth(g_npc_scripting.speech, GetNpcMonster(g_npc_scripting.session.npc));
        }
    }

    if (gXStatus.fNpcDialogueMode) {
        if (g_npc_interaction_state->dialogue_hidden != 0) {
            SetTargetCursor(1);
            return;
        }
        if (!g_npc_scripting.session.quote_active) {
            if (static_cast<int>(g_npc_scripting.message_lines.size()) < 1) {
                if (gXStatus.iCurrentCursor != W8_CURSOR_TEXT_INPUT) {
                    SetTargetCursor(W8_CURSOR_NONE);
                    return;
                }
            }
        } else if (!gXStatus.scripted_scene) {
            SetTargetCursor(W8_CURSOR_MAP_LOAD);
        }
    }
}

/* Completion callback armed on the NPC voice. A natural stream end finishes
   the quote but leaves the session active for a 700 ms hold. */
// FUNCTION: WIZ8 0x005251C0
static void NpcVoiceEosCallback()
{
    FinishNpcVoicePlayback(false);
    g_npc_scripting.session.quote_active = true;
    HoldNpcSpeech(g_npc_scripting.speech, 700);
}

/* Speak one subquote of the staged quote: build the voice path under
   Data\Sound\<PCs|NPCs>\<stem>\<stem>_<quote>[a..].wav, pick the display and
   plain text (EMPTY/BLANK/UNKNOWN/CLASSIFIED/SOUND are sentinels), start the
   sound with the EOS callback armed, and hang the mouth-gap track on the
   party slot's monster-manager entry or on g_npc_scripting for ungrouped
   NPCs. notice_only routes the text through ShowNotice instead. */
// FUNCTION: WIZ8 0x00525350
void SpeakNpcSubquote(W8NpcScriptQuote* quote, unsigned char subquote_index, bool notice_only,
                      bool force_npc_voice)
{
    W8MonsterManagerEntry* entry;
    W8Monster* monster;
    int quote_index;
    std::string voice_dir;
    std::string voice_path;
    std::string voice_stem;
    std::string display_text;
    std::string plain_text;
    std::string prefixed_text;

    static const int s_empty_quote_text_ids[2] = {1867, 1868};

    if (quote->dialogue_alert != 0) {
        voice_path = "Data\\Sound\\NPCs\\Dialogue Alert.wav";
        display_text = quote->subquotes[subquote_index];
        if (!notice_only) {
            g_npc_scripting.portrait_message_active = true;
            SetNpcQuoteBubbleVisible(true, display_text, 0, -1, -1);
            HoldNpcSpeech(g_npc_scripting.speech,
                          wiz8::text::grapheme_count(display_text) * 60 + 2000);
            return;
        }
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, GetTextBoxScrollRange());
        return;
    }

    if (g_npc_scripting.session.npc->is_grouped && !force_npc_voice) {
        voice_dir = "PCs";
    } else {
        voice_dir = "NPCs";
    }
    voice_stem = FormatNpcVoiceSoundPath(g_npc_scripting.session.npc);
    voice_path = fmt::format("Data\\Sound\\{}\\{}\\{}_{:03d}", voice_dir, voice_stem, voice_stem,
                             g_npc_scripting.session.current_quote_index);
    if (subquote_index > 0) {
        voice_path.push_back(static_cast<char>(subquote_index + 'a' - 1));
    }
    voice_path += ".wav";
    if (quote->subquotes.empty()) {
        display_text =
            fmt::format(" Missing quote #{:d} for {}.", g_npc_scripting.session.current_quote_index,
                        g_npc_scripting.session.npc->record->source_name);
        plain_text =
            fmt::format(" Missing quote #{:d} for {}.", g_npc_scripting.session.current_quote_index,
                        g_npc_scripting.session.npc->record->source_name);
    } else {
        display_text = fmt::format(" \"{}\"", quote->subquotes[subquote_index]);
        plain_text = quote->subquotes[subquote_index];
    }
    if (plain_text.size() == 0) {
        return;
    }
    if (!notice_only) {
        if (CompareTextIgnoreAsciiCase(plain_text, "EMPTY") == 0 ||
            CompareTextIgnoreAsciiCase(plain_text, "BLANK") == 0) {
            quote_index = g_npc_scripting.session.current_quote_index;
            if (quote_index != 0x67 && quote_index != 0x68 && quote_index != 0x69) {
                return;
            }
            plain_text =
                fmt::sprintf(g_localized_strings[s_empty_quote_text_ids[g_empty_quote_text_index]],
                             g_npc_scripting.session.npc->record->source_name);
            ++g_empty_quote_text_index;
            if (g_empty_quote_text_index == 2) {
                g_empty_quote_text_index = 0;
            }
            DisplayNpcQuote(plain_text, true);
            return;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "UNKNOWN") == 0) {
            quote_index = Random(100) < 50 ? 0x1c : 0x1d;
            QueueNpcScriptLine(quote_index, false, false, false);
            return;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "CLASSIFIED") == 0) {
            QueueNpcScriptLine(0x1e, false, false, false);
            return;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "SOUND") != 0 &&
            !g_npc_scripting.session.npc->is_grouped) {
            if (!gXStatus.fNpcDialogueMode &&
                g_npc_scripting.session.npc->record->voice_script == 0) {
                prefixed_text = fmt::format(
                    "{}: {}", g_npc_scripting.session.npc->record->source_name, display_text);
                display_text = prefixed_text;
            }
            SetNpcQuoteBubbleVisible(true, display_text, 0,
                                     g_npc_scripting.session.current_quote_index, -1, {},
                                     g_npc_scripting.session.npc->partner_index);
        }
        const bool voiced = PlayNpcVoice(g_npc_scripting.speech, voice_path, NpcVoiceEosCallback);
        monster = GetNpcMonster(g_npc_scripting.session.npc);
        if (monster != 0) {
            monster->StartTalking(true);
        }
        if (!voiced) {
            HoldNpcSpeech(g_npc_scripting.speech,
                          wiz8::text::grapheme_count(display_text) * 60 + 2000);
            entry = GetNpcGroupEntry(g_npc_scripting.session.npc);
            if (entry != 0) {
                SetPartyPortraitEventState(g_npc_scripting.session.npc->group_index, true,
                                           g_npc_scripting.session.current_quote_index,
                                           display_text, 1);
                entry->voice_sound_handle = nullptr;
                entry->voice_time_remaining_ms = g_npc_scripting.speech.hold_ms;
            }
        } else {
            entry = GetNpcGroupEntry(g_npc_scripting.session.npc);
            if (entry != 0) {
                SetPartyPortraitEventState(g_npc_scripting.session.npc->group_index, true,
                                           g_npc_scripting.session.current_quote_index,
                                           display_text, 1);
                entry->voice_sound_handle = g_npc_scripting.speech.voice_handle;
                const auto playback_time =
                    wiz8::sound::playback_time(g_npc_scripting.speech.voice_handle)
                        .value_or(wiz8::sound::PlaybackTime{});
                entry->voice_time_remaining_ms = playback_time.total_ms;
                gXStatus.monster_manager_entries[g_npc_scripting.session.npc->group_index]
                    .mouth_gap = LoadMouthGapTrack(voice_path);
                g_status.buffers.XChar[g_npc_scripting.session.npc->group_index]
                    .pending_event_type = g_npc_scripting.session.current_quote_index;
                entry->pending_event_type = g_npc_scripting.session.current_quote_index;
                g_npc_interaction_state->last_notice_npc_kind =
                    g_npc_scripting.session.npc->partner_index;
            } else {
                g_npc_scripting.speech.gap_track = LoadMouthGapTrack(voice_path);
            }
        }
        g_npc_scripting.session.quote_active = true;
        return;
    }
    prefixed_text =
        fmt::format("{}: {}", g_npc_scripting.session.npc->record->source_name, display_text);
    display_text = prefixed_text;
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, GetTextBoxScrollRange());
}

// FUNCTION: WIZ8 0x00525C50
static void FinishNpcVoicePlayback(bool resume_script)
{
    if (g_npc_scripting.portrait_message_active) {
        g_npc_scripting.portrait_message_active = false;
        SetNpcQuoteBubbleVisible(false, {}, 0, -1, -1);
        g_npc_scripting.session.finished_quote_index = g_npc_scripting.session.current_quote_index;
        return;
    }
    if (g_npc_scripting.session.quote_active) {
        StopNpcVoice(g_npc_scripting.speech);
        W8Monster* monster = GetNpcMonster(g_npc_scripting.session.npc);
        if (monster != 0) {
            monster->StopTalking();
        }
        W8MonsterManagerEntry* entry = GetNpcGroupEntry(g_npc_scripting.session.npc);
        if (entry != 0) {
            if (entry->active_character_event == 0) {
                SetPartyPortraitEventState(g_npc_scripting.session.npc->group_index, false, -1, {},
                                           1);
            } else {
                gXStatus.character_event_queue->CompleteActiveEvent(entry->active_character_event);
            }
        }
        g_npc_scripting.session.quote_active = false;
        g_npc_scripting.speech.voice_handle = nullptr;
        g_npc_scripting.session.finished_quote_index = g_npc_scripting.session.current_quote_index;
        if (g_npc_scripting.reply == W8NpcReplyState::none && resume_script) {
            if (g_npc_scripting.session.npc != 0 && g_npc_scripting.session.npc->script_file != 0 &&
                g_npc_scripting.session.current_quote_index <
                    g_npc_scripting.session.npc->script_file->quote_count) {
                W8NpcScriptQuote* quotes = g_npc_scripting.session.npc->script_file->quotes.data();
                SetNpcQuoteBubbleVisible(false, {},
                                         &quotes[g_npc_scripting.session.current_quote_index],
                                         g_npc_scripting.session.current_quote_index, -1);
                return;
            }
            SetNpcQuoteBubbleVisible(false, {}, 0, -1, -1);
        }
    }
}

/* Guarded finish entry point: suppressed outright while the level-4 gate
   flag is set; a non-forced call inside 500 ms of the last script tick is a
   no-op, so the tick path can poll without re-finishing. */
// FUNCTION: WIZ8 0x00525D90
void TryFinishNpcVoicePlayback(bool force)
{
    if (g_status.world_cursor_gate == 0 || g_status.current_level != 4) {
        if (!force && w8_get_ticks() - g_npc_scripting.speech.voice_started_at <= 500) {
            return;
        }
        FinishNpcVoicePlayback(true);
    }
}

void ShowNpcPortraitMessage(std::string_view text, W8QuoteNotice notice)
{
    g_npc_scripting.portrait_message_active = true;
    SetNpcQuoteBubbleVisible(true, text, 0, -1, -1, notice, -1);
    HoldNpcSpeech(g_npc_scripting.speech, ComputePortraitMessageDuration(text));
}

/* Repost the NPC's current quote bubble after a modal sub-dialog closes, or
   clear it when the dialogue NPC/script went away. */
// FUNCTION: WIZ8 0x00529510
void RestoreCurrentNpcQuoteBubble(void)
{
    W8NpcScriptFile* script_file;

    if (g_npc_scripting.session.npc != 0 &&
        (script_file = g_npc_scripting.session.npc->script_file) != 0 &&
        g_npc_scripting.session.current_quote_index < static_cast<int>(script_file->quote_count)) {
        SetNpcQuoteBubbleVisible(
            false, {}, script_file->quotes.data() + g_npc_scripting.session.current_quote_index,
            g_npc_scripting.session.current_quote_index, 0xffffffff);
        return;
    }
    SetNpcQuoteBubbleVisible(false, {}, 0, -1, 0xffffffff);
}

/* Raise the quote bubble over `text` and, when `play_sound` is set, kick off
   the generic start-game click that accompanies a silent text notice. */
// FUNCTION: WIZ8 0x00529570
void DisplayNpcQuote(std::string_view text, bool play_sound)
{
    std::string sound_path;

    ShowNpcScriptNoticeBubble(text);
    g_npc_scripting.session.quote_active = true;
    if (play_sound) {
        sound_path = "Data\\Sound\\misc\\startgame.wav";
        wiz8::sound::play(sound_path);
    }
}
