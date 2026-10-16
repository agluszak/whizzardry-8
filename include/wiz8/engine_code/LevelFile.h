#ifndef WIZ8_ENGINE_CODE_LEVELFILE_H
#define WIZ8_ENGINE_CODE_LEVELFILE_H

#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/retail_geometry_types.h"

class OctMeshModel;
namespace wiz8 {
class File;
}

/* Engine Code\LevelFile.cpp: the level-editor .lvl format the level builder
   reads, and the .pvl header it writes in front of the copied remainder.

   Two kinds of types live here. Fixed-width disk records are packed and
   transferred whole; every other record is an ordinary runtime type that owns
   its sub-records and is transferred field by field in file order. */

enum W8LevelMeshFlag {
    W8_LEVEL_MESH_LOD_VERTICES = 1,
    W8_LEVEL_MESH_SHORT_LOD_VERTICES = 2,
    W8_LEVEL_MESH_COMPRESSED_FACES = 4
};

enum W8LevelLightFlag { W8_LEVEL_LIGHT_HAS_DEFINITION = 2 };

enum W8LevelTriggerType : char {
    W8_LEVEL_TRIGGER_SWITCH = 1,
    W8_LEVEL_TRIGGER_INVISIBLE = 2,
    W8_LEVEL_TRIGGER_SOUND = 3,
    W8_LEVEL_TRIGGER_SUPER = 4
};

/* ---- Fixed-width disk records ---- */
#pragma pack(push, 1)

struct W8LevelFileFramePosition {
    unsigned short frame;
    unsigned short tag;
};

/* Compressed mesh face; ReadMesh expands these into W8ReadMeshFace. */
struct W8LevelFileCompressedFace { /* 0x21 */
    unsigned short vertex_indices[3];
    float texture_coordinates[3][2];
    unsigned short material_index;
    unsigned char flags;
};

/* The stParametricLightDefinition fields from flags through subcycle_max.
   flags bit 0x10 marks a light that owns a path. */
struct W8LevelFileLightExtra { /* 0x3c */
    unsigned int flags;
    float flicker_chance;
    wiz8::retail::Vec3f color;
    wiz8::retail::Vec3f color_to;
    float intensity;
    float intensity_to;
    float period;
    float rate;
    float path_speed;
    int subcycle_min;
    int subcycle_max;
};

/* A world item placement. The .pvl item section stores the same fields plus
   an optional inline trigger behind has_trigger. */
struct W8LevelFileItemRecord { /* 0x44 */
    char item_name[0x14];       /* item script name */
    wiz8::retail::Vec3f position; /* scaled by world_scale on load */
    int positional5;
    int positional7;
    int positional8;
    int positional9;
    unsigned char has_trigger;
    unsigned char positional10[3];
    int positional11;
    int positional12;
    int positional13;
    int positional14;
};

/* A clipping plane: a 64-byte name and a four-float position. */
struct W8LevelFileClippingPlaneRecord { /* 0x50 */
    char name0[0x40];
    struct {
        float x, y, z, w;
    } serialized_position;
};

/* The fixed head of a monster placement; its path nodes follow. */
struct W8LevelFileMonsterHeader { /* 0x22 */
    char monster_name[0x14];
    unsigned char path_version;
    unsigned char path_scaled;
    int path_position;
    unsigned char unknown_1a[4];
    int path_node_count;
};

struct W8LevelFileTriggerPosition { /* 0x1c */
    wiz8::retail::Vec3f position;
    float angle;
    wiz8::retail::Vec3f direction;
};

struct W8LevelFileTriggerHotSpot { /* 0x85 */
    unsigned char unknown_00[0x85];
};

/* Door trigger action data: version, nine flag bytes, the item index, a
   position gate, the position and the linked trigger name. */
struct W8LevelFileDoor { /* 0x99 */
    unsigned char version;
    unsigned char flags[9];
    unsigned short item;
    unsigned char has_position;
    wiz8::retail::Vec3f position;
    char linked_trigger[0x80];
};

/* A trigger plane's four corners. */
struct W8LevelFilePlane { /* 0x30 */
    wiz8::retail::Vec3f vertices[4];
};

/* One bounds pair: minimum then maximum corner. */
struct W8LevelFileBounds { /* 0x18 */
    wiz8::retail::Vec3f minimum;
    wiz8::retail::Vec3f maximum;
};

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

struct W8LevelFileParticleSystem { /* 0x226 */
    char version;
    W8LevelParticleRecord particle;
};

/* A W8NamedPosition preceded by its version byte. */
struct W8LevelFileNamedPosition { /* 0x9d */
    unsigned char version;
    char name[0x80];
    wiz8::retail::Vec3f position;
    float angle;
    wiz8::retail::Vec3f direction;
};

#pragma pack(pop)

/* ---- Runtime records, transferred field by field ---- */

// Runtime keyframes; level-file transfers encode each component explicitly.
struct W8LevelFilePathNode {
    glm::vec3 position{0};
    float angle = 0;
    glm::vec3 axis{0};
};
struct W8LevelFileScaledPathNode {
    W8LevelFilePathNode path;
    glm::vec3 scale{0};
};

/* A keyframed path. Scaled paths (scaled == 2) store W8LevelFileScaledPathNode
   frames, the others W8LevelFilePathNode frames. */
struct W8LevelFilePathAI {
    unsigned char version = 0;
    unsigned char scaled = 0;
    int position = 0;
    unsigned char unknown_06[4]{};
    std::vector<W8LevelFileScaledPathNode> scaled_paths;
    std::vector<W8LevelFilePathNode> paths;

    bool IsScaled() const { return scaled == 2; }
    int PathCount() const
    {
        return static_cast<int>(IsScaled() ? scaled_paths.size() : paths.size());
    }
    /* A frame as a scaled node; unscaled frames get unit scale. */
    W8LevelFileScaledPathNode Node(int frame) const
    {
        if (IsScaled()) return scaled_paths[frame];
        return {paths[frame], glm::vec3(1.0f)};
    }
};

/* A mesh. Plain meshes hold num_vertices positions; LOD meshes hold one
   vertex frame per LOD, either as floats or as packed shorts scaled by
   lod_scale. Faces are either full or compressed records. */
struct W8LevelFileMesh {
    int version = 0;
    int num_vertices = 0;
    int num_faces = 0;
    unsigned char flags = 0;           /* W8LevelMeshFlag; version >= 3 */
    glm::vec3 location{0};      /* version >= 2 */
    float rotation_angle = 0;          /* version >= 2 */
    glm::vec3 rotation_axis{0}; /* version >= 2 */
    glm::vec3 scale{0};         /* version >= 2 */
    char mapping_count = 0;            /* version >= 4; at most one mapping */
    short mapped_value = 0;
    short mapped_key = 0;
    char lod_mode = 0;    /* LOD meshes */
    float lod_scale = 0;  /* short LOD meshes with lod_mode >= 2 */
    std::vector<glm::vec3> vertices;
    std::vector<std::vector<glm::vec3>> lods;
    std::vector<std::vector<short>> lod_shorts; /* three shorts per vertex */
    std::vector<W8LevelFileCompressedFace> compressed_faces;
    std::vector<W8SerializedMeshFace> faces;

    bool HasLods() const { return (flags & W8_LEVEL_MESH_LOD_VERTICES) != 0; }
    bool HasShortLods() const { return (flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) != 0; }
    bool HasCompressedFaces() const { return (flags & W8_LEVEL_MESH_COMPRESSED_FACES) != 0; }
    int LodCount() const
    {
        return static_cast<int>(HasShortLods() ? lod_shorts.size() : lods.size());
    }
};

/* A light. flags bit 2 adds the parametric definition, whose own flag adds a
   path. */
struct W8LevelFileLight {
    short version = 0;
    unsigned char create = 0;  /* != 0 -> created light path over static */
    unsigned char visible = 0; /* bake requires != 0 to emit the light */
    unsigned short flags = 0;  /* W8LevelLightFlag */
    unsigned char unknown_06[2]{};
    glm::vec3 position{0.0f}; /* consumed by the vertex-lighting pass */
    glm::vec3 colour{0.0f};
    float intensity = 0;
    float range = 0;
    char name[0x14]{}; /* version >= 2; sun/moon/lightning classify it */
    std::optional<W8LevelFileLightExtra> extra;
    std::optional<W8LevelFilePathAI> path_ai;
};

struct W8LevelFileAnimLight {
    char version = 0;
    glm::vec3 position{0.0f};
    glm::vec3 color{0.0f};
    float intensity = 0;
    float range = 0;
    std::optional<W8LevelFileLightExtra> extra; /* version >= 2 */
};

struct W8LevelFileMonster {
    W8LevelFileMonsterHeader header{};
    std::vector<W8LevelFilePathNode> path;
};

/* A camera waypoint: two unresolved ints, an optional scale and a path. */
struct W8LevelFileCamera {
    int positional0 = 0;
    int positional1 = 0;
    char has_scale = 0;
    char name[0x14]{};
    float scale = 0; /* has_scale != 0 */
    W8LevelFilePathAI pathAI;
};

/* A door reference: kind 1 carries a door record. */
struct W8LevelFileDoorRef {
    unsigned char kind = 0;
    std::optional<W8LevelFileDoor> door;
};

/* The region a linked record surrounds: 36 vertex triples, the face that
   links to an environment record and a flag byte are serialized; the two
   scales come from the owning trigger. */
struct W8LevelFileLinkedRecord {
    unsigned char kind = 0;
    glm::vec3 vertices[36]{};
    signed char linked_face = 0;
    unsigned char flag = 0;
    float normal_scale = 0;
    float forward_scale = 0;
};

/* Type-1 (switch) trigger. */
struct W8LevelFileSwitch {
    char version = 0;
    int cycle_bounce = 0;
    int state_count = 0;
    float animate_states = 0; /* != 0 -> W8_TRIGGER_ANIMATE_STATES */
    int range = 0;            /* -> range_maximum (*500) */
    int action = 0;           /* -> initial_action */
    int value = 0;            /* serialized; unused */
    int animate_action = 0;   /* != 0 -> W8_TRIGGER_ANIMATE_ACTION */
    unsigned char packed_flags = 0; /* bit0 FIRE_LINKED, bit1 LINK_ON_DEACTIVATE */
    unsigned char enabled = 0;
    char name[0x80]{};
    char recipients[0x100]{};
    char sound[0x80]{};             /* wave filename -> "data\sound\%s" */
    float minimum_range = 0;        /* version >= 2 -> range_minimum (*500) */
    char surface_id[0x40]{};        /* version >= 2 */
    unsigned char has_door_trigger = 0; /* version >= 3 */
    W8LevelFileDoorRef door;
    int action_value = 0;           /* version >= 4 */
};

/* Type-2 (invisible) trigger. Retail gates a kind-2 linked record on a byte
   it never sets, so that record is never read or written. */
struct W8LevelFileInvisible {
    char version = 0;
    float range = 0;
    glm::vec3 position{0.0f};
    int action = 0;
    int searchable = 0;
    unsigned char fire_linked = 0;
    unsigned char enabled = 0;
    char name[0x80]{};
    char recipients[0x100]{};
    unsigned char plane_flag = 0;             /* version >= 2: == 1 registers a trigger plane */
    std::optional<W8LevelFilePlane> plane;    /* version >= 2 */
    float angle = 0;                          /* version >= 3 */
    glm::vec3 direction{0.0f};            /* version >= 3 */
    unsigned char unused = 0;                 /* version >= 3 */
    char action_string[0x80]{};               /* version >= 3: action-17 payload */
    unsigned char flag = 0;                   /* version >= 4 -> flags bit3 */
    int action_value = 0;                     /* version >= 4 */
    unsigned char has_legacy_geometry = 0;    /* version >= 5 */
    unsigned char geometry_kind = 0;          /* has_legacy_geometry != 0 */
};

/* Type-3 (ambient sound) trigger. */
struct W8LevelFileSound {
    char version = 0;
    int volume_min = 0;
    int volume_max = 0;
    int speed_min = 0;
    int speed_max = 0;
    int time_min = 0;
    int time_max = 0;
    int unbounded = 0; /* == 0 gates the bounded radius */
    float radius = 0;
    glm::vec3 position{0.0f};
    glm::vec3 region_u{0.0f};
    glm::vec3 region_v{0.0f};
    char wave[0x80]{};
    unsigned char has_position = 0;     /* version >= 2 */
    unsigned char looping = 0;          /* version >= 2 */
    glm::vec3 region_center{0.0f};  /* version >= 3 */
    float region_angle = 0;             /* version >= 3 */
    glm::vec3 region_min{0.0f};     /* version >= 3 */
    glm::vec3 region_max{0.0f};     /* version >= 3 */
    char name[0x80]{};                  /* version >= 4 */
    unsigned char shared = 0;           /* version >= 5 */
};

/* Type-4 (super) trigger. */
struct W8LevelFileSuperTrigger {
    char version = 0;
    char name[0x80]{};
    unsigned char flags = 0; /* bit0 skips the placement and hotspot records */
    unsigned char active = 0;
    unsigned char kind = 0;
    unsigned char when_active = 0;
    unsigned char prop_index = 0;
    unsigned char activation_count = 0;
    unsigned char inactive_count = 0;
    int trigger = 0;
    int trigger_on = 0;
    int trigger_off = 0;
    char recipients[0x100]{};
    unsigned char ataxia_or_cure = 0;
    char ps_events[0x100]{};
    unsigned char allow_save = 0;
    int price = 0;
    unsigned char door_kind = 0;
    char animation[0x80]{};
    float size[3]{};          /* version >= 2 */
    float direction = 0;      /* version >= 2 */
    unsigned char wait0 = 0;  /* version >= 2 */
    unsigned char wait1 = 0;  /* version >= 2 */
    unsigned char wait2 = 0;  /* version >= 2 */
    unsigned char loop = 0;   /* version >= 2 */
    float speed = 0;          /* version >= 2 */
    float unknown_4b3[3]{};   /* version >= 2 */
    unsigned char ignore = 0;
    unsigned char group = 0;
    unsigned char set_group = 0;
    char groups[0x100]{};
    char objects[0x100]{};
    unsigned char close_door = 0;
    int wait3 = 0;
    int field = 0;
    char event[0x100]{};
    float normal_scale = 0;        /* copied to the linked record */
    char particle_system[0x80]{};  /* version >= 3 */
    unsigned char placement_kind = 0;                /* !(flags & 1) */
    std::optional<W8LevelFileTriggerPosition> position; /* placement_kind == 1 */
    std::optional<W8LevelFilePlane> plane;           /* placement_kind == 2 */
    std::optional<W8LevelFileTriggerHotSpot> hotspot;   /* !(flags & 1) */
    unsigned char has_door = 0;
    W8LevelFileDoorRef door;                         /* has_door != 0 */
    std::optional<W8LevelFileLinkedRecord> record;   /* door.kind == 2 */
};

/* A trigger: its type selects the record. Unknown types carry no record. */
struct W8LevelFileTrigger {
    unsigned char version = 0;
    char type = 0; /* W8LevelTriggerType */
    std::variant<std::monostate, W8LevelFileSwitch, W8LevelFileInvisible, W8LevelFileSound,
                 W8LevelFileSuperTrigger>
        data;
};

/* One morph or transform frame: a flag byte, a mesh and its materials. */
struct W8LevelFileFrame {
    unsigned char flags = 0;
    W8LevelFileMesh mesh;
    std::vector<W8MaterialRecord> textures;
};

/* Morph channel. num_frames frames are declared; when the first frame is an
   LOD mesh it holds every vertex frame and the rest are not stored. */
struct W8LevelFileMorph {
    unsigned char channel = 0;
    unsigned char num_frames = 0;
    std::vector<W8LevelFileFrame> frames; /* num_frames entries */
};

/* Transform channel: its frames and the path that moves them. */
struct W8LevelFileTransform {
    unsigned char channel = 0;
    unsigned char num_frames = 0;
    std::vector<W8LevelFileFrame> frames;
    W8LevelFilePathAI pathAI;
};

/* The animated object of a prop. path_lists selects morph channels (0) or
   transform channels. */
struct W8LevelFileAnimObj {
    char version = 0;
    char animation_playing = 0;
    char frame_method = 0;
    char behaviour = 0;
    char cycle = 0;
    char path_lists = 0;
    float playback_scale = 15.0f; /* version >= 3 */
    char start_frame = 0;         /* version >= 5 */
    char random_play = 0;         /* version >= 6 */
    float play_chance = 1.0f;     /* version >= 6 */
    unsigned char discarded[0x32]{}; /* serialized; never used */
    std::vector<char> channel_counts; /* one byte per animation */
    std::vector<W8LevelFileBounds> bounds;          /* version >= 7 */
    std::vector<W8LevelFileAnimLight> anim_lights;  /* version >= 8 */
    std::optional<W8LevelFilePathAI> path_ai;       /* morphs, version >= 9 */
    std::vector<W8LevelFileMorph> morphs;           /* path_lists == 0: one per animation */
    std::vector<W8LevelFileTransform> transforms;   /* path_lists != 0 */

    /* Clamp a frame to the shortest transform path. */
    unsigned short ClampFrame(unsigned short frame) const
    {
        for (const auto& transform : transforms) {
            const int count = transform.pathAI.PathCount();
            if (count <= frame) frame = static_cast<unsigned short>(count - 1);
        }
        return frame;
    }
};

struct W8LevelFileProp {
    char version = 0;
    unsigned char frame_count = 0;  /* upper bound for segment frames */
    unsigned char option = 0;       /* version >= 5 */
    glm::vec3 position{0.0f};   /* version >= 5 */
    unsigned int flags = 0;         /* version >= 6; W8_PROP_COLLIDABLE etc. */
    char name[0x40]{};              /* version >= 7 */
    std::vector<W8LevelFileFramePosition> frame_positions; /* version >= 8 */
    W8LevelFileAnimObj anim_obj;
    std::optional<W8LevelFileTrigger> trigger;
    char has_footsteps = 0;         /* version >= 9 */
    unsigned char footstep_surface = 0;
    unsigned char footstep_material = 0;
};

/* The environment block: fog, environment colour, camera mode and the two
   optional 256-entry colour ramps. */
struct W8LevelFileBlock {
    unsigned char fog_enabled = 0;
    float environment_red = 0;
    float environment_green = 0;
    float environment_blue = 0;
    float intensity = 0;
    float view_distance = 0;
    /* >= 1 -> camera_position; >= 2 -> camera_angle and camera_axis too. */
    unsigned char camera_mode = 0;
    glm::vec3 camera_position{0.0f};
    float camera_angle = 0;
    glm::vec3 camera_axis{0.0f};
    unsigned char has_light_colours = 0;
    unsigned char light_colours[0x300]{};
    unsigned char has_environment_colours = 0;
    unsigned char environment_colours[0x300]{};
};

/* A whole level file. */
struct W8LevelFile {
    W8LevelFileMesh mesh;
    std::vector<W8MaterialRecord> textures;
    std::vector<W8LevelFileLight> lights;
    std::vector<W8LevelFileMonster> monsters;
    std::vector<W8LevelFileItemRecord> items;
    int missile_count = 0;
    std::vector<W8LevelFileProp> props;
    std::vector<W8LevelFileProp> bitmaps;
    std::vector<W8LevelFileCamera> cameras;
    int has_block = 0;
    W8LevelFileBlock block;
    std::vector<W8LevelFileTrigger> triggers;
    int camera_mode = 0; /* SetCameraSwayMode */
    unsigned char clipping_plane_version = 0;
    std::vector<W8LevelFileClippingPlaneRecord> clipping_planes;
    glm::vec3 environment_offset{0.0f};
    std::vector<W8LevelFileParticleSystem> particle_systems;
    std::vector<W8LevelFileNamedPosition> named_positions;
    /* PrePathing::CreateAutomapNodes fills these with the sorted automap cell keys. */
    std::vector<w8_ulong> automap_nodes;
    unsigned char unknown_6b9[4]{};
    int read_end_position = 0;
};

/* The trigger sub-records the builder resolves, in file order: props', then
   bitmaps', then the level's own triggers. The pointers stay valid while the
   level's records are not added or removed. */
struct W8LevelFileTriggerParts {
    std::vector<W8LevelFileSwitch*> switches;
    std::vector<W8LevelFilePlane*> planes;
    std::vector<W8LevelFileLinkedRecord*> linked_records;
};
W8LevelFileTriggerParts CollectTriggerParts(W8LevelFile& level);

/* Readers leave the record empty on failure. */
std::unique_ptr<W8LevelFile> ReadLevelFile(wiz8::File& file);
/* The .pvl header: the built submesh models (written and consumed) and the
   root mesh count replace the source mesh. */
bool WriteLevelFile(wiz8::File& file, wiz8::File& remainder, const W8LevelFile& level,
                    std::span<OctMeshModel> models, int mesh_count);
bool ReadMeshFile(wiz8::File& file, W8LevelFileMesh& mesh);
bool WriteMeshFile(wiz8::File& file, const W8LevelFileMesh& mesh);
bool ReadLightFile(wiz8::File& file, W8LevelFileLight& light);
bool WriteLightFile(wiz8::File& file, const W8LevelFileLight& light);
bool ReadAnimLightFile(wiz8::File& file, W8LevelFileAnimLight& light);
bool WriteAnimLightFile(wiz8::File& file, const W8LevelFileAnimLight& light);
bool ReadTriggerFile(wiz8::File& file, W8LevelFileTrigger& trigger);
bool WriteTriggerFile(wiz8::File& file, const W8LevelFileTrigger& trigger);
/* The super-trigger body that follows a trigger's version and type. */
bool ReadSuperTriggerFile(wiz8::File& file, W8LevelFileTrigger& trigger);
bool WriteSuperTriggerFile(wiz8::File& file, const W8LevelFileTrigger& trigger);
bool ReadDoorTriggerFile(wiz8::File& file, W8LevelFileDoorRef& door);
bool WriteDoorTriggerFile(wiz8::File& file, const W8LevelFileDoorRef& door);
bool ReadPathAIFile(wiz8::File& file, W8LevelFilePathAI& path);
bool WritePathAIFile(wiz8::File& file, const W8LevelFilePathAI& path);
bool ReadAnimObjFile(wiz8::File& file, W8LevelFileAnimObj& anim_obj);
bool WriteAnimObjFile(wiz8::File& file, const W8LevelFileAnimObj& anim_obj);
bool ReadPropsFile(wiz8::File& file, int count, std::vector<W8LevelFileProp>& props);
bool WritePropsFile(wiz8::File& file, const std::vector<W8LevelFileProp>& props);
bool ReadParticleSystemFile(wiz8::File& file, W8LevelFileParticleSystem& system);
bool WriteParticleSystemFile(wiz8::File& file, const W8LevelFileParticleSystem& system);
bool ReadLevelFileBlock(wiz8::File& file, W8LevelFileBlock& block);
bool WriteLevelFileBlock(wiz8::File& file, const W8LevelFileBlock& block);

#endif
