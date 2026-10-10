#include <string_view>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __SOUNDMAN_
#define __SOUNDMAN_

#include "Types.h"
#include <memory>

struct AudioClip;
// Owns decoded audio, not an entry in a numeric resource registry.
using SoundClip = std::shared_ptr<AudioClip>;

extern BOOLEAN gfEnableStartup;

// Sample status flags
#define SAMPLE_LOCKED 0x00000002
#define SAMPLE_RANDOM 0x00000004
#define SAMPLE_RANDOM_MANUAL 0x00000008

// Sound error values (they're all the same)
#define NO_SAMPLE 0xffffffff
#define SOUND_ERROR 0xffffffff

// Maximum allowable priority value
#define PRIORITY_MAX 0xfffffffe
#define PRIORITY_RANDOM PRIORITY_MAX - 1

// Structure definition for 3D sound positional information used by
// various other structs and functions
typedef struct {
    FLOAT flX, flY, flZ;
    FLOAT flVelX, flVelY, flVelZ;
    FLOAT flFaceX, flFaceY, flFaceZ;
    FLOAT flUpX, flUpY, flUpZ;
    FLOAT flFalloffMin, flFalloffMax;
    UINT32 uiVolume;
} SOUND3DPOS;

// Structure definition for sound parameters being passed down to
//		the sample playing function
typedef struct {
    UINT32 uiSpeed;
    UINT32 uiPitchBend; // Random pitch bend range +/-
    UINT32 uiVolume;
    UINT32 uiPan;
    UINT32 uiLoop;
    UINT32 uiPriority;
    void (*EOSCallback)(void*);
    void* pCallbackData;
} SOUNDPARMS;

// Structure definition for 3D sound parameters being passed down to
//		the sample playing function
typedef struct {
    UINT32 uiSpeed;
    UINT32 uiPitchBend; // Random pitch bend range +/-
    UINT32 uiVolume;    // volume at distance zero
    UINT32 uiLoop;
    UINT32 uiPriority;
    void (*EOSCallback)(void*);
    void* pCallbackData;

    SOUND3DPOS Pos; // NOT optional, MUST be set
} SOUND3DPARMS;

// Structure definition for parameters to the random sample playing
//		function
typedef struct {
    UINT32 uiTimeMin, uiTimeMax;
    UINT32 uiSpeedMin, uiSpeedMax;
    UINT32 uiVolMin, uiVolMax;
    UINT32 uiPanMin, uiPanMax;
    UINT32 uiPriority;
    UINT32 uiMaxInstances;
} RANDOMPARMS;

// Global startup/shutdown functions
extern BOOLEAN InitializeSoundManager(void);
extern void ShutdownSoundManager(void);

// Configuration functions
extern BOOLEAN SoundSetCacheThreshhold(UINT32 uiThreshold);

// Master volume control functions
extern void SoundSetDefaultVolume(UINT32 uiVolume);

// Cache control functions
extern SoundClip SoundLoadSample(STR pFilename);
extern BOOLEAN SoundEmptyCache(void);

// Play/service sample functions
extern UINT32 SoundPlay(std::string_view pFilename, SOUNDPARMS* pParms);
extern UINT32 SoundPlayStreamedFile(std::string_view pFilename, SOUNDPARMS* pParms);

extern SoundClip SoundPlayRandom(STR pFilename, RANDOMPARMS* pParms);
extern BOOLEAN SoundRandomShouldPlay(SoundClip uiSample);
extern UINT32 SoundStartRandom(SoundClip uiSample);
extern BOOLEAN SoundServiceStreams(void);
extern BOOLEAN SoundServiceRandom(void);
// Sound instance manipulation functions
extern void SoundSetMusic(UINT32 uiSound);
extern BOOLEAN SoundStopMusic(void);
extern BOOLEAN SoundStopAllRandom(void);
extern BOOLEAN SoundStop(UINT32 uiSoundID);
extern BOOLEAN SoundIsPlaying(UINT32 uiSoundID);
extern BOOLEAN SoundFileIsPlaying(std::string_view filename);
extern BOOLEAN SoundSetFadeVolume(UINT32 uiSoundID, UINT32 uiVolume, UINT32 uiRate,
                                  BOOLEAN fStopAtZero);
extern BOOLEAN SoundSetVolume(UINT32 uiSoundID, UINT32 uiVolume);
extern UINT32 SoundGetVolume(UINT32 uiSoundID);
extern UINT32 SoundGetPosition(UINT32 uiSoundID);
extern BOOLEAN SoundGetMilliSecondPosition(UINT32 uiSoundID, UINT32* puiTotalMilliseconds,
                                           UINT32* puiCurrentMilliseconds);

// Sound instance group functions
extern BOOLEAN SoundStopGroup(UINT32 uiPriority);
extern void SoundSetSampleFlags(SoundClip uiSample, UINT32 uiFlags);
extern void SoundRemoveSampleFlags(SoundClip uiSample, UINT32 uiFlags);

extern void SoundEnableSound(BOOLEAN fEnable);

// 3D sound control
extern void Sound3DSetPosition(UINT32 uiSample, FLOAT flX, FLOAT flY, FLOAT flZ);
extern void Sound3DSetListener(FLOAT flX, FLOAT flY, FLOAT flZ);
extern void Sound3DSetDirection(UINT32 uiSample, FLOAT flXFace, FLOAT flYFace, FLOAT flZFace,
                                FLOAT flXUp, FLOAT flYUp, FLOAT flZUp);
extern void Sound3DSetListenerOrientation(FLOAT faceX, FLOAT faceY, FLOAT faceZ,
                                         FLOAT upX, FLOAT upY, FLOAT upZ);
extern void Sound3DSetListenerVelocity(FLOAT x, FLOAT y, FLOAT z);
extern void Sound3DSetEnvironment(INT32 iEnvironment);
extern UINT32 Sound3DPlay(STR pFilename, SOUND3DPARMS* pParms);
extern UINT32 Sound3DStartSample(SoundClip uiSample, UINT32 uiChannel, SOUND3DPARMS* pParms);
extern UINT32 Sound3DStartRandom(SoundClip uiSample, SOUND3DPOS* Pos);

#endif
