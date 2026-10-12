#pragma once

#include <cstdint>
#include <optional>
#include "surrender/srBoundingBox.h"
#include <memory>
#include <string>
#include <vector>

#include "glm/ext/vector_float3.hpp"

class stModelInstance;
struct W8ReadLevelInfo;
struct W8World;
namespace wiz8 { class File; }
template <class T> class srVector3T;

struct W8AniMesh {
    W8AniMesh();
    W8AniMesh(const W8AniMesh& other);
    W8AniMesh& operator=(const W8AniMesh&) = delete;
    ~W8AniMesh();

    struct State {
        bool loaded = false;
        bool frame_count_loaded = false;
        bool radius_loaded = false;
        bool keep_loaded = false;
        bool cache_protected = false;
        bool single_instance = false;
    };

    State flags{};
    std::uint8_t frame_count{};
    std::vector<std::unique_ptr<stModelInstance>> meshes;
    glm::vec3 bounds_minimum{0, 0, 0};
    glm::vec3 bounds_maximum{0, 0, 0};
    float radius{};
    std::uint32_t loaded_bytes{};
    std::int8_t list_index{};
    std::string bitmap_directory;
    std::string filename;
    std::int32_t file_offset{};
    W8World* world{};
    std::int32_t last_used{};
};

extern std::int32_t g_animesh_cache_stamp;
extern std::int32_t g_animesh_cache_bytes;
extern std::int32_t g_animesh_cache_limit;
extern std::int32_t g_animesh_cache_secondary_limit;
extern std::vector<W8AniMesh*> g_animesh_cache_list;

void InitializeAniMeshCache(std::int32_t primary_limit, std::int32_t secondary_limit);
void FreeAniMeshCache(void);

float GetAniMeshFrameRadius(W8AniMesh* mesh, std::uint8_t frame);
std::optional<srBoundingBox> GetAniMeshBounds(W8AniMesh* mesh);
bool LoadAniMesh(wiz8::File* file, W8AniMesh* mesh, bool load_all);
bool LoadAniMeshFromInfo(W8ReadLevelInfo* info, W8AniMesh* mesh, bool load_all);
bool UnloadAniMesh(W8AniMesh* mesh, bool force);
stModelInstance* GetAniMeshFrame(W8AniMesh* mesh, std::uint8_t frame);

std::uint8_t AniMeshValue(W8AniMesh* mesh);
bool AniMeshRadius(W8AniMesh* mesh, float* radius);
/* Two parameters, not three: the retail body reads its flag from the second
   stack slot, and GrCycle's 0x004A7470 pushes exactly the pair. */
void SetAniMeshCacheProtected(W8AniMesh* mesh, bool enabled);
void EnforceAniMeshMemoryLimit(W8AniMesh* current);
