/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <SDL3/SDL_log.h>
#include <wiz8/native_audio.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include "soundman.h"
#include "wiz8/filesystem.h"
#include "Types.h"
#include "random.h"
#include "compat/compiler.h"
#include "compat/native.h"

#define SOUND_DEFAULT_MEMORY (8048 * 1024) // default memory limit
#define SOUND_DEFAULT_THRESH (256 * 8024)  // size for sample to be double-buffered

#define SOUND_PARMS_DEFAULT 0xffffffff

struct AudioClip
{
    std::string pName;
    UINT32 uiSize = 0, uiFlags = 0, uiSpeed = 0, uiCacheHits = 0;
    std::shared_ptr<MIX_Mixer> owner;
    w8_native::Audio audio;
    uint64_t uiTimeNext = 0;
    UINT32 uiTimeMin = 0, uiTimeMax = 0;
    UINT32 uiSpeedMin = 0, uiSpeedMax = 0, uiVolMin = 0, uiVolMax = 0;
    UINT32 uiPanMin = 0, uiPanMax = 0, uiPriority = 0, uiInstances = 0, uiMaxInstances = 0;
};
struct AudioPlayback
{
    std::shared_ptr<MIX_Mixer> owner;
    // Track destruction synchronizes with mixing before closing its input.
    SoundClip clip;
    w8_native::AudioInput input;
    w8_native::Track track;
    UINT32 uiPriority = PRIORITY_MAX;
    void (*EOSCallback)(void*) = nullptr;
    void* pCallbackData = nullptr;
    uint64_t uiTimeStamp = 0, uiFadeTime = 0;
    UINT32 uiFadeVolume = 0, uiFadeRate = 0;
    UINT32 volume = 127, playbackRate = 44100;
    Sint64 duration = -1;
    BOOLEAN fMusic = FALSE, fStopAtZero = TRUE;
    bool spatial = false;
    SOUND3DPOS position{};
};
struct Listener
{
    FLOAT x = 0, y = 0, z = 0;
    FLOAT faceX = 0, faceY = 0, faceZ = -1, upX = 0, upY = 1, upZ = 0;
    FLOAT velocityX = 0, velocityY = 0, velocityZ = 0;
} listener;

SoundClip SoundLoadSample(STR pFilename);
SoundClip SoundGetCached(STR pFilename);
SoundClip SoundLoadDisk(STR pFilename);

BOOLEAN SoundInitHardware(void);
template<class Parameters>
static SoundPlayback StartSample(SoundClip sample, const Parameters* parameters, const SOUND3DPOS* position = nullptr);
static SoundPlayback SoundStartStream(STR filename, SOUNDPARMS* parameters);
BOOLEAN SoundPlayStreamed(STR pFilename);
BOOLEAN SoundCleanCache(void);
static void FinishPlayback(SoundPlayback playback);
static BOOLEAN SetGain(AudioPlayback& channel, UINT32 volume);

// GLOBAL: WIZ8 0x005ff644
UINT32 guiSoundDefaultVolume = 127;
// GLOBAL: WIZ8 0x005ff648
UINT32 guiSoundMemoryLimit = SOUND_DEFAULT_MEMORY; // Maximum memory used for sounds
// GLOBAL: WIZ8 0x00650e4c
UINT32 guiSoundMemoryUsed = 0; // Memory currently in use
// GLOBAL: WIZ8 0x005ff64c
UINT32 guiSoundCacheThreshold = SOUND_DEFAULT_THRESH; // Double-buffered threshold

// GLOBAL: WIZ8 0x00650e50
BOOLEAN fSoundSystemInit = FALSE; // Startup called T/F
// GLOBAL: WIZ8 0x005ff651
BOOLEAN gfEnableStartup = TRUE; // Allow hardware to starup

// GLOBAL: WIZ8 0x006e4aa0
static std::vector<SoundClip> sampleCache;
// GLOBAL: WIZ8 0x006e4120
// Retain fire-and-forget playback until completion; callers own the same objects.
static std::vector<SoundPlayback> activePlayback;

static SoundPlayback CreatePlayback()
{
    const auto playing = activePlayback;
    for (const auto& playback : playing)
        if (!SoundIsPlaying(playback)) FinishPlayback(playback);
    if (!fSoundSystemInit) return {};
    auto playback = std::make_shared<AudioPlayback>();
    playback->owner = w8_native::retain_audio_mixer();
    playback->track.reset(MIX_CreateTrack(playback->owner.get()));
    return playback->track ? playback : SoundPlayback{};
}

static std::string ResolveSoundPath(std::string_view path)
{
    if (path.empty()) return {};
    std::string filename(path);
    strupr(filename.data());
    if ([&]() { const auto status = wiz8::file_status(filename.data()); return status && status->info.type == SDL_PATHTYPE_FILE; }()) return filename;
    if (filename.ends_with(".WAV")) filename.replace(filename.size() - 4, 4, ".MP3");
    else if (filename.ends_with(".MP3")) filename.replace(filename.size() - 4, 4, ".WAV");
    return filename;
}

// FUNCTION: WIZ8 0x004086c0
void SoundEnableSound(BOOLEAN fEnable)
{
    gfEnableStartup = fEnable;
}

// FUNCTION: WIZ8 0x004086d0
BOOLEAN InitializeSoundManager(void)
{
    ShutdownSoundManager();
    listener = Listener{};
#ifndef SOUND_DISABLE
    fSoundSystemInit = gfEnableStartup && SoundInitHardware();
#endif
    guiSoundMemoryLimit = SOUND_DEFAULT_MEMORY;
    guiSoundMemoryUsed = 0;
    guiSoundCacheThreshold = SOUND_DEFAULT_THRESH;
    return TRUE;
}

// FUNCTION: WIZ8 0x00408850
void ShutdownSoundManager(void)
{
    fSoundSystemInit = FALSE;
    const auto stopped = std::move(activePlayback);
    activePlayback.clear();
    for (const auto& playback : stopped) {
        playback->EOSCallback = nullptr;
        FinishPlayback(playback);
    }
    sampleCache.clear();
    guiSoundMemoryUsed = 0;
    w8_native::shutdown_audio();
}

// FUNCTION: WIZ8 0x00408860
SoundPlayback SoundPlay(std::string_view path, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit) return {};
    auto filename = ResolveSoundPath(path);
    if (filename.empty() || SoundPlayStreamed(filename.data())) return {};
    return StartSample(SoundLoadSample(filename.data()), parameters);
}

// FUNCTION: WIZ8 0x00408ad0
SoundPlayback SoundPlayStreamedFile(std::string_view path, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit) return {};
    auto filename = ResolveSoundPath(path);
    return filename.empty() ? SoundPlayback{} : SoundStartStream(filename.data(), parameters);
}

// FUNCTION: WIZ8 0x00408d60
SoundClip SoundPlayRandom(STR filename, RANDOMPARMS* parameters)
{
    if (!fSoundSystemInit || !parameters || parameters->uiTimeMin == SOUND_PARMS_DEFAULT)
        return {};
    auto clip = SoundLoadSample(filename);
    if (!clip) return {};
    const auto value = [](UINT32 input, UINT32 fallback) {
        return input == SOUND_PARMS_DEFAULT ? fallback : input;
    };
    clip->uiFlags |= SAMPLE_RANDOM | SAMPLE_LOCKED;
    clip->uiTimeMin = parameters->uiTimeMin;
    clip->uiTimeMax = value(parameters->uiTimeMax, clip->uiTimeMin);
    clip->uiSpeedMin = parameters->uiSpeedMin;
    clip->uiSpeedMax = parameters->uiSpeedMax;
    clip->uiVolMin = value(parameters->uiVolMin, guiSoundDefaultVolume);
    clip->uiVolMax = value(parameters->uiVolMax, guiSoundDefaultVolume);
    clip->uiPanMin = value(parameters->uiPanMin, 64);
    clip->uiPanMax = value(parameters->uiPanMax, 64);
    clip->uiMaxInstances = value(parameters->uiMaxInstances, 1);
    clip->uiPriority = value(parameters->uiPriority, PRIORITY_RANDOM);
    clip->uiInstances = 0;
    clip->uiTimeNext = w8_get_ticks() + clip->uiTimeMin +
        (clip->uiTimeMax > clip->uiTimeMin ? Random(clip->uiTimeMax - clip->uiTimeMin) : 0);
    return clip;
}

// FUNCTION: WIZ8 0x00408ef0
BOOLEAN SoundIsPlaying(SoundPlayback playback)
{
    return fSoundSystemInit && playback && playback->track && MIX_TrackPlaying(playback->track.get());
}

// FUNCTION: WIZ8 0x00408f70
BOOLEAN SoundStop(SoundPlayback playback)
{
    if (!SoundIsPlaying(playback)) return FALSE;
    FinishPlayback(playback);
    return TRUE;
}

// FUNCTION: WIZ8 0x00409020
BOOLEAN SoundStopGroup(UINT32 uiPriority)
{
    bool stopped = false;
    const auto playing = activePlayback;
    for (const auto& playback : playing)
        if (playback->uiPriority == uiPriority) stopped |= SoundStop(playback);
    return stopped;
}

// FUNCTION: WIZ8 0x00409120
void SoundSetDefaultVolume(UINT32 uiVolume)
{
    guiSoundDefaultVolume = std::min(uiVolume, 127u);
}

// FUNCTION: WIZ8 0x00409140
BOOLEAN SoundSetFadeVolume(SoundPlayback playback, UINT32 uiVolume, UINT32 uiRate, BOOLEAN fStopAtZero)
{
    if (!fSoundSystemInit || !playback || !playback->track) return FALSE;
    const auto target = std::min(uiVolume, 127u);
    const auto difference = std::abs(int(target) - int(playback->volume));
    if (!difference) return FALSE;
    playback->uiFadeVolume = target;
    playback->fStopAtZero = fStopAtZero;
    playback->uiFadeRate = uiRate / difference;
    playback->uiFadeTime = w8_get_ticks();
    return TRUE;
}

// FUNCTION: WIZ8 0x00409210
BOOLEAN SoundSetVolume(SoundPlayback playback, UINT32 uiVolume)
{
    if (!fSoundSystemInit || !playback || !playback->track) return FALSE;
    playback->uiFadeVolume = std::min(uiVolume, 127u);
    return SetGain(*playback, uiVolume);
}

static BOOLEAN SetGain(AudioPlayback& channel, UINT32 volume)
{
    const UINT32 clamped = std::min(volume, 127u);
    if (!MIX_SetTrackGain(channel.track.get(), float(clamped) / 127)) return FALSE;
    channel.volume = clamped;
    return TRUE;
}

// FUNCTION: WIZ8 0x004092a0
UINT32 SoundGetVolume(SoundPlayback playback)
{
    return fSoundSystemInit && playback && playback->track ? playback->volume : SOUND_ERROR;
}

// FUNCTION: WIZ8 0x00409310
BOOLEAN SoundServiceRandom(void)
{
    // Completion callbacks may mutate the cache while obtaining a channel.
    std::vector<SoundClip> clips;
    for (const auto& clip : sampleCache)
        if (clip->uiFlags & SAMPLE_RANDOM) clips.push_back(clip);
    for (const auto& clip : clips)
        if (!(clip->uiFlags & SAMPLE_RANDOM_MANUAL) && SoundRandomShouldPlay(clip))
            SoundStartRandom(clip);
    return FALSE;
}

// FUNCTION: WIZ8 0x00409360
BOOLEAN SoundRandomShouldPlay(SoundClip uiSample)
{
    if (!fSoundSystemInit || !uiSample) return FALSE;
    if (uiSample->uiFlags & SAMPLE_RANDOM)
        if (uiSample->uiTimeNext <= w8_get_ticks())
            if (uiSample->uiInstances < uiSample->uiMaxInstances)
                return (TRUE);

    return (FALSE);
}

// FUNCTION: WIZ8 0x004093b0
SoundPlayback SoundStartRandom(SoundClip uiSample)
{
    if (!fSoundSystemInit || !uiSample) return {};
    SOUNDPARMS parameters;
    memset(&parameters, 0xff, sizeof(parameters));
    parameters.uiVolume = uiSample->uiVolMin +
        (uiSample->uiVolMax > uiSample->uiVolMin ? Random(uiSample->uiVolMax - uiSample->uiVolMin) : 0);
    parameters.uiPan = uiSample->uiPanMin +
        (uiSample->uiPanMax > uiSample->uiPanMin ? Random(uiSample->uiPanMax - uiSample->uiPanMin) : 0);
    parameters.uiLoop = 1;
    parameters.uiPriority = uiSample->uiPriority;
    auto playback = StartSample(uiSample, &parameters);
    if (playback) {
        uiSample->uiTimeNext = w8_get_ticks() + uiSample->uiTimeMin +
            (uiSample->uiTimeMax > uiSample->uiTimeMin ? Random(uiSample->uiTimeMax - uiSample->uiTimeMin) : 0);
        ++uiSample->uiInstances;
    }
    return playback;
}

// FUNCTION: WIZ8 0x00409550
BOOLEAN SoundStopAllRandom(void)
{
    const auto playing = activePlayback;
    for (const auto& playback : playing)
        if (playback->clip && (playback->clip->uiFlags & SAMPLE_RANDOM)) FinishPlayback(playback);
    for (const auto& clip : sampleCache)
        clip->uiFlags &= ~(SAMPLE_RANDOM | SAMPLE_LOCKED);
    return FALSE;
}

// FUNCTION: WIZ8 0x004095b0
BOOLEAN SoundServiceStreams(void)
{
    if (!fSoundSystemInit) return TRUE;
    // Callbacks may stop other playback, start new playback, or shut down audio.
    const auto playing = activePlayback;
    for (const auto& playback : playing) {
        auto& channel = *playback;
        if (!channel.track) continue;
        if (!SoundIsPlaying(playback)) { FinishPlayback(playback); continue; }
        const auto time = w8_get_ticks();
        if (channel.volume == channel.uiFadeVolume || time - channel.uiFadeTime < channel.uiFadeRate) continue;
        const auto volume = channel.volume < channel.uiFadeVolume ? channel.volume + 1 : channel.volume - 1;
        if (volume == 0 && channel.fStopAtZero) FinishPlayback(playback);
        else {
            SetGain(channel, volume);
            channel.uiFadeTime = time;
        }
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004097f0
UINT32 SoundGetPosition(SoundPlayback playback)
{
    return fSoundSystemInit && playback && playback->track ? w8_get_ticks() - playback->uiTimeStamp : 0;
}

// FUNCTION: WIZ8 0x00409840
BOOLEAN SoundGetMilliSecondPosition(SoundPlayback playback, UINT32* total, UINT32* current)
{
    if (!total || !current) return FALSE;
    *total = *current = 0;
    if (!fSoundSystemInit || !playback || !playback->track) return FALSE;
    const auto& channel = *playback;
    const auto position = MIX_GetTrackPlaybackPosition(channel.track.get());
    *total = UINT32(std::max<Sint64>(0, MIX_TrackFramesToMS(channel.track.get(), channel.duration)));
    *current = UINT32(std::max<Sint64>(0, MIX_TrackFramesToMS(channel.track.get(), position)));
    return TRUE;
}

// FUNCTION: WIZ8 0x004098d0
BOOLEAN SoundSetCacheThreshhold(UINT32 uiThreshold)
{
    if (uiThreshold == 0)
        guiSoundCacheThreshold = SOUND_DEFAULT_THRESH;
    else
        guiSoundCacheThreshold = uiThreshold;

    return (TRUE);
}

// FUNCTION: WIZ8 0x004098f0
BOOLEAN SoundEmptyCache(void)
{
    const auto playing = activePlayback;
    for (const auto& playback : playing)
        if (!playback->fMusic) FinishPlayback(playback);
    sampleCache.clear();
    guiSoundMemoryUsed = 0;
    return TRUE;
}

SoundClip SoundLoadSample(STR pFilename)
{
    if (auto clip = SoundGetCached(pFilename)) return clip;
    return SoundLoadDisk(pFilename);
}

SoundClip SoundGetCached(STR pFilename)
{
    if (!pFilename || !*pFilename) return {};
    for (const auto& clip : sampleCache)
        if (SDL_strcasecmp(clip->pName.c_str(), pFilename) == 0) return clip;
    return {};
}

// FUNCTION: WIZ8 0x00409970
SoundClip SoundLoadDisk(STR filename)
{
    if (!fSoundSystemInit || !filename) return {};
    auto input = w8_native::open_audio_input(filename);
    if (!input) return {};
    const Sint64 size = SDL_GetIOSize(input.get());
    if (size <= 0 || size > guiSoundMemoryLimit) return {};
    while (uint64_t(size) + guiSoundMemoryUsed > guiSoundMemoryLimit)
        if (!SoundCleanCache()) return {};
    w8_native::Audio audio(MIX_LoadAudio_IO(w8_native::audio_mixer(), input.get(), false, false));
    SDL_AudioSpec format{};
    if (!audio || !MIX_GetAudioFormat(audio.get(), &format)) return {};
    auto clip = std::make_shared<AudioClip>();
    clip->pName = filename;
    clip->uiSize = UINT32(size);
    clip->uiSpeed = UINT32(format.freq);
    clip->owner = w8_native::retain_audio_mixer();
    clip->audio = std::move(audio);
    guiSoundMemoryUsed += clip->uiSize;
    sampleCache.push_back(clip);
    return clip;
}

BOOLEAN SoundCleanCache(void)
{
    auto victim = sampleCache.end();
    for (auto it = sampleCache.begin(); it != sampleCache.end(); ++it) {
        // Active tracks and ambient emitters own their clips directly.
        if (it->use_count() != 1 || ((*it)->uiFlags & SAMPLE_LOCKED)) continue;
        if (victim == sampleCache.end() || (*it)->uiCacheHits < (*victim)->uiCacheHits)
            victim = it;
    }
    if (victim == sampleCache.end()) return FALSE;
    guiSoundMemoryUsed -= (*victim)->uiSize;
    sampleCache.erase(victim);
    return TRUE;
}

// FUNCTION: WIZ8 0x00409c50
BOOLEAN SoundInitHardware(void)
{
    return w8_native::initialize_audio();
}

// FUNCTION: WIZ8 0x00409fe0

static bool UpdateSpatial(AudioPlayback& channel)
{
    const auto& p = channel.position;
    const float dx = p.flX - listener.x, dy = p.flY - listener.y, dz = p.flZ - listener.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    float rightX = listener.faceY * listener.upZ - listener.faceZ * listener.upY;
    float rightY = listener.faceZ * listener.upX - listener.faceX * listener.upZ;
    float rightZ = listener.faceX * listener.upY - listener.faceY * listener.upX;
    const float length = std::sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
    if (length > 0) { rightX /= length; rightY /= length; rightZ /= length; }
    const float pan = distance > 0 ? std::clamp((dx * rightX + dy * rightY + dz * rightZ) / distance, -1.0f, 1.0f) : 0;
    // Update3DSounds and ambient sound servicing supply falloff and camera-relative coordinates.
    // Preserve the old backend's disabled distance model and 20% opposite-ear floor.
    const MIX_StereoGains gains{distance > 0 ? std::max(0.2f, (1 - pan) * 0.5f) : 1,
                                distance > 0 ? std::max(0.2f, (1 + pan) * 0.5f) : 1};
    const float sourceAlong = distance > 0 ? -(dx * p.flVelX + dy * p.flVelY + dz * p.flVelZ) / distance : 0;
    const float listenerAlong = distance > 0 ? -(dx * listener.velocityX + dy * listener.velocityY + dz * listener.velocityZ) / distance : 0;
    const float doppler = (343.3f - std::min(listenerAlong, 343.0f)) / (343.3f - std::min(sourceAlong, 343.0f));
    const auto naturalRate = MIX_TrackMSToFrames(channel.track.get(), 1000);
    return naturalRate > 0 && MIX_SetTrackStereo(channel.track.get(), &gains) &&
           MIX_SetTrackFrequencyRatio(channel.track.get(), std::clamp(float(channel.playbackRate) / naturalRate * doppler, 0.01f, 100.0f));
}

static UINT32 RandomRange(UINT32 minimum, UINT32 maximum)
{
    return minimum + (maximum > minimum ? Random(maximum - minimum) : 0);
}

template<class Parameters>
static SoundPlayback StartTrack(SoundPlayback playback, SoundClip sample, const Parameters* parameters, const SOUND3DPOS* position = nullptr)
{
    auto& channel = *playback;
    const auto naturalRate = MIX_TrackMSToFrames(channel.track.get(), 1000);
    if (naturalRate <= 0) return {};
    UINT32 rate = UINT32(naturalRate);
    if (sample && (sample->uiFlags & SAMPLE_RANDOM))
    {
        const auto& cached = *sample;
        if (cached.uiSpeedMin != SOUND_PARMS_DEFAULT && cached.uiSpeedMax != SOUND_PARMS_DEFAULT)
            rate = RandomRange(cached.uiSpeedMin, cached.uiSpeedMax);
    }
    else if (parameters && parameters->uiSpeed != SOUND_PARMS_DEFAULT) rate = parameters->uiSpeed;
    if (parameters && parameters->uiPitchBend != SOUND_PARMS_DEFAULT)
    {
        const auto bend = uint64_t(rate) * parameters->uiPitchBend / 100;
        if (bend <= UINT32_MAX / 2)
            rate = UINT32(std::clamp<int64_t>(int64_t(rate) + int64_t(Random(UINT32(bend * 2))) - int64_t(bend), 1, UINT32_MAX));
    }
    channel.playbackRate = rate;
    channel.spatial = position != nullptr;
    if (position) channel.position = *position;
    const UINT32 volume = parameters && parameters->uiVolume != SOUND_PARMS_DEFAULT ? parameters->uiVolume : guiSoundDefaultVolume;
    const UINT32 loops = parameters && parameters->uiLoop != SOUND_PARMS_DEFAULT ? parameters->uiLoop : 1;
    channel.uiFadeVolume = std::min(volume, 127u);
    channel.uiPriority = parameters && parameters->uiPriority != SOUND_PARMS_DEFAULT ? parameters->uiPriority : PRIORITY_MAX;
    const auto options = SDL_CreateProperties();
    bool ok = options && SetGain(channel, volume) &&
              SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops == 0 ? -1 : Sint64(loops) - 1);
    if (channel.spatial) ok = ok && UpdateSpatial(channel);
    else
    {
        ok = ok && rate > 0 && MIX_SetTrackFrequencyRatio(channel.track.get(), float(rate) / naturalRate);
        if constexpr (requires { parameters->uiPan; })
        {
            const auto pan = parameters && parameters->uiPan != SOUND_PARMS_DEFAULT ? std::min(parameters->uiPan, 127u) : 64;
            const float offset = std::clamp(float(int(pan) - 64) / 63, -1.0f, 1.0f);
            const MIX_StereoGains gains{1 - std::max(0.0f, offset), 1 + std::min(0.0f, offset)};
            ok = ok && MIX_SetTrackStereo(channel.track.get(), &gains);
        }
    }
    ok = ok && MIX_PlayTrack(channel.track.get(), options);
    SDL_DestroyProperties(options);
    if (!ok) return {};
    channel.clip = sample;
    channel.uiTimeStamp = w8_get_ticks();
    if (sample) ++sample->uiCacheHits;
    if (parameters && reinterpret_cast<w8_ulong_ptr>(parameters->EOSCallback) != w8_ulong_ptr(-1))
    {
        channel.EOSCallback = parameters->EOSCallback;
        channel.pCallbackData = parameters->pCallbackData;
    }
    activePlayback.push_back(playback);
    return playback;
}

template<class Parameters>
static SoundPlayback StartSample(SoundClip sample, const Parameters* parameters, const SOUND3DPOS* position)
{
    if (!fSoundSystemInit || !sample || !sample->audio) return {};
    auto playback = CreatePlayback();
    if (!playback || !MIX_SetTrackAudio(playback->track.get(), sample->audio.get())) return {};
    playback->duration = MIX_GetAudioDuration(sample->audio.get());
    return StartTrack(playback, sample, parameters, position);
}

// FUNCTION: WIZ8 0x0040a2e0
static SoundPlayback SoundStartStream(STR filename, SOUNDPARMS* parameters)
{
    auto playback = CreatePlayback();
    if (!playback) return {};
    auto& channel = *playback;
    channel.input = w8_native::open_audio_input(filename);
    if (!channel.input) return {};
    std::unique_ptr<MIX_AudioDecoder, decltype(&MIX_DestroyAudioDecoder)> decoder(
        MIX_CreateAudioDecoder_IO(channel.input.get(), false, 0), MIX_DestroyAudioDecoder);
    if (!decoder) return {};
    channel.duration = SDL_GetNumberProperty(MIX_GetAudioDecoderProperties(decoder.get()), MIX_PROP_METADATA_DURATION_FRAMES_NUMBER, -1);
    decoder.reset();
    if (SDL_SeekIO(channel.input.get(), 0, SDL_IO_SEEK_SET) != 0 ||
        !MIX_SetTrackIOStream(channel.track.get(), channel.input.get(), false)) return {};
    return StartTrack(playback, {}, parameters);
}

BOOLEAN SoundPlayStreamed(STR pFilename)
try
{
    const auto file = wiz8::open_file(pFilename);
    return file->size() >= guiSoundCacheThreshold;
}
catch (const std::exception&) { return FALSE; }

// FUNCTION: WIZ8 0x0040a5c0
static void FinishPlayback(SoundPlayback playback)
{
    if (!playback || !playback->track) return;
    const auto clip = playback->clip;
    const auto callback = playback->EOSCallback;
    void* data = playback->pCallbackData;
    playback->EOSCallback = nullptr;
    playback->pCallbackData = nullptr;
    playback->track.reset();
    playback->input.reset();
    playback->clip.reset();
    playback->owner.reset();
    std::erase(activePlayback, playback);
    if (clip && (clip->uiFlags & SAMPLE_RANDOM) && clip->uiInstances) --clip->uiInstances;
    // Retained objects become inert before callbacks, and never identify later playback.
    if (callback) callback(data);
}

// FUNCTION: WIZ8 0x0040a8b0
void SoundSetSampleFlags(SoundClip uiSample, UINT32 uiFlags)
{
    if (uiSample) uiSample->uiFlags |= uiFlags;
}

// FUNCTION: WIZ8 0x0040a8e0
void SoundRemoveSampleFlags(SoundClip uiSample, UINT32 uiFlags)
{
    if (uiSample) uiSample->uiFlags &= ~uiFlags;
}

// FUNCTION: WIZ8 0x0040a910
BOOLEAN SoundFileIsPlaying(std::string_view filename)
{
    for (const auto& playback : activePlayback) {
        const auto& clip = playback->clip;
        if (clip && SoundIsPlaying(playback) && clip->pName.size() == filename.size() &&
            SDL_strncasecmp(clip->pName.c_str(), filename.data(), filename.size()) == 0) return TRUE;
    }
    return FALSE;
}

// FUNCTION: WIZ8 0x0040a9a0
void SoundSetMusic(SoundPlayback playback)
{
    if (playback) playback->fMusic = TRUE;
}

// FUNCTION: WIZ8 0x0040a9d0
BOOLEAN SoundStopMusic(void)
{
    bool stopped = false;
    const auto playing = activePlayback;
    for (const auto& playback : playing)
        if (playback->fMusic) stopped |= SoundStop(playback);
    return stopped;
}

// FUNCTION: WIZ8 0x0040ab20
void Sound3DSetPosition(SoundPlayback playback, FLOAT x, FLOAT y, FLOAT z)
{
    if (!playback || !playback->track || !playback->spatial) return;
    auto& channel = *playback;
    channel.position.flX = x; channel.position.flY = y; channel.position.flZ = z;
    UpdateSpatial(channel);
}

void Sound3DSetListener(FLOAT x, FLOAT y, FLOAT z)
{
    listener.x = x; listener.y = y; listener.z = z;
    for (const auto& playback : activePlayback) if (playback->spatial) UpdateSpatial(*playback);
}
void Sound3DSetListenerOrientation(FLOAT faceX, FLOAT faceY, FLOAT faceZ, FLOAT upX, FLOAT upY, FLOAT upZ)
{
    listener.faceX = faceX; listener.faceY = faceY; listener.faceZ = faceZ;
    listener.upX = upX; listener.upY = upY; listener.upZ = upZ;
    for (const auto& playback : activePlayback) if (playback->spatial) UpdateSpatial(*playback);
}
void Sound3DSetListenerVelocity(FLOAT x, FLOAT y, FLOAT z)
{
    listener.velocityX = x; listener.velocityY = y; listener.velocityZ = z;
    for (const auto& playback : activePlayback) if (playback->spatial) UpdateSpatial(*playback);
}

// FUNCTION: WIZ8 0x0040ab80
void Sound3DSetDirection(SoundPlayback playback, FLOAT faceX, FLOAT faceY, FLOAT faceZ, FLOAT upX, FLOAT upY, FLOAT upZ)
{
    if (!playback || !playback->track || !playback->spatial) return;
    auto& p = playback->position;
    p.flFaceX = faceX; p.flFaceY = faceY; p.flFaceZ = faceZ;
    p.flUpX = upX; p.flUpY = upY; p.flUpZ = upZ;
}

// FUNCTION: WIZ8 0x0040b210
void Sound3DSetEnvironment(INT32)
{
    // The previous native backend had no environmental reverb either.
}

// FUNCTION: WIZ8 0x0040abf0
SoundPlayback Sound3DPlay(STR pFilename, SOUND3DPARMS* pParms)
{
    if (!fSoundSystemInit || !pParms) return {};
    auto clip = SoundLoadSample(pFilename);
    if (!clip) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sound3DPlay: failed loading %s", pFilename);
        return {};
    }
    return StartSample(clip, pParms, &pParms->Pos);
}

// FUNCTION: WIZ8 0x0040b040
SoundPlayback Sound3DStartRandom(SoundClip uiSample, SOUND3DPOS* pPos)
{
    if (!fSoundSystemInit || !uiSample || !pPos) return {};
    SOUND3DPARMS parameters;
    memset(&parameters, 0xff, sizeof(parameters));
    parameters.uiLoop = 1;
    parameters.uiPriority = uiSample->uiPriority;
    parameters.Pos = *pPos;
    parameters.uiVolume = pPos->uiVolume;
    auto playback = StartSample(uiSample, &parameters, pPos);
    if (playback) {
        uiSample->uiTimeNext = w8_get_ticks() + uiSample->uiTimeMin +
            (uiSample->uiTimeMax > uiSample->uiTimeMin ? Random(uiSample->uiTimeMax - uiSample->uiTimeMin) : 0);
        ++uiSample->uiInstances;
    }
    return playback;
}
