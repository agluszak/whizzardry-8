#pragma once

#include <vector>

#include <memory>

#include "wiz8/filesystem.h"

#include "surrender/srMath.h"
struct W8MaterialRecord;
struct W8MeshMaterial;

#include <vector>
#include <algorithm>

struct W8ReadLevelInfo;

#pragma pack(push, 1)
struct W8ReadMeshFace {
    int vertices[3];
    glm::vec2 texture_coordinates[3];
    int material_index;
    unsigned char flags;
};
#pragma pack(pop)

W8_ABI_ASSERT(sizeof(W8ReadMeshFace) == 0x29, "W8ReadMeshFace_size_must_be_0x29");
class srMaterialIFace;
class srModelInstance;
class srTextureIFace;
class srShader;
class stMeshModel;
class srMeshModel;
class srClass;

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
