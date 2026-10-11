#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "wiz8/mouth_gap.h"
#include "wiz8/filesystem.h"
#include "soundman.h"
#include "audio_resources.h"

/* Engine Code\gap.c (demo source-path string, line 0xd9 inside the update's
   mouth-open branch; retail sits exactly between IList.cpp's tail and
   Bink.cpp's first emission). Voice samples ship a companion file at
   <voice dir>\GAP\<voice name>.gap holding little-endian int32 (start, end)
   millisecond pairs on the SoundGetPosition clock. While playback sits
   strictly inside a range the track reports the mouth held open; between
   ranges the consumers animate freely. */

// FUNCTION: WIZ8 0x005E2D10
void LoadMouthGapTrack(std::string_view path, W8MouthGapTrack* track)
{
    unsigned int start;
    unsigned int end;
    std::unique_ptr<wiz8::File> file;
    W8MouthGapRange* previous = 0;
    W8MouthGapRange* range;

    FreeMouthGapTrack(track);
    track->range_count = 0;
    track->unused = 0;
    track->head = 0;
    track->cursor = 0;
    track->mouth_open = 0;

    const auto separator = path.find_last_of("\\/");
    const auto directory = separator == std::string_view::npos ? std::string_view{} : path.substr(0, separator + 1);
    auto filename = path.substr(separator == std::string_view::npos ? 0 : separator + 1);
    if (const auto dot = filename.find_last_of('.'); dot != std::string_view::npos)
        filename = filename.substr(0, dot);
    const auto gap_path = std::string(directory) + "GAP\\" + std::string(filename) + ".gap";

    file = [&]() { try { return wiz8::open_file(gap_path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (file != 0) {
        while (file->size() - file->tell() >= 8) {
            file->read_exact(&start, 4);
            file->read_exact(&end, 4);
            range = (W8MouthGapRange*)malloc(sizeof(W8MouthGapRange));
            if (previous != 0) {
                previous->next = range;
            } else {
                track->head = track->cursor = range;
            }
            ++track->range_count;
            range->next = 0;
            range->start_ms = start;
            range->end_ms = end;
            previous = range;
        }
        track->mouth_open = 0;
        track->unused = 0;
        file.reset();
    }
}

// FUNCTION: WIZ8 0x005E2EF0
void FreeMouthGapTrack(W8MouthGapTrack* track)
{
    W8MouthGapRange* range = track->head;

    if (range != 0) {
        W8MouthGapRange* next;
        for (next = range->next; next != 0; next = next->next) {
            free(range);
            range = next;
        }
        free(range);
    }
    track->head = 0;
    track->cursor = 0;
    track->range_count = 0;
}

// FUNCTION: WIZ8 0x005E2F40
void UpdateMouthGapTrack(SoundPlayback playback, W8MouthGapTrack* track)
{
    if (track != 0) {
        if (track->range_count > 0) {
            unsigned int position = SoundGetPosition(playback);
            W8MouthGapRange* range = track->head;
            while (position > range->end_ms) {
                range = range->next;
                if (range == 0) {
                    goto clear;
                }
            }
            if (range->start_ms < position && position < range->end_ms) {
                track->mouth_open = 1;
                return;
            }
        }
    clear:
        track->mouth_open = 0;
    }
}
