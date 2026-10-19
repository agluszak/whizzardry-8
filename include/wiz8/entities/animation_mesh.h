#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/spatial/bounding_box.h"

class MeshInstance;
namespace wiz8 {
class File;
}

struct AnimationMesh {
    AnimationMesh();
    AnimationMesh(const AnimationMesh& other);
    AnimationMesh& operator=(const AnimationMesh&) = delete;
    ~AnimationMesh();

    struct State {
        bool loaded = false;
        bool frame_count_loaded = false;
        bool radius_loaded = false;
        bool keep_loaded = false;
        bool cache_protected = false;
        bool single_instance = false;
    };

    State flags{};
    unsigned char frame_count{};
    std::vector<std::unique_ptr<MeshInstance>> meshes;
    glm::vec3 bounds_minimum{0, 0, 0};
    glm::vec3 bounds_maximum{0, 0, 0};
    float radius{};
    unsigned int loaded_bytes{};
    std::filesystem::path bitmap_directory;
    std::filesystem::path source_file;
    std::int64_t file_offset{};
    int last_used{};
};

extern int g_animesh_cache_stamp;
extern int g_animesh_cache_bytes;
extern int g_animesh_cache_limit;
extern std::vector<AnimationMesh*> g_animesh_cache_list;

void InitializeAniMeshCache(int primary_limit, int secondary_limit);
void FreeAniMeshCache(void);

std::optional<BoundingBox> GetAniMeshBounds(AnimationMesh* mesh);
std::unique_ptr<AnimationMesh>
ReadAnimatedMesh(wiz8::File& file, const std::filesystem::path& texture_directory, bool load_all);
bool UnloadAniMesh(AnimationMesh* mesh, bool force);
MeshInstance* GetAniMeshFrame(AnimationMesh* mesh, unsigned char frame);

unsigned char AniMeshValue(AnimationMesh* mesh);
bool AniMeshRadius(AnimationMesh* mesh, float* radius);
/* Two parameters, not three: the retail body reads its flag from the second
   stack slot, and GrCycle's 0x004A7470 pushes exactly the pair. */
void SetAniMeshCacheProtected(AnimationMesh* mesh, bool enabled);
void EnforceAniMeshMemoryLimit(AnimationMesh* current);
