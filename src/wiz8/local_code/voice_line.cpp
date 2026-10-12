#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include "soundman.h"
#include "wiz8/voice_line.h"

bool W8VoiceLine::Play(std::string_view path, std::uint32_t volume,
                       std::function<void()> on_finished)
{
    playback_ = wiz8::sound::play(
        path, {.volume = volume, .on_finished = [this, on_finished = std::move(on_finished)] {
                   if (!stopping_) {
                       on_finished();
                   }
               }});
    return playback_ != nullptr;
}

void W8VoiceLine::Stop()
{
    if (playback_ == nullptr) {
        return;
    }
    stopping_ = true;
    wiz8::sound::stop(playback_);
    stopping_ = false;
    playback_ = nullptr;
}
