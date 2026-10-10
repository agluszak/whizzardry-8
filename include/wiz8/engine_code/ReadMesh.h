#pragma once

#include "wiz8/filesystem.h"

#include "surrender/srMath.h"
struct W8MaterialRecord;

#include "wiz8/vector.h"

struct W8ReadLevelInfo;

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
void ReleaseRetainedMaterials();
unsigned char ReadSingleLevelMeshBody(W8ReadLevelInfo* info, srModelInstance** instance, int, int,
                                      const char* name, bool load_materials);
stMeshModel*
BuildSingleLevelMesh(int face_count, W8ReadMeshFace* faces, int vertex_count, int material_count,
                     srMaterialIFace** materials, srTextureIFace** textures, srShader* render_flags,
                     unsigned int* mesh_count, int*** vertex_maps, unsigned int* vertex_map_count,
                     W8GrowableVector<short>* mapped_values, W8GrowableVector<short>* mapped_keys);

enum {
    W8_MESH_ORDER_POLYGONS = 1UL,
    W8_MESH_ORDER_VERTICES = 2UL,
    W8_MESH_ORDER_TRIANGLE_STRIPS = 4UL
};

void OptimizeMeshOrder(srMeshModel* model, w8_ulong flags);

void ClearMaterialRecordPadding(W8MaterialRecord* material);
void ReadMeshTransform(wiz8::File* file, srVector3T<float>* location, glm::mat3* rotation,
                       srVector3T<float>* scale);
