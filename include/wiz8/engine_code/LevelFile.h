#ifndef WIZ8_ENGINE_CODE_LEVELFILE_H
#define WIZ8_ENGINE_CODE_LEVELFILE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srMath.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/materials.h"

class OctMeshModel;
namespace wiz8 { class File; }

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

/* One path keyframe: position and an axis/angle rotation. Scaled paths add a
   per-frame scale. */
struct W8LevelFilePathNode { /* 0x1c */
    srVector3T<float> position;
    float angle;
    srVector3T<float> axis;
};

struct W8LevelFileScaledPathNode { /* 0x28 */
    W8LevelFilePathNode path;
    srVector3T<float> scale;
};

struct W8LevelFileFramePosition {
    std::uint16_t frame;
    std::uint16_t tag;
};

/* Compressed mesh face; ReadMesh expands these into W8ReadMeshFace. */
struct W8LevelFileCompressedFace { /* 0x21 */
    std::uint16_t vertex_indices[3];
    float texture_coordinates[3][2];
    std::uint16_t material_index;
    std::uint8_t flags;
};

/* The stParametricLightDefinition fields from flags through subcycle_max.
   flags bit 0x10 marks a light that owns a path. */
struct W8LevelFileLightExtra { /* 0x3c */
    std::uint32_t flags;
    float flicker_chance;
    srVector3T<float> color;
    srVector3T<float> color_to;
    float intensity;
    float intensity_to;
    float period;
    float rate;
    float path_speed;
    std::int32_t subcycle_min;
    std::int32_t subcycle_max;
};

/* A world item placement. The .pvl item section stores the same fields plus
   an optional inline trigger behind has_trigger. */
struct W8LevelFileItemRecord { /* 0x44 */
    char item_name[0x14];       /* item script name */
    srVector3T<float> position; /* scaled by world_scale on load */
    std::int32_t positional5;
    std::int32_t positional7;
    std::int32_t positional8;
    std::int32_t positional9;
    bool has_trigger;
    std::uint8_t positional10[3];
    std::int32_t positional11;
    std::int32_t positional12;
    std::int32_t positional13;
    std::int32_t positional14;
};

/* A clipping plane: a 64-byte name and a four-float position. */
struct W8LevelFileClippingPlaneRecord { /* 0x50 */
    char name0[0x40];
    glm::vec4 serialized_position;
};

/* The fixed head of a monster placement; its path nodes follow. */
struct W8LevelFileMonsterHeader { /* 0x22 */
    char monster_name[0x14];
    std::uint8_t path_version;
    std::uint8_t path_scaled;
    std::int32_t path_position;
    std::uint8_t unknown_1a[4];
    std::int32_t path_node_count;
};

struct W8LevelFileTriggerPosition { /* 0x1c */
    srVector3T<float> position;
    float angle;
    srVector3T<float> direction;
};

struct W8LevelFileTriggerHotSpot { /* 0x85 */
    std::uint8_t unknown_00[0x85];
};

/* Door trigger action data: version, nine flag bytes, the item index, a
   position gate, the position and the linked trigger name. */
struct W8LevelFileDoor { /* 0x99 */
    std::uint8_t version;
    std::uint8_t flags[9];
    std::uint16_t item;
    bool has_position;
    srVector3T<float> position;
    char linked_trigger[0x80];
};

/* A trigger plane's four corners. */
struct W8LevelFilePlane { /* 0x30 */
    srVector3T<float> vertices[4];
};

/* One bounds pair: minimum then maximum corner. */
struct W8LevelFileBounds { /* 0x18 */
    srVector3T<float> minimum;
    srVector3T<float> maximum;
};

/* The 0x225-byte particle body shared by the level file's particle systems
   and the world file's particle records. */
struct W8LevelParticleRecord {
    char name[64];                   /* 0x000 */
    srVector3T<float> location;      /* 0x040 */
    float rotation_angle;            /* 0x04c */
    srVector3T<float> rotation_axis; /* 0x050 */
    std::uint8_t positional[0x0c];  /* 0x05c */
    std::uint32_t particle_count;     /* 0x068 */
    /* Emission spread extents; x/y bound both emission_minimum and
       emission_maximum symmetrically, z only the maximum. */
    srVector3T<float> spread;        /* 0x06c */
    std::int32_t has_acceleration;            /* 0x078 */
    srVector3T<float> acceleration;  /* 0x07c */
    std::int32_t expiry_mode;                 /* 0x088: nonzero selects texture-frame expiry */
    std::int32_t bounds_mode;                 /* 0x08c */
    srVector3T<float> bounds_origin; /* 0x090 */
    float bounds_radius;             /* 0x09c */
    srVector3T<float> bounds_extent; /* 0x0a0 */
    std::uint32_t lifetime;           /* 0x0ac */
    std::int32_t emission_mode;               /* 0x0b0 */
    std::uint32_t emission_interval;  /* 0x0b4 */
    std::int32_t los_check;                   /* 0x0b8: nonzero enables the line-of-sight check */
    std::int32_t speed_mode;                  /* 0x0bc */
    float initial_speed;             /* 0x0c0 */
    float speed_min;                 /* 0x0c4 */
    float speed_max;                 /* 0x0c8 */
    float particle_size;             /* 0x0cc: billboard quad scale */
    std::int32_t flutter_mode;                /* 0x0d0 */
    float flutter_value;             /* 0x0d4 */
    float flutter_period;            /* 0x0d8 */
    std::int32_t direction_mode;              /* 0x0dc */
    float direction0;                /* 0x0e0 */
    float direction1;                /* 0x0e4 */
    std::int32_t initially_active;            /* 0x0e8 */
    W8MaterialRecord material;       /* 0x0ec */
    std::int16_t attachment_key;            /* 0x216, version >= 2 */
    std::int32_t emission_limit;              /* 0x218, version >= 3 */
    std::uint8_t requires_sorted_renderer; /* 0x21c, version >= 3 */
    std::int32_t start_frame;                 /* 0x21d, version >= 4 */
    std::int32_t end_frame;                   /* 0x221, version >= 4 */
};

struct W8LevelFileParticleSystem { /* 0x226 */
    char version;
    W8LevelParticleRecord particle;
};

/* A W8NamedPosition preceded by its version byte. */
struct W8LevelFileNamedPosition { /* 0x9d */
    std::uint8_t version;
    char name[0x80];
    srVector3T<float> position;
    float angle;
    srVector3T<float> direction;
};

#pragma pack(pop)

/* ---- Runtime records, transferred field by field ---- */

/* A keyframed path. Scaled paths (scaled == 2) store W8LevelFileScaledPathNode
   frames, the others W8LevelFilePathNode frames. */
struct W8LevelFilePathAI {
    std::uint8_t version = 0;
    std::uint8_t scaled = 0;
    std::int32_t position = 0;
    std::uint8_t unknown_06[4]{};
    std::vector<W8LevelFileScaledPathNode> scaled_paths;
    std::vector<W8LevelFilePathNode> paths;

    bool IsScaled() const { return scaled == 2; }
    std::int32_t PathCount() const
    {
        return static_cast<std::int32_t>(IsScaled() ? scaled_paths.size() : paths.size());
    }
    /* A frame as a scaled node; unscaled frames get unit scale. */
    W8LevelFileScaledPathNode Node(std::int32_t frame) const
    {
        if (IsScaled()) return scaled_paths[frame];
        return {paths[frame], srVector3T<float>(1.0f, 1.0f, 1.0f)};
    }
};

/* A mesh. Plain meshes hold num_vertices positions; LOD meshes hold one
   vertex frame per LOD, either as floats or as packed shorts scaled by
   lod_scale. Faces are either full or compressed records. */
struct W8LevelFileMesh {
    std::int32_t version = 0;
    std::int32_t num_vertices = 0;
    std::int32_t num_faces = 0;
    std::uint8_t flags = 0;           /* W8LevelMeshFlag; version >= 3 */
    srVector3T<float> location{};      /* version >= 2 */
    float rotation_angle = 0;          /* version >= 2 */
    srVector3T<float> rotation_axis{}; /* version >= 2 */
    srVector3T<float> scale{};         /* version >= 2 */
    char mapping_count = 0;            /* version >= 4; at most one mapping */
    std::int16_t mapped_value = 0;
    std::int16_t mapped_key = 0;
    char lod_mode = 0;    /* LOD meshes */
    float lod_scale = 0;  /* short LOD meshes with lod_mode >= 2 */
    std::vector<srVector3T<float>> vertices;
    std::vector<std::vector<srVector3T<float>>> lods;
    std::vector<std::vector<std::int16_t>> lod_shorts; /* three shorts per vertex */
    std::vector<W8LevelFileCompressedFace> compressed_faces;
    std::vector<W8SerializedMeshFace> faces;

    bool HasLods() const { return (flags & W8_LEVEL_MESH_LOD_VERTICES) != 0; }
    bool HasShortLods() const { return (flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) != 0; }
    bool HasCompressedFaces() const { return (flags & W8_LEVEL_MESH_COMPRESSED_FACES) != 0; }
    std::int32_t LodCount() const { return static_cast<std::int32_t>(HasShortLods() ? lod_shorts.size() : lods.size()); }
};

/* A light. flags bit 2 adds the parametric definition, whose own flag adds a
   path. */
struct W8LevelFileLight {
    std::int16_t version = 0;
    std::uint8_t create = 0;  /* != 0 -> created light path over static */
    bool visible = false; /* bake requires != 0 to emit the light */
    std::uint16_t flags = 0;  /* W8LevelLightFlag */
    std::uint8_t unknown_06[2]{};
    srVector3T<float> position{}; /* consumed by the vertex-lighting pass */
    srVector3T<float> colour{};
    float intensity = 0;
    float range = 0;
    char name[0x14]{}; /* version >= 2; sun/moon/lightning classify it */
    std::optional<W8LevelFileLightExtra> extra;
    std::optional<W8LevelFilePathAI> path_ai;
};

struct W8LevelFileAnimLight {
    char version = 0;
    srVector3T<float> position{};
    srVector3T<float> color{};
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
    std::int32_t positional0 = 0;
    std::int32_t positional1 = 0;
    char has_scale = 0;
    char name[0x14]{};
    float scale = 0; /* has_scale != 0 */
    W8LevelFilePathAI pathAI;
};

/* A door reference: kind 1 carries a door record. */
struct W8LevelFileDoorRef {
    std::uint8_t kind = 0;
    std::optional<W8LevelFileDoor> door;
};

/* The region a linked record surrounds: 36 vertex triples, the face that
   links to an environment record and a flag byte are serialized; the two
   scales come from the owning trigger. */
struct W8LevelFileLinkedRecord {
    std::uint8_t kind = 0;
    srVector3T<float> vertices[36]{};
    std::int8_t linked_face = 0;
    std::uint8_t flag = 0;
    float normal_scale = 0;
    float forward_scale = 0;
};

/* Type-1 (switch) trigger. */
struct W8LevelFileSwitch {
    char version = 0;
    std::int32_t cycle_bounce = 0;
    std::int32_t state_count = 0;
    float animate_states = 0; /* != 0 -> W8_TRIGGER_ANIMATE_STATES */
    std::int32_t range = 0;            /* -> range_maximum (*500) */
    std::int32_t action = 0;           /* -> initial_action */
    std::int32_t value = 0;            /* serialized; unused */
    std::int32_t animate_action = 0;   /* != 0 -> W8_TRIGGER_ANIMATE_ACTION */
    std::uint8_t packed_flags = 0; /* bit0 FIRE_LINKED, bit1 LINK_ON_DEACTIVATE */
    std::uint8_t enabled = 0;
    char name[0x80]{};
    char recipients[0x100]{};
    char sound[0x80]{};             /* wave filename -> "data\sound\%s" */
    float minimum_range = 0;        /* version >= 2 -> range_minimum (*500) */
    char surface_id[0x40]{};        /* version >= 2 */
    bool has_door_trigger = false; /* version >= 3 */
    W8LevelFileDoorRef door;
    std::int32_t action_value = 0;           /* version >= 4 */
};

/* Type-2 (invisible) trigger. Retail gates a kind-2 linked record on a byte
   it never sets, so that record is never read or written. */
struct W8LevelFileInvisible {
    char version = 0;
    float range = 0;
    srVector3T<float> position{};
    std::int32_t action = 0;
    std::int32_t searchable = 0;
    std::uint8_t fire_linked = 0;
    std::uint8_t enabled = 0;
    char name[0x80]{};
    char recipients[0x100]{};
    bool plane_flag = false;             /* version >= 2: == 1 registers a trigger plane */
    std::optional<W8LevelFilePlane> plane;    /* version >= 2 */
    float angle = 0;                          /* version >= 3 */
    srVector3T<float> direction{};            /* version >= 3 */
    std::uint8_t unused = 0;                 /* version >= 3 */
    char action_string[0x80]{};               /* version >= 3: action-17 payload */
    std::uint8_t flag = 0;                   /* version >= 4 -> flags bit3 */
    std::int32_t action_value = 0;                     /* version >= 4 */
    bool has_legacy_geometry = false;    /* version >= 5 */
    std::uint8_t geometry_kind = 0;          /* has_legacy_geometry != 0 */
};

/* Type-3 (ambient sound) trigger. */
struct W8LevelFileSound {
    char version = 0;
    std::int32_t volume_min = 0;
    std::int32_t volume_max = 0;
    std::int32_t speed_min = 0;
    std::int32_t speed_max = 0;
    std::int32_t time_min = 0;
    std::int32_t time_max = 0;
    std::int32_t unbounded = 0; /* == 0 gates the bounded radius */
    float radius = 0;
    srVector3T<float> position{};
    srVector3T<float> region_u{};
    srVector3T<float> region_v{};
    char wave[0x80]{};
    bool has_position = false;     /* version >= 2 */
    std::uint8_t looping = 0;          /* version >= 2 */
    srVector3T<float> region_center{};  /* version >= 3 */
    float region_angle = 0;             /* version >= 3 */
    srVector3T<float> region_min{};     /* version >= 3 */
    srVector3T<float> region_max{};     /* version >= 3 */
    char name[0x80]{};                  /* version >= 4 */
    std::uint8_t shared = 0;           /* version >= 5 */
};

/* Type-4 (super) trigger. */
struct W8LevelFileSuperTrigger {
    char version = 0;
    char name[0x80]{};
    std::uint8_t flags = 0; /* bit0 skips the placement and hotspot records */
    std::uint8_t active = 0;
    std::uint8_t kind = 0;
    bool when_active = false;
    std::uint8_t prop_index = 0;
    std::uint8_t activation_count = 0;
    std::uint8_t inactive_count = 0;
    std::int32_t trigger = 0;
    std::int32_t trigger_on = 0;
    std::int32_t trigger_off = 0;
    char recipients[0x100]{};
    std::uint8_t ataxia_or_cure = 0;
    char ps_events[0x100]{};
    std::uint8_t allow_save = 0;
    std::int32_t price = 0;
    std::uint8_t door_kind = 0;
    char animation[0x80]{};
    float size[3]{};          /* version >= 2 */
    float direction = 0;      /* version >= 2 */
    std::uint8_t wait0 = 0;  /* version >= 2 */
    std::uint8_t wait1 = 0;  /* version >= 2 */
    std::uint8_t wait2 = 0;  /* version >= 2 */
    std::uint8_t loop = 0;   /* version >= 2 */
    float speed = 0;          /* version >= 2 */
    float unknown_4b3[3]{};   /* version >= 2 */
    std::uint8_t ignore = 0;
    std::uint8_t group = 0;
    std::uint8_t set_group = 0;
    char groups[0x100]{};
    char objects[0x100]{};
    std::uint8_t close_door = 0;
    std::int32_t wait3 = 0;
    std::int32_t field = 0;
    char event[0x100]{};
    float normal_scale = 0;        /* copied to the linked record */
    char particle_system[0x80]{};  /* version >= 3 */
    std::uint8_t placement_kind = 0;                /* !(flags & 1) */
    std::optional<W8LevelFileTriggerPosition> position; /* placement_kind == 1 */
    std::optional<W8LevelFilePlane> plane;           /* placement_kind == 2 */
    std::optional<W8LevelFileTriggerHotSpot> hotspot;   /* !(flags & 1) */
    bool has_door = false;
    W8LevelFileDoorRef door;                         /* has_door != 0 */
    std::optional<W8LevelFileLinkedRecord> record;   /* door.kind == 2 */
};

/* A trigger: its type selects the record. Unknown types carry no record. */
struct W8LevelFileTrigger {
    std::uint8_t version = 0;
    char type = 0; /* W8LevelTriggerType */
    std::variant<std::monostate, W8LevelFileSwitch, W8LevelFileInvisible, W8LevelFileSound,
                 W8LevelFileSuperTrigger>
        data;
};

/* One morph or transform frame: a flag byte, a mesh and its materials. */
struct W8LevelFileFrame {
    std::uint8_t flags = 0;
    W8LevelFileMesh mesh;
    std::vector<W8MaterialRecord> textures;
};

/* Morph channel. num_frames frames are declared; when the first frame is an
   LOD mesh it holds every vertex frame and the rest are not stored. */
struct W8LevelFileMorph {
    std::uint8_t channel = 0;
    std::uint8_t num_frames = 0;
    std::vector<W8LevelFileFrame> frames; /* num_frames entries */
};

/* Transform channel: its frames and the path that moves them. */
struct W8LevelFileTransform {
    std::uint8_t channel = 0;
    std::uint8_t num_frames = 0;
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
    std::uint8_t discarded[0x32]{}; /* serialized; never used */
    std::vector<char> channel_counts; /* one byte per animation */
    std::vector<W8LevelFileBounds> bounds;          /* version >= 7 */
    std::vector<W8LevelFileAnimLight> anim_lights;  /* version >= 8 */
    std::optional<W8LevelFilePathAI> path_ai;       /* morphs, version >= 9 */
    std::vector<W8LevelFileMorph> morphs;           /* path_lists == 0: one per animation */
    std::vector<W8LevelFileTransform> transforms;   /* path_lists != 0 */

    /* Clamp a frame to the shortest transform path. */
    std::uint16_t ClampFrame(std::uint16_t frame) const
    {
        for (const auto& transform : transforms) {
            const std::int32_t count = transform.pathAI.PathCount();
            if (count <= frame) frame = static_cast<std::uint16_t>(count - 1);
        }
        return frame;
    }
};

struct W8LevelFileProp {
    char version = 0;
    std::uint8_t frame_count = 0;  /* upper bound for segment frames */
    std::uint8_t option = 0;       /* version >= 5 */
    srVector3T<float> position{};   /* version >= 5 */
    std::uint32_t flags = 0;         /* version >= 6; W8_PROP_COLLIDABLE etc. */
    char name[0x40]{};              /* version >= 7 */
    std::vector<W8LevelFileFramePosition> frame_positions; /* version >= 8 */
    W8LevelFileAnimObj anim_obj;
    std::optional<W8LevelFileTrigger> trigger;
    char has_footsteps = 0;         /* version >= 9 */
    std::uint8_t footstep_surface = 0;
    std::uint8_t footstep_material = 0;
};

/* The environment block: fog, environment colour, camera mode and the two
   optional 256-entry colour ramps. */
struct W8LevelFileBlock {
    bool fog_enabled = false;
    float environment_red = 0;
    float environment_green = 0;
    float environment_blue = 0;
    float intensity = 0;
    float view_distance = 0;
    /* >= 1 -> camera_position; >= 2 -> camera_angle and camera_axis too. */
    std::uint8_t camera_mode = 0;
    srVector3T<float> camera_position{};
    float camera_angle = 0;
    srVector3T<float> camera_axis{};
    bool has_light_colours = false;
    std::uint8_t light_colours[0x300]{};
    bool has_environment_colours = false;
    std::uint8_t environment_colours[0x300]{};
};

/* A whole level file. */
struct W8LevelFile {
    W8LevelFileMesh mesh;
    std::vector<W8MaterialRecord> textures;
    std::vector<W8LevelFileLight> lights;
    std::vector<W8LevelFileMonster> monsters;
    std::vector<W8LevelFileItemRecord> items;
    std::int32_t missile_count = 0;
    std::vector<W8LevelFileProp> props;
    std::vector<W8LevelFileProp> bitmaps;
    std::vector<W8LevelFileCamera> cameras;
    std::int32_t has_block = 0;
    W8LevelFileBlock block;
    std::vector<W8LevelFileTrigger> triggers;
    std::int32_t camera_mode = 0; /* SetCameraSwayMode */
    std::uint8_t clipping_plane_version = 0;
    std::vector<W8LevelFileClippingPlaneRecord> clipping_planes;
    srVector3T<float> environment_offset{};
    std::vector<W8LevelFileParticleSystem> particle_systems;
    std::vector<W8LevelFileNamedPosition> named_positions;
    /* PrePathing::CreateAutomapNodes fills these with the sorted automap cell keys. */
    std::vector<std::uint32_t> automap_nodes;
    std::uint8_t unknown_6b9[4]{};
    std::int32_t read_end_position = 0;
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
                    std::span<OctMeshModel> models, std::int32_t mesh_count);
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
bool ReadPropsFile(wiz8::File& file, std::int32_t count, std::vector<W8LevelFileProp>& props);
bool WritePropsFile(wiz8::File& file, const std::vector<W8LevelFileProp>& props);
bool ReadParticleSystemFile(wiz8::File& file, W8LevelFileParticleSystem& system);
bool WriteParticleSystemFile(wiz8::File& file, const W8LevelFileParticleSystem& system);
bool ReadLevelFileBlock(wiz8::File& file, W8LevelFileBlock& block);
bool WriteLevelFileBlock(wiz8::File& file, const W8LevelFileBlock& block);

#endif
