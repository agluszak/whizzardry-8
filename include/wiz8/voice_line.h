#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "wiz8/audio_resources.h"

/* A spoken line: NPC dialogue and character events both speak through one.
   Its completion handler runs only when the line ends by itself; Stop and
   destruction end the playback without running it. The handler borrows the
   line, so a line stays where it was constructed. */
class W8VoiceLine {
public:
    W8VoiceLine() = default;
    W8VoiceLine(const W8VoiceLine&) = delete;
    W8VoiceLine& operator=(const W8VoiceLine&) = delete;
    ~W8VoiceLine()
    {
        Stop();
    }

    /* Start `path` at `volume` (0-127). Returns whether it started. */
    bool Play(std::string_view path, std::uint32_t volume, std::function<void()> on_finished);
    void Stop();
    const wiz8::sound::Playback& playback() const
    {
        return playback_;
    }

private:
    wiz8::sound::Playback playback_;
    bool stopping_ = false;
};
