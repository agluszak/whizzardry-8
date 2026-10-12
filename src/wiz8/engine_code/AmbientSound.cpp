#include <utility>
#include <optional>
#include <cstdint>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>

#include "wiz8/audio_resources.h"
#include "surrender/srMath.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/sr_api.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/float_constants.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/xstatus.h"
#include "wiz8/virtual_file.h"
#include "wiz8/filesystem.h"
#include "random.h"
#include "soundman.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/PolyPick.h"
#include "compat/native.h"
#include "game_timer.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"

// GLOBAL: WIZ8 0x005ec5a8
const float g_ambient_sound_inner_radius_scale = 0.6000000238418579f;

/* Random one-shot schedule. List-file sounds fire on their own as 2D one-shots;
   emitters roll their own schedule and fire positioned. Retail allows one
   instance of each. */
struct W8RandomAmbient
{
    wiz8::sound::Clip clip;
    std::uint32_t time_min = 0, time_max = 0;
    std::uint32_t volume_min = 0, volume_max = 0;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> speed;
    bool automatic = false;
    bool enabled = true;
    std::uint64_t next_at = 0;
    wiz8::sound::Playback instance;

    void Reschedule() { next_at = w8_get_ticks() + RandomRange(time_min, time_max); }
    bool Due() const
    {
        return enabled && next_at <= w8_get_ticks() && !wiz8::sound::is_playing(instance);
    }
    wiz8::sound::Playback Fire(std::uint32_t volume, std::optional<glm::vec3> position)
    {
        auto playback = wiz8::sound::play(
            clip, {.volume = volume,
                   .rate = speed ? std::optional(RandomRange(speed->first, speed->second)) : std::nullopt,
                   .group = wiz8::sound::Group::ambient,
                   .position = position});
        if (playback) {
            Reschedule();
            instance = playback;
        }
        return playback;
    }
    static std::uint32_t RandomRange(std::uint32_t minimum, std::uint32_t maximum)
    {
        return minimum + (maximum > minimum ? Random(maximum - minimum) : 0);
    }
};

namespace {
std::vector<std::shared_ptr<W8RandomAmbient>> g_random_ambients;

/* -1 marks unset values in ambient records; an unset interval disables the schedule. */
std::shared_ptr<W8RandomAmbient> RegisterRandomAmbient(const char* path, std::int32_t time_min, std::int32_t time_max,
                                                       std::int32_t speed_min, std::int32_t speed_max,
                                                       std::uint32_t volume_min,
                                                       std::uint32_t volume_max, bool automatic)
{
    if (time_min == -1) {
        return {};
    }
    auto clip = wiz8::sound::load(path);
    if (!clip) {
        return {};
    }
    auto random = std::make_shared<W8RandomAmbient>();
    random->clip = std::move(clip);
    random->time_min = std::uint32_t(time_min);
    random->time_max = time_max == -1 ? random->time_min : std::uint32_t(time_max);
    random->volume_min = volume_min;
    random->volume_max = volume_max;
    if (speed_min != -1 && speed_max != -1) {
        random->speed.emplace(std::uint32_t(speed_min), std::uint32_t(speed_max));
    }
    random->automatic = automatic;
    random->Reschedule();
    g_random_ambients.push_back(random);
    return random;
}

void ServiceRandomAmbients()
{
    // Starting playback may dispatch callbacks that register or stop schedules.
    const auto schedules = g_random_ambients;
    for (const auto& random : schedules) {
        if (random->automatic && random->Due()) {
            random->Fire(W8RandomAmbient::RandomRange(random->volume_min, random->volume_max),
                         std::nullopt);
        }
    }
}
} // namespace

// FUNCTION: WIZ8 0x00479030
void StopAllAmbientSounds()
{
    for (const auto& random : g_random_ambients) {
        random->enabled = false;
    }
    g_random_ambients.clear();
    wiz8::sound::stop_group(wiz8::sound::Group::ambient);
}

// FUNCTION: WIZ8 0x00479040
W8AmbientSound::W8AmbientSound()
    : volume_min(0), volume_max(0), current_volume(0), speed_min(0), speed_max(0), time_min(0),
      time_max(0), radius(0), in_range(0), looping(0), sound_handle(), random(),
      shared(0), bounded(0), region_angle(0)
{
    config.wave_name[0] = 0;
    position.SetZero();
    pacSoundName = 0;
    stopped = 0;
}

/* Region test used by UpdatePosition and the group handoff: translate the
   listener into region-center space, un-apply the per-axis scale, rotate back
   around the configured axis, then test against the min/max bounds. */
// FUNCTION: WIZ8 0x004790d0
bool W8AmbientSound::IsInsideRegion(const srVector3T<float>* listener)
{
    srVector3T<float> relative;
    float scale_x;
    float scale_y;
    float scale_z;

    relative = *listener - region_center;
    scale_x = g_float_one / region_scale.x;
    scale_y = g_float_one / region_scale.y;
    scale_z = g_float_one / region_scale.z;
    if (scale_x != g_float_one || scale_y != g_float_one || scale_z != g_float_one) {
        relative.x = scale_x * relative.x;
        relative.y *= scale_y;
        relative.z *= scale_z;
    }
    if (region_angle != g_float_zero &&
        (region_axis.x != g_float_zero || region_axis.y != g_float_zero ||
         region_axis.z != g_float_zero)) {
        glm::mat3 rotation;
        double angle = -region_angle;

        rotation = glm::mat3(1);
        if (angle != g_double_zero) {
            rotation *=
                glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                              normalizedOrZero(glm::vec3(region_axis.glmValue()))));
            }
            relative = rotation * relative.glmValue();
    }
    return PointInsideBounds(&relative, &region_min, &region_max);
}

/* Listener-relative servicing. Out of range: shared sounds try to hand their
   live voice to a matching group member that is in range, otherwise fade to
   zero; positional sounds stop immediately. In range: mark the entry, restart
   one-shot scheduling, re-aim a live 3D voice around the camera, and nudge its
   volume up inside the inner 40% of the radius. Shared sounds retarget the
   fade at the bus-scaled maximum. */
// FUNCTION: WIZ8 0x00479350
void W8AmbientSound::UpdatePosition(const srVector3T<float>* listener)
{
    bool handed_off = false;

    if (stopped != 0) {
        return;
    }
    {
        srVector3T<float> to_listener = *listener - position;
        float distance = to_listener.Length();
        std::uint8_t inside = IsInsideRegion(listener);

        if (radius <= distance || (bounded != 0 && inside == 0)) {
            if (in_range != 0) {
                in_range = 0;
                if (shared != 0) {
                    W8AmbientSound* match = FindNextMatching(config.wave_name, 0);
                    while (match != 0) {
                        if (handed_off) {
                            return;
                        }
                        {
                            srVector3T<float> to_match = *listener - match->position;
                            std::uint8_t match_inside = match->IsInsideRegion(listener);
                            if (to_match.Length() < match->radius &&
                                (match->bounded == 0 || match_inside != 0) &&
                                match->sound_handle == nullptr) {
                                match->in_range = 1;
                                match->sound_handle = sound_handle;
                                match->target_volume =
                                    (match->volume_max * g_settings.sound_effects_volume) / 0x7f;
                                match->current_volume = wiz8::sound::volume(sound_handle).value_or(0);
                                match->fade_timer.SetDuration(g_float_one_and_a_half /
                                                              match->target_volume);
                                match->fade_timer.Restart();
                                match->fade_timer.m_flags.paused = false;
                                match->fade_timer.m_start =
                                    match->fade_timer.GetTime() - match->fade_timer.m_start;
                                match->fade_timer.SetDuration(-1.0f);
                                sound_handle = nullptr;
                                handed_off = true;
                            }
                        }
                        match = FindNextMatching(config.wave_name, match);
                    }
                    if (handed_off) {
                        return;
                    }
                    target_volume = 0;
                    {
                        std::uint32_t full_volume =
                            (volume_max * g_settings.sound_effects_volume) / 0x7f;
                        fade_timer.SetDuration(g_float_one_and_a_half / full_volume);
                        fade_timer.Restart();
                        fade_timer.m_flags.paused = false;
                        fade_timer.m_start = fade_timer.GetTime() - fade_timer.m_start;
                        fade_timer.SetDuration(-1.0f);
                    }
                    return;
                }
                if (sound_handle != nullptr) {
                    wiz8::sound::stop(sound_handle);
                    sound_handle = nullptr;
                }
            }
            return;
        }
        if (in_range == 0) {
            in_range = 1;
            Service(true);
        }
        if (shared == 0) {
            if (sound_handle != nullptr) {
                float angle = -GetCameraYawRadians();
                glm::mat3 rotation;
                srVector3T<float> transformed;
                std::uint32_t volume;

                rotation = glm::mat3(1);
                rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
                srVector3T<float> offset = position - *listener;
                transformed = rotation * offset.glmValue();
                wiz8::sound::set_position(sound_handle, transformed.glmValue());
                if (radius * g_navigator_mode3_scale <= distance) {
                    volume = current_volume;
                } else {
                    volume = static_cast<std::uint32_t>(
                        (g_float_one - (distance - radius * g_navigator_mode3_scale) /
                                           (radius * g_ambient_sound_inner_radius_scale)) *
                        current_volume);
                }
                wiz8::sound::set_volume(sound_handle, volume);
            }
            return;
        }
        {
            std::uint32_t full_volume = (volume_max * g_settings.sound_effects_volume) / 0x7f;
            if (target_volume != full_volume) {
                target_volume = full_volume;
                fade_timer.SetDuration(g_float_one_and_a_half / full_volume);
                fade_timer.Restart();
                fade_timer.m_flags.paused = false;
                fade_timer.m_start = fade_timer.GetTime() - fade_timer.m_start;
                fade_timer.SetDuration(-1.0f);
            }
        }
    }
}

/* Per-frame service while in range. Non-looping sounds register a random
   schedule once; each call asks it (or a 2/3 roll on range entry) whether to
   fire a positioned one-shot at the emitter. Looping sounds start once:
   positional emitters are positioned, shared group sounds start at zero volume
   so UpdateFade can ramp them up. */
// FUNCTION: WIZ8 0x00479970
void W8AmbientSound::Service(bool entered)
{
    if (stopped != 0 || in_range == 0) {
        return;
    }
    if (looping == 0) {
        if (!random) {
            random = RegisterRandomAmbient(
                config.wave_name, time_min, time_max, speed_min, speed_max,
                (volume_min * g_settings.sound_effects_volume) / 0x7f,
                (volume_max * g_settings.sound_effects_volume) / 0x7f, false);
        }
        if (random != nullptr) {
            std::uint8_t should_play;

            if (!entered) {
                should_play = random->Due();
            } else {
                should_play = Random(3);
            }
            if (!gXStatus.fCombatMode && should_play != 0 &&
                g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
                float angle = -GetCameraYawRadians();
                srVector3T<float> camera;
                glm::mat3 rotation;
                srVector3T<float> offset;
                srVector3T<float> transformed;
                srVector3T<float> to_listener;
                float distance;
                std::uint32_t volume;
                std::uint32_t heard_volume;

                camera = cameraLocation();
                rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
                offset = position - camera;
                transformed = rotation * offset.glmValue();
                to_listener = camera - position;
                distance = to_listener.Length();
                volume = static_cast<std::uint32_t>(((Random(volume_max - volume_min) + volume_min) *
                                                    g_settings.sound_effects_volume) /
                                                   0x7f);
                current_volume = volume;
                if (distance < radius * g_navigator_mode3_scale) {
                    heard_volume = current_volume;
                } else {
                    heard_volume = static_cast<std::uint32_t>(
                        (g_float_one - (distance - radius * g_navigator_mode3_scale) /
                                           (radius * g_ambient_sound_inner_radius_scale)) *
                        current_volume);
                }
                sound_handle = random->Fire(heard_volume, transformed.glmValue());
            }
        }
        return;
    }
    if (sound_handle != nullptr) {
        return;
    }
    if (shared == 0) {
        float angle = -GetCameraYawRadians();
        srVector3T<float> camera;
        glm::mat3 rotation;
        srVector3T<float> offset;
        srVector3T<float> transformed;

        camera = cameraLocation();
        current_volume = (volume_max * g_settings.sound_effects_volume) / 0x7f;
        rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
        offset = position - camera;
        transformed = rotation * offset.glmValue();
        sound_handle = wiz8::sound::play(
            config.wave_name, {.volume = current_volume, .loops = 0, .position = transformed.glmValue()});
        if (sound_handle == nullptr) {
            in_range = 0;
        }
        return;
    }
    if (wiz8::sound::is_file_playing(config.wave_name)) {
        return;
    }
    {
        current_volume = 0;
        target_volume = (volume_max * g_settings.sound_effects_volume) / 0x7f;
        sound_handle = wiz8::sound::play(config.wave_name, {.volume = 0, .loops = 0});
        fade_timer.SetDuration(g_float_one_and_a_half / target_volume);
        fade_timer.Restart();
        fade_timer.m_flags.paused = false;
        fade_timer.m_start = fade_timer.GetTime() - fade_timer.m_start;
        fade_timer.SetDuration(-1.0f);
    }
}

/* The contiguous class lifecycle and shared world-list accesses identify these
   pre-assertion bodies as the same AmbientSound.cpp unit. */
// FUNCTION: WIZ8 0x0047a260
W8AmbientSound* W8AmbientSound::FindNextMatching(const char* match_name, W8AmbientSound* previous)
{
    std::int32_t count;
    std::int32_t index;

    if (g_world == 0) {
        return 0;
    }
    count = static_cast<std::int32_t>(g_world->plsAmbientSounds.size());
    if (previous == 0) {
        index = 0;
    } else {
        index = wiz8::index_of(g_world->plsAmbientSounds, previous) + 1;
        if (index < 0 || index > count) {
            return 0;
        }
    }
    for (; index < count; ++index) {
        W8AmbientSound* candidate = GetWorldAmbientSound(g_world, index);
        if (candidate != 0 && candidate != this && candidate->shared != 0 &&
            _stricmp(candidate->config.wave_name, match_name) == 0) {
            return candidate;
        }
    }
    return 0;
}

// FUNCTION: WIZ8 0x0047a310
void W8AmbientSound::UpdateFade()
{
    if (shared != 0) {
        if (sound_handle != nullptr) {
            W8GameTimer* timer = &fade_timer;

            if (timer->GetProgress() >= 1.0f) {
                if (target_volume > current_volume) {
                    ++current_volume;
                } else if (target_volume < current_volume) {
                    --current_volume;
                } else if (!timer->m_flags.paused) {
                    timer->m_flags.paused = true;
                    timer->m_start = timer->GetTime() - timer->m_start;
                }
                wiz8::sound::set_volume(sound_handle, current_volume);
            }
        }
        if (in_range == 0 && target_volume == current_volume && sound_handle != nullptr) {
            wiz8::sound::stop(sound_handle);
            sound_handle = nullptr;
        }
    }
}

// FUNCTION: WIZ8 0x0047a3e0
void UpdateAmbientSounds(W8World* world)
{
    if (world != 0) {
        std::int32_t count;
        std::int32_t index;

        ServiceRandomAmbients();
        wiz8::sound::service();
        count = static_cast<std::int32_t>(world->plsAmbientSounds.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(world, index);
            if (sound != 0) {
                sound->Service(false);
                sound->UpdateFade();
            }
        }
        Update3DSounds();
    }
}

// GLOBAL: WIZ8 0x0065a108
std::uint8_t g_default_footstep_surface;

// GLOBAL: WIZ8 0x0065a109
std::uint8_t g_default_footstep_material;

// GLOBAL: WIZ8 0x0065a10a
bool g_footstep_alternate;

// GLOBAL: WIZ8 0x0065a10c
std::int32_t g_previous_ambient_footstep_variant;

// GLOBAL: WIZ8 0x00609edc
const char* g_footstep_names[] = {
    "None",        "Gritty",     "Grass",     "Stone",       "ShallowWater",   "CreakyWood",
    "SolidWood",   "HollowWood", "Metal",     "Gravel",      "RoughStone",     "Marble",
    "Mud",         "Sand",       "Leaves",    "Snow",        "Carpet",         "Magic",
    "ClimbLadder", "ClimbRock",  "ClimbRope", "SwimSurface", "SwimUnderwater", "Crawl",
    "Fly",         "",
};
// GLOBAL: WIZ8 0x00609eb8
const char* g_footstep_surfaces[] = {
    "None",       "SmallCave", "MediumCave",   "LargeCave",      "SmallRoom",
    "MediumRoom", "LargeRoom", "OutdoorsFlat", "OutdoorsCanyon",
};
// GLOBAL: WIZ8 0x00609f44
const char* g_footstep_fixed_name = "Jump";
// GLOBAL: WIZ8 0x00609f48
const char* g_footstep_scuff_name = "Scuff";

/* Surface and material ids are range-checked against 1..9 / 1..25 before
   indexing the name tables. */
// FUNCTION: WIZ8 0x0047a440
wiz8::sound::Playback PlayFootstep(std::int8_t surface, std::int8_t material, W8FootstepKind kind)
{
    std::int8_t selected_surface;
    std::int8_t selected_material;
    char path[260];
    std::int32_t attempts = 0;

    if (GetRenderOptionState(W8_RENDER_OPTION_FOOTSTEP_SOUND) == 0) {
        return {};
    }
    selected_surface = surface;
    if (selected_surface < W8_FOOTSTEP_SURFACE_SMALL_CAVE ||
        selected_surface > W8_FOOTSTEP_SURFACE_MAX) {
        selected_surface = g_default_footstep_surface;
    }
    selected_material = material;
    if (selected_material < W8_FOOTSTEP_MATERIAL_GRITTY ||
        selected_material > W8_FOOTSTEP_MATERIAL_MAX) {
        selected_material = g_default_footstep_material;
    }
    if (selected_material >= W8_FOOTSTEP_MATERIAL_CLIMB_LADDER) {
        sprintf(path, "Data\\Sound\\Footsteps\\Step_%s.WAV", g_footstep_names[selected_material]);
    } else {
        std::int32_t variant;
        do {
            variant = Random(4) + 1;
            ++attempts;
        } while (variant == g_previous_ambient_footstep_variant && attempts < 100);
        BuildFootstepPath(path, selected_surface, selected_material, kind, variant);
        g_previous_ambient_footstep_variant = variant;
    }
    g_footstep_alternate = !g_footstep_alternate;
    return wiz8::sound::play(path, {.volume = std::uint32_t(g_settings.footstep_volume)});
}

// FUNCTION: WIZ8 0x0047a540
void BuildFootstepPath(char* path, std::int8_t surface, std::int8_t material, char kind,
                       std::int32_t variant)
{
    if (kind == W8_FOOTSTEP_KIND_STEP) {
        sprintf(path, "Data\\Sound\\Footsteps\\%s\\Step_%s_%s_%.2d.WAV", g_footstep_names[material],
                g_footstep_surfaces[surface], g_footstep_names[material], variant);
        return;
    }
    if (kind == W8_FOOTSTEP_KIND_JUMP) {
        sprintf(path, "Data\\Sound\\Footsteps\\%s\\Step_%s_%s_%s.WAV", g_footstep_names[material],
                g_footstep_surfaces[surface], g_footstep_names[material], g_footstep_fixed_name);
        return;
    }
    if (kind == W8_FOOTSTEP_KIND_SCUFF) {
        sprintf(path, "Data\\Sound\\Footsteps\\%s\\Step_%s_%s_%s_%.2d.WAV",
                g_footstep_names[material], g_footstep_surfaces[surface],
                g_footstep_names[material], g_footstep_scuff_name, variant);
    }
}

// FUNCTION: WIZ8 0x0047a600
void RepositionAmbientSounds(W8World* world)
{
    if (world != 0) {
        std::int32_t count;
        std::int32_t index;

        ServiceRandomAmbients();
        count = static_cast<std::int32_t>(world->plsAmbientSounds.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(world, index);
            if (sound != 0) {
                srVector3T<float> position = cameraLocation();
                sound->UpdatePosition(&position);
            }
        }
    }
}

// FUNCTION: WIZ8 0x0047a670
static W8AmbientSound* CreateAmbientSound()
{
    W8AmbientSound* sound = new W8AmbientSound;

    if (sound == 0) {
        srAssertFail("pSound", "C:\\Projects\\Wizardry 8\\Engine Code\\AmbientSound.cpp", 0x2f5, 0);
    }
    sound->target_volume = 0;
    return sound;
}

// FUNCTION: WIZ8 0x0047a700
void DestroyAmbientSound(W8AmbientSound* ambient)
{
    if (ambient == 0) {
        srAssertFail("pAmbient", "C:\\Projects\\Wizardry 8\\Engine Code\\AmbientSound.cpp", 0x2fd,
                     0);
    }
    if (ambient->sound_handle != nullptr) {
        wiz8::sound::stop(ambient->sound_handle);
    }
    if (ambient->random != nullptr) {
        ambient->random->enabled = false;
        std::erase(g_random_ambients, ambient->random);
    }
    if (ambient->pacSoundName != 0) {
        delete[] ambient->pacSoundName;
    }
    delete ambient;
}

/* Build a complete ambient-sound row and attach it to the world's list. The
   twenty parameters and their widths come directly from the stack reads. */
// FUNCTION: WIZ8 0x0047a790
std::uint8_t AddAmbientSound(W8World* world, const char* name, const W8AmbientSoundConfig* config,
                              const srVector3T<float>* position,
                              const srVector3T<float>* region_min,
                              const srVector3T<float>* region_max, std::int32_t volume_min, std::int32_t volume_max,
                              std::int32_t time_min, std::int32_t time_max, std::int32_t speed_min, std::int32_t speed_max,
                              float radius, bool looping, std::uint8_t bounded,
                              const srVector3T<float>* region_center, float region_angle,
                              const srVector3T<float>* region_axis,
                              const srVector3T<float>* region_scale, std::uint8_t shared)
{
    W8AmbientSound* sound = CreateAmbientSound();

    if (name != 0) {
        sound->pacSoundName = new char[strlen(name) + 1];
        if (sound->pacSoundName == 0) {
            srAssertFail("pSound->pacSoundName",
                         "C:\\Projects\\Wizardry 8\\Engine Code\\AmbientSound.cpp", 0x35e,
                         "AmbientSound.cpp: Error allocating sound name");
        }
        strcpy(sound->pacSoundName, name);
    }
    sound->config = *config;
    sound->position = *position;
    sound->region_min = *region_min;
    sound->region_max = *region_max;
    sound->region_center = *region_center;
    sound->region_angle = region_angle;
    sound->region_axis = *region_axis;
    sound->region_scale = *region_scale;
    sound->volume_min = volume_min == -1 ? 0x7f : volume_min;
    sound->volume_max = volume_max == -1 ? 0x7f : volume_max;
    sound->time_min = time_min == -1 ? 5000 : time_min;
    sound->time_max = time_max == -1 ? 20000 : time_max;
    sound->speed_min = speed_min;
    sound->speed_max = speed_max;
    sound->radius = radius;
    sound->looping = looping;
    sound->bounded = bounded;
    sound->shared = shared;
    world->plsAmbientSounds.push_back(sound);
    return 1;
}

/* The by-name script operations share one lookup and the same start/stop
   steps; retail expands all of them, re-testing the found pointer after the
   lookup has already dereferenced it. */
static W8AmbientSound* FindAmbientSoundByName(const char* name)
{
    std::int32_t count =
        static_cast<std::int32_t>(g_world->plsAmbientSounds.size());
    std::int32_t index;

    for (index = 0; index < count; ++index) {
        W8AmbientSound* sound = GetWorldAmbientSound(g_world, index);
        if (sound->pacSoundName != 0 && _stricmp(sound->pacSoundName, name) == 0) {
            return sound;
        }
    }
    return 0;
}

static void StartAmbientSoundAtCamera(W8AmbientSound* sound)
{
    srVector3T<float> position = cameraLocation();
    sound->stopped = 0;
    sound->UpdatePosition(&position);
}

static void StopAmbientSound(W8AmbientSound* sound)
{
    wiz8::sound::stop(sound->sound_handle);
    sound->in_range = 0;
    sound->stopped = 1;
    sound->sound_handle = nullptr;
}

// FUNCTION: WIZ8 0x0047a950
void PositionAmbientSoundByName(W8World* /* unused */, const char* name)
{
    W8AmbientSound* sound = FindAmbientSoundByName(name);
    if (sound != 0) {
        StartAmbientSoundAtCamera(sound);
    }
}

// FUNCTION: WIZ8 0x0047a9e0
void StopAmbientSoundByName(W8World* /* unused */, const char* name)
{
    W8AmbientSound* sound = FindAmbientSoundByName(name);
    if (sound != 0) {
        StopAmbientSound(sound);
    }
}

// FUNCTION: WIZ8 0x0047aa70
void ToggleAmbientSoundByName(W8World* /* unused */, const char* name)
{
    W8AmbientSound* sound = FindAmbientSoundByName(name);
    if (sound != 0) {
        if (sound->stopped != 0) {
            StartAmbientSoundAtCamera(sound);
            return;
        }
        StopAmbientSound(sound);
    }
}

// GLOBAL: WIZ8 0x0065A110
std::uint16_t g_empty_ambient_name;

// FUNCTION: WIZ8 0x0047ab40
std::uint8_t ReadAmbientSoundListFile(char* filename)
{
    std::unique_ptr<wiz8::File> handle;
    bool more = true;
    char directory[260];
    char name[260];
    char path[260];
    char line[260];
    std::int32_t configured[10];
    std::int32_t direct_selector = 0;

    handle = [&]() { try { return wiz8::open_file(filename, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (handle == 0) {
        return 0;
    }
    ReadTextLine(handle.get(), directory, 100, more);
    while (more) {
        std::int32_t index;
        for (index = 0; index < 10; ++index) {
            configured[index] = -1;
        }
        memset(line, 0, sizeof(line));
        ReadTextLine(handle.get(), line, sizeof(line), more);
        if (strlen(line) != 0) {
            sscanf(line, "%s %d %d %d %d %d %d %d", name, &configured[2], &configured[3],
                   &configured[4], &configured[5], &configured[0], &configured[1],
                   &direct_selector);
            sprintf(path, "%s\\%s", directory, name);
            if (direct_selector == -1) {
                const auto volume = [](std::int32_t value) {
                    return value == -1 ? std::uint32_t(g_settings.sound_effects_volume) : std::uint32_t(value);
                };
                RegisterRandomAmbient(path, configured[0], configured[1], configured[2], configured[3],
                                      volume(configured[4]), volume(configured[5]), true);
            } else {
                wiz8::sound::play(
                    path, {.volume = std::uint32_t((g_settings.sound_effects_volume * configured[5]) / 0x7f),
                           .loops = std::uint32_t(direct_selector),
                           .group = wiz8::sound::Group::ambient});
            }
        }
    }
    if (handle) handle->close();
    handle.reset();
    return 1;
}

/* The volume/mute helpers operate on the whole sound-effects bus, not just the
   ambient list: each writes the configured effects volume through the SGP
   default, then re-drives every live ambient voice and 3D instance so the new
   level is heard immediately. */
// FUNCTION: WIZ8 0x0047ad00
void SetSoundEffectsVolume(std::uint8_t volume)
{
    W8World* world;
    std::int32_t count;
    std::int32_t index;

    g_settings.sound_effects_volume = volume;
    wiz8::sound::set_default_volume(volume);
    if (g_world != 0) {
        count =
            static_cast<std::int32_t>(g_world->plsAmbientSounds.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(g_world, index);
            if (sound != 0) {
                if (sound->sound_handle != nullptr) {
                    std::uint32_t adjusted =
                        (sound->volume_max * g_settings.sound_effects_volume) / 0x7f;
                    sound->target_volume = adjusted;
                    sound->current_volume = adjusted;
                    wiz8::sound::set_volume(sound->sound_handle, adjusted);
                }
                sound->in_range = 0;
            }
        }
        Update3DSounds();
    }
    world = GetWorld();
    if (world != 0) {
        ServiceRandomAmbients();
        count = static_cast<std::int32_t>(world->plsAmbientSounds.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(world, index);
            if (sound != 0) {
                srVector3T<float> position = cameraLocation();
                sound->UpdatePosition(&position);
            }
        }
        UpdateAmbientSounds(world);
    }
}

// FUNCTION: WIZ8 0x0047ae70
std::uint8_t GetSoundEffectsVolume(void)
{
    return g_settings.sound_effects_volume;
}

// FUNCTION: WIZ8 0x0047ae80
bool IsSoundEffectsMuted(void)
{
    return g_settings.muted_sound_effects_volume != 0xff;
}

// FUNCTION: WIZ8 0x0047ae90
void SetSoundEffectsMuted(std::uint8_t muted)
{
    if (muted != 0) {
        if (g_settings.muted_sound_effects_volume == 0xff) {
            g_settings.muted_sound_effects_volume = g_settings.sound_effects_volume;
            SetSoundEffectsVolume(0);
        }
    } else if (g_settings.muted_sound_effects_volume != 0xff) {
        SetSoundEffectsVolume(g_settings.muted_sound_effects_volume);
        g_settings.muted_sound_effects_volume = 0xff;
    }
}

// FUNCTION: WIZ8 0x0047b140
void SaveAmbientSoundList(wiz8::File& handle)
{
    std::uint8_t version = 1;
    std::uint32_t count;
    std::uint8_t ok;
    char empty_name[0x80];
    std::int32_t index;

    memcpy(empty_name, &g_empty_ambient_name, 2);
    memset(empty_name + 2, 0, sizeof(empty_name) - 2);
    ok = (handle.write(&version, 1), true);
    count = g_world->plsAmbientSounds.size();
    ok = ok && (handle.write(&count, 4), true);
    for (index = 0; index < static_cast<std::int32_t>(count); ++index) {
        W8AmbientSound* sound = GetWorldAmbientSound(g_world, index);
        if (sound != 0) {
            if (sound->pacSoundName == 0) {
                if (ok) {
                    ok = (handle.write(empty_name, sizeof(empty_name)), true);
                }
            } else if (ok) {
                // The disk record is fixed width, but live names are strlen + 1
                // allocations.
                char name[0x80] = {};
                strncpy(name, sound->pacSoundName, sizeof(name) - 1);
                ok = (handle.write(name, sizeof(name)), true);
            }
            if (ok) {
                ok = (handle.write(&sound->stopped, 1), true);
            }
        }
    }
}

/* The serialized per-record payload is just the 0x80-byte name and the stopped
   byte; on load each name is matched against the live list and the matching
   script commands are replayed — the helpers rescan the list themselves, and
   VC6 inlines that rescan twice here. */
// FUNCTION: WIZ8 0x0047b270
void LoadAmbientSoundList(wiz8::File& handle)
{
    std::uint8_t version;
    std::uint8_t ok;
    std::int32_t count = 0;
    std::int32_t index;
    char name[0x80];
    std::uint8_t stopped_flag;

    ok = (handle.read(&version, 1).bytes == static_cast<std::size_t>(1));
    if (ok != 0) {
        ok = (handle.read(&count, 4).bytes == static_cast<std::size_t>(4));
    }
    for (index = 0; index < count; ++index) {
        std::int32_t length;
        std::int32_t scan;

        if (ok != 0 && (handle.read(name, 0x80).bytes == static_cast<std::size_t>(0x80)) != 0) {
            ok = (handle.read(&stopped_flag, 1).bytes == static_cast<std::size_t>(1));
        } else {
            ok = 0;
        }
        length =
            static_cast<std::int32_t>(g_world->plsAmbientSounds.size());
        for (scan = 0; scan < length; ++scan) {
            W8AmbientSound* sound = GetWorldAmbientSound(g_world, scan);
            if (sound->pacSoundName != 0 && _stricmp(sound->pacSoundName, name) == 0) {
                if (sound != 0) {
                    if (stopped_flag == 0) {
                        PositionAmbientSoundByName(0, name);
                    } else {
                        StopAmbientSoundByName(0, name);
                    }
                }
                break;
            }
        }
    }
}
