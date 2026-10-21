#include "wiz8/audio/sound3d.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/spatial/level_spatial_index.h"

Sound3D::Sound3D(std::string_view name)
{
    wave_name = name;
    sound_handle = nullptr;
    volume = 0x7f;
    falloff = 25000.0f;
}

Sound3D::~Sound3D()
{
    if (sound_handle != nullptr) {
        wiz8::sound::stop(sound_handle);
    }
}

unsigned char Sound3D::Play(bool loop)
{
    if (wave_name.empty()) {
        return 0;
    }
    auto options = BuildSoundOptions(cameraLocation());
    if (loop) {
        options.loops = 0;
    }
    sound_handle = wiz8::sound::play(wave_name, std::move(options));
    return sound_handle != nullptr;
}

wiz8::sound::PlayOptions Sound3D::BuildSoundOptions(const glm::vec3& listener)
{
    const float angle = -GetCameraYawRadians();
    const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
    const glm::vec3 node_position = position;
    const glm::vec3 offset = node_position - listener;
    const glm::vec3 listener_offset = listener - node_position;
    return {.volume = static_cast<unsigned int>(volume),
            .gain_scale =
                falloff > 0 ? std::clamp(1.0f - glm::length(listener_offset) / falloff, 0.0f, 1.0f)
                            : 0.0f,
            .position = rotation * offset};
}

bool Sound3D::IsPlaying()
{
    if (sound_handle != nullptr && wiz8::sound::is_playing(sound_handle)) {
        return true;
    }
    return false;
}

void Sound3D::Stop()
{
    if (sound_handle != nullptr) {
        wiz8::sound::stop(sound_handle);
        sound_handle = nullptr;
    }
}

void Sound3D::Update(const glm::vec3& listener, float yaw)
{
    if (!sound_handle) return;
    if (!wiz8::sound::is_playing(sound_handle)) {
        sound_handle = nullptr;
        return;
    }
    const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(-yaw, glm::vec3(0, 1, 0)));
    wiz8::sound::set_position(sound_handle, rotation * (position - listener));
    const float gain =
        falloff > 0 ? std::clamp(1.0f - glm::length(position - listener) / falloff, 0.0f, 1.0f) : 0;
    wiz8::sound::set_volume(sound_handle, volume, gain,
                            wiz8::sound::VolumeRounding::after_attenuation);
}
