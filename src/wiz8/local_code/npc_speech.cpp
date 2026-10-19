#include "wiz8/npc_speech.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include "compat/native.h"
#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/entities/monster.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/unicode.h"

bool PlayNpcVoice(W8NpcSpeech& speech, std::string_view path, std::function<void()> on_finished)
{
    wiz8::sound::stop(speech.voice_handle);
    speech.voice_handle =
        wiz8::sound::play(path, {.volume = std::uint32_t(g_settings.voice_volume * 70 / 100),
                                 .on_finished = std::move(on_finished)});
    speech.voice_started_at = w8_get_ticks();
    speech.voice_playing = speech.voice_handle != nullptr;
    return speech.voice_playing;
}

void StopNpcVoice(W8NpcSpeech& speech)
{
    speech.voice_playing = false;
    wiz8::sound::stop(speech.voice_handle);
    speech.voice_handle.reset();
    speech.gap_track = {};
}

int ComputePortraitMessageDuration(std::string_view text)
{
    return static_cast<int>(wiz8::text::to_utf16(text).size() * 0x3c + 2000);
}

void HoldNpcSpeech(W8NpcSpeech& speech, std::uint64_t hold_ms)
{
    speech.hold_ms = hold_ms;
    speech.hold_started_at = w8_get_ticks();
}

bool IsNpcSpeechHoldOver(const W8NpcSpeech& speech)
{
    return speech.hold_ms < w8_get_ticks() - speech.hold_started_at;
}

void UpdateNpcMouth(W8NpcSpeech& speech, Monster* monster)
{
    UpdateMouthGapTrack(speech.voice_handle, speech.gap_track);
    if (monster != 0) {
        monster->mouth_open = speech.gap_track.mouth_open;
    }
}
