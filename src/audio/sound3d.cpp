#include "wiz8/audio/sound3d.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/local_code/Configuration.h"

std::vector<Sound3D*> g_sound3d_instances;
// Only fire-and-forget positional sounds belong to the audio service.
static std::vector<std::unique_ptr<Sound3D>> g_service_sounds;

Sound3D* Retain3DSound(std::unique_ptr<Sound3D> sound)
{
    auto* borrowed = sound.get();
    g_service_sounds.push_back(std::move(sound));
    return borrowed;
}

void Release3DSounds()
{
    g_service_sounds.clear();
}

Sound3D::Sound3D(std::string_view name)
{
    wave_name = name;
    sound_handle = nullptr;
    volume = 0x7f;
    falloff = 25000.0f;
    g_sound3d_instances.push_back(this);
}

Sound3D::~Sound3D()
{
    if (sound_handle != nullptr) {
        wiz8::sound::stop(sound_handle);
    }
    wiz8::erase_first(g_sound3d_instances, this);
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
    const unsigned int scaled_volume = (volume * g_settings.sound_effects_volume) / 0x7f;
    const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
    const glm::vec3 node_position = position;
    const glm::vec3 offset = node_position - listener;
    const glm::vec3 listener_offset = listener - node_position;
    return {.volume = static_cast<unsigned int>((1.0f - glm::length(listener_offset) / falloff) *
                                                scaled_volume),
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

/* Update borrowed positional nodes, then retire finished service-owned sounds.
   Caller-owned sounds retain their independent lifetime. */
void Update3DSounds()
{
    glm::vec3 listener = cameraLocation();
    int index = 0;
    int count = static_cast<int>(g_sound3d_instances.size());
    if (count > 0) {
        do {
            Sound3D* sound = g_sound3d_instances[index];
            if (sound->sound_handle != nullptr) {
                const glm::vec3 world = sound->position;
                if (!wiz8::sound::is_playing(sound->sound_handle)) {
                    sound->sound_handle = nullptr;
                } else {
                    const glm::vec3 to_listener = listener - world;
                    const float distance = glm::length(to_listener);
                    if (sound->falloff <= distance) {
                        wiz8::sound::set_volume(sound->sound_handle, 0);
                    } else {
                        const float angle = -GetCameraYawRadians();
                        const glm::mat3 rotation =
                            glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
                        const glm::vec3 offset = world - listener;
                        const glm::vec3 transformed = rotation * offset;
                        wiz8::sound::set_position(sound->sound_handle, transformed);
                        const unsigned int volume =
                            (sound->volume * g_settings.sound_effects_volume) / 0x7f;
                        wiz8::sound::set_volume(sound->sound_handle,
                                                static_cast<std::uint32_t>(
                                                    (1.0f - distance / sound->falloff) * volume));
                    }
                }
            }
            ++index;
        } while (index < count);
    }
    std::erase_if(g_service_sounds, [](const auto& sound) { return !sound->IsPlaying(); });
}
