#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio_resources.h"

class Monster;

/* The NPC voice line being played, the hold that keeps a quote or portrait
   message on screen, and the mouth-gap track of an ungrouped speaker. */
struct W8NpcSpeech {
    wiz8::sound::Playback voice_handle;
    /* The presentation is voiced; a notice bubble clears it to hold silently. */
    bool voice_playing = false;
    uint64_t voice_started_at = 0;
    std::uint64_t hold_ms = 0;
    uint64_t hold_started_at = 0;
    W8MouthGapTrack gap_track;
};

/* Start `path` at the voice volume; on_finished runs when it ends by itself.
   Returns whether the voice started. */
bool PlayNpcVoice(W8NpcSpeech& speech, std::string_view path, std::function<void()> on_finished);
/* Stop a playing voice without running its on_finished and drop its mouth-gap track. */
void StopNpcVoice(W8NpcSpeech& speech);
/* How long a line of unvoiced text stays on screen. */
int ComputePortraitMessageDuration(std::string_view text);
void HoldNpcSpeech(W8NpcSpeech& speech, std::uint64_t hold_ms);
bool IsNpcSpeechHoldOver(const W8NpcSpeech& speech);
/* Advance the mouth-gap track and open or close the speaker's mouth. */
void UpdateNpcMouth(W8NpcSpeech& speech, Monster* monster);
