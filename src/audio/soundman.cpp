/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09, 2026-10-11, 2026-10-16.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "wiz8/audio/soundman.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "asset_paths.h"
#include "wiz8/audio/native_audio.h"
#include "wiz8/audio_resources.h"
#include "wiz8/filesystem.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"

namespace wiz8::sound {
struct AudioClip {
    std::string name;
    std::uint32_t bytes = 0;
    std::uint32_t uses = 0;
    std::shared_ptr<MIX_Mixer> owner;
    w8_native::Audio audio;
};

struct AudioPlayback {
    std::string resource;
    std::shared_ptr<MIX_Mixer> owner;
    // Track destruction synchronizes with mixing before closing its input.
    Clip clip;
    w8_native::AudioInput input;
    w8_native::Track track;
    std::function<void()> on_finished;
    Group group = Group::effects;
    VolumeChannel channel = VolumeChannel::effects;
    std::uint32_t source_volume = 127;
    float gain_scale = 1.0f;
    std::uint64_t started_at = 0;
    Sint64 duration = -1;
    bool positional = false;
};

namespace {
constexpr std::uint32_t kCacheBudgetBytes = 8048 * 1024;

struct Service {
    bool enabled = false;
    std::array<std::uint32_t, 5> channel_volume{127, 127, 127, 127, 127};
    std::uint32_t cached_bytes = 0;
    std::vector<Clip> cache;
    // Retains fire-and-forget playback until completion; callers may share the same objects.
    std::vector<Playback> active;
} service_state;

bool live(const Playback& playback)
{
    return service_state.enabled && playback && playback->track;
}

void finish(const Playback& playback)
{
    if (!playback || !playback->track) return;
    auto callback = std::exchange(playback->on_finished, {});
    playback->track.reset();
    playback->input.reset();
    playback->clip.reset();
    playback->owner.reset();
    std::erase(service_state.active, playback);
    // Retained objects become inert before callbacks, and never identify later playback.
    if (callback) callback();
}

std::string resolve_path(std::string_view path)
{
    if (path.empty()) return {};
    std::string filename(path);
    SDL_strupr(filename.data());
    if (const auto status = wiz8::file_status(wiz8::asset_path(filename));
        status && status->info.type == SDL_PATHTYPE_FILE)
        return filename;
    if (filename.ends_with(".WAV"))
        filename.replace(filename.size() - 4, 4, ".MP3");
    else if (filename.ends_with(".MP3"))
        filename.replace(filename.size() - 4, 4, ".WAV");
    return filename;
}

bool set_gain(MIX_Track* track, std::uint32_t volume)
{
    return MIX_SetTrackGain(track, float(std::min(volume, 127u)) / 127);
}

// The listener sits at the origin facing -Z, so +X is its right ear. Keep the old
// backend's disabled distance model and 20% opposite-ear floor; callers apply falloff.
bool apply_spatial(MIX_Track* track, const glm::vec3& position)
{
    const float distance = glm::length(position);
    const float pan = distance > 0 ? std::clamp(position.x / distance, -1.0f, 1.0f) : 0;
    const MIX_StereoGains gains{distance > 0 ? std::max(0.2f, (1 - pan) * 0.5f) : 1,
                                distance > 0 ? std::max(0.2f, (1 + pan) * 0.5f) : 1};
    return MIX_SetTrackStereo(track, &gains);
}

Playback create_playback(Group group)
{
    if (!service_state.enabled) return {};
    auto playback = std::make_shared<AudioPlayback>();
    playback->group = group;
    playback->owner = w8_native::retain_audio_mixer();
    playback->track.reset(MIX_CreateTrack(playback->owner.get()));
    if (!playback->track) return {};
    return playback;
}

Playback start(Playback playback, Clip clip, PlayOptions options)
{
    auto& track = *playback->track;
    const auto natural_rate = MIX_TrackMSToFrames(&track, 1000);
    if (natural_rate <= 0) return {};
    auto rate = options.rate.value_or(std::uint32_t(natural_rate));
    if (options.pitch_bend_percent != 0) {
        const auto bend = std::uint64_t(rate) * options.pitch_bend_percent / 100;
        if (bend <= UINT32_MAX / 2)
            rate = std::uint32_t(std::clamp<std::int64_t>(
                std::int64_t(rate) +
                    std::int64_t(PresentationRng().below(std::uint32_t(bend * 2))) -
                    std::int64_t(bend),
                1, UINT32_MAX));
    }
    playback->positional = options.position.has_value();
    playback->channel = options.channel;
    playback->source_volume = options.volume.value_or(127);
    playback->gain_scale = options.gain_scale;
    const auto properties = SDL_CreateProperties();
    bool ok = properties && rate > 0 &&
              set_volume(playback, playback->source_volume, playback->gain_scale) &&
              MIX_SetTrackFrequencyRatio(
                  &track, std::clamp(float(rate) / float(natural_rate), 0.01f, 100.0f)) &&
              (!options.position || apply_spatial(&track, *options.position)) &&
              SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER,
                                    options.loops == 0 ? -1 : Sint64(options.loops) - 1) &&
              (options.fade_in_ms == 0 ||
               SDL_SetNumberProperty(properties, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER,
                                     options.fade_in_ms)) &&
              MIX_PlayTrack(&track, properties);
    SDL_DestroyProperties(properties);
    if (!ok) return {};
    if (clip) ++clip->uses;
    playback->clip = std::move(clip);
    playback->started_at = MonotonicMilliseconds();
    playback->on_finished = std::move(options.on_finished);
    service_state.active.push_back(playback);
    return playback;
}

bool evict_one()
{
    auto& cache = service_state.cache;
    auto victim = cache.end();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        // Active tracks and ambient emitters own their clips directly.
        if (it->use_count() != 1) continue;
        if (victim == cache.end() || (*it)->uses < (*victim)->uses) victim = it;
    }
    if (victim == cache.end()) return false;
    service_state.cached_bytes -= (*victim)->bytes;
    cache.erase(victim);
    return true;
}

Clip load_resolved(const std::string& filename)
{
    for (const auto& clip : service_state.cache)
        if (SDL_strcasecmp(clip->name.c_str(), filename.c_str()) == 0) return clip;
    auto input = w8_native::open_audio_input(filename.c_str());
    if (!input) return {};
    const Sint64 size = SDL_GetIOSize(input.get());
    if (size <= 0 || size > kCacheBudgetBytes) return {};
    while (std::uint64_t(size) + service_state.cached_bytes > kCacheBudgetBytes)
        if (!evict_one()) return {};
    w8_native::Audio audio(MIX_LoadAudio_IO(w8_native::audio_mixer(), input.get(), false, false));
    if (!audio) return {};
    auto clip = std::make_shared<AudioClip>();
    clip->name = filename;
    clip->bytes = std::uint32_t(size);
    clip->owner = w8_native::retain_audio_mixer();
    clip->audio = std::move(audio);
    service_state.cached_bytes += clip->bytes;
    service_state.cache.push_back(clip);
    return clip;
}

Playback stream_resolved(const std::string& filename, PlayOptions options)
{
    auto playback = create_playback(options.group);
    if (!playback) return {};
    playback->resource = filename;
    playback->input = w8_native::open_audio_input(filename.c_str());
    if (!playback->input) return {};
    std::unique_ptr<MIX_AudioDecoder, decltype(&MIX_DestroyAudioDecoder)> decoder(
        MIX_CreateAudioDecoder_IO(playback->input.get(), false, 0), MIX_DestroyAudioDecoder);
    if (!decoder) return {};
    playback->duration = SDL_GetNumberProperty(MIX_GetAudioDecoderProperties(decoder.get()),
                                               MIX_PROP_METADATA_DURATION_FRAMES_NUMBER, -1);
    decoder.reset();
    if (SDL_SeekIO(playback->input.get(), 0, SDL_IO_SEEK_SET) != 0 ||
        !MIX_SetTrackIOStream(playback->track.get(), playback->input.get(), false))
        return {};
    return start(std::move(playback), {}, std::move(options));
}

std::optional<std::uint32_t> file_size(const std::string& filename)
{
    const auto status = wiz8::file_status(wiz8::asset_path(filename));
    if (!status || status->info.type != SDL_PATHTYPE_FILE) return std::nullopt;
    return std::uint32_t(std::min<Uint64>(status->info.size, UINT32_MAX));
}
} // namespace

bool initialize(bool enabled)
{
    shutdown();
    service_state.enabled = enabled && w8_native::initialize_audio();
    return true;
}

void shutdown()
{
    service_state.enabled = false;
    const auto stopped = std::exchange(service_state.active, {});
    for (const auto& playback : stopped) {
        playback->on_finished = {};
        finish(playback);
    }
    service_state.cache.clear();
    service_state.cached_bytes = 0;
    w8_native::shutdown_audio();
}

void service()
{
    if (!service_state.enabled) return;
    const auto playing = service_state.active;
    for (const auto& playback : playing)
        if (playback->track && !MIX_TrackPlaying(playback->track.get()) &&
            !MIX_TrackPaused(playback->track.get()))
            finish(playback);
}

Clip load(std::string_view path)
{
    if (!service_state.enabled) return {};
    const auto filename = resolve_path(path);
    return filename.empty() ? Clip{} : load_resolved(filename);
}

Playback play(std::string_view path, PlayOptions options)
{
    if (!service_state.enabled) return {};
    const auto filename = resolve_path(path);
    if (filename.empty()) return {};
    // Large clips are streamed instead of occupying the cache.
    if (const auto size = file_size(filename); size && *size >= kMaxCachedClipBytes)
        return stream_resolved(filename, std::move(options));
    auto clip = load_resolved(filename);
    if (!clip) {
        // Retail calls name a few clips missing from the shipped data (beep2.wav).
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sound: failed loading %s", filename.c_str());
        return {};
    }
    return play(clip, std::move(options));
}

Playback play(const Clip& clip, PlayOptions options)
{
    if (!service_state.enabled || !clip || !clip->audio) return {};
    auto playback = create_playback(options.group);
    if (!playback || !MIX_SetTrackAudio(playback->track.get(), clip->audio.get())) return {};
    playback->resource = clip->name;
    playback->duration = MIX_GetAudioDuration(clip->audio.get());
    return start(std::move(playback), clip, std::move(options));
}

Playback stream(std::string_view path, PlayOptions options)
{
    if (!service_state.enabled) return {};
    const auto filename = resolve_path(path);
    return filename.empty() ? Playback{} : stream_resolved(filename, std::move(options));
}

void release_cache()
{
    stop_group(Group::effects);
    stop_group(Group::ambient);
    service_state.cache.clear();
    service_state.cached_bytes = 0;
}

bool is_playing(const Playback& playback)
{
    return live(playback) && MIX_TrackPlaying(playback->track.get());
}

bool is_file_playing(std::string_view path)
{
    const auto resource = resolve_path(path);
    return std::ranges::any_of(service_state.active, [&](const Playback& playback) {
        return is_playing(playback) && playback->resource == resource;
    });
}

bool stop(const Playback& playback, std::uint32_t fade_out_ms)
{
    const bool was_playing = is_playing(playback);
    if (playback) playback->on_finished = {};
    if (was_playing && fade_out_ms != 0) {
        auto* track = playback->track.get();
        return MIX_StopTrack(track, MIX_TrackMSToFrames(track, fade_out_ms));
    }
    finish(playback);
    return was_playing;
}

bool stop_group(Group group)
{
    const auto batch = service_state.active;
    bool playing = false;
    for (const auto& playback : batch) {
        if (playback->group == group) playing |= stop(playback);
    }
    return playing;
}

void set_channel_volume(VolumeChannel channel, std::uint32_t volume)
{
    if (channel == VolumeChannel::unscaled) return;
    service_state.channel_volume[static_cast<std::size_t>(channel)] = std::min(volume, 127u);
    for (const auto& playback : service_state.active)
        if (playback->channel == channel)
            set_volume(playback, playback->source_volume, playback->gain_scale);
}

bool set_volume(const Playback& playback, std::uint32_t volume, float gain_scale)
{
    if (!live(playback)) return false;
    playback->source_volume = volume;
    playback->gain_scale = gain_scale;
    const auto setting = service_state.channel_volume[static_cast<std::size_t>(playback->channel)];
    const auto scaled = static_cast<std::uint32_t>((std::uint64_t(volume) * setting) / 127);
    return set_gain(playback->track.get(), static_cast<std::uint32_t>(scaled * gain_scale));
}

std::optional<std::uint32_t> volume(const Playback& playback)
{
    if (!live(playback)) return std::nullopt;
    return std::uint32_t(MIX_GetTrackGain(playback->track.get()) * 127 + 0.5f);
}

void set_position(const Playback& playback, glm::vec3 position)
{
    if (!live(playback) || !playback->positional) return;
    apply_spatial(playback->track.get(), position);
}

std::uint32_t elapsed_ms(const Playback& playback)
{
    return live(playback) ? std::uint32_t(MonotonicMilliseconds() - playback->started_at) : 0;
}

std::optional<PlaybackTime> playback_time(const Playback& playback)
{
    if (!live(playback)) return std::nullopt;
    auto* track = playback->track.get();
    return PlaybackTime{
        std::uint32_t(std::max<Sint64>(0, MIX_TrackFramesToMS(track, playback->duration))),
        std::uint32_t(
            std::max<Sint64>(0, MIX_TrackFramesToMS(track, MIX_GetTrackPlaybackPosition(track))))};
}
} // namespace wiz8::sound
