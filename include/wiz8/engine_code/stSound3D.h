#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "soundman.h"
#include "wiz8/audio_resources.h"
#include "wiz8/geometry.h"

class stSound3D;
// Borrowed update index; callers or the audio service own every entry.
extern std::vector<stSound3D*> g_sound3d_instances;
stSound3D* Retain3DSound(std::unique_ptr<stSound3D> sound);
void Release3DSounds();
void Update3DSounds();

class stSound3D {
public:
    stSound3D(std::string_view sound_name);
    stSound3D(const stSound3D& other) : stSound3D(other.wave_name.c_str()) { *this = other; }
    ~stSound3D();
    bool IsPlaying();
    void Stop();
    // Loop until stopped; the caller keeps ownership unless it calls Retain3DSound.
    unsigned char Play(bool loop);
    wiz8::sound::PlayOptions BuildSoundOptions(const glm::vec3& listener);

    void setLocation(const glm::vec3& pos) { position = pos; }
    void setLocation(double x, double y, double z)
    {
        position = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    }
    const glm::vec3& getLocation() const { return position; }

    glm::vec3 position{0.0f};
    wiz8::sound::Playback sound_handle; // Current playback; empty when silent.
    int volume = 0x7f;                  // base volume before distance/effects scale
    float falloff = 25000.0f;           // audible range in world units
    std::string wave_name;

    /* Copy the wave filename and placement, but leave a live playback handle unshared. */
    stSound3D& operator=(const stSound3D& other)
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
