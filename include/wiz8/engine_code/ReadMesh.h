#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <cstddef>

#include "glm/fwd.hpp"

struct W8MaterialRecord;
struct W8MeshMaterial;
struct W8ReadLevelInfo;
namespace wiz8 { class File; }
template <class T> class srVector3T;

#pragma pack(push, 1)
struct W8SerializedMeshFace {
    std::int32_t vertices[3];
    float texture_coordinates[3][2];
    std::int32_t material_index;
    std::uint8_t flags;
};
#pragma pack(pop)

static_assert(sizeof(W8SerializedMeshFace) == 0x29);

// Runtime math objects have natural alignment; disk records contain only values.
struct W8ReadMeshFace {
    std::int32_t vertices[3];
    glm::vec2 texture_coordinates[3];
    std::int32_t material_index;
    std::uint8_t flags;
};
inline W8ReadMeshFace DecodeMeshFace(const W8SerializedMeshFace& record) {
    W8ReadMeshFace face{};
    for (std::int32_t corner = 0; corner < 3; ++corner) {
        face.vertices[corner] = record.vertices[corner];
        face.texture_coordinates[corner] = {record.texture_coordinates[corner][0],
                                             record.texture_coordinates[corner][1]};
    }
    face.material_index = record.material_index; face.flags = record.flags;
    return face;
}

class srModelInstance;
class stModelInstance;
class stMeshModel;
class srMeshModel;

// Loaders return detached owned instances; failure returns an empty result.
std::unique_ptr<stModelInstance> ReadSingleLevelMesh(W8ReadLevelInfo* info,
                                                   const char* name, bool load_materials);
std::vector<std::unique_ptr<stModelInstance>>
ReadMultipleLevelMeshes(W8ReadLevelInfo* info, std::size_t expected_count, const char* name);
std::uint8_t SkipSingleLevelMesh(W8ReadLevelInfo* info);
void ReleaseReadMeshScratch();
std::shared_ptr<stMeshModel>
BuildSingleLevelMesh(std::int32_t face_count, W8ReadMeshFace* faces, std::int32_t vertex_count,
                     const std::vector<W8MeshMaterial>& materials, std::uint32_t* mesh_count, std::vector<std::vector<std::int32_t>>& vertex_maps,
                     std::vector<std::int16_t>* mapped_values, std::vector<std::int16_t>* mapped_keys);

void OptimizeMeshOrder(srMeshModel* model);

void ClearMaterialRecordPadding(W8MaterialRecord* material);
void ReadMeshTransform(wiz8::File* file, srVector3T<float>* location, glm::mat3* rotation,
                       srVector3T<float>* scale);
