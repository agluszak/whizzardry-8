#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "wiz8/audio_resources.h"

/* A (start, end) millisecond interval on the playback clock while which the
   speaker's mouth is held open. */
struct W8MouthGapRange {
    std::uint32_t start_ms;
    std::uint32_t end_ms;
};

struct W8MouthGapTrack {
    std::vector<W8MouthGapRange> ranges;
    bool mouth_open = false;
};

/* Load the ranges of <voice dir>\GAP\<voice name>.gap; empty when it has none. */
W8MouthGapTrack LoadMouthGapTrack(std::string_view voice_path);
/* Set mouth_open while the playback position is strictly inside a range. */
void UpdateMouthGapTrack(wiz8::sound::Playback playback, W8MouthGapTrack& track);
