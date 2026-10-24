#pragma once

#include <functional>
#include <string_view>

#include "wiz8/audio/mouth_gap.h"
#include "wiz8/audio_resources.h"

class Monster;

/* Playback and mouth resources. The dialogue owns the utterance's logical wait. */
struct W8NpcSpeech {
    W8NpcSpeech() = default;
    // Destroying the speech stops its voice, so the voice's completion never runs.
    ~W8NpcSpeech();
    W8NpcSpeech(const W8NpcSpeech&) = delete;
    W8NpcSpeech& operator=(const W8NpcSpeech&) = delete;

    wiz8::sound::Playback voice_handle;
    W8MouthGapTrack gap_track;
};

/* Start `path` at the voice volume; on_finished runs when it ends by itself.
   Returns whether the voice started. */
bool PlayNpcVoice(W8NpcSpeech& speech, std::string_view path, std::function<void()> on_finished);
/* Stop a playing voice without running its on_finished and drop its mouth-gap track. */
void StopNpcVoice(W8NpcSpeech& speech);
/* How long a line of unvoiced text stays on screen. */
int ComputePortraitMessageDuration(std::string_view text);
/* Advance the mouth-gap track and open or close the speaker's mouth. */
void UpdateNpcMouth(W8NpcSpeech& speech, Monster* monster);
