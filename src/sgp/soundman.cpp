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
#include "FileMan.h"
#include "LibraryDataBase.h"
#include <wiz8/native_audio.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "random.h"

// Uncomment this to disable the startup of sound hardware
//#define SOUND_DISABLE

// global settings
#define SOUND_MAX_CACHED 128 // number of cache slots

#define SOUND_MAX_CHANNELS 32 // number of mixer channels


#define SOUND_DEFAULT_MEMORY (8048 * 1024) // default memory limit
#define SOUND_DEFAULT_THRESH (256 * 8024)  // size for sample to be double-buffered

// playing/random value to indicate default
#define SOUND_PARMS_DEFAULT 0xffffffff



struct CachedSound
{
    CHAR8 pName[128]{};
    UINT32 uiSize = 0, uiFlags = 0, uiSpeed = 0, uiCacheHits = 0;
    std::shared_ptr<MIX_Mixer> owner;
    w8_native::Audio audio;
    UINT32 uiTimeNext = 0, uiTimeMin = 0, uiTimeMax = 0;
    UINT32 uiSpeedMin = 0, uiSpeedMax = 0, uiVolMin = 0, uiVolMax = 0;
    UINT32 uiPanMin = 0, uiPanMax = 0, uiPriority = 0, uiInstances = 0, uiMaxInstances = 0;
};
struct SoundChannel
{
    std::shared_ptr<MIX_Mixer> owner;
    // Track destruction synchronizes with mixing before closing its input.
    w8_native::AudioInput input;
    w8_native::Track track;
    UINT32 uiSample = NO_SAMPLE, uiSoundID = NO_SAMPLE, uiPriority = PRIORITY_MAX;
    void (*EOSCallback)(void*) = nullptr;
    void* pCallbackData = nullptr;
    UINT32 uiTimeStamp = 0, uiFadeVolume = 0, uiFadeRate = 0, uiFadeTime = 0;
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

// Local Function Prototypes
BOOLEAN SoundInitCache(void);
UINT32 SoundLoadSample(STR pFilename);
UINT32 SoundGetCached(STR pFilename);
UINT32 SoundLoadDisk(STR pFilename);

// Low level
UINT32 SoundGetEmptySample(void);
UINT32 SoundFreeSampleIndex(UINT32 uiSample);
UINT32 SoundGetIndexByID(UINT32 uiSoundID);
BOOLEAN SoundInitHardware(void);
UINT32 SoundGetFreeChannel(void);
UINT32 SoundStartSample(UINT32 uiSample, UINT32 uiChannel, SOUNDPARMS* pParms);
UINT32 SoundStartStream(STR pFilename, UINT32 uiChannel, SOUNDPARMS* pParms);
UINT32 SoundGetUniqueID(void);
BOOLEAN SoundPlayStreamed(STR pFilename);
void SoundResetChannel(UINT32 channel);
BOOLEAN SoundCleanCache(void);
BOOLEAN SoundSampleIsPlaying(UINT32 uiSample);
BOOLEAN SoundIndexIsPlaying(UINT32 uiSound);
BOOLEAN SoundStopIndex(UINT32 uiSound);
UINT32 SoundGetVolumeIndex(UINT32 uiChannel);
BOOLEAN SoundSetVolumeIndex(UINT32 uiChannel, UINT32 uiVolume);

// Global variables
// GLOBAL: WIZ8 0x005ff644
UINT32 guiSoundDefaultVolume = 127;
// GLOBAL: WIZ8 0x005ff648
UINT32 guiSoundMemoryLimit = SOUND_DEFAULT_MEMORY; // Maximum memory used for sounds
// GLOBAL: WIZ8 0x00650e4c
UINT32 guiSoundMemoryUsed = 0; // Memory currently in use
// GLOBAL: WIZ8 0x005ff64c
UINT32 guiSoundCacheThreshold = SOUND_DEFAULT_THRESH; // Double-buffered threshold

// Local module variables
// GLOBAL: WIZ8 0x00650e50
BOOLEAN fSoundSystemInit = FALSE; // Startup called T/F
// GLOBAL: WIZ8 0x005ff651
BOOLEAN gfEnableStartup = TRUE; // Allow hardware to starup

// Sample cache list for files loaded
// GLOBAL: WIZ8 0x006e4aa0
CachedSound pSampleList[SOUND_MAX_CACHED];
// Sound channel list for output channels
// GLOBAL: WIZ8 0x006e4120
SoundChannel pSoundList[SOUND_MAX_CHANNELS];

static std::string ResolveSoundPath(const char* path)
{
    if (!path || !*path) return {};
    std::string filename(path);
    strupr(filename.data());
    if (FileExists(filename.data())) return filename;
    if (filename.ends_with(".WAV")) filename.replace(filename.size() - 4, 4, ".MP3");
    else if (filename.ends_with(".MP3")) filename.replace(filename.size() - 4, 4, ".WAV");
    return filename;
}

// High Level Interface

// SoundEnableSound
//	Allows or disallows the startup of the sound hardware.
//	Returns:	Nothing.

// FUNCTION: WIZ8 0x004086c0
void SoundEnableSound(BOOLEAN fEnable)
{
    gfEnableStartup = fEnable;
}

// InitializeSoundManager
//	Zeros out the structs for the system info, and initializes the cache.
//	Returns:	TRUE always

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
    SoundInitCache();
    guiSoundMemoryUsed = 0;
    guiSoundCacheThreshold = SOUND_DEFAULT_THRESH;
    return TRUE;
}

// ShutdownSoundManager
//		Silences all currently playing sound, deallocates any memory allocated,
//	and releases the sound hardware.

// FUNCTION: WIZ8 0x00408850
void ShutdownSoundManager(void)
{
    for (auto& channel : pSoundList) channel.EOSCallback = nullptr;
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index) SoundStopIndex(index);
    for (UINT32 index = 0; index < SOUND_MAX_CACHED; ++index) SoundFreeSampleIndex(index);
    w8_native::shutdown_audio();
    fSoundSystemInit = FALSE;
}

// SoundPlay
//		Starts a sample playing. If the sample is not loaded in the cache, it will
//	be found and loaded. The pParms structure is used to
//	override the attributes of the sample such as playback speed, and to specify
//	a volume. Any entry containing SOUND_PARMS_DEFAULT will be set by the system.
//	Returns:	If the sound was started, it returns a sound ID unique to that
//						instance of the sound
//						If an error occured, SOUND_ERROR will be returned
//	!!Note:  Can no longer play streamed files

// FUNCTION: WIZ8 0x00408860
UINT32 SoundPlay(const char* path, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit) return SOUND_ERROR;
    auto filename = ResolveSoundPath(path);
    if (filename.empty() || SoundPlayStreamed(filename.data())) return SOUND_ERROR;
    const auto sample = SoundLoadSample(filename.data());
    if (sample == NO_SAMPLE) return SOUND_ERROR;
    const auto channel = SoundGetFreeChannel();
    return channel == SOUND_ERROR ? SOUND_ERROR : SoundStartSample(sample, channel, parameters);
}

// SoundPlayStreamedFile
//		The sample will
//	be played as a double-buffered sample. The pParms structure is used to
//	override the attributes of the sample such as playback speed, and to specify
//	a volume. Any entry containing SOUND_PARMS_DEFAULT will be set by the system.
//	Returns:	If the sound was started, it returns a sound ID unique to that
//						instance of the sound
//						If an error occured, SOUND_ERROR will be returned

// FUNCTION: WIZ8 0x00408ad0
UINT32 SoundPlayStreamedFile(const char* path, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit) return SOUND_ERROR;
    auto filename = ResolveSoundPath(path);
    if (filename.empty()) return SOUND_ERROR;
    const auto channel = SoundGetFreeChannel();
    return channel == SOUND_ERROR ? SOUND_ERROR : SoundStartStream(filename.data(), channel, parameters);
}

// SoundPlayRandom
//		Registers a sample to be played randomly within the specified parameters.
//	Parameters are passed in through pParms. Any parameter containing
//	SOUND_PARMS_DEFAULT will be set by the system. Only the uiTimeMin entry may
//	NOT be defaulted.
//	* Samples designated "random" are ALWAYS loaded into the cache, and locked
//	in place. They are never double-buffered, and this call will fail if they
//	cannot be loaded. *
//	Returns:	If successful, it returns the sample index it is loaded to, else
//						SOUND_ERROR is returned.

// FUNCTION: WIZ8 0x00408d60
UINT32 SoundPlayRandom(STR pFilename, RANDOMPARMS* pParms)
{
    UINT32 uiSample;

    if (fSoundSystemInit && pParms && pParms->uiTimeMin != SOUND_PARMS_DEFAULT) {
        if ((uiSample = SoundLoadSample(pFilename)) != NO_SAMPLE) {
            pSampleList[uiSample].uiFlags |= (SAMPLE_RANDOM | SAMPLE_LOCKED);

            if (pParms->uiTimeMin == SOUND_PARMS_DEFAULT)
                return (SOUND_ERROR);
            else
                pSampleList[uiSample].uiTimeMin = pParms->uiTimeMin;

            if (pParms->uiTimeMax == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiTimeMax = pParms->uiTimeMin;
            else
                pSampleList[uiSample].uiTimeMax = pParms->uiTimeMax;

            pSampleList[uiSample].uiSpeedMin = pParms->uiSpeedMin;

            pSampleList[uiSample].uiSpeedMax = pParms->uiSpeedMax;

            if (pParms->uiVolMin == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiVolMin = guiSoundDefaultVolume;
            else
                pSampleList[uiSample].uiVolMin = pParms->uiVolMin;

            if (pParms->uiVolMax == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiVolMax = guiSoundDefaultVolume;
            else
                pSampleList[uiSample].uiVolMax = pParms->uiVolMax;

            if (pParms->uiPanMin == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiPanMin = 64;
            else
                pSampleList[uiSample].uiPanMin = pParms->uiPanMin;

            if (pParms->uiPanMax == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiPanMax = 64;
            else
                pSampleList[uiSample].uiPanMax = pParms->uiPanMax;

            if (pParms->uiMaxInstances == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiMaxInstances = 1;
            else
                pSampleList[uiSample].uiMaxInstances = pParms->uiMaxInstances;

            if (pParms->uiPriority == SOUND_PARMS_DEFAULT)
                pSampleList[uiSample].uiPriority = PRIORITY_RANDOM;
            else
                pSampleList[uiSample].uiPriority = pParms->uiPriority;

            pSampleList[uiSample].uiInstances = 0;

            pSampleList[uiSample].uiTimeNext =
                GetTickCount() + pSampleList[uiSample].uiTimeMin +
                (pSampleList[uiSample].uiTimeMax > pSampleList[uiSample].uiTimeMin ? Random(pSampleList[uiSample].uiTimeMax - pSampleList[uiSample].uiTimeMin) : 0);
            return (uiSample);
        }
    }

    return (SOUND_ERROR);
}

// SoundIsPlaying
//		Returns TRUE/FALSE that an instance of a sound is still playing.

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

// SoundIndexIsPlaying
// Returns TRUE/FALSE whether a sound channel's sample is currently playing.
// Returns BOOLEAN            - TRUE = playing, FALSE = stopped or nothing allocated
// UINT32 uiSound             - Channel number of sound
// Created:  2/24/00 Derek Beland

BOOLEAN SoundIndexIsPlaying(UINT32 index)
{
    return fSoundSystemInit && index < SOUND_MAX_CHANNELS && pSoundList[index].track &&
           MIX_TrackPlaying(pSoundList[index].track.get());
}

// SoundStop
//		Stops the playing of a sound instance, if still playing.
//	Returns:	TRUE if the sample was actually stopped, FALSE if it could not be
//						found, or was not playing.

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

// SoundStopGroup
//		Stops multiple instances of sounds that have the indicated priority. This
//	is useful for silencing all ambient sounds when switching to menus, etc.
//	Returns:	TRUE if samples were actually stopped, FALSE if none were found

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

// SoundSetDefaultVolume
// Sets the volume to use when a default is not chosen.
// Returns BOOLEAN            -
// UINT32 uiVolume            -
// Created:  3/28/00 Derek Beland

// FUNCTION: WIZ8 0x00409120
void SoundSetDefaultVolume(UINT32 uiVolume)
{
    guiSoundDefaultVolume = __min(uiVolume, 127);
}

// SoundSetFadeVolume
// Sets a target volume to fade towards. The fade volume is updated in SoundServiceStreams.
// Returns BOOLEAN            - TRUE if the fading volume was set, FALSE otherwise
// UINT32 uiSoundID           - ID of sound
// UINT32 uiVolume            - Volume to fade towards (0-127)
// UINT32 uiRate              - Total time taken to change volume
// BOOLEAN fStopAtZero        - If TRUE, sample is stopped when volume reaches zero
// Created:  3/17/00 Derek Beland

// FUNCTION: WIZ8 0x00409140
BOOLEAN SoundSetFadeVolume(UINT32 uiSoundID, UINT32 uiVolume, UINT32 uiRate, BOOLEAN fStopAtZero)
{
    UINT32 uiSound, uiVolCap, uiVolumeDiff;

    if (fSoundSystemInit) {
        uiVolCap = __min(uiVolume, 127);

        if ((uiSound = SoundGetIndexByID(uiSoundID)) != NO_SAMPLE) {
            uiVolumeDiff = abs(static_cast<int>(uiVolCap - SoundGetVolumeIndex(uiSound)));

            if (!uiVolumeDiff)
                return (FALSE);

            pSoundList[uiSound].uiFadeVolume = uiVolCap;
            pSoundList[uiSound].fStopAtZero = fStopAtZero;
            pSoundList[uiSound].uiFadeRate = uiRate / uiVolumeDiff;
            pSoundList[uiSound].uiFadeTime = GetTickCount();

            return (TRUE);
        }
    }

    return (FALSE);
}

// SoundSetVolume
//		Sets the volume on a currently playing sound.
//	Returns:	TRUE if the volume was actually set on the sample, FALSE if the
//						sample had already expired or couldn't be found

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

// SoundSetVolumeIndex
// Sounds the volume on a sound channel.
// Returns BOOLEAN            - TRUE if the volume was set
// UINT32 uiChannel           - Sound channel
// UINT32 uiVolume            - New volume 0-127
// Created:  3/17/00 Derek Beland

BOOLEAN SoundSetVolumeIndex(UINT32 index, UINT32 volume)
{
    if (!fSoundSystemInit || index >= SOUND_MAX_CHANNELS || !pSoundList[index].track) return FALSE;
    auto& channel = pSoundList[index];
    const UINT32 clamped = std::min(volume, 127u);
    if (!MIX_SetTrackGain(channel.track.get(), float(clamped) / 127)) return FALSE;
    channel.volume = clamped;
    return TRUE;
}

// SoundGetVolume
//		Returns the current volume setting of a sound that is playing. If the sound
//	has expired, or could not be found, SOUND_ERROR is returned.

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

// SoundGetVolumeIndex
// Returns the current volume of a sound channel.
// Returns UINT32             - Volume 0-127
// UINT32 uiChannel           - Channel
// Created:  3/17/00 Derek Beland

UINT32 SoundGetVolumeIndex(UINT32 index)
{
    return index < SOUND_MAX_CHANNELS && pSoundList[index].track ? pSoundList[index].volume : SOUND_ERROR;
}

// SoundServiceRandom
//		This function should be polled by the application if random samples are
//	used. The time marks on each are checked and if it is time to spawn a new
//	instance of the sound, the number already in existance are checked, and if
//	there is room, a new one is made and the count updated.
//		If random samples are not being used, there is no purpose in polling this
//	function.
//	Returns:	TRUE if a new random sound was created, FALSE if nothing was done.

// FUNCTION: WIZ8 0x00409310
BOOLEAN SoundServiceRandom(void)
{
    UINT32 uiCount;

    for (uiCount = 0; uiCount < SOUND_MAX_CACHED; uiCount++) {
        if (!(pSampleList[uiCount].uiFlags & SAMPLE_RANDOM_MANUAL) &&
            SoundRandomShouldPlay(uiCount))
            SoundStartRandom(uiCount);
    }

    return (FALSE);
}

// SoundRandomShouldPlay
//	Determines whether a random sound is ready for playing or not.
//	Returns:	TRUE if a the sample should be played.

// FUNCTION: WIZ8 0x00409360
BOOLEAN SoundRandomShouldPlay(UINT32 uiSample)
{
    if (uiSample >= SOUND_MAX_CACHED) return FALSE;
    if (pSampleList[uiSample].uiFlags & SAMPLE_RANDOM)
        if (pSampleList[uiSample].uiTimeNext <= GetTickCount())
            if (pSampleList[uiSample].uiInstances < pSampleList[uiSample].uiMaxInstances)
                return (TRUE);

    return (FALSE);
}

// SoundStartRandom
//	Starts an instance of a random sample.
//	Returns:	TRUE if a new random sound was created, FALSE if nothing was done.

// FUNCTION: WIZ8 0x004093b0
UINT32 SoundStartRandom(UINT32 uiSample)
{
    if (!fSoundSystemInit || uiSample >= SOUND_MAX_CACHED || !pSampleList[uiSample].audio) return SOUND_ERROR;
    UINT32 uiChannel, uiSoundID;
    SOUNDPARMS spParms;

    if ((uiChannel = SoundGetFreeChannel()) != SOUND_ERROR) {
        memset(&spParms, 0xff, sizeof(SOUNDPARMS));

        //		spParms.uiSpeed=pSampleList[uiSample].uiSpeedMin+Random(pSampleList[uiSample].uiSpeedMax-pSampleList[uiSample].uiSpeedMin);
        spParms.uiVolume = pSampleList[uiSample].uiVolMin +
                           (pSampleList[uiSample].uiVolMax > pSampleList[uiSample].uiVolMin ? Random(pSampleList[uiSample].uiVolMax - pSampleList[uiSample].uiVolMin) : 0);
        spParms.uiPan = pSampleList[uiSample].uiPanMin +
                        (pSampleList[uiSample].uiPanMax > pSampleList[uiSample].uiPanMin ? Random(pSampleList[uiSample].uiPanMax - pSampleList[uiSample].uiPanMin) : 0);
        spParms.uiLoop = 1;
        spParms.uiPriority = pSampleList[uiSample].uiPriority;

        if ((uiSoundID = SoundStartSample(uiSample, uiChannel, &spParms)) != SOUND_ERROR) {
            pSampleList[uiSample].uiTimeNext =
                GetTickCount() + pSampleList[uiSample].uiTimeMin +
                (pSampleList[uiSample].uiTimeMax > pSampleList[uiSample].uiTimeMin ? Random(pSampleList[uiSample].uiTimeMax - pSampleList[uiSample].uiTimeMin) : 0);
            pSampleList[uiSample].uiInstances++;
            return (uiSoundID);
        }
    }
    return (NO_SAMPLE);
}

// SoundStopAllRandom
//		This function should be polled by the application if random samples are
//	used. The time marks on each are checked and if it is time to spawn a new
//	instance of the sound, the number already in existance are checked, and if
//	there is room, a new one is made and the count updated.
//		If random samples are not being used, there is no purpose in polling this
//	function.
//	Returns:	TRUE if a new random sound was created, FALSE if nothing was done.

// FUNCTION: WIZ8 0x00409550
BOOLEAN SoundStopAllRandom(void)
{
    UINT32 uiChannel, uiSample;

    // Stop all currently playing random sounds
    for (uiChannel = 0; uiChannel < SOUND_MAX_CHANNELS; uiChannel++) {
        if (pSoundList[uiChannel].track) {
            uiSample = pSoundList[uiChannel].uiSample;

            // if this was a random sample, decrease the iteration count
            if (pSampleList[uiSample].uiFlags & SAMPLE_RANDOM)
                SoundStopIndex(uiChannel);
        }
    }

    // Unlock all random sounds so they can be dumped from the cache, and
    // take the random flag off so they won't be serviced/played
    for (uiSample = 0; uiSample < SOUND_MAX_CACHED; uiSample++) {
        if (pSampleList[uiSample].uiFlags & SAMPLE_RANDOM)
            pSampleList[uiSample].uiFlags &= (~(SAMPLE_RANDOM | SAMPLE_LOCKED));
    }

    return (FALSE);
}

// SoundServiceStreams
//		Can be polled in tight loops where sound buffers might starve due to heavy
//	hardware use, etc. Streams DO NOT normally need to be serviced manually, but
//	in some cases (heavy file loading) it might be desirable.
//		If you are using the end of sample callbacks, you must call this function
//	periodically to check the sample's status.
//	Returns:	TRUE always.

// FUNCTION: WIZ8 0x004095b0
BOOLEAN SoundServiceStreams(void)
{
    if (!fSoundSystemInit) return TRUE;
    for (UINT32 index = 0; index < SOUND_MAX_CHANNELS; ++index)
    {
        auto& channel = pSoundList[index];
        if (!channel.track) continue;
        if (!SoundIndexIsPlaying(index)) { SoundStopIndex(index); continue; }
        const auto time = GetTickCount();
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

// SoundGetPosition
//	Reports the current time position of the sample.
//	Note: You should be checking SoundIsPlaying very carefully while
//	calling this function.
//	Returns:	The current time of the sample in milliseconds.

// FUNCTION: WIZ8 0x004097f0
UINT32 SoundGetPosition(UINT32 uiSoundID)
{
    //UINT32 uiSound, uiFreq=0, uiPosition=0, uiBytesPerSample=0, uiFormat=0;
    UINT32 uiSound, uiTime, uiPosition;

    if (fSoundSystemInit) {
        if ((uiSound = SoundGetIndexByID(uiSoundID)) != NO_SAMPLE) {

            uiTime = GetTickCount();
            // check for rollover
            if (uiTime < pSoundList[uiSound].uiTimeStamp)
                uiPosition = (0 - pSoundList[uiSound].uiTimeStamp) + uiTime;
            else
                uiPosition = (uiTime - pSoundList[uiSound].uiTimeStamp);

            return (uiPosition);
        }
    }

    return (0);
}
//  SoundGetMilliSecondPosition
//  Get the sounds total length and our current position within that
//  sound in milliseconds
//  Returns BOOLEAN:	TRUE if the sound exists.
//  Created by:     Gilles Beauparlant
//  Created on:     7/23/99

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

// Cacheing Subsystem

// SoundInitCache
//		Zeros out the structures of the sample list.

BOOLEAN SoundInitCache(void)
{
    for (auto& sound : pSampleList) sound = CachedSound{};
    return TRUE;
}

// SoundSetCacheThreshold
//		Sets the sound size above which samples will be played double-buffered,
// below which they will be loaded into the cache.
//	Returns: TRUE, always

// FUNCTION: WIZ8 0x004098d0
BOOLEAN SoundSetCacheThreshhold(UINT32 uiThreshold)
{
    if (uiThreshold == 0)
        guiSoundCacheThreshold = SOUND_DEFAULT_THRESH;
    else
        guiSoundCacheThreshold = uiThreshold;

    return (TRUE);
}

// SoundEmptyCache
//		Frees up all samples in the cache.
//	Returns: TRUE, always

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

    for (uiCount = 0; uiCount < SOUND_MAX_CACHED; uiCount++)
        SoundFreeSampleIndex(uiCount);

    return (TRUE);
}

// SoundLoadSample
//		Frees up all samples in the cache.
//	Returns: TRUE, always

UINT32 SoundLoadSample(STR pFilename)
{
    UINT32 uiSample = NO_SAMPLE;

    if ((uiSample = SoundGetCached(pFilename)) != NO_SAMPLE)
        return (uiSample);

    return (SoundLoadDisk(pFilename));
}

// SoundGetCached
//		Tries to locate a sound by looking at what is currently loaded in the
//	cache.
//	Returns: The sample index if successful, NO_SAMPLE if the file wasn't found
//						in the cache.

UINT32 SoundGetCached(STR pFilename)
{
    if (!pFilename || !*pFilename) return NO_SAMPLE;
    UINT32 uiCount;

    for (uiCount = 0; uiCount < SOUND_MAX_CACHED; uiCount++) {
        if (pSampleList[uiCount].audio && _stricmp(pSampleList[uiCount].pName, pFilename) == 0)
            return (uiCount);
    }

    return (NO_SAMPLE);
}

// SoundLoadDisk
//		Loads a sound file from disk into the cache, allocating memory and a slot
//	for storage.
//	Returns: The sample index if successful, NO_SAMPLE if the file wasn't found
//						in the cache.

// FUNCTION: WIZ8 0x00409970
UINT32 SoundLoadDisk(STR filename)
{
    if (!fSoundSystemInit || !filename || strlen(filename) >= sizeof(CachedSound::pName)) return NO_SAMPLE;
    auto input = w8_native::open_audio_input(filename);
    if (!input) return NO_SAMPLE;
    const Sint64 size = SDL_GetIOSize(input.get());
    if (size <= 0 || size > guiSoundMemoryLimit) return NO_SAMPLE;
    while (uint64_t(size) + guiSoundMemoryUsed > guiSoundMemoryLimit)
        if (!SoundCleanCache()) return NO_SAMPLE;
    auto index = SoundGetEmptySample();
    if (index == NO_SAMPLE && SoundCleanCache()) index = SoundGetEmptySample();
    if (index == NO_SAMPLE) return NO_SAMPLE;
    w8_native::Audio audio(MIX_LoadAudio_IO(w8_native::audio_mixer(), input.get(), false, false));
    SDL_AudioSpec format{};
    if (!audio || !MIX_GetAudioFormat(audio.get(), &format)) return NO_SAMPLE;
    auto& sound = pSampleList[index];
    sound = CachedSound{};
    std::copy_n(filename, strlen(filename) + 1, sound.pName);
    strupr(sound.pName);
    sound.uiSize = UINT32(size);
    sound.uiSpeed = UINT32(format.freq);
    sound.uiFlags = SAMPLE_ALLOCATED;
    sound.owner = w8_native::retain_audio_mixer();
    sound.audio = std::move(audio);
    guiSoundMemoryUsed += sound.uiSize;
    return index;
}

// SoundCleanCache
//		Removes the least-used sound from the cache to make room.
//	Returns:	TRUE if a sample was freed, FALSE if none

BOOLEAN SoundCleanCache(void)
{
    UINT32 uiCount, uiLowestHits = NO_SAMPLE, uiLowestHitsCount = 0;

    for (uiCount = 0; uiCount < SOUND_MAX_CACHED; uiCount++) {
        if ((pSampleList[uiCount].uiFlags & SAMPLE_ALLOCATED) &&
            !(pSampleList[uiCount].uiFlags & SAMPLE_LOCKED)) {
            if ((uiLowestHits == NO_SAMPLE) ||
                (uiLowestHitsCount > pSampleList[uiCount].uiCacheHits)) {
                if (!SoundSampleIsPlaying(uiCount)) {
                    uiLowestHits = uiCount;
                    uiLowestHitsCount = pSampleList[uiCount].uiCacheHits;
                }
            }
        }
    }

    if (uiLowestHits != NO_SAMPLE) {
        SoundFreeSampleIndex(uiLowestHits);
        return (TRUE);
    }

    return (FALSE);
}

// Low Level Interface (Local use only)

// SoundSampleIsPlaying
//		Returns TRUE/FALSE that a sample is currently in use for playing a sound.

BOOLEAN SoundSampleIsPlaying(UINT32 uiSample)
{
    UINT32 uiCount;

    for (uiCount = 0; uiCount < SOUND_MAX_CHANNELS; uiCount++) {
        if (pSoundList[uiCount].uiSample == uiSample)
            return (TRUE);
    }

    return (FALSE);
}

// SoundGetEmptySample
//		Returns the slot number of an available sample index.
//	Returns:	A free sample index, or NO_SAMPLE if none are left.

UINT32 SoundGetEmptySample(void)
{
    UINT32 uiCount;

    for (uiCount = 0; uiCount < SOUND_MAX_CACHED; uiCount++) {
        if (!(pSampleList[uiCount].uiFlags & SAMPLE_ALLOCATED))
            return (uiCount);
    }

    return (NO_SAMPLE);
}

// SoundFreeSampleIndex
//		Frees up a sample referred to by it's index slot number.
//	Returns:	Slot number if something was free, NO_SAMPLE otherwise.

UINT32 SoundFreeSampleIndex(UINT32 index)
{
    if (index >= SOUND_MAX_CACHED || !(pSampleList[index].uiFlags & SAMPLE_ALLOCATED)) return NO_SAMPLE;
    // SDL_mixer retains assigned audio; detach the cache index before a slot is reused.
    for (auto& channel : pSoundList)
        if (channel.uiSample == index) channel.uiSample = NO_SAMPLE;
    guiSoundMemoryUsed -= pSampleList[index].uiSize;
    pSampleList[index] = CachedSound{};
    return index;
}

// SoundGetIndexByID
//		Searches out a sound instance referred to by it's ID number.
//	Returns:	If the instance was found, the slot number. NO_SAMPLE otherwise.

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

// Initialize the shared native mixer, or an offline mixer in the test harness.

// FUNCTION: WIZ8 0x00409c50
BOOLEAN SoundInitHardware(void)
{
    return w8_native::initialize_audio();
}

// SoundGetFreeChannel
//		Finds an unused sound channel in the channel list.
//	Returns:	Index of a sound channel if one was found, SOUND_ERROR if not.

// FUNCTION: WIZ8 0x00409f30
void SoundResetChannel(UINT32 index)
{
    auto& channel = pSoundList[index];
    channel.track.reset();
    channel.input.reset();
    channel = SoundChannel{};
    channel.uiTimeStamp = GetTickCount();
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

// SoundStartSample
//		Starts up a sample on the specified channel. Override parameters are passed
//	in through the structure pointer pParms. Any entry with a value of 0xffffffff
//	will be filled in by the system.
//	Returns:	Unique sound ID if successful, SOUND_ERROR if not.

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
static UINT32 StartTrack(UINT32 index, UINT32 sample, const Parameters* parameters, const SOUND3DPOS* position = nullptr)
{
    auto& channel = pSoundList[index];
    const auto naturalRate = MIX_TrackMSToFrames(channel.track.get(), 1000);
    if (naturalRate <= 0) { SoundResetChannel(index); return SOUND_ERROR; }
    UINT32 rate = UINT32(naturalRate);
    if (sample < SOUND_MAX_CACHED && (pSampleList[sample].uiFlags & SAMPLE_RANDOM))
    {
        const auto& cached = pSampleList[sample];
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
    channel.uiSample = sample;
    channel.uiSoundID = SoundGetUniqueID();
    channel.uiTimeStamp = GetTickCount();
    channel.fLooping = loops == 0;
    if (sample < SOUND_MAX_CACHED)
    {
        ++pSampleList[sample].uiCacheHits;
        if (channel.fLooping) pSampleList[sample].uiFlags |= SAMPLE_LOCKED;
    }
    if (parameters && reinterpret_cast<w8_ulong_ptr>(parameters->EOSCallback) != w8_ulong_ptr(-1))
    {
        channel.EOSCallback = parameters->EOSCallback;
        channel.pCallbackData = parameters->pCallbackData;
    }
    return channel.uiSoundID;
}

UINT32 SoundStartSample(UINT32 sample, UINT32 index, SOUNDPARMS* parameters)
{
    if (!fSoundSystemInit || index >= SOUND_MAX_CHANNELS || sample >= SOUND_MAX_CACHED || !pSampleList[sample].audio) return SOUND_ERROR;
    auto& channel = pSoundList[index];
    channel.owner = w8_native::retain_audio_mixer();
    channel.track.reset(MIX_CreateTrack(w8_native::audio_mixer()));
    if (!channel.track || !MIX_SetTrackAudio(channel.track.get(), pSampleList[sample].audio.get()))
    { SoundResetChannel(index); return SOUND_ERROR; }
    channel.duration = MIX_GetAudioDuration(pSampleList[sample].audio.get());
    return StartTrack(index, sample, parameters);
}

// SoundStartStream
//		Starts up a stream on the specified channel. Override parameters are passed
//	in through the structure pointer pParms. Any entry with a value of 0xffffffff
//	will be filled in by the system.
//	Returns:	Unique sound ID if successful, SOUND_ERROR if not.

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
    return StartTrack(index, NO_SAMPLE, parameters);
}

// SoundGetUniqueID
//		Returns a unique ID number with every call. Basically it's just a 32-bit
// static value that is incremented each time.

UINT32 SoundGetUniqueID(void)
{
    static UINT32 uiNextID = 0;

    if (uiNextID == SOUND_ERROR)
        uiNextID++;

    return (uiNextID++);
}

// SoundPlayStreamed
//		Returns TRUE/FALSE whether a sound file should be played as a streamed
//	sample, or loaded into the cache. The decision is based on the size of the
//	file compared to the guiSoundCacheThreshold.
//	Returns:	TRUE if it should be streamed, FALSE if loaded.

BOOLEAN SoundPlayStreamed(STR pFilename)
{
    HWFILE hDisk;
    UINT32 uiFilesize;

    if ((hDisk = FileOpen(pFilename, FILE_ACCESS_READ, FALSE)) != 0) {
        uiFilesize = FileGetSize(hDisk);
        FileClose(hDisk);
        return (uiFilesize >= guiSoundCacheThreshold);
    }

    return (FALSE);
}

// SoundStopIndex
//		Stops a sound referred to by it's slot number. This function is the only
//	one that should be deallocating sample handles. The random sounds have to have
//	their counters maintained, and using this as the central function ensures
//	that they stay in sync.
//	Returns:	TRUE if the sample was stopped, FALSE if it could not be found.

// FUNCTION: WIZ8 0x0040a5c0
BOOLEAN SoundStopIndex(UINT32 index)
{
    if (index >= SOUND_MAX_CHANNELS || !pSoundList[index].track) return FALSE;
    auto& channel = pSoundList[index];
    const auto sample = channel.uiSample;
    const bool looping = channel.fLooping;
    const auto callback = channel.EOSCallback;
    void* data = channel.pCallbackData;
    SoundResetChannel(index);
    if (sample < SOUND_MAX_CACHED)
    {
        auto& cached = pSampleList[sample];
        if ((cached.uiFlags & SAMPLE_RANDOM) && cached.uiInstances) --cached.uiInstances;
        if (looping && !(cached.uiFlags & SAMPLE_RANDOM))
        {
            bool inUse = false;
            for (const auto& other : pSoundList) inUse |= other.uiSample == sample && other.fLooping;
            if (!inUse) cached.uiFlags &= ~SAMPLE_LOCKED;
        }
    }
    // Dispatch only on the game thread, after clearing state: callback reentry is safe.
    if (callback) callback(data);
    return TRUE;
}

// FUNCTIONS TO SET / RESET SAMPLE FLAGS
// FUNCTION: WIZ8 0x0040a8b0
void SoundSetSampleFlags(UINT32 uiSample, UINT32 uiFlags)
{
    // CHECK FOR VALUE SAMPLE
    if (uiSample < SOUND_MAX_CACHED && (pSampleList[uiSample].uiFlags & SAMPLE_ALLOCATED)) {
        // SET
        pSampleList[uiSample].uiFlags |= uiFlags;
    }
}

// FUNCTION: WIZ8 0x0040a8e0
void SoundRemoveSampleFlags(UINT32 uiSample, UINT32 uiFlags)
{
    // CHECK FOR VALID SAMPLE
    if ((pSampleList[uiSample].uiFlags & SAMPLE_ALLOCATED)) {
        //REMOVE
        pSampleList[uiSample].uiFlags &= (~uiFlags);
    }
}

// SoundSampleIsInUse
//	Returns:	TRUE if the sample index is currently being played by the system.

BOOLEAN SoundSampleIsInUse(UINT32 sample)
{
    return SoundSampleIsPlaying(sample);
}

// SoundFileIsPlaying
// Returns true or false on whether a certain file is currently being played. This function
// will only work on sounds loaded into the cache, it will NOT work on streamed sounds.
// Returns BOOLEAN            -
// CHAR8 *pFilename           -
// Created:  2/24/00 Derek Beland

// FUNCTION: WIZ8 0x0040a910
BOOLEAN SoundFileIsPlaying(CHAR8* pFilename)
{
    UINT32 uiCount;

    for (uiCount = 0; uiCount < SOUND_MAX_CHANNELS; uiCount++) {
        if (SoundIndexIsPlaying(uiCount)) {
            // Streamed voices carry SOUND_ERROR instead of a cache index.
            if (pSoundList[uiCount].uiSample >= SOUND_MAX_CACHED) {
                continue;
            }
            if (stricmp(pSampleList[pSoundList[uiCount].uiSample].pName, pFilename) == 0)
                return (TRUE);
        }
    }

    return (FALSE);
}

// SoundSetMusic
// Marks a sample as being music. Cannot be stopped by anything other than SoundStopMusic.
// Returns nothing.
// UINT32 uiSound             - Sound instance to stop
// Created:  3/16/00 Derek Beland

// FUNCTION: WIZ8 0x0040a9a0
void SoundSetMusic(UINT32 uiSoundID)
{
    UINT32 uiSound = SoundGetIndexByID(uiSoundID);

    if (uiSound != NO_SAMPLE)
        pSoundList[uiSound].fMusic = TRUE;
}

// SoundStopMusic
// Stops any sound instance with the music flag.
// Returns nothing.
// Created:  3/16/00 Derek Beland

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
// New 3D Sound Code

// Sound3DSetPosition
// Sets the 3-space position of a sound sample.
// Returns nothing.
// UINT32 uiSample            - ID of sample
// FLOAT flX                  - X coordinate
// FLOAT flY                  - Y coordinate
// FLOAT flZ                  - Z coordinate
// Created:  8/17/99 Derek Beland

// FUNCTION: WIZ8 0x0040ab20
void Sound3DSetPosition(UINT32 id, FLOAT x, FLOAT y, FLOAT z)
{
    const auto index = SoundGetIndexByID(id);
    if (index == NO_SAMPLE || !pSoundList[index].track || !pSoundList[index].spatial) return;
    auto& channel = pSoundList[index];
    channel.position.flX = x; channel.position.flY = y; channel.position.flZ = z;
    UpdateSpatial(channel);
}

// Sound3DSetListener
// Sets the listener location. This should be set to the current camera location.
// Returns nothing.
// FLOAT flX                  - X coordinate
// FLOAT flY                  - Y coordinate
// FLOAT flZ                  - Z coordinate
// Created:  8/17/99 Derek Beland

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

// Sound3DSetDirection
// Sets the orientation of a source. The inputs are two vectors that are *always* at
// right angles to each other. The first is the facing vector, and the second is the up
// vector, which points out of the top of the listeners head.
// Returns nothing.
// FLOAT flXFace              - X coordinate facing
// FLOAT flYFace              - Y coordinate facing
// FLOAT flZFace              - Z coordinate facing
// FLOAT flXUp                - X coordinate up
// FLOAT flYUp                - Y coordinate up
// FLOAT flZUp                - Z coordinate up
// Created:  8/17/99 Derek Beland

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

// Sound3DSetEnvironment
// Sets the current environment type for the listener. This determines which atmospheric
// effects are applied to the 3D sounds. Eg. Caves, underwater, etc.
// Returns nothing.
// INT32 iEnvironment         - Index of environment type
// Created:  8/17/99 Derek Beland

// FUNCTION: WIZ8 0x0040b210
void Sound3DSetEnvironment(INT32)
{
    // The previous native backend had no environmental reverb either.
}

// Sound3DPlay
// Starts a 3D sample playing.
// Returns UINT32             - Sound index
// STR pFilename              - Pointer to filename of sound
// SOUNDPARMS *pParms         - Parameter struct (or NULL for defaults)
// Created:  8/17/99 Derek Beland

// FUNCTION: WIZ8 0x0040abf0
UINT32 Sound3DPlay(STR pFilename, SOUND3DPARMS* pParms)
{
    UINT32 uiSample, uiChannel;

    if (fSoundSystemInit) {
        if ((uiSample = SoundLoadSample(pFilename)) != NO_SAMPLE) {
            if ((uiChannel = SoundGetFreeChannel()) != SOUND_ERROR) {
                return (Sound3DStartSample(uiSample, uiChannel, pParms));
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Sound3DPlay: ERROR: Failed loading sample %s\n", pFilename);
        }
    }

    return (SOUND_ERROR);
}

// Sound3DStartSample
//		Starts up a sample on the specified channel. Override parameters are passed
//	in through the structure pointer pParms. Any entry with a value of 0xffffffff
//	will be filled in by the system.
//	Returns:	Unique sound ID if successful, SOUND_ERROR if not.

// FUNCTION: WIZ8 0x0040ad40
UINT32 Sound3DStartSample(UINT32 sample, UINT32 index, SOUND3DPARMS* parameters)
{
    if (!fSoundSystemInit || !parameters || index >= SOUND_MAX_CHANNELS || sample >= SOUND_MAX_CACHED || !pSampleList[sample].audio) return SOUND_ERROR;
    auto& channel = pSoundList[index];
    channel.owner = w8_native::retain_audio_mixer();
    channel.track.reset(MIX_CreateTrack(w8_native::audio_mixer()));
    if (!channel.track || !MIX_SetTrackAudio(channel.track.get(), pSampleList[sample].audio.get()))
    { SoundResetChannel(index); return SOUND_ERROR; }
    channel.duration = MIX_GetAudioDuration(pSampleList[sample].audio.get());
    return StartTrack(index, sample, parameters, &parameters->Pos);
}

// Sound3DStartRandom
//	Starts an instance of a random sample.
//	Returns:	TRUE if a new random sound was created, FALSE if nothing was done.

// FUNCTION: WIZ8 0x0040b040
UINT32 Sound3DStartRandom(UINT32 uiSample, SOUND3DPOS* pPos)
{
    if (!fSoundSystemInit || uiSample >= SOUND_MAX_CACHED || !pSampleList[uiSample].audio) return SOUND_ERROR;
    UINT32 uiChannel, uiSoundID;
    SOUND3DPARMS sp3DParms;

    if (pPos && ((uiChannel = SoundGetFreeChannel()) != SOUND_ERROR)) {
        memset(&sp3DParms, 0xff, sizeof(SOUND3DPARMS));

        //		sp3DParms.uiSpeed=pSampleList[uiSample].uiSpeedMin+Random(pSampleList[uiSample].uiSpeedMax-pSampleList[uiSample].uiSpeedMin);
        sp3DParms.uiLoop = 1;
        sp3DParms.uiPriority = pSampleList[uiSample].uiPriority;

        //		memcpy(&sp3DParms.Pos, pPos, sizeof(SOUND3DPOS));

        sp3DParms.Pos.flX = pPos->flX;
        sp3DParms.Pos.flY = pPos->flY;
        sp3DParms.Pos.flZ = pPos->flZ;

        sp3DParms.Pos.flVelX = pPos->flVelX;
        sp3DParms.Pos.flVelY = pPos->flVelY;
        sp3DParms.Pos.flVelZ = pPos->flVelZ;

        sp3DParms.Pos.flFaceX = pPos->flFaceX;
        sp3DParms.Pos.flFaceY = pPos->flFaceY;
        sp3DParms.Pos.flFaceZ = pPos->flFaceZ;

        sp3DParms.Pos.flUpX = pPos->flUpX;
        sp3DParms.Pos.flUpY = pPos->flUpY;
        sp3DParms.Pos.flUpZ = pPos->flUpZ;

        sp3DParms.Pos.flFalloffMax = pPos->flFalloffMax;
        sp3DParms.Pos.flFalloffMin = pPos->flFalloffMin;

        sp3DParms.Pos.uiVolume = pPos->uiVolume;
        sp3DParms.uiVolume = pPos->uiVolume;

        if ((uiSoundID = Sound3DStartSample(uiSample, uiChannel, &sp3DParms)) != SOUND_ERROR) {
            pSampleList[uiSample].uiTimeNext =
                GetTickCount() + pSampleList[uiSample].uiTimeMin +
                (pSampleList[uiSample].uiTimeMax > pSampleList[uiSample].uiTimeMin ? Random(pSampleList[uiSample].uiTimeMax - pSampleList[uiSample].uiTimeMin) : 0);
            pSampleList[uiSample].uiInstances++;
            return (uiSoundID);
        }
    }

    return (NO_SAMPLE);
}
