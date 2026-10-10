#include <string_view>
#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
/*********************************************************************************
* SGP Digital Sound Module
*
*		This module handles the playing of digital samples, preloaded or streamed.
*
* Derek Beland, May 28, 1997
*********************************************************************************/
#include <stdio.h>
#include <string.h>
#include "soundman.h"
#include "wiz8/filesystem.h"
#include "Types.h"

#include <wiz8/native_audio.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "random.h"

#define SOUND_MAX_CHANNELS 32 // number of mixer channels

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
struct SoundChannel
{
    std::shared_ptr<MIX_Mixer> owner;
    // Track destruction synchronizes with mixing before closing its input.
    SoundClip clip;
    w8_native::AudioInput input;
    w8_native::Track track;
    UINT32 uiSoundID = NO_SAMPLE, uiPriority = PRIORITY_MAX;
    void (*EOSCallback)(void*) = nullptr;
    void* pCallbackData = nullptr;
    uint64_t uiTimeStamp = 0, uiFadeTime = 0;
    UINT32 uiFadeVolume = 0, uiFadeRate = 0;
    UINT32 volume = 127, playbackRate = 44100;
    Sint64 duration = -1;
    BOOLEAN fMusic = FALSE, fStopAtZero = TRUE;
    bool spatial = false, fLooping = false;
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

UINT32 SoundGetIndexByID(UINT32 uiSoundID);
BOOLEAN SoundInitHardware(void);
UINT32 SoundGetFreeChannel(void);
UINT32 SoundStartSample(SoundClip uiSample, UINT32 uiChannel, SOUNDPARMS* pParms);
UINT32 SoundStartStream(STR pFilename, UINT32 uiChannel, SOUNDPARMS* pParms);
UINT32 SoundGetUniqueID(void);
BOOLEAN SoundPlayStreamed(STR pFilename);
void SoundResetChannel(UINT32 channel);
BOOLEAN SoundCleanCache(void);
BOOLEAN SoundIndexIsPlaying(UINT32 uiSound);
BOOLEAN SoundStopIndex(UINT32 uiSound);
UINT32 SoundGetVolumeIndex(UINT32 uiChannel);
BOOLEAN SoundSetVolumeIndex(UINT32 uiChannel, UINT32 uiVolume);

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
SoundChannel pSoundList[SOUND_MAX_CHANNELS];

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
    for (auto& channel : pSoundList) channel = SoundChannel{};
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
    for (auto& channel : pSoundList) channel.EOSCallback = nullptr;
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index) SoundStopIndex(index);
    sampleCache.clear();
    guiSoundMemoryUsed = 0;
    w8_native::shutdown_audio();
    fSoundSystemInit = FALSE;
}

// FUNCTION: WIZ8 0x00408860
UINT32 SoundPlay(std::string_view path, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit) return SOUND_ERROR;
    auto filename = ResolveSoundPath(path);
    if (filename.empty() || SoundPlayStreamed(filename.data())) return SOUND_ERROR;
    const auto sample = SoundLoadSample(filename.data());
    if (!sample) return SOUND_ERROR;
    const auto channel = SoundGetFreeChannel();
    return channel == SOUND_ERROR ? SOUND_ERROR : SoundStartSample(sample, channel, parameters);
}

// FUNCTION: WIZ8 0x00408ad0
UINT32 SoundPlayStreamedFile(std::string_view path, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit) return SOUND_ERROR;
    auto filename = ResolveSoundPath(path);
    if (filename.empty()) return SOUND_ERROR;
    const auto channel = SoundGetFreeChannel();
    return channel == SOUND_ERROR ? SOUND_ERROR : SoundStartStream(filename.data(), channel, parameters);
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
BOOLEAN SoundIsPlaying(UINT32 uiSoundID)
{
    UINT32 uiSound;

    if (fSoundSystemInit) {
        uiSound = SoundGetIndexByID(uiSoundID);
        if (uiSound != NO_SAMPLE)
            return (SoundIndexIsPlaying(uiSound));
    }

    return (FALSE);
}

BOOLEAN SoundIndexIsPlaying(UINT32 index)
{
    return fSoundSystemInit && index < SOUND_MAX_CHANNELS && pSoundList[index].track &&
           MIX_TrackPlaying(pSoundList[index].track.get());
}

// FUNCTION: WIZ8 0x00408f70
BOOLEAN SoundStop(UINT32 uiSoundID)
{
    UINT32 uiSound;

    if (fSoundSystemInit) {
        if (SoundIsPlaying(uiSoundID)) {
            uiSound = SoundGetIndexByID(uiSoundID);
            if (uiSound != NO_SAMPLE) {
                SoundStopIndex(uiSound);
                return (TRUE);
            }
        }
    }
    return (FALSE);
}

// FUNCTION: WIZ8 0x00409020
BOOLEAN SoundStopGroup(UINT32 uiPriority)
{
    UINT32 uiCount;
    BOOLEAN fStopped = FALSE;

    if (fSoundSystemInit) {
        for (uiCount = 0; uiCount < SOUND_MAX_CHANNELS; uiCount++) {
            if (pSoundList[uiCount].track) {
                if (pSoundList[uiCount].uiPriority == uiPriority) {
                    SoundStop(pSoundList[uiCount].uiSoundID);
                    fStopped = TRUE;
                }
            }
        }
    }

    return (fStopped);
}

// FUNCTION: WIZ8 0x00409120
void SoundSetDefaultVolume(UINT32 uiVolume)
{
    guiSoundDefaultVolume = std::min(uiVolume, 127u);
}

// FUNCTION: WIZ8 0x00409140
BOOLEAN SoundSetFadeVolume(UINT32 uiSoundID, UINT32 uiVolume, UINT32 uiRate, BOOLEAN fStopAtZero)
{
    UINT32 uiSound, uiVolCap, uiVolumeDiff;

    if (fSoundSystemInit) {
        uiVolCap = std::min(uiVolume, 127u);

        if ((uiSound = SoundGetIndexByID(uiSoundID)) != NO_SAMPLE) {
            uiVolumeDiff = abs(static_cast<int>(uiVolCap - SoundGetVolumeIndex(uiSound)));

            if (!uiVolumeDiff)
                return (FALSE);

            pSoundList[uiSound].uiFadeVolume = uiVolCap;
            pSoundList[uiSound].fStopAtZero = fStopAtZero;
            pSoundList[uiSound].uiFadeRate = uiRate / uiVolumeDiff;
            pSoundList[uiSound].uiFadeTime = w8_get_ticks();

            return (TRUE);
        }
    }

    return (FALSE);
}

// FUNCTION: WIZ8 0x00409210
BOOLEAN SoundSetVolume(UINT32 uiSoundID, UINT32 uiVolume)
{
    UINT32 uiSound;

    if (fSoundSystemInit) {

        if ((uiSound = SoundGetIndexByID(uiSoundID)) != NO_SAMPLE) {
            pSoundList[uiSound].uiFadeVolume = std::min(uiVolume, 127u);
            return (SoundSetVolumeIndex(uiSound, uiVolume));
        }
    }

    return (FALSE);
}

BOOLEAN SoundSetVolumeIndex(UINT32 index, UINT32 volume)
{
    if (!fSoundSystemInit || index >= SOUND_MAX_CHANNELS || !pSoundList[index].track) return FALSE;
    auto& channel = pSoundList[index];
    const UINT32 clamped = std::min(volume, 127u);
    if (!MIX_SetTrackGain(channel.track.get(), float(clamped) / 127)) return FALSE;
    channel.volume = clamped;
    return TRUE;
}

// FUNCTION: WIZ8 0x004092a0
UINT32 SoundGetVolume(UINT32 uiSoundID)
{
    UINT32 uiSound;

    if (fSoundSystemInit) {
        if ((uiSound = SoundGetIndexByID(uiSoundID)) != NO_SAMPLE)
            return (SoundGetVolumeIndex(uiSound));
    }

    return (SOUND_ERROR);
}

UINT32 SoundGetVolumeIndex(UINT32 index)
{
    return index < SOUND_MAX_CHANNELS && pSoundList[index].track ? pSoundList[index].volume : SOUND_ERROR;
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
UINT32 SoundStartRandom(SoundClip uiSample)
{
    if (!fSoundSystemInit || !uiSample || !uiSample->audio) return SOUND_ERROR;
    UINT32 uiChannel, uiSoundID;
    SOUNDPARMS spParms;

    if ((uiChannel = SoundGetFreeChannel()) != SOUND_ERROR) {
        memset(&spParms, 0xff, sizeof(SOUNDPARMS));

        spParms.uiVolume = uiSample->uiVolMin +
                           (uiSample->uiVolMax > uiSample->uiVolMin ? Random(uiSample->uiVolMax - uiSample->uiVolMin) : 0);
        spParms.uiPan = uiSample->uiPanMin +
                        (uiSample->uiPanMax > uiSample->uiPanMin ? Random(uiSample->uiPanMax - uiSample->uiPanMin) : 0);
        spParms.uiLoop = 1;
        spParms.uiPriority = uiSample->uiPriority;

        if ((uiSoundID = SoundStartSample(uiSample, uiChannel, &spParms)) != SOUND_ERROR) {
            uiSample->uiTimeNext =
                w8_get_ticks() + uiSample->uiTimeMin +
                (uiSample->uiTimeMax > uiSample->uiTimeMin ? Random(uiSample->uiTimeMax - uiSample->uiTimeMin) : 0);
            uiSample->uiInstances++;
            return (uiSoundID);
        }
    }
    return (NO_SAMPLE);
}

// FUNCTION: WIZ8 0x00409550
BOOLEAN SoundStopAllRandom(void)
{
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index) {
        const auto& clip = pSoundList[index].clip;
        if (clip && (clip->uiFlags & SAMPLE_RANDOM)) SoundStopIndex(index);
    }
    for (const auto& clip : sampleCache)
        clip->uiFlags &= ~(SAMPLE_RANDOM | SAMPLE_LOCKED);
    return FALSE;
}

// FUNCTION: WIZ8 0x004095b0
BOOLEAN SoundServiceStreams(void)
{
    if (!fSoundSystemInit) return TRUE;
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index)
    {
        auto& channel = pSoundList[index];
        if (!channel.track) continue;
        if (!SoundIndexIsPlaying(index)) { SoundStopIndex(index); continue; }
        const auto time = w8_get_ticks();
        if (channel.volume == channel.uiFadeVolume || time - channel.uiFadeTime < channel.uiFadeRate) continue;
        const auto volume = channel.volume < channel.uiFadeVolume ? channel.volume + 1 : channel.volume - 1;
        if (volume == 0 && channel.fStopAtZero) SoundStopIndex(index);
        else
        {
            SoundSetVolumeIndex(index, volume);
            channel.uiFadeTime = time;
        }
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x004097f0
UINT32 SoundGetPosition(UINT32 uiSoundID)
{
    //UINT32 uiSound, uiFreq=0, uiPosition=0, uiBytesPerSample=0, uiFormat=0;
    UINT32 uiSound;
    uint64_t uiTime, uiPosition;

    if (fSoundSystemInit) {
        if ((uiSound = SoundGetIndexByID(uiSoundID)) != NO_SAMPLE) {

            uiTime = w8_get_ticks();
            uiPosition = uiTime - pSoundList[uiSound].uiTimeStamp;

            return (uiPosition);
        }
    }

    return (0);
}

// FUNCTION: WIZ8 0x00409840
BOOLEAN SoundGetMilliSecondPosition(UINT32 id, UINT32* total, UINT32* current)
{
    if (!total || !current) return FALSE;
    *total = *current = 0;
    const auto index = SoundGetIndexByID(id);
    if (!fSoundSystemInit || index == NO_SAMPLE || !pSoundList[index].track) return FALSE;
    const auto& channel = pSoundList[index];
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
    UINT32 uiCount;

    if (fSoundSystemInit) {
        for (uiCount = 0; uiCount < SOUND_MAX_CHANNELS; ++uiCount) {
            if (!pSoundList[uiCount].fMusic)
                SoundStopIndex(uiCount);
        }
    }

    sampleCache.clear();
    guiSoundMemoryUsed = 0;

    return (TRUE);
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

UINT32 SoundGetIndexByID(UINT32 uiSoundID)
{
    if (uiSoundID == NO_SAMPLE) return NO_SAMPLE;
    UINT32 uiCount;

    for (uiCount = 0; uiCount < SOUND_MAX_CHANNELS; uiCount++) {
        if (pSoundList[uiCount].uiSoundID == uiSoundID)
            return (uiCount);
    }

    return (NO_SAMPLE);
}

// FUNCTION: WIZ8 0x00409c50
BOOLEAN SoundInitHardware(void)
{
    return w8_native::initialize_audio();
}

// FUNCTION: WIZ8 0x00409f30
void SoundResetChannel(UINT32 index)
{
    auto& channel = pSoundList[index];
    channel.track.reset();
    channel.input.reset();
    channel = SoundChannel{};
    channel.uiTimeStamp = w8_get_ticks();
}

UINT32 SoundGetFreeChannel(void)
{
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index)
    {
        if (!SoundIndexIsPlaying(index)) SoundStopIndex(index);
        if (!pSoundList[index].track)
        {
            SoundResetChannel(index);
            return index;
        }
    }
    return SOUND_ERROR;
}

// FUNCTION: WIZ8 0x00409fe0

static bool UpdateSpatial(SoundChannel& channel)
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
static UINT32 StartTrack(UINT32 index, SoundClip sample, const Parameters* parameters, const SOUND3DPOS* position = nullptr)
{
    auto& channel = pSoundList[index];
    const auto naturalRate = MIX_TrackMSToFrames(channel.track.get(), 1000);
    if (naturalRate <= 0) { SoundResetChannel(index); return SOUND_ERROR; }
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
    bool ok = options && SoundSetVolumeIndex(index, volume) &&
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
    if (!ok) { SoundResetChannel(index); return SOUND_ERROR; }
    channel.clip = sample;
    channel.uiSoundID = SoundGetUniqueID();
    channel.uiTimeStamp = w8_get_ticks();
    channel.fLooping = loops == 0;
    if (sample)
    {
        ++sample->uiCacheHits;
        if (channel.fLooping) sample->uiFlags |= SAMPLE_LOCKED;
    }
    if (parameters && reinterpret_cast<w8_ulong_ptr>(parameters->EOSCallback) != w8_ulong_ptr(-1))
    {
        channel.EOSCallback = parameters->EOSCallback;
        channel.pCallbackData = parameters->pCallbackData;
    }
    return channel.uiSoundID;
}

UINT32 SoundStartSample(SoundClip sample, UINT32 index, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit || index >= SOUND_MAX_CHANNELS || !sample || !sample->audio) return SOUND_ERROR;
    auto& channel = pSoundList[index];
    channel.owner = w8_native::retain_audio_mixer();
    channel.track.reset(MIX_CreateTrack(w8_native::audio_mixer()));
    if (!channel.track || !MIX_SetTrackAudio(channel.track.get(), sample->audio.get()))
    { SoundResetChannel(index); return SOUND_ERROR; }
    channel.duration = MIX_GetAudioDuration(sample->audio.get());
    return StartTrack(index, sample, parameters);
}

// FUNCTION: WIZ8 0x0040a2e0
UINT32 SoundStartStream(STR filename, UINT32 index, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit || index >= SOUND_MAX_CHANNELS) return SOUND_ERROR;
    auto& channel = pSoundList[index];
    channel.owner = w8_native::retain_audio_mixer();
    channel.input = w8_native::open_audio_input(filename);
    if (!channel.input) return SOUND_ERROR;
    std::unique_ptr<MIX_AudioDecoder, decltype(&MIX_DestroyAudioDecoder)> decoder(
        MIX_CreateAudioDecoder_IO(channel.input.get(), false, 0), MIX_DestroyAudioDecoder);
    if (!decoder) { SoundResetChannel(index); return SOUND_ERROR; }
    channel.duration = SDL_GetNumberProperty(MIX_GetAudioDecoderProperties(decoder.get()), MIX_PROP_METADATA_DURATION_FRAMES_NUMBER, -1);
    decoder.reset();
    channel.track.reset(MIX_CreateTrack(w8_native::audio_mixer()));
    if (SDL_SeekIO(channel.input.get(), 0, SDL_IO_SEEK_SET) != 0 || !channel.track ||
        !MIX_SetTrackIOStream(channel.track.get(), channel.input.get(), false))
    { SoundResetChannel(index); return SOUND_ERROR; }
    return StartTrack(index, {}, parameters);
}

UINT32 SoundGetUniqueID(void)
{
    static UINT32 uiNextID = 0;

    if (uiNextID == SOUND_ERROR)
        uiNextID++;

    return (uiNextID++);
}

BOOLEAN SoundPlayStreamed(STR pFilename)
try
{
    const auto file = wiz8::open_file(pFilename);
    return file->size() >= guiSoundCacheThreshold;
}
catch (const std::exception&) { return FALSE; }

// FUNCTION: WIZ8 0x0040a5c0
BOOLEAN SoundStopIndex(UINT32 index)
{
    if (index >= SOUND_MAX_CHANNELS || !pSoundList[index].track) return FALSE;
    auto& channel = pSoundList[index];
    const auto sample = channel.clip;
    const bool looping = channel.fLooping;
    const auto callback = channel.EOSCallback;
    void* data = channel.pCallbackData;
    SoundResetChannel(index);
    if (sample)
    {
        auto& cached = *sample;
        if ((cached.uiFlags & SAMPLE_RANDOM) && cached.uiInstances) --cached.uiInstances;
        if (looping && !(cached.uiFlags & SAMPLE_RANDOM))
        {
            bool inUse = false;
            for (const auto& other : pSoundList) inUse |= other.clip == sample && other.fLooping;
            if (!inUse) cached.uiFlags &= ~SAMPLE_LOCKED;
        }
    }
    // Dispatch only on the game thread, after clearing state: callback reentry is safe.
    if (callback) callback(data);
    return TRUE;
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
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index) {
        const auto& clip = pSoundList[index].clip;
        if (clip && SoundIndexIsPlaying(index) && clip->pName.size() == filename.size() &&
            SDL_strncasecmp(clip->pName.c_str(), filename.data(), filename.size()) == 0)
            return TRUE;
    }
    return FALSE;
}

// FUNCTION: WIZ8 0x0040a9a0
void SoundSetMusic(UINT32 uiSoundID)
{
    UINT32 uiSound = SoundGetIndexByID(uiSoundID);

    if (uiSound != NO_SAMPLE)
        pSoundList[uiSound].fMusic = TRUE;
}

// FUNCTION: WIZ8 0x0040a9d0
BOOLEAN SoundStopMusic(void)
{
    UINT32 uiCount;
    BOOLEAN fStopped = FALSE;

    if (fSoundSystemInit) {
        for (uiCount = 0; uiCount < SOUND_MAX_CHANNELS; uiCount++) {
            if (pSoundList[uiCount].track) {
                if (pSoundList[uiCount].fMusic) {
                    SoundStop(pSoundList[uiCount].uiSoundID);
                    fStopped = TRUE;
                }
            }
        }
    }

    return (fStopped);
}

// FUNCTION: WIZ8 0x0040ab20
void Sound3DSetPosition(UINT32 id, FLOAT x, FLOAT y, FLOAT z)
{
    const auto index = SoundGetIndexByID(id);
    if (index == NO_SAMPLE || !pSoundList[index].track || !pSoundList[index].spatial) return;
    auto& channel = pSoundList[index];
    channel.position.flX = x; channel.position.flY = y; channel.position.flZ = z;
    UpdateSpatial(channel);
}

void Sound3DSetListener(FLOAT x, FLOAT y, FLOAT z)
{
    listener.x = x; listener.y = y; listener.z = z;
    for (auto& channel : pSoundList) if (channel.track && channel.spatial) UpdateSpatial(channel);
}
void Sound3DSetListenerOrientation(FLOAT faceX, FLOAT faceY, FLOAT faceZ, FLOAT upX, FLOAT upY, FLOAT upZ)
{
    listener.faceX = faceX; listener.faceY = faceY; listener.faceZ = faceZ;
    listener.upX = upX; listener.upY = upY; listener.upZ = upZ;
    for (auto& channel : pSoundList) if (channel.track && channel.spatial) UpdateSpatial(channel);
}
void Sound3DSetListenerVelocity(FLOAT x, FLOAT y, FLOAT z)
{
    listener.velocityX = x; listener.velocityY = y; listener.velocityZ = z;
    for (auto& channel : pSoundList) if (channel.track && channel.spatial) UpdateSpatial(channel);
}

// FUNCTION: WIZ8 0x0040ab80
void Sound3DSetDirection(UINT32 id, FLOAT faceX, FLOAT faceY, FLOAT faceZ, FLOAT upX, FLOAT upY, FLOAT upZ)
{
    const auto index = SoundGetIndexByID(id);
    if (index == NO_SAMPLE || !pSoundList[index].spatial) return;
    auto& p = pSoundList[index].position;
    p.flFaceX = faceX; p.flFaceY = faceY; p.flFaceZ = faceZ;
    p.flUpX = upX; p.flUpY = upY; p.flUpZ = upZ;
    // Sources are omnidirectional, as in the previous backend (no source cone).
}

// FUNCTION: WIZ8 0x0040b210
void Sound3DSetEnvironment(INT32)
{
    // The previous native backend had no environmental reverb either.
}

// FUNCTION: WIZ8 0x0040abf0
UINT32 Sound3DPlay(STR pFilename, SOUND3DPARMS* pParms)
{
    SoundClip uiSample;
    UINT32 uiChannel;

    if (fSoundSystemInit) {
        if ((uiSample = SoundLoadSample(pFilename)) != nullptr) {
            if ((uiChannel = SoundGetFreeChannel()) != SOUND_ERROR) {
                return (Sound3DStartSample(uiSample, uiChannel, pParms));
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sound3DPlay: ERROR: Failed loading sample %s\n", pFilename);
        }
    }

    return (SOUND_ERROR);
}

// FUNCTION: WIZ8 0x0040ad40
UINT32 Sound3DStartSample(SoundClip sample, UINT32 index, SOUND3DPARMS* parameters)
{
    if (!fSoundSystemInit || !parameters || index >= SOUND_MAX_CHANNELS || !sample || !sample->audio) return SOUND_ERROR;
    auto& channel = pSoundList[index];
    channel.owner = w8_native::retain_audio_mixer();
    channel.track.reset(MIX_CreateTrack(w8_native::audio_mixer()));
    if (!channel.track || !MIX_SetTrackAudio(channel.track.get(), sample->audio.get()))
    { SoundResetChannel(index); return SOUND_ERROR; }
    channel.duration = MIX_GetAudioDuration(sample->audio.get());
    return StartTrack(index, sample, parameters, &parameters->Pos);
}

// FUNCTION: WIZ8 0x0040b040
UINT32 Sound3DStartRandom(SoundClip uiSample, SOUND3DPOS* pPos)
{
    if (!fSoundSystemInit || !uiSample || !uiSample->audio) return SOUND_ERROR;
    UINT32 uiChannel, uiSoundID;
    SOUND3DPARMS sp3DParms;

    if (pPos && ((uiChannel = SoundGetFreeChannel()) != SOUND_ERROR)) {
        memset(&sp3DParms, 0xff, sizeof(SOUND3DPARMS));

        sp3DParms.uiLoop = 1;
        sp3DParms.uiPriority = uiSample->uiPriority;

        sp3DParms.Pos = *pPos;
        sp3DParms.uiVolume = pPos->uiVolume;

        if ((uiSoundID = Sound3DStartSample(uiSample, uiChannel, &sp3DParms)) != SOUND_ERROR) {
            uiSample->uiTimeNext =
                w8_get_ticks() + uiSample->uiTimeMin +
                (uiSample->uiTimeMax > uiSample->uiTimeMin ? Random(uiSample->uiTimeMax - uiSample->uiTimeMin) : 0);
            uiSample->uiInstances++;
            return (uiSoundID);
        }
    }

    return (NO_SAMPLE);
}
