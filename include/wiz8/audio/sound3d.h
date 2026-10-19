#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/audio_resources.h"
#include "wiz8/geometry.h"

class Sound3D {
public:
    Sound3D(std::string_view sound_name);
    Sound3D(const Sound3D& other) : Sound3D(other.wave_name.c_str()) { *this = other; }
    ~Sound3D();
    bool IsPlaying();
    void Update(const glm::vec3& listener, float yaw, unsigned effects_volume);
    void Stop();
    // The caller owns this voice for its complete lifetime.
    unsigned char Play(bool loop);
    wiz8::sound::PlayOptions BuildSoundOptions(const glm::vec3& listener);

    glm::vec3 position{0.0f};
    wiz8::sound::Playback sound_handle; // Current playback; empty when silent.
    int volume = 0x7f;                  // base volume before distance/effects scale
    float falloff = 25000.0f;           // audible range in world units
    std::string wave_name;

    /* Copy the wave filename and placement, but leave a live playback handle unshared. */
    Sound3D& operator=(const Sound3D& other)
    {
        if (this == &other) return *this;
        Stop();
        position = other.position;
        wave_name = other.wave_name;
        falloff = other.falloff;
        volume = other.volume;
        return *this;
    }
};
