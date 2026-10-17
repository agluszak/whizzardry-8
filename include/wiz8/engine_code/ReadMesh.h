#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "glm/fwd.hpp"

struct W8MaterialRecord;
struct W8MeshMaterial;
namespace wiz8 {
class File;
}

#pragma pack(push, 1)
struct W8SerializedMeshFace {
    int vertices[3];
    float texture_coordinates[3][2];
    int material_index;
    unsigned char flags;
};
#pragma pack(pop)

static_assert(sizeof(W8SerializedMeshFace) == 0x29);

// Runtime math objects have natural alignment; disk records contain only values.
struct W8ReadMeshFace {
    int vertices[3];
    glm::vec2 texture_coordinates[3];
    int material_index;
    unsigned char flags;
};
inline W8ReadMeshFace DecodeMeshFace(const W8SerializedMeshFace& record)
{
    W8ReadMeshFace face{};
    for (int corner = 0; corner < 3; ++corner) {
        face.vertices[corner] = record.vertices[corner];
        face.texture_coordinates[corner] = {record.texture_coordinates[corner][0],
                                            record.texture_coordinates[corner][1]};
    }
    face.material_index = record.material_index;
    face.flags = record.flags;
    return face;
}

class srModelInstance;
class stModelInstance;
class stMeshModel;
class srMeshModel;

// Loaders return detached owned instances; failure returns an empty result.
std::unique_ptr<stModelInstance>
ReadSingleLevelMesh(wiz8::File& file, const std::filesystem::path& texture_directory);
std::vector<std::unique_ptr<stModelInstance>>
ReadMultipleLevelMeshes(wiz8::File& file, std::size_t expected_count,
                        const std::filesystem::path& texture_directory);
// Decode and discard the record, sharing the full loader's wire-format interpretation.
bool ReadSingleLevelMeshHasVertexFrames(wiz8::File& file);
std::shared_ptr<stMeshModel>
BuildSingleLevelMesh(int face_count, W8ReadMeshFace* faces, int vertex_count,
                     const std::vector<W8MeshMaterial>& materials, unsigned int* mesh_count,
                     std::vector<std::vector<int>>& vertex_maps, std::vector<short>* mapped_values,
                     std::vector<short>* mapped_keys);

void OptimizeMeshOrder(srMeshModel* model);
