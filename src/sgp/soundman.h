/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09, 2026-10-11.
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
namespace wiz8::sound
{
// Mixer tags; retail used priority 0xfffffff0 for every ambient sound.
enum class Group : std::uint8_t { effects, ambient, music };

struct PlayOptions
{
    // 0..127; unset uses the default effects volume.
    std::optional<std::uint32_t> volume{};
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
    // Dispatched from service() or the next playback start, never the mixer thread.
    std::function<void()> on_finished{};
};

struct PlaybackTime
{
    std::uint32_t total_ms;
    std::uint32_t current_ms;
};

// A disabled service accepts every call and plays nothing.
bool initialize(bool enabled);
void shutdown();
// Dispatches completion callbacks for tracks the mixer has finished.
void service();

// Retail never caches clips this large; play() rejects them, stream() accepts them.
inline constexpr std::uint32_t kMaxCachedClipBytes = 0xc8000;

SoundClip load(std::string_view path);
SoundPlayback play(std::string_view path, PlayOptions options = {});
SoundPlayback play(const SoundClip& clip, PlayOptions options = {});
SoundPlayback stream(std::string_view path, PlayOptions options = {});
// Stops effects and ambient playback and drops the clip cache; music continues.
void release_cache();

bool is_playing(const SoundPlayback& playback);
bool is_file_playing(std::string_view path);
// Returns whether the playback was audible; a fade keeps it playing until silent.
bool stop(const SoundPlayback& playback, std::uint32_t fade_out_ms = 0);
bool stop_group(Group group);

void set_default_volume(std::uint32_t volume);
bool set_volume(const SoundPlayback& playback, std::uint32_t volume);
std::optional<std::uint32_t> volume(const SoundPlayback& playback);
void set_position(const SoundPlayback& playback, glm::vec3 position);

// Wall-clock time since the playback started, as retail mouth-gap tracks expect.
std::uint32_t elapsed_ms(const SoundPlayback& playback);
std::optional<PlaybackTime> playback_time(const SoundPlayback& playback);
} // namespace wiz8::sound
