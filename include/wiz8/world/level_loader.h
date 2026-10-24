#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/material_loader.h"
#include "wiz8/retail_geometry_types.h"

namespace wiz8 {
class File;
}

struct World;
struct W8NamedPosition;
class ParticleSystem;
class WorldScene;

#pragma pack(push, 1)
/* The 0x225-byte particle body shared by the level file's particle systems
   and the world file's particle records. */
struct W8LevelParticleRecord {
    char name[64];                   /* 0x000 */
    wiz8::retail::Vec3f location;      /* 0x040 */
    float rotation_angle;            /* 0x04c */
    wiz8::retail::Vec3f rotation_axis; /* 0x050 */
    unsigned char positional[0x0c];  /* 0x05c */
    unsigned int particle_count;     /* 0x068 */
    /* Emission spread extents; x/y bound both emission_minimum and
       emission_maximum symmetrically, z only the maximum. */
    wiz8::retail::Vec3f spread;        /* 0x06c */
    int has_acceleration;            /* 0x078 */
    wiz8::retail::Vec3f acceleration;  /* 0x07c */
    int expiry_mode;                 /* 0x088: nonzero selects texture-frame expiry */
    int bounds_mode;                 /* 0x08c */
    wiz8::retail::Vec3f bounds_origin; /* 0x090 */
    float bounds_radius;             /* 0x09c */
    wiz8::retail::Vec3f bounds_extent; /* 0x0a0 */
    unsigned int lifetime;           /* 0x0ac */
    int emission_mode;               /* 0x0b0 */
    unsigned int emission_interval;  /* 0x0b4 */
    int los_check;                   /* 0x0b8: nonzero enables the line-of-sight check */
    int speed_mode;                  /* 0x0bc */
    float initial_speed;             /* 0x0c0 */
    float speed_min;                 /* 0x0c4 */
    float speed_max;                 /* 0x0c8 */
    float particle_size;             /* 0x0cc: billboard quad scale */
    int flutter_mode;                /* 0x0d0 */
    float flutter_value;             /* 0x0d4 */
    float flutter_period;            /* 0x0d8 */
    int direction_mode;              /* 0x0dc */
    float direction0;                /* 0x0e0 */
    float direction1;                /* 0x0e4 */
    int initially_active;            /* 0x0e8 */
    W8MaterialRecord material;       /* 0x0ec */
    short attachment_key;            /* 0x216, version >= 2 */
    int emission_limit;              /* 0x218, version >= 3 */
    unsigned char requires_sorted_renderer; /* 0x21c, version >= 3 */
    int start_frame;                 /* 0x21d, version >= 4 */
    int end_frame;                   /* 0x221, version >= 4 */
};
#pragma pack(pop)
static_assert(sizeof(W8LevelParticleRecord) == 0x225);

class Game;

struct LevelLoadContext {
    World* world = nullptr;
    wiz8::File* hFile = nullptr;
    std::filesystem::path bitmap_folder;
};

unsigned char ReadLevel(Game& game, World* world, wiz8::File* handle,
                        const std::filesystem::path& texture_directory);
bool ReadWorldItems(Game& game, LevelLoadContext& info);
// The destination owns particles; scene attachment borrows them.
unsigned char ReadWorldParticles(LevelLoadContext* info, World* owner,
                                 std::vector<std::unique_ptr<ParticleSystem>>* particles);

// Append a complete named-position section; failure leaves the destination unchanged.
bool ReadNamedPositions(wiz8::File* file, std::vector<W8NamedPosition>& positions);

// Camera entries and paths are committed together after the full table loads.
unsigned char ReadWorldCameras(LevelLoadContext* info, World* world);

extern const float g_path_endpoint_scale;

class LevelSpatialIndex;
struct World;

std::unique_ptr<LevelSpatialIndex> ReadCollisionGeometry(std::string_view path, World* owner);
bool BuildCollisionIndex(LevelSpatialIndex* game_data);

// Temporary PR 664 metadata probe; remove after the retail census.
std::optional<int> InspectRawPvlMonsterCount(wiz8::File& file,
                                             const std::filesystem::path& textures);
