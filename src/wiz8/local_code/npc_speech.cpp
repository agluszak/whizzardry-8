#include "wiz8/npc_speech.h"

#include <functional>
#include <string_view>
#include <utility>

#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/entities/monster.h"
#include "wiz8/unicode.h"

bool PlayNpcVoice(W8NpcSpeech& speech, std::string_view path, std::function<void()> on_finished)
{
    wiz8::sound::stop(speech.voice_handle);
    speech.voice_handle = wiz8::sound::play(path, {.channel = wiz8::sound::VolumeChannel::voice,
                                                   .gain_scale = 0.7f,
                                                   .on_finished = std::move(on_finished)});
    return speech.voice_handle != nullptr;
}

W8NpcSpeech::~W8NpcSpeech()
{
    StopNpcVoice(*this);
}

void StopNpcVoice(W8NpcSpeech& speech)
{
    wiz8::sound::stop(speech.voice_handle);
    speech.voice_handle.reset();
    speech.gap_track = {};
}

int ComputePortraitMessageDuration(std::string_view text)
{
    return static_cast<int>(wiz8::text::to_utf16(text).size() * 0x3c + 2000);
}

void UpdateNpcMouth(W8NpcSpeech& speech, Monster* monster)
{
    UpdateMouthGapTrack(speech.voice_handle, speech.gap_track);
    if (monster != 0) {
        monster->mouth_open = speech.gap_track.mouth_open;
    }
}
