#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "wiz8/audio_resources.h"
#include "wiz8/mouth_gap.h"

class W8Monster;

/* The NPC voice line being played, the hold that keeps a quote or portrait
   message on screen, and the mouth-gap track of an ungrouped speaker. */
struct W8NpcSpeech {
    bool voice_playing;
    wiz8::sound::Playback voice_handle;
    uint64_t voice_started_at;
    unsigned int hold_ms;
    uint64_t hold_started_at;
    W8MouthGapTrack gap_track;
    /* Set while StopNpcVoice stops the voice, so its on_finished is skipped. */
    bool stopping;
};

/* Start `path` at the voice volume; on_finished runs when it ends by itself.
   Returns whether the voice started. */
bool PlayNpcVoice(W8NpcSpeech& speech, std::string_view path, std::function<void()> on_finished);
/* Stop a playing voice without running its on_finished and drop its mouth-gap track. */
void StopNpcVoice(W8NpcSpeech& speech);
/* How long a line of unvoiced text stays on screen. */
int ComputePortraitMessageDuration(std::string_view text);
void HoldNpcSpeech(W8NpcSpeech& speech, unsigned int hold_ms);
bool IsNpcSpeechHoldOver(const W8NpcSpeech& speech);
/* Advance the mouth-gap track and open or close the speaker's mouth. */
void UpdateNpcMouth(W8NpcSpeech& speech, W8Monster* monster);
