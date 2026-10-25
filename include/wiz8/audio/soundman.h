/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09, 2026-10-11, 2026-10-16.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include <glm/ext/vector_float3.hpp>

#include "wiz8/audio_resources.h"

/* Sound playback over SDL3_mixer. Clips are cached by name; every playback owns
   one mixer track. Game policy (ambient scheduling, distance volume, music
   transitions) lives with its callers. */
namespace wiz8::sound {
// Playback groups allow independent stopping of effects, ambient sounds and music.
enum class Group : std::uint8_t { effects, ambient, music };

enum class VolumeChannel : std::uint8_t { effects, voice, footsteps, music, unscaled };

// Positional updates historically round after attenuation; ambient fades round before it.
enum class VolumeRounding : std::uint8_t { before_attenuation, after_attenuation };

struct PlayOptions {
    // Authored gain, independent of the user setting. Unset means 127.
    std::optional<std::uint32_t> volume{};
    VolumeChannel channel = VolumeChannel::effects;
    float gain_scale = 1.0f;
    // Total plays; 0 repeats until stopped.
    std::uint32_t loops = 1;
    // Sample frames per second; unset keeps the clip's own rate.
    std::optional<std::uint32_t> rate{};
    // Random +/- percentage applied to the rate.
    std::uint32_t pitch_bend_percent = 0;
    Group group = Group::effects;
    // Listener-relative position; the listener faces -Z with +Y up.
    std::optional<glm::vec3> position{};
    std::uint32_t fade_in_ms = 0;
    // Dispatched on the game thread by service().
    std::function<void()> on_finished{};
};

struct PlaybackTime {
    std::uint32_t total_ms;
    std::uint32_t current_ms;
};

// A disabled service accepts every call and plays nothing.
bool initialize(bool enabled);
void shutdown();
// Dispatches completion callbacks for tracks the mixer has finished.
void service();

// play() streams clips at or above this size instead of adding them to the cache.
inline constexpr std::uint32_t kMaxCachedClipBytes = 0xc8000;

Clip load(std::string_view path);
Playback play(std::string_view path, PlayOptions options = {});
Playback play(const Clip& clip, PlayOptions options = {});
Playback stream(std::string_view path, PlayOptions options = {});
// Stops effects and ambient playback and drops the clip cache; music continues.
void release_cache();

bool is_playing(const Playback& playback);
// Terminal, serviced completion. Paused and not-yet-serviced tracks still hold a wait.
bool finished(const Playback& playback);
bool is_file_playing(std::string_view path);
// Stops playback without calling on_finished (including deferred completion).
// Returns whether it was audible; a fade keeps it playing until silent.
bool stop(const Playback& playback, std::uint32_t fade_out_ms = 0);
bool stop_group(Group group);

void set_channel_volume(VolumeChannel channel, std::uint32_t volume);
bool set_volume(const Playback& playback, std::uint32_t volume, float gain_scale = 1.0f,
                VolumeRounding rounding = VolumeRounding::before_attenuation);
std::optional<std::uint32_t> volume(const Playback& playback);
void set_position(const Playback& playback, glm::vec3 position);

// Wall-clock time since the playback started, as retail mouth-gap tracks expect.
std::uint32_t elapsed_ms(const Playback& playback);
std::optional<PlaybackTime> playback_time(const Playback& playback);
} // namespace wiz8::sound
