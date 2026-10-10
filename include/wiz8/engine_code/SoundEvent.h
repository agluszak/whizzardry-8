#pragma once

#include "wiz8/audio_resources.h"

#include <memory>
#include <string>
#include <vector>

#include "surrender/srMath.h"

/* Engine Code\SoundEvent.cpp. The unit's assertion-backed interval starts at
   the factory below. */

/* Event-kind bits: the mls script keywords SOUND_FRAME, SOUND_CYCLE and
   SOUND_FOOTSTEP map to these values (Monster.cpp loader), and
   UpdateSoundEvents iterates the caller's mask one bit at a time, so a kind
   is both a classification and a single-bit mask. */
enum W8SoundEventKind {
    W8_SOUND_EVENT_UNRECOGNIZED = -1,
    W8_SOUND_EVENT_NONE = 0,
    W8_SOUND_EVENT_FRAME = 0x1,
    W8_SOUND_EVENT_CYCLE = 0x2,
    W8_SOUND_EVENT_FOOTSTEP = 0x100,
};

// An event owns its wave name and, while looping, its live voice.
class W8SoundEvent {
public:
    W8SoundEvent() = default;
    W8SoundEvent(W8SoundEventKind kind, int cycle, int frame, int subcycle,
                 std::string wave_name, bool looping);
    W8SoundEvent(const W8SoundEvent&) = delete;
    W8SoundEvent& operator=(const W8SoundEvent&) = delete;
    ~W8SoundEvent();

    bool Play(const srVector3T<float>& position, int cycle);

    W8SoundEventKind kind = W8_SOUND_EVENT_NONE;
    int cycle = -1;
    int frame = -1;
    int subcycle = 0;
    std::string wave_name;
    int pitch = 0;
    int volume_min = 0;
    int volume_max = 0;
    SoundPlayback sound_handle = nullptr;
    unsigned char probability = 100;
    bool looping = false;
    int location_id = 0;
    float falloff = 12500.0f;
    int footstep_volume = 10;
    int footstep_combat_volume = 30;
};


void UpdateSoundEvents(const std::vector<std::unique_ptr<W8SoundEvent>>& events,
                       const srVector3T<float>& position, unsigned int event_mask,
                       int cycle, int frame, int subcycle);

/* Reports the ground height, surface and material at one position through the
   level's game data: settles a probe with SettleToGround and reads the hit
   W8GDSurface's footstep_surface/footstep_material. Engine Code\
   GameData.cpp owns it; only this unit's player consumes it, so its seam
   lives here. */
float GetGroundSurfaceInfo(const srVector3T<float>* position, char* surface,
                           char* material); /* 0x00420CA0 */
