#include "wiz8/mouth_gap.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "asset_paths.h"
#include "audio_resources.h"
#include "soundman.h"
#include "wiz8/filesystem.h"

/* Voice samples ship a companion file at <voice dir>\GAP\<voice name>.gap
   holding little-endian int32 (start, end) millisecond pairs. */

W8MouthGapTrack LoadMouthGapTrack(std::string_view voice_path)
{
    const auto separator = voice_path.find_last_of("\\/");
    const auto directory = separator == std::string_view::npos
                               ? std::string_view{}
                               : voice_path.substr(0, separator + 1);
    auto filename = voice_path.substr(separator == std::string_view::npos ? 0 : separator + 1);
    if (const auto dot = filename.find_last_of('.'); dot != std::string_view::npos)
        filename = filename.substr(0, dot);
    const auto gap_path = std::string(directory) + "GAP\\" + std::string(filename) + ".gap";

    W8MouthGapTrack track;
    std::unique_ptr<wiz8::File> file;
    try {
        file = wiz8::open_file(wiz8::asset_path(gap_path), wiz8::OpenMode::read);
    } catch (const std::exception&) {
        return track;
    }
    while (file->size() - file->tell() >= 8) {
        W8MouthGapRange range;
        file->read_exact(&range.start_ms, sizeof(range.start_ms));
        file->read_exact(&range.end_ms, sizeof(range.end_ms));
        track.ranges.push_back(range);
    }
    return track;
}

void UpdateMouthGapTrack(wiz8::sound::Playback playback, W8MouthGapTrack& track)
{
    if (track.ranges.empty()) {
        track.mouth_open = false;
        return;
    }
    const auto position = wiz8::sound::elapsed_ms(playback);
    const auto range = std::ranges::find_if(
        track.ranges, [&](const W8MouthGapRange& r) { return position <= r.end_ms; });
    track.mouth_open =
        range != track.ranges.end() && range->start_ms < position && position < range->end_ms;
}
