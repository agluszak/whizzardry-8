#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>

#include "compat/compiler.h"
#include "glm/fwd.hpp"

struct W8MaterialRecord;
struct W8MeshMaterial;
struct W8ReadLevelInfo;
namespace wiz8 { class File; }
template <class T> class srVector3T;

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
inline W8ReadMeshFace DecodeMeshFace(const W8SerializedMeshFace& record) {
    W8ReadMeshFace face{};
    for (int corner = 0; corner < 3; ++corner) {
        face.vertices[corner] = record.vertices[corner];
        face.texture_coordinates[corner] = {record.texture_coordinates[corner][0],
                                             record.texture_coordinates[corner][1]};
    }
    face.material_index = record.material_index; face.flags = record.flags;
    return face;
}

class srModelInstance;
class stMeshModel;
class srMeshModel;

unsigned char ReadSingleLevelMesh(W8ReadLevelInfo* info, srModelInstance** instance,
                                  int unused_first, int unused_second, const char* name,
                                  bool load_materials);
unsigned char ReadMultipleLevelMeshes(W8ReadLevelInfo* info, srModelInstance** instances,
                                      w8_ulong count, const char* name);
unsigned char SkipSingleLevelMesh(W8ReadLevelInfo* info);
void ReleaseReadMeshScratch();
unsigned char ReadSingleLevelMeshBody(W8ReadLevelInfo* info, srModelInstance** instance, int, int,
                                      const char* name, bool load_materials);
std::shared_ptr<stMeshModel>
BuildSingleLevelMesh(int face_count, W8ReadMeshFace* faces, int vertex_count,
                     const std::vector<W8MeshMaterial>& materials, unsigned int* mesh_count, std::vector<std::vector<int>>& vertex_maps,
                     std::vector<short>* mapped_values, std::vector<short>* mapped_keys);

void OptimizeMeshOrder(srMeshModel* model);

void ClearMaterialRecordPadding(W8MaterialRecord* material);
void ReadMeshTransform(wiz8::File* file, srVector3T<float>* location, glm::mat3* rotation,
                       srVector3T<float>* scale);
