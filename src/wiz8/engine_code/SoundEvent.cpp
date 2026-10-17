#include "wiz8/engine_code/SoundEvent.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "random.h"
#include "soundman.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/xstatus.h"

#define SOUNDEVENT_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\SoundEvent.cpp"

// Looping events own the voice they start.
// FUNCTION: WIZ8 0x004d5770
W8SoundEvent::~W8SoundEvent()
{
    if (looping && sound_handle != nullptr) {
        wiz8::sound::stop(sound_handle);
    }
}

/* The wave name is copied into storage the event owns and its destructor
   releases. */
// FUNCTION: WIZ8 0x004d57a0
W8SoundEvent::W8SoundEvent(W8SoundEventKind kind, int cycle, int frame, int subcycle,
                           std::string wave_name, bool looping)
    : kind(kind), cycle(cycle), frame(frame), subcycle(subcycle), wave_name(std::move(wave_name)),
      looping(looping)
{
}

// GLOBAL: WIZ8 0x0061095c
static int g_last_sound_event = -1;

// GLOBAL: WIZ8 0x00683420
static int g_previous_footstep_variant;

/* For every bit of the caller's mask, append the events of that kind whose
   own classification matches, then play one of them at random without
   repeating the previous choice for as long as a second candidate exists. */
// FUNCTION: WIZ8 0x004d5890
void UpdateSoundEvents(const std::vector<std::unique_ptr<W8SoundEvent>>& events,
                       const glm::vec3& position, unsigned int event_mask, int cycle, int frame,
                       int subcycle)
{
    std::vector<W8SoundEvent*> candidates;
    for (unsigned int bit = 1; bit != 0; bit <<= 1) {
        if ((bit & event_mask) == 0) continue;
        candidates.clear();
        for (const auto& event : events) {
            const auto kind = event->kind;
            if ((bit & kind) == 0) continue;
            if (kind == W8_SOUND_EVENT_FRAME || kind == W8_SOUND_EVENT_FOOTSTEP) {
                if ((event->cycle != -1 && event->cycle != cycle) || event->frame != frame ||
                    event->subcycle != subcycle)
                    continue;
            } else if (kind == W8_SOUND_EVENT_CYCLE) {
                if (event->cycle != cycle || event->subcycle != subcycle) continue;
            }
            candidates.push_back(event.get());
        }
        if (candidates.empty()) continue;
        int selected;
        do {
            selected = static_cast<int>(Random(candidates.size()));
        } while (candidates.size() > 1 && selected == g_last_sound_event);
        if (candidates[selected]->Play(position, cycle)) g_last_sound_event = selected;
    }
}

/* Resolve one event into a positional 3D play: decide the base volume from
   the event's own routing, rotate the listener offset into the camera frame,
   and let the ground/step branch rebuild the wave name before the play. */
// FUNCTION: WIZ8 0x004d5a10
bool W8SoundEvent::Play(const glm::vec3& position, int cycle)
{
    W8Monster* monster = 0;
    bool track_sound = false;

    if (IsSoundEffectsMuted() || !Chance(probability)) {
        return false;
    }
    float angle = -GetCameraYawRadians();
    unsigned int base_volume = 0x78;
    glm::vec3 camera_position = cameraLocation();
    glm::vec3 camera_offset = camera_position - position;
    if (glm::length(camera_offset) < falloff) {
        if (kind == W8_SOUND_EVENT_FOOTSTEP || wave_name.contains("step") ||
            (location_id != 0 && cycle == 4)) {
            base_volume = footstep_volume;
            track_sound = true;
            if (location_id != 0) {
                monster = GetMonsterByLocationID(location_id);
                if (gXStatus.fCombatMode || monster->nearest_to_party) {
                    base_volume = footstep_combat_volume;
                }
            }
        } else if (volume_min == 0 && volume_max == 0) {
            if (wave_name.contains('+')) {
                base_volume = 0x87;
            }
        } else {
            base_volume =
                static_cast<unsigned int>(Random(volume_max - volume_min + 1)) + volume_min;
        }

        glm::mat3 rotation;

        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));

        glm::vec3 offset = position - camera_position;
        glm::vec3 transformed = rotation * offset;
        unsigned int event_volume =
            static_cast<unsigned int>((1.0f - glm::length(camera_offset) / falloff) * base_volume);
        event_volume = (g_settings.sound_effects_volume * event_volume) / 0x7f;
        if (event_volume != 0) {

            if (kind == W8_SOUND_EVENT_FOOTSTEP) {
                int variant;
                int attempts = 0;
                do {
                    variant = static_cast<int>(Random(4)) + 1;
                    ++attempts;
                    if (variant != g_previous_footstep_variant) {
                        break;
                    }
                } while (attempts < 100);
                g_previous_footstep_variant = variant;

                char path[260];
                const auto ground = groundSurfaceInfo(position);
                /* Retail hardcodes the surface to OutdoorsFlat here: the query
                   result that matters for the path is the material. */
                BuildFootstepPath(path, W8_FOOTSTEP_SURFACE_OUTDOORS_FLAT, ground.material,
                                  W8_FOOTSTEP_KIND_STEP, variant);
                wave_name = path;
            }
            sound_handle = wiz8::sound::play(
                wave_name, {.volume = event_volume,
                            .loops = looping ? 0u : 1u,
                            .pitch_bend_percent = wave_name.contains('!') ? 0u : 20u,
                            .position = transformed});
            if (sound_handle == nullptr) {
                return false;
            }
            if (track_sound && monster != 0) {
                monster->TrackSound(sound_handle);
            }
        }
    }
    return true;
}
