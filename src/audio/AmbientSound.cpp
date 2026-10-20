#include "wiz8/audio/AmbientSound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "asset_paths.h"
#include "compat/native.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/audio_resources.h"
#include "wiz8/chunk.h"
#include "wiz8/containers.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/math.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/quality.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/world/world.h"

static constexpr float g_ambient_sound_inner_radius_scale = 0.6000000238418579f;

W8AmbientTrack::~W8AmbientTrack()
{
    if (instance) wiz8::sound::stop(instance);
}
void W8AmbientTrack::Reschedule()
{
    next_at = w8_get_ticks() + RandomRange(time_min, time_max);
}
bool W8AmbientTrack::Due() const
{
    return !loops && next_at <= w8_get_ticks() && !wiz8::sound::is_playing(instance);
}
std::uint32_t W8AmbientTrack::RandomRange(std::uint32_t minimum, std::uint32_t maximum)
{
    return minimum + (maximum > minimum ? Random(maximum - minimum) : 0);
}
wiz8::sound::Playback W8AmbientTrack::Fire(std::uint32_t volume, std::optional<glm::vec3> position)
{
    auto playback = wiz8::sound::play(
        clip,
        {.volume = volume,
         .loops = loops.value_or(1),
         .rate = speed ? std::optional(RandomRange(speed->first, speed->second)) : std::nullopt,
         .group = wiz8::sound::Group::ambient,
         .position = position});
    if (playback) {
        Reschedule();
        instance = playback;
    }
    return playback;
}

namespace {
std::unique_ptr<W8AmbientTrack> ReadRandomAmbient(const char* path, int time_min, int time_max,
                                                  int speed_min, int speed_max,
                                                  std::uint32_t volume_min,
                                                  std::uint32_t volume_max)
{
    if (time_min == -1) return {};
    auto clip = wiz8::sound::load(path);
    if (!clip) return {};
    auto track = std::make_unique<W8AmbientTrack>();
    track->clip = std::move(clip);
    track->time_min = std::uint32_t(time_min);
    track->time_max = time_max == -1 ? track->time_min : std::uint32_t(time_max);
    track->volume_min = volume_min;
    track->volume_max = volume_max;
    if (speed_min != -1 && speed_max != -1)
        track->speed.emplace(std::uint32_t(speed_min), std::uint32_t(speed_max));
    return track;
}
void ServiceRandomAmbients(World& world)
{
    for (const auto& track : world.audio.tracks)
        if (track->Due())
            track->Fire(W8AmbientTrack::RandomRange(track->volume_min, track->volume_max),
                        std::nullopt);
}
} // namespace

void StartAmbientSoundList(World& world)
{
    for (const auto& track : world.audio.tracks) {
        if (track->loops)
            track->Fire(track->volume_max, std::nullopt);
        else
            track->Reschedule();
    }
}

W8AmbientSound::W8AmbientSound()
    : volume_min(0), volume_max(0), target_volume(0), current_volume(0), speed_min(0), speed_max(0),
      time_min(0), time_max(0), radius(0), in_range(0), looping(0), sound_handle(), random(),
      shared(0), bounded(0), region_angle(0)
{
    config.wave_name[0] = 0;
    position = glm::vec3(0);
    stopped = 0;
}

/* Region test used by UpdatePosition and the group handoff: translate the
   listener into region-center space, un-apply the per-axis scale, rotate back
   around the configured axis, then test against the min/max bounds. */
bool W8AmbientSound::IsInsideRegion(const glm::vec3* listener)
{
    glm::vec3 relative;
    float scale_x;
    float scale_y;
    float scale_z;

    relative = *listener - region_center;
    scale_x = 1.0f / region_scale.x;
    scale_y = 1.0f / region_scale.y;
    scale_z = 1.0f / region_scale.z;
    if (scale_x != 1.0f || scale_y != 1.0f || scale_z != 1.0f) {
        relative.x = scale_x * relative.x;
        relative.y *= scale_y;
        relative.z *= scale_z;
    }
    if (region_angle != 0.0f &&
        (region_axis.x != 0.0f || region_axis.y != 0.0f || region_axis.z != 0.0f)) {
        glm::mat3 rotation;
        double angle = -region_angle;

        rotation = glm::mat3(1);
        if (angle != 0.0) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                      normalizedOrZero(glm::vec3(region_axis))));
        }
        relative = rotation * relative;
    }
    return PointInsideBounds(&relative, &region_min, &region_max);
}

/* Listener-relative servicing. Out of range: shared sounds try to hand their
   live voice to a matching group member that is in range, otherwise fade to
   zero; positional sounds stop immediately. In range: mark the entry, restart
   one-shot scheduling, re-aim a live 3D voice around the camera, and nudge its
   volume up inside the inner 40% of the radius. Shared sounds retarget the
   fade at the bus-scaled maximum. */
void W8AmbientSound::UpdatePosition(const glm::vec3* listener)
{
    bool handed_off = false;

    if (stopped != 0) {
        return;
    }
    {
        glm::vec3 to_listener = *listener - position;
        float distance = glm::length(to_listener);
        unsigned char inside = IsInsideRegion(listener);

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
                            glm::vec3 to_match = *listener - match->position;
                            unsigned char match_inside = match->IsInsideRegion(listener);
                            if (glm::length(to_match) < match->radius &&
                                (match->bounded == 0 || match_inside != 0) &&
                                match->sound_handle == nullptr) {
                                match->in_range = 1;
                                match->sound_handle = sound_handle;
                                match->target_volume =
                                    (match->volume_max * g_settings.sound_effects_volume) / 0x7f;
                                match->current_volume =
                                    wiz8::sound::volume(sound_handle).value_or(0);
                                match->fade_timer.SetDuration(1.5f / match->target_volume);
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
                        unsigned int full_volume =
                            (volume_max * g_settings.sound_effects_volume) / 0x7f;
                        fade_timer.SetDuration(1.5f / full_volume);
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
                glm::vec3 transformed;
                unsigned int volume;

                rotation = glm::mat3(1);
                rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
                glm::vec3 offset = position - *listener;
                transformed = rotation * offset;
                wiz8::sound::set_position(sound_handle, transformed);
                if (radius * kNavigatorMode3Scale <= distance) {
                    volume = current_volume;
                } else {
                    volume = static_cast<unsigned int>(
                        (1.0f - (distance - radius * kNavigatorMode3Scale) /
                                    (radius * g_ambient_sound_inner_radius_scale)) *
                        current_volume);
                }
                wiz8::sound::set_volume(sound_handle, volume);
            }
            return;
        }
        {
            unsigned int full_volume = (volume_max * g_settings.sound_effects_volume) / 0x7f;
            if (target_volume != full_volume) {
                target_volume = full_volume;
                fade_timer.SetDuration(1.5f / full_volume);
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
void W8AmbientSound::Service(bool entered)
{
    if (stopped != 0 || in_range == 0) {
        return;
    }
    if (looping == 0) {
        if (!random) {
            random = ReadRandomAmbient(config.wave_name, time_min, time_max, speed_min, speed_max,
                                       (volume_min * g_settings.sound_effects_volume) / 0x7f,
                                       (volume_max * g_settings.sound_effects_volume) / 0x7f);
            if (random) random->Reschedule();
        }
        if (random != nullptr) {
            unsigned char should_play;

            if (!entered) {
                should_play = random->Due();
            } else {
                should_play = Random(3);
            }
            if (!g_combat.active() && should_play != 0 &&
                CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
                float angle = -GetCameraYawRadians();
                glm::vec3 camera;
                glm::mat3 rotation;
                glm::vec3 offset;
                glm::vec3 transformed;
                glm::vec3 to_listener;
                float distance;
                unsigned int volume;
                unsigned int heard_volume;

                camera = cameraLocation();
                rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
                offset = position - camera;
                transformed = rotation * offset;
                to_listener = camera - position;
                distance = glm::length(to_listener);
                volume = static_cast<unsigned int>(((Random(volume_max - volume_min) + volume_min) *
                                                    g_settings.sound_effects_volume) /
                                                   0x7f);
                current_volume = volume;
                if (distance < radius * kNavigatorMode3Scale) {
                    heard_volume = current_volume;
                } else {
                    heard_volume = static_cast<unsigned int>(
                        (1.0f - (distance - radius * kNavigatorMode3Scale) /
                                    (radius * g_ambient_sound_inner_radius_scale)) *
                        current_volume);
                }
                sound_handle = random->Fire(heard_volume, transformed);
            }
        }
        return;
    }
    if (sound_handle != nullptr) {
        return;
    }
    if (shared == 0) {
        float angle = -GetCameraYawRadians();
        glm::vec3 camera;
        glm::mat3 rotation;
        glm::vec3 offset;
        glm::vec3 transformed;

        camera = cameraLocation();
        current_volume = (volume_max * g_settings.sound_effects_volume) / 0x7f;
        rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
        offset = position - camera;
        transformed = rotation * offset;
        sound_handle = wiz8::sound::play(
            config.wave_name, {.volume = current_volume, .loops = 0, .position = transformed});
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
        fade_timer.SetDuration(1.5f / target_volume);
        fade_timer.Restart();
        fade_timer.m_flags.paused = false;
        fade_timer.m_start = fade_timer.GetTime() - fade_timer.m_start;
        fade_timer.SetDuration(-1.0f);
    }
}

/* The contiguous class lifecycle and shared world-list accesses identify these
   pre-assertion bodies as the same AmbientSound.cpp unit. */
W8AmbientSound* W8AmbientSound::FindNextMatching(std::string_view match_name,
                                                 W8AmbientSound* previous)
{
    int count;
    int index;

    if (GetWorld() == 0) {
        return 0;
    }
    count = static_cast<int>(GetWorld()->audio.emitters.size());
    if (previous == 0) {
        index = 0;
    } else {
        index = wiz8::index_of(GetWorld()->audio.emitters, previous,
                               [](const auto& owner) { return owner.get(); }) +
                1;
        if (index < 0 || index > count) {
            return 0;
        }
    }
    for (; index < count; ++index) {
        W8AmbientSound* candidate = GetWorldAmbientSound(GetWorld(), index);
        if (candidate != 0 && candidate != this && candidate->shared != 0 &&
            CompareTextIgnoreAsciiCase(candidate->config.wave_name, match_name) == 0) {
            return candidate;
        }
    }
    return 0;
}

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

void UpdateAmbientSounds(World* world)
{
    if (world != 0) {
        int count;
        int index;

        ServiceRandomAmbients(*world);
        count = static_cast<int>(world->audio.emitters.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(world, index);
            if (sound != 0) {
                sound->Service(false);
                sound->UpdateFade();
            }
        }
        world->audio.update(cameraLocation(), GetCameraYawRadians(),
                            g_settings.sound_effects_volume);
    }
}

// Surface and material fallbacks: the "None" row of each name table.
constexpr auto kDefaultFootstepSurface = W8_FOOTSTEP_SURFACE_NONE;
constexpr auto kDefaultFootstepMaterial = W8_FOOTSTEP_MATERIAL_NONE;

static int g_previous_ambient_footstep_variant;

const char* const g_footstep_names[] = {
    "None",        "Gritty",     "Grass",     "Stone",       "ShallowWater",   "CreakyWood",
    "SolidWood",   "HollowWood", "Metal",     "Gravel",      "RoughStone",     "Marble",
    "Mud",         "Sand",       "Leaves",    "Snow",        "Carpet",         "Magic",
    "ClimbLadder", "ClimbRock",  "ClimbRope", "SwimSurface", "SwimUnderwater", "Crawl",
    "Fly",         "",
};
const char* const g_footstep_surfaces[] = {
    "None",       "SmallCave", "MediumCave",   "LargeCave",      "SmallRoom",
    "MediumRoom", "LargeRoom", "OutdoorsFlat", "OutdoorsCanyon",
};
static constexpr const char* g_footstep_fixed_name = "Jump";
static constexpr const char* g_footstep_scuff_name = "Scuff";

/* Surface and material ids are range-checked against 1..9 / 1..25 before
   indexing the name tables. */
// FUNCTION: WIZ8 0x0047a440
wiz8::sound::Playback PlayFootstep(W8FootstepSurface surface, W8FootstepMaterial material,
                                   W8FootstepKind kind)
{
    W8FootstepSurface selected_surface;
    W8FootstepMaterial selected_material;
    char path[260];
    int attempts = 0;

    if (GetRenderOptionState(W8_RENDER_OPTION_FOOTSTEP_SOUND) == 0) {
        return {};
    }
    selected_surface = surface;
    if (selected_surface < W8_FOOTSTEP_SURFACE_SMALL_CAVE ||
        selected_surface > W8_FOOTSTEP_SURFACE_MAX) {
        selected_surface = kDefaultFootstepSurface;
    }
    selected_material = material;
    if (selected_material < W8_FOOTSTEP_MATERIAL_GRITTY ||
        selected_material > W8_FOOTSTEP_MATERIAL_MAX) {
        selected_material = kDefaultFootstepMaterial;
    }
    if (selected_material >= W8_FOOTSTEP_MATERIAL_CLIMB_LADDER) {
        sprintf(path, "Data\\Sound\\Footsteps\\Step_%s.WAV", g_footstep_names[selected_material]);
    } else {
        int variant;
        do {
            variant = Random(4) + 1;
            ++attempts;
        } while (variant == g_previous_ambient_footstep_variant && attempts < 100);
        BuildFootstepPath(path, selected_surface, selected_material, kind, variant);
        g_previous_ambient_footstep_variant = variant;
    }
    return wiz8::sound::play(path, {.volume = std::uint32_t(g_settings.footstep_volume)});
}

// FUNCTION: WIZ8 0x0047a540
void BuildFootstepPath(char* path, W8FootstepSurface surface, W8FootstepMaterial material,
                       W8FootstepKind kind, int variant)
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

void RepositionAmbientSounds()
{
    if (GetWorld() != nullptr) {
        int count;
        int index;

        ServiceRandomAmbients(*GetWorld());
        count = static_cast<int>(GetWorld()->audio.emitters.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(GetWorld(), index);
            if (sound != 0) {
                glm::vec3 position = cameraLocation();
                sound->UpdatePosition(&position);
            }
        }
    }
}

W8AmbientSound::~W8AmbientSound()
{
    if (sound_handle) wiz8::sound::stop(sound_handle);
}

/* Build a complete ambient-sound row and attach it to the world's list. The
   twenty parameters and their widths come directly from the stack reads. */
unsigned char AddAmbientSound(World* world, std::string_view name,
                              const W8AmbientSoundConfig* config, const glm::vec3* position,
                              const glm::vec3* region_min, const glm::vec3* region_max,
                              int volume_min, int volume_max, int time_min, int time_max,
                              int speed_min, int speed_max, float radius, bool looping,
                              unsigned char bounded, const glm::vec3* region_center,
                              float region_angle, const glm::vec3* region_axis,
                              const glm::vec3* region_scale, unsigned char shared)
{
    auto sound = std::make_unique<W8AmbientSound>();

    if (!name.empty()) sound->name = name;
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
    world->audio.emitters.push_back(std::move(sound));
    return 1;
}

/* The by-name script operations share one lookup and the same start/stop
   steps; retail expands all of them, re-testing the found pointer after the
   lookup has already dereferenced it. */
static W8AmbientSound* FindAmbientSoundByName(std::string_view name)
{
    const int count = static_cast<int>(GetWorld()->audio.emitters.size());

    for (int index = 0; index < count; ++index) {
        W8AmbientSound* sound = GetWorldAmbientSound(GetWorld(), index);
        if (!sound->name.empty() && CompareTextIgnoreAsciiCase(sound->name, name) == 0) {
            return sound;
        }
    }
    return nullptr;
}

static void StartAmbientSoundAtCamera(W8AmbientSound* sound)
{
    glm::vec3 position = cameraLocation();
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

void PositionAmbientSoundByName(World* /* unused */, std::string_view name)
{
    W8AmbientSound* sound = FindAmbientSoundByName(name);
    if (sound != 0) {
        StartAmbientSoundAtCamera(sound);
    }
}

void StopAmbientSoundByName(World* /* unused */, std::string_view name)
{
    W8AmbientSound* sound = FindAmbientSoundByName(name);
    if (sound != 0) {
        StopAmbientSound(sound);
    }
}

void ToggleAmbientSoundByName(std::string_view name)
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

unsigned char ReadAmbientSoundListFile(World& world, std::string_view filename)
try {
    auto file = wiz8::open_file(wiz8::asset_path(filename));
    std::istringstream input(file->read_text());
    std::string directory, line;
    if (!std::getline(input, directory)) return false;
    if (directory.ends_with('\r')) directory.pop_back();
    int direct_selector = 0;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string name;
        if (!(fields >> name) || name.starts_with('#')) continue;
        int configured[6]{-1, -1, -1, -1, -1, -1};
        fields >> configured[2] >> configured[3] >> configured[4] >> configured[5] >>
            configured[0] >> configured[1] >> direct_selector;
        const auto path = fmt::format("{}\\{}", directory, name);
        if (direct_selector == -1) {
            const auto volume = [](int value) {
                return value == -1 ? std::uint32_t(g_settings.sound_effects_volume)
                                   : std::uint32_t(value);
            };
            auto track =
                ReadRandomAmbient(path.c_str(), configured[0], configured[1], configured[2],
                                  configured[3], volume(configured[4]), volume(configured[5]));
            if (track) world.audio.tracks.push_back(std::move(track));
        } else {
            auto track = std::make_unique<W8AmbientTrack>();
            track->clip = wiz8::sound::load(path);
            if (!track->clip) continue;
            track->volume_min = track->volume_max =
                std::uint32_t((g_settings.sound_effects_volume * configured[5]) / 0x7f);
            track->loops = std::uint32_t(direct_selector);
            world.audio.tracks.push_back(std::move(track));
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* The volume/mute helpers operate on the whole sound-effects bus, not just the
   ambient list: each writes the configured effects volume through the SGP
   default, then re-drives every live ambient voice and 3D instance so the new
   level is heard immediately. */
void SetSoundEffectsVolume(unsigned char volume)
{
    World* world;
    int count;
    int index;

    g_settings.sound_effects_volume = volume;
    wiz8::sound::set_default_volume(volume);
    if (GetWorld() != 0) {
        count = static_cast<int>(GetWorld()->audio.emitters.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(GetWorld(), index);
            if (sound != 0) {
                if (sound->sound_handle != nullptr) {
                    unsigned int adjusted =
                        (sound->volume_max * g_settings.sound_effects_volume) / 0x7f;
                    sound->target_volume = adjusted;
                    sound->current_volume = adjusted;
                    wiz8::sound::set_volume(sound->sound_handle, adjusted);
                }
                sound->in_range = 0;
            }
        }
        GetWorld()->audio.update(cameraLocation(), GetCameraYawRadians(),
                                 g_settings.sound_effects_volume);
    }
    world = GetWorld();
    if (world != 0) {
        ServiceRandomAmbients(*world);
        count = static_cast<int>(world->audio.emitters.size());
        for (index = 0; index < count; ++index) {
            W8AmbientSound* sound = GetWorldAmbientSound(world, index);
            if (sound != 0) {
                glm::vec3 position = cameraLocation();
                sound->UpdatePosition(&position);
            }
        }
        UpdateAmbientSounds(world);
    }
}

bool IsSoundEffectsMuted(void)
{
    return g_settings.muted_sound_effects_volume != 0xff;
}

void SetSoundEffectsMuted(unsigned char muted)
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

void SaveAmbientSoundList(wiz8::File& handle)
{
    const unsigned char version = 1;
    const auto count = static_cast<std::uint32_t>(GetWorld()->audio.emitters.size());
    handle.write(&version, sizeof(version));
    handle.write(&count, sizeof(count));
    for (const auto& sound : GetWorld()->audio.emitters) {
        handle.write_padded(sound->name, 128);
        handle.write(&sound->stopped, 1);
    }
}

std::vector<W8AmbientSoundState> ReadAmbientSoundStates(W8Chunk& handle)
{
    unsigned char version;
    std::uint32_t count;
    handle.ReadBytes(&version, sizeof(version));
    handle.ReadBytes(&count, sizeof(count));
    if (count > (handle.RemainingBytes()) / 129)
        throw std::runtime_error("invalid ambient sound state count");
    std::vector<W8AmbientSoundState> pending;
    pending.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::array<char, 128> name;
        unsigned char stopped;
        handle.ReadBytes(name.data(), name.size());
        handle.ReadBytes(&stopped, 1);
        const auto end = std::find(name.begin(), name.end(), '\0');
        if (end == name.end()) throw std::runtime_error("unterminated ambient sound name");
        pending.push_back({std::string(name.data(), end - name.begin()), stopped != 0});
    }
    return pending;
}

void ApplyAmbientSoundStates(World& world, std::span<const W8AmbientSoundState> states)
{
    for (const auto& [name, stopped] : states)
        for (const auto& sound : world.audio.emitters)
            if (sound->name == name) sound->stopped = stopped;
}

void AmbientAudioSystem::update(const glm::vec3& listener, float yaw, unsigned effects_volume)
{
    for (auto& sound : script_sounds) sound->Update(listener, yaw, effects_volume);
    for (auto& sound : one_shots) sound->Update(listener, yaw, effects_volume);
    std::erase_if(one_shots, [](const auto& sound) { return !sound->IsPlaying(); });
}

AmbientAudioSystem::~AmbientAudioSystem()
{
    wiz8::sound::stop(footstep_loop);
}
