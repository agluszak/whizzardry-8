#include <cstdint>
#include <string_view>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __SOUNDMAN_
#define __SOUNDMAN_

#include <optional>

#include "wiz8/audio_resources.h"

extern bool gfEnableStartup;

// Sample status flags
#define SAMPLE_LOCKED 0x00000002
#define SAMPLE_RANDOM 0x00000004
#define SAMPLE_RANDOM_MANUAL 0x00000008

// Invalid scalar query result; playback creation returns an empty SoundPlayback.
#define SOUND_ERROR 0xffffffff

// Maximum allowable priority value
#define PRIORITY_MAX 0xfffffffe
#define PRIORITY_RANDOM PRIORITY_MAX - 1

// Structure definition for 3D sound positional information used by
// various other structs and functions
typedef struct {
    float flX, flY, flZ;
    float flVelX, flVelY, flVelZ;
    float flFaceX, flFaceY, flFaceZ;
    float flUpX, flUpY, flUpZ;
    float flFalloffMin, flFalloffMax;
    std::uint32_t uiVolume;
} SOUND3DPOS;

// Structure definition for sound parameters being passed down to
//		the sample playing function
typedef struct {
    std::uint32_t uiSpeed;
    std::uint32_t uiPitchBend; // Random pitch bend range +/-
    std::uint32_t uiVolume;
    std::uint32_t uiPan;
    std::uint32_t uiLoop;
    std::uint32_t uiPriority;
    void (*EOSCallback)(void*);
    void* pCallbackData;
} SOUNDPARMS;

// Structure definition for 3D sound parameters being passed down to
//		the sample playing function
typedef struct {
    std::uint32_t uiSpeed;
    std::uint32_t uiPitchBend; // Random pitch bend range +/-
    std::uint32_t uiVolume;    // volume at distance zero
    std::uint32_t uiLoop;
    std::uint32_t uiPriority;
    void (*EOSCallback)(void*);
    void* pCallbackData;

    SOUND3DPOS Pos; // NOT optional, MUST be set
} SOUND3DPARMS;

// Structure definition for parameters to the random sample playing
//		function
typedef struct {
    std::uint32_t uiTimeMin, uiTimeMax;
    std::uint32_t uiSpeedMin, uiSpeedMax;
    std::uint32_t uiVolMin, uiVolMax;
    std::uint32_t uiPanMin, uiPanMax;
    std::uint32_t uiPriority;
    std::uint32_t uiMaxInstances;
} RANDOMPARMS;

// Global startup/shutdown functions
extern bool InitializeSoundManager(void);
extern void ShutdownSoundManager(void);

// Configuration functions
extern bool SoundSetCacheThreshhold(std::uint32_t uiThreshold);

// Master volume control functions
extern void SoundSetDefaultVolume(std::uint32_t uiVolume);

// Cache control functions
extern SoundClip SoundLoadSample(char* pFilename);
extern bool SoundEmptyCache(void);

// Play/service sample functions
extern SoundPlayback SoundPlay(std::string_view pFilename, SOUNDPARMS* pParms);
extern SoundPlayback SoundPlayStreamedFile(std::string_view pFilename, SOUNDPARMS* pParms);

extern SoundClip SoundPlayRandom(char* pFilename, RANDOMPARMS* pParms);
extern bool SoundRandomShouldPlay(SoundClip uiSample);
extern SoundPlayback SoundStartRandom(SoundClip uiSample);
extern bool SoundServiceStreams(void);
extern bool SoundServiceRandom(void);
// Sound instance manipulation functions
extern void SoundSetMusic(SoundPlayback playback);
extern bool SoundStopMusic(void);
extern bool SoundStopAllRandom(void);
extern bool SoundStop(SoundPlayback playback);
extern bool SoundIsPlaying(SoundPlayback playback);
extern bool SoundFileIsPlaying(std::string_view filename);
extern bool SoundSetFadeVolume(SoundPlayback playback, std::uint32_t uiVolume, std::uint32_t uiRate,
                                  bool fStopAtZero);
extern bool SoundSetVolume(SoundPlayback playback, std::uint32_t uiVolume);
extern std::uint32_t SoundGetVolume(SoundPlayback playback);
extern std::uint32_t SoundGetPosition(SoundPlayback playback);
struct SoundPlaybackTime {
    std::uint32_t total_ms;
    std::uint32_t current_ms;
};
extern std::optional<SoundPlaybackTime> SoundGetMilliSecondPosition(SoundPlayback playback);

// Sound instance group functions
extern bool SoundStopGroup(std::uint32_t uiPriority);
extern void SoundSetSampleFlags(SoundClip uiSample, std::uint32_t uiFlags);
extern void SoundRemoveSampleFlags(SoundClip uiSample, std::uint32_t uiFlags);

extern void SoundEnableSound(bool fEnable);

// 3D sound control
extern void Sound3DSetPosition(SoundPlayback playback, float flX, float flY, float flZ);
extern void Sound3DSetListener(float flX, float flY, float flZ);
extern void Sound3DSetDirection(SoundPlayback playback, float flXFace, float flYFace, float flZFace,
                                float flXUp, float flYUp, float flZUp);
extern void Sound3DSetListenerOrientation(float faceX, float faceY, float faceZ,
                                         float upX, float upY, float upZ);
extern void Sound3DSetListenerVelocity(float x, float y, float z);
extern void Sound3DSetEnvironment(std::int32_t iEnvironment);
extern SoundPlayback Sound3DPlay(char* pFilename, SOUND3DPARMS* pParms);
extern SoundPlayback Sound3DStartRandom(SoundClip uiSample, SOUND3DPOS* Pos);

#endif
