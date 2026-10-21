#pragma once

#include <filesystem>
#include <memory>
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

    unsigned char frame_count{};
    // One instance whose model carries every vertex frame, or one instance per frame.
    bool single_instance = false;
    std::vector<std::unique_ptr<MeshInstance>> meshes;
    BoundingBox bounds;
    float radius{};
    // The file the frames came from; a death cycle's name marks it as a gib.
    std::filesystem::path source_file;
};

std::unique_ptr<AnimationMesh> ReadAnimatedMesh(wiz8::File& file,
                                                const std::filesystem::path& texture_directory);
MeshInstance* GetAniMeshFrame(AnimationMesh* mesh, unsigned char frame);
