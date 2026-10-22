#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "wiz8/render/transform.h"

namespace wiz8 {
class File;
}

enum W8LevelMeshFlag {
    W8_LEVEL_MESH_LOD_VERTICES = 1,
    W8_LEVEL_MESH_SHORT_LOD_VERTICES = 2,
    W8_LEVEL_MESH_COMPRESSED_FACES = 4
};

#pragma pack(push, 1)
struct W8SerializedMeshFace {
    int vertices[3];
    float texture_coordinates[3][2];
    int material_index;
    unsigned char flags;
};
/* The compressed face form ReadMesh expands into W8ReadMeshFace. */
struct W8CompressedMeshFace {
    unsigned short vertex_indices[3];
    float texture_coordinates[3][2];
    unsigned short material_index;
    unsigned char flags;
};
#pragma pack(pop)

static_assert(sizeof(W8SerializedMeshFace) == 0x29);
static_assert(sizeof(W8CompressedMeshFace) == 0x21);

class Mesh;
class MeshInstance;

// Loaders return detached owned instances; failure returns an empty result.
std::unique_ptr<MeshInstance> ReadSingleLevelMesh(wiz8::File& file,
                                                  const std::filesystem::path& texture_directory);
std::vector<std::unique_ptr<MeshInstance>>
ReadMultipleLevelMeshes(wiz8::File& file, std::size_t expected_count,
                        const std::filesystem::path& texture_directory);
// The shared mesh a record describes and where the record places it, without an instance.
struct PlacedMesh {
    std::shared_ptr<Mesh> mesh;
    Transform3D placement;
};
PlacedMesh ReadPlacedMesh(wiz8::File& file, const std::filesystem::path& texture_directory);
