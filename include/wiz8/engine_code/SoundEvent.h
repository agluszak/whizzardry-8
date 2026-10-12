#pragma once

#include <cstdint>
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
    W8SoundEvent(W8SoundEventKind kind, std::int32_t cycle, std::int32_t frame, std::int32_t subcycle,
                 std::string wave_name, bool looping);
    W8SoundEvent(const W8SoundEvent&) = delete;
    W8SoundEvent& operator=(const W8SoundEvent&) = delete;
    ~W8SoundEvent();

    bool Play(const srVector3T<float>& position, std::int32_t cycle);

    W8SoundEventKind kind = W8_SOUND_EVENT_NONE;
    std::int32_t cycle = -1;
    std::int32_t frame = -1;
    std::int32_t subcycle = 0;
    std::string wave_name;
    std::int32_t pitch = 0;
    std::int32_t volume_min = 0;
    std::int32_t volume_max = 0;
    wiz8::sound::Playback sound_handle = nullptr;
    std::uint8_t probability = 100;
    bool looping = false;
    std::int32_t location_id = 0;
    float falloff = 12500.0f;
    std::int32_t footstep_volume = 10;
    std::int32_t footstep_combat_volume = 30;
};


void UpdateSoundEvents(const std::vector<std::unique_ptr<W8SoundEvent>>& events,
                       const srVector3T<float>& position, std::uint32_t event_mask,
                       std::int32_t cycle, std::int32_t frame, std::int32_t subcycle);

/* Reports the ground height, surface and material at one position through the
   level's game data: settles a probe with settleToGround and reads the hit
   W8GDSurface's footstep_surface/footstep_material. Engine Code\
   GameData.cpp owns it; only this unit's player consumes it, so its seam
   lives here. */
struct GroundSurfaceInfo {
    float height;
    char surface;
    char material;
};
GroundSurfaceInfo groundSurfaceInfo(const glm::vec3& position); /* 0x00420CA0 */
