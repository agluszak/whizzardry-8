#include "wiz8/npc_speech.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include "compat/native.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/mouth_gap.h"
#include "wiz8/unicode.h"

bool PlayNpcVoice(W8NpcSpeech& speech, std::string_view path, std::function<void()> on_finished)
{
    speech.voice_playing = speech.voice.Play(
        path, std::uint32_t(g_settings.voice_volume * 70 / 100), std::move(on_finished));
    speech.voice_started_at = w8_get_ticks();
    return speech.voice_playing;
}

void StopNpcVoice(W8NpcSpeech& speech)
{
    if (!speech.voice_playing) {
        return;
    }
    speech.voice_playing = false;
    speech.voice.Stop();
    speech.gap_track = {};
}

void ResetNpcSpeech(W8NpcSpeech& speech)
{
    StopNpcVoice(speech);
    speech.voice.Stop();
    speech.voice_started_at = 0;
    speech.hold_ms = 0;
    speech.hold_started_at = 0;
    speech.gap_track = {};
}

// FUNCTION: WIZ8 0x00525E60
int ComputePortraitMessageDuration(std::string_view text)
{
    return static_cast<int>(wiz8::text::to_utf16(text).size() * 0x3c + 2000);
}

void HoldNpcSpeech(W8NpcSpeech& speech, unsigned int hold_ms)
{
    speech.hold_ms = hold_ms;
    speech.hold_started_at = w8_get_ticks();
}

bool IsNpcSpeechHoldOver(const W8NpcSpeech& speech)
{
    return speech.hold_ms < w8_get_ticks() - speech.hold_started_at;
}

void UpdateNpcMouth(W8NpcSpeech& speech, W8Monster* monster)
{
    UpdateMouthGapTrack(speech.voice.playback(), speech.gap_track);
    if (monster != 0) {
        monster->mouth_open = speech.gap_track.mouth_open;
    }
}
