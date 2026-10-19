#pragma once
struct W8Chunk;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio_resources.h"
#include "wiz8/engine_code/game_timer.h"

struct W8World;
// One level-owned voice and its optional random replay schedule.
struct W8AmbientTrack {
    wiz8::sound::Clip clip;
    wiz8::sound::Playback instance;
    std::uint32_t time_min = 0, time_max = 0;
    std::uint32_t volume_min = 0, volume_max = 0;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> speed;
    std::optional<std::uint32_t> loops;
    std::uint64_t next_at = 0;

    ~W8AmbientTrack();
    void Reschedule();
    bool Due() const;
    wiz8::sound::Playback Fire(std::uint32_t volume, std::optional<glm::vec3> position);
    static std::uint32_t RandomRange(std::uint32_t minimum, std::uint32_t maximum);
};
namespace wiz8 {
class File;
}  // namespace wiz8

struct W8AmbientSoundConfig {
    char wave_name[0x80];
};

/* Engine Code\AmbientSound.cpp. Offsets below describe the retail object;
   native emitters own their audio clip and no longer have that binary layout. */
class W8AmbientSound {
public:
    W8AmbientSound();
    ~W8AmbientSound();
    W8AmbientSound(const W8AmbientSound&) = delete;
    W8AmbientSound& operator=(const W8AmbientSound&) = delete;

    /* 0x004790D0: true when listener is inside the rotated/scaled min-max region */
    bool IsInsideRegion(const glm::vec3* listener);
    /* 0x00479350: listener-relative range test, group handoff, 3D re-aim, fade retarget */
    void UpdatePosition(const glm::vec3* listener);
    /* 0x00479970: per-frame service while in range; 'entered' forces an immediate roll */
    void Service(bool entered);
    /* 0x0047A310: step the 1.5-second shared-group volume fade */
    void UpdateFade();

    W8AmbientSound* FindNextMatching(const char* match_name, W8AmbientSound* previous);

    std::string name;
    W8AmbientSoundConfig config; /* 0x004: wave filename; shared sounds match on it */
    unsigned char stopped;       /* 0x084: script-stopped via the ByName commands */
    glm::vec3 position;  /* 0x088: emitter world position */
    unsigned int volume_min;     /* 0x094 */
    unsigned int volume_max;     /* 0x098 */
    unsigned int target_volume;  /* 0x09c: fade endpoint */
    unsigned int current_volume; /* 0x0a0: live fade value */
    int speed_min;               /* 0x0a4: playback speed low bound */
    int speed_max;               /* 0x0a8: playback speed high bound */
    int time_min;                /* 0x0ac: random retrigger interval, low ms */
    int time_max;                /* 0x0b0: random retrigger interval, high ms */
    float radius;                /* 0x0b4: audible range in world units */
    unsigned char in_range;      /* 0x0b8: listener currently inside radius/region */
    unsigned char looping;       /* 0x0b9: continuous loop vs random one-shot */
    wiz8::sound::Playback sound_handle;      // Current playback, shared during group handoff.
    std::unique_ptr<W8AmbientTrack> random; // One-shot schedule; empty for looping sounds.
    unsigned char shared;  /* 0x0c4: shared 2D group sound; voices hand off by name */
    unsigned char bounded; /* 0x0c5: region test enabled (v2 trigger field) */
    glm::vec3 region_min;    /* 0x0c8 */
    glm::vec3 region_max;    /* 0x0d4 */
    glm::vec3 region_center; /* 0x0e0 */
    float region_angle;              /* 0x0ec: rotation about region_axis, radians */
    glm::vec3 region_axis;   /* 0x0f0 */
    glm::vec3 region_scale;  /* 0x0fc: per-axis region scale */
    W8GameTimer fade_timer;          /* 0x108 */
};

/* Reverb environment id indexing g_footstep_surfaces. The serialized selector
   is a byte, so the enum keeps that underlying type. */
enum W8FootstepSurface : unsigned char {
    W8_FOOTSTEP_SURFACE_NONE = 0,
    W8_FOOTSTEP_SURFACE_SMALL_CAVE = 1,
    W8_FOOTSTEP_SURFACE_MEDIUM_CAVE = 2,
    W8_FOOTSTEP_SURFACE_LARGE_CAVE = 3,
    W8_FOOTSTEP_SURFACE_SMALL_ROOM = 4,
    W8_FOOTSTEP_SURFACE_MEDIUM_ROOM = 5,
    W8_FOOTSTEP_SURFACE_LARGE_ROOM = 6,
    W8_FOOTSTEP_SURFACE_OUTDOORS_FLAT = 7,
    W8_FOOTSTEP_SURFACE_OUTDOORS_CANYON = 8,
    W8_FOOTSTEP_SURFACE_MAX = 9,
    W8_FOOTSTEP_SURFACE_UNSET = 0xff,
};

/* Step material id indexing g_footstep_names; ids at and above
   CLIMB_LADDER bypass the per-surface naming path and play a single
   "Step_<name>.WAV". */
enum W8FootstepMaterial : unsigned char {
    W8_FOOTSTEP_MATERIAL_NONE = 0,
    W8_FOOTSTEP_MATERIAL_GRITTY = 1,
    W8_FOOTSTEP_MATERIAL_GRASS = 2,
    W8_FOOTSTEP_MATERIAL_STONE = 3,
    W8_FOOTSTEP_MATERIAL_SHALLOW_WATER = 4,
    W8_FOOTSTEP_MATERIAL_CREAKY_WOOD = 5,
    W8_FOOTSTEP_MATERIAL_SOLID_WOOD = 6,
    W8_FOOTSTEP_MATERIAL_HOLLOW_WOOD = 7,
    W8_FOOTSTEP_MATERIAL_METAL = 8,
    W8_FOOTSTEP_MATERIAL_GRAVEL = 9,
    W8_FOOTSTEP_MATERIAL_ROUGH_STONE = 10,
    W8_FOOTSTEP_MATERIAL_MARBLE = 11,
    W8_FOOTSTEP_MATERIAL_MUD = 12,
    W8_FOOTSTEP_MATERIAL_SAND = 13,
    W8_FOOTSTEP_MATERIAL_LEAVES = 14,
    W8_FOOTSTEP_MATERIAL_SNOW = 15,
    W8_FOOTSTEP_MATERIAL_CARPET = 16,
    W8_FOOTSTEP_MATERIAL_MAGIC = 17,
    W8_FOOTSTEP_MATERIAL_CLIMB_LADDER = 18,
    W8_FOOTSTEP_MATERIAL_CLIMB_ROCK = 19,
    W8_FOOTSTEP_MATERIAL_CLIMB_ROPE = 20,
    W8_FOOTSTEP_MATERIAL_SWIM_SURFACE = 21,
    W8_FOOTSTEP_MATERIAL_SWIM_UNDERWATER = 22,
    W8_FOOTSTEP_MATERIAL_CRAWL = 23,
    W8_FOOTSTEP_MATERIAL_FLY = 24,
    W8_FOOTSTEP_MATERIAL_MAX = 25,
    W8_FOOTSTEP_MATERIAL_UNSET = 0xff,
};

/* Which footstep wave shape PlayFootstep builds: an ordinary numbered step, a
   single jump, or a numbered scuff. */
enum W8FootstepKind : unsigned char {
    W8_FOOTSTEP_KIND_STEP = 0,
    W8_FOOTSTEP_KIND_JUMP = 1,
    W8_FOOTSTEP_KIND_SCUFF = 2,
};

void BuildFootstepPath(char* path, W8FootstepSurface surface, W8FootstepMaterial material,
                       W8FootstepKind kind, int variant);
wiz8::sound::Playback PlayFootstep(W8FootstepSurface surface, W8FootstepMaterial material,
                                   W8FootstepKind kind);
void UpdateAmbientSounds(W8World* world);
void RepositionAmbientSounds();
unsigned char ReadAmbientSoundListFile(W8World& world, const char* filename);
void StartAmbientSoundList(W8World& world);

void PositionAmbientSoundByName(W8World* /* unused */, const char* name);
void StopAmbientSoundByName(W8World* /* unused */, const char* name);
void ToggleAmbientSoundByName(const char* name);
unsigned char AddAmbientSound(W8World* world, const char* name, const W8AmbientSoundConfig* config,
                              const glm::vec3* position, const glm::vec3* region_min,
                              const glm::vec3* region_max, int volume_min, int volume_max,
                              int time_min, int time_max, int speed_min, int speed_max,
                              float radius, bool looping, unsigned char bounded,
                              const glm::vec3* region_center, float region_angle,
                              const glm::vec3* region_axis, const glm::vec3* region_scale,
                              unsigned char shared);

void SaveAmbientSoundList(wiz8::File& handle);
struct W8AmbientSoundState {
    std::string name;
    bool stopped;
};
std::vector<W8AmbientSoundState> ReadAmbientSoundStates(W8Chunk& file);
void ApplyAmbientSoundStates(W8World& world, std::span<const W8AmbientSoundState> states);

bool IsSoundEffectsMuted(void);
void SetSoundEffectsVolume(unsigned char volume);
void SetSoundEffectsMuted(unsigned char muted);

extern W8FootstepSurface g_default_footstep_surface;
extern W8FootstepMaterial g_default_footstep_material;
extern bool g_footstep_alternate;
extern int g_previous_ambient_footstep_variant;
extern const char* g_footstep_names[];
extern const char* g_footstep_surfaces[];
