#include "wiz8/compat/unaligned.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/ReadMesh.h"

#include <algorithm>
#include <array>
#include <map>
#include <numeric>
#include <stdexcept>
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/sr_api.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/virtual_file.h"
#include "wiz8/containers.h"
#include "wiz8/filesystem.h"

#include "surrender/srCore.h"
#include "surrender/srNode.h"
#include <algorithm>
#include <ranges>
#include <span>
#include <tuple>
#include <stdlib.h>

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <glm/gtc/matrix_transform.hpp>

static int operator==(const srShader& left, const srShader& right)
{
    return left.value == right.value;
}

// FUNCTION: WIZ8 0x004896C0
void ReadMeshTransform(wiz8::File* file, srVector3T<float>* location, glm::mat3* rotation,
                       srVector3T<float>* scale)
{
    float angle;
    srVector3T<float> axis;

    file->read_exact(&location->x, sizeof(location->x));
    file->read_exact(&location->y, sizeof(location->y));
    file->read_exact(&location->z, sizeof(location->z));
    file->read_exact(&angle, sizeof(angle));
    file->read_exact(&axis.x, sizeof(axis.x));
    file->read_exact(&axis.y, sizeof(axis.y));
    file->read_exact(&axis.z, sizeof(axis.z));

    (*rotation) = glm::mat3(1);
    (*rotation) *=
        glm::mat3_cast(glm::angleAxis(float(angle), normalizedOrZero(glm::vec3(axis.glmValue()))));

    file->read_exact(&scale->x, sizeof(scale->x));
    file->read_exact(&scale->y, sizeof(scale->y));
    file->read_exact(&scale->z, sizeof(scale->z));
}

/* The last level material list, reused while consecutive meshes share it. */
static std::vector<W8MaterialRecord> g_read_mesh_material_records;
static std::vector<W8MeshMaterial> g_read_mesh_materials;

/* Loaded level meshes shared by name; animated textures keep per-load state. */
static ResourceCache<stMeshModel> g_level_meshes;

static void CacheLevelMesh(const char* name, const std::shared_ptr<stMeshModel>& model)
{
    if (name == 0 || name[0] == '\0') {
        return;
    }
    if (MeshHasAnimatedTexture(model.get())) {
        g_level_meshes.erase(name);
    } else {
        g_level_meshes.insert(name, model);
    }
}

static bool ReadMeshFaceNeedsSplit(const W8ReadMeshFace& face,
                                   const std::vector<W8MeshMaterial>& materials)
{
    return (face.flags & 4) != 0 ||
           (static_cast<stMaterial*>(materials[face.material_index].material.get())->m_surface_flags &
            W8_MATERIAL_TWO_SIDED) != 0;
}

/* Reorder polygons so equal shader/texture keys are adjacent, then vertices so
   equal materials are adjacent and, within them, follow first use. */
// FUNCTION: WIZ8 0x004867F0
void OptimizeMeshOrder(srMeshModel* model)
{
    const auto polygon_count = static_cast<std::size_t>(model->polygon_count);
    const auto vertex_count = static_cast<std::size_t>(model->vertex_location_count);
    if (polygon_count == 0 || vertex_count == 0) {
        return;
    }

    std::vector<const srShader*> shader_tables;
    std::vector<const std::shared_ptr<srTextureIFace>*> texture_tables;
    std::vector<const std::shared_ptr<srMaterialIFace>*> material_tables;
    for (w8_long pass = 0; pass < model->pass_count; ++pass) {
        if (const srShader* shaders = model->getPolyShader(pass, 0)) {
            shader_tables.push_back(shaders);
        }
        for (w8_long layer = 0; layer < 2; ++layer) {
            if (const auto* textures = model->getPolyTexture(pass, layer, 0)) {
                texture_tables.push_back(textures);
            }
        }
        for (int side = 0; side < 2; ++side) {
            if (const auto* materials =
                    model->getVertexMaterial(pass, static_cast<srMeshModel::e_side>(side), 0)) {
                material_tables.push_back(materials);
            }
        }
    }

    std::vector<w8_ulong> polygons(polygon_count);
    std::iota(polygons.begin(), polygons.end(), 0);
    std::stable_sort(polygons.begin(), polygons.end(), [&](w8_ulong left, w8_ulong right) {
        for (const srShader* shaders : shader_tables) {
            if (shaders[left].value != shaders[right].value) {
                return shaders[left].value < shaders[right].value;
            }
        }
        for (const auto* textures : texture_tables) {
            if (textures[left] != textures[right]) {
                return std::less<>{}(textures[left].get(), textures[right].get());
            }
        }
        return false;
    });

    std::vector<w8_ulong> first_use(vertex_count, 0);
    w8_ulong used = 0;
    const srVector3i* polygon_vertices = model->getPolyVertex();
    for (w8_ulong polygon : polygons) {
        for (int vertex : {polygon_vertices[polygon].x, polygon_vertices[polygon].y,
                           polygon_vertices[polygon].z}) {
            if (first_use[vertex] == 0) {
                first_use[vertex] = ++used;
            }
        }
    }

    std::vector<w8_ulong> vertices(vertex_count);
    std::iota(vertices.begin(), vertices.end(), 0);
    std::stable_sort(vertices.begin(), vertices.end(), [&](w8_ulong left, w8_ulong right) {
        for (const auto* materials : material_tables) {
            if (materials[left] != materials[right]) {
                return std::less<>{}(materials[left].get(), materials[right].get());
            }
        }
        return first_use[left] < first_use[right];
    });

    model->reindexPolygons(polygons.data());
    model->reindexVertices(vertices.data());
}

// FUNCTION: WIZ8 0x00488650
std::shared_ptr<stMeshModel>
BuildSingleLevelMesh(int face_count, W8ReadMeshFace* faces, int vertex_count,
                     const std::vector<W8MeshMaterial>& materials, unsigned int* mesh_count, std::vector<std::vector<int>>& vertex_maps,
                     std::vector<short>* mapped_values, std::vector<short>* mapped_keys)
{
    std::vector<srShader> polygon_types;
    /* Each (material, source vertex) pair becomes one vertex per polygon type. */
    std::map<std::pair<const srMaterialIFace*, int>, int> vertex_indices[8];
    std::vector<int> duplicated_from[8];
    std::vector<int> duplicated_to[8];
    std::vector<short> mapped_meshes;
    std::vector<short> mapped_vertices;
    int capacities[8];
    int polygon_counts[8];
    int vertex_counts[8];
    int extra_uv_counts[8];
    std::array<std::vector<std::shared_ptr<srMaterialIFace>>, 8> vertex_materials;
    std::array<std::vector<std::shared_ptr<srTextureIFace>>, 8> polygon_textures;
    std::array<std::vector<glm::vec2>, 8> vertex_uvs;
    std::array<std::vector<glm::vec2>, 8> extra_uvs;
    std::array<std::vector<srVector3i>, 8> polygon_vertices;
    std::array<std::vector<srVector3i>, 8> polygon_shades;
    std::array<std::vector<w8_ulong>, 8> vertex_shades;
    std::shared_ptr<stMeshModel> first_model;
    int type;
    int face_index;

    for (int index = 0; index < static_cast<int>(mapped_values->size()); ++index) {
        mapped_meshes.push_back(-1);
        mapped_vertices.push_back(-1);
    }

    for (const W8MeshMaterial& material : materials) {
        if (wiz8::index_of(polygon_types, material.render_flags.value, &srShader::value) == -1) {
            int capacity = 0;
            for (face_index = 0; face_index < face_count; ++face_index) {
                W8ReadMeshFace& face = faces[face_index];
                if (materials[face.material_index].render_flags.value == material.render_flags.value) {
                    ++capacity;
                    if (ReadMeshFaceNeedsSplit(face, materials)) {
                        ++capacity;
                    }
                }
            }
            if (capacity != 0) {
                capacities[static_cast<int>(polygon_types.size())] = capacity;
                polygon_types.push_back(material.render_flags);
            }
        }
    }

    if (mesh_count != 0) {
        *mesh_count = static_cast<int>(polygon_types.size());
    }
    vertex_maps.resize(static_cast<int>(polygon_types.size()));

    for (type = 0; type < static_cast<int>(polygon_types.size()); ++type) {
        polygon_counts[type] = 0;
        vertex_counts[type] = 0;
        extra_uv_counts[type] = 0;
        int capacity = capacities[type];
        vertex_materials[type].resize(capacity * 3);
        polygon_textures[type].resize(capacity);
        vertex_uvs[type].resize(capacity * 3);
        extra_uvs[type].resize(capacity * 3);
        polygon_vertices[type].resize(capacity * 3);
        polygon_shades[type].resize(capacity);
        vertex_shades[type].resize(capacity * 3);
        vertex_maps[type].resize(capacity * 3);
    }

    for (face_index = 0; face_index < face_count; ++face_index) {
        W8ReadMeshFace& face = faces[face_index];
        const W8MeshMaterial& material = materials[face.material_index];
        type = wiz8::index_of(polygon_types, material.render_flags.value, &srShader::value);
        if (type < 0) {
            srAssertFail("iPolyType >=0", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp",
                         0x45b, 0);
        }
        srVector3i& poly = polygon_vertices[type][polygon_counts[type]];
        srVector3i& shade = polygon_shades[type][polygon_counts[type]];
        for (int corner = 0; corner < 3; ++corner) {
            int original_vertex = face.vertices[corner];
            auto [found, inserted] = vertex_indices[type].try_emplace(
                std::pair{material.material.get(), original_vertex}, vertex_counts[type]);
            int vertex = found->second;
            if (inserted) {
                vertex_materials[type][vertex] = material.material;
                vertex_uvs[type][vertex] = face.texture_coordinates[corner];
                (corner == 0 ? poly.x : (corner == 1 ? poly.y : poly.z)) = vertex;
                vertex_maps[type][vertex] = original_vertex;
                vertex_shades[type][vertex] = vertex;
                ++vertex_counts[type];
                for (int mapped = 0; mapped < static_cast<int>(mapped_values->size()); ++mapped) {
                    if ((*mapped_values)[mapped] == original_vertex) {
                        mapped_vertices[mapped] = static_cast<short>(vertex);
                        mapped_meshes[mapped] = static_cast<short>(type);
                        break;
                    }
                }
            } else if (vertex_uvs[type][vertex].x == face.texture_coordinates[corner].x &&
                       vertex_uvs[type][vertex].y == face.texture_coordinates[corner].y) {
                (corner == 0 ? poly.x : (corner == 1 ? poly.y : poly.z)) = vertex;
            } else {
                (corner == 0 ? poly.x : (corner == 1 ? poly.y : poly.z)) =
                    -1 - extra_uv_counts[type];
                extra_uvs[type][extra_uv_counts[type]] = face.texture_coordinates[corner];
                ++extra_uv_counts[type];
            }
            (corner == 0 ? shade.x : (corner == 1 ? shade.y : shade.z)) = vertex;
        }
        polygon_textures[type][polygon_counts[type]] = material.texture;
        ++polygon_counts[type];

        if (ReadMeshFaceNeedsSplit(face, materials)) {
            srVector3i& split_poly = polygon_vertices[type][polygon_counts[type]];
            srVector3i& split_shade = polygon_shades[type][polygon_counts[type]];
            for (int corner = 0; corner < 3; ++corner) {
                int source_vertex = corner == 0 ? shade.x : (corner == 1 ? shade.y : shade.z);
                int vertex = vertex_counts[type];
                vertex_materials[type][vertex] = vertex_materials[type][source_vertex];
                vertex_shades[type][vertex] = vertex;
                vertex_maps[type][vertex] = vertex_maps[type][source_vertex];
                vertex_uvs[type][vertex] = face.texture_coordinates[corner];
                duplicated_from[type].push_back(source_vertex);
                duplicated_to[type].push_back(vertex);
                (corner == 0 ? split_poly.x : (corner == 1 ? split_poly.y : split_poly.z)) = vertex;
                (corner == 0 ? split_shade.x : (corner == 1 ? split_shade.y : split_shade.z)) =
                    vertex;
                ++vertex_counts[type];
            }
            polygon_textures[type][polygon_counts[type]] =
                polygon_textures[type][polygon_counts[type] - 1];
            ++polygon_counts[type];
        }
    }

    for (type = 0; type < static_cast<int>(polygon_types.size()); ++type) {
        if (capacities[type] == 0) {
            continue;
        }
        auto owned_model = std::make_unique<stMeshModel>(polygon_counts[type], vertex_counts[type]);
        stMeshModel* model = owned_model.get();
        if (model == 0) {
            srAssertFail("pstMeshModel[iCount]",
                         "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x4e7, 0);
        }

        for (int mapped = 0; mapped < static_cast<int>(mapped_meshes.size()); ++mapped) {
            if (mapped_meshes[mapped] == type) {
                for (int entry = 0; entry < static_cast<int>(mapped_values->size()); ++entry) {
                    model->SetMappedVertex(mapped_vertices[entry], (*mapped_keys)[entry]);
                }
                break;
            }
        }

        std::shared_ptr<srTextureIFace>* model_textures = model->getPolyTexture(0, 0, 1);
        for (int polygon = 0; polygon < polygon_counts[type]; ++polygon) {
            model_textures[polygon] = polygon_textures[type][polygon];
        }

        bool one_material = true;
        for (int vertex = 0; vertex < vertex_counts[type] - 1; ++vertex) {
            if (vertex_materials[type][vertex].get() != vertex_materials[type][vertex + 1].get()) {
                one_material = false;
            }
        }
        if (one_material) {
            model->setMaterial(vertex_materials[type][0], 0, srMeshModel::SIDE_FRONT);
        } else {
            std::shared_ptr<srMaterialIFace>* model_materials =
                model->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1);
            for (int vertex = 0; vertex < vertex_counts[type]; ++vertex) {
                model_materials[vertex] = vertex_materials[type][vertex];
            }
        }

        srVector3i* model_polygons = model->getPolyVertex();
        model->setUVCount(vertex_counts[type] + extra_uv_counts[type]);
        glm::vec2* model_uvs = model->getVertexTexCoords(0, 0, 1);
        memcpy(model_uvs, vertex_uvs[type].data(), vertex_counts[type] * sizeof(glm::vec2));
        memcpy(model_uvs + vertex_counts[type], extra_uvs[type].data(),
               extra_uv_counts[type] * sizeof(glm::vec2));
        srVector3i* model_uv_indices = model->getPolyUVIndex(0, 1);
        for (int uv_polygon = 0; uv_polygon < polygon_counts[type]; ++uv_polygon) {
            for (int uv_corner = 0; uv_corner < 3; ++uv_corner) {
                int index = uv_corner == 0
                                ? polygon_vertices[type][uv_polygon].x
                                : (uv_corner == 1 ? polygon_vertices[type][uv_polygon].y
                                                  : polygon_vertices[type][uv_polygon].z);
                if (index < 0) {
                    index = vertex_counts[type] + (-1 - index);
                }
                (uv_corner == 0 ? model_uv_indices[uv_polygon].x
                                : (uv_corner == 1 ? model_uv_indices[uv_polygon].y
                                                  : model_uv_indices[uv_polygon].z)) = index;
            }
        }
        w8_ulong* model_shades = model->getVertexShadeIndex(1);
        memcpy(model_polygons, polygon_shades[type].data(), polygon_counts[type] * sizeof(srVector3i));
        memcpy(model_shades, vertex_shades[type].data(), vertex_counts[type] * sizeof(w8_ulong));

        srShader shader;
        shader = polygon_types[type];
        model->setShader(shader, 0);

        model->setDirtyAll();
        if ((polygon_types[type].value & 0x6000) == 0x4000) {
            model->flags |= W8_MESH_SORTED_RENDERING;
            model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
        } else {
            model->flags &= ~W8_MESH_SORTED_RENDERING;
        }
        if (first_model == 0) {
            first_model = std::move(owned_model);
        } else {
            first_model->AddPart(std::move(owned_model));
        }
    }
    first_model->setDirty(srMeshModel::DIRTY_BOUNDS);
    return first_model;
}

// FUNCTION: WIZ8 0x00487E10
static const std::vector<W8MeshMaterial>& ReadMeshMaterials(W8ReadLevelInfo* info)
{
    if (info == 0) {
        srAssertFail("pInfo", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x256, 0);
    }
    if (info->bitmap_folder == 0) {
        srAssertFail("pInfo->strBitmapDir", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp",
                     0x257, 0);
    }
    if (info->world == 0) {
        srAssertFail("pInfo->pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x258,
                     0);
    }
    if (info->hFile == 0) {
        srAssertFail("pInfo->hFile", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x259,
                     0);
    }

    short count = 0;
    short index;
    info->hFile->read_exact(&count, sizeof(count));
    if (count < 1) {
        ReleaseReadMeshScratch();
        return g_read_mesh_materials;
    }

    std::vector<W8MaterialRecord> records(count);
    (info->hFile->read(records.data(), 0x11a).bytes == static_cast<std::size_t>(0x11a));
    if (records[0].version < 4) {
        for (index = 1; index < count; ++index) {
            (info->hFile->read(records.data() + index, 0x11a).bytes == static_cast<std::size_t>(0x11a));
        }
    } else {
        (info->hFile->read(records[0].texture_modes, sizeof(records[0].texture_modes)).bytes == static_cast<std::size_t>(sizeof(records[0].texture_modes)));
        if (count > 1) {
            (info->hFile->read(records.data() + 1, (count - 1) * sizeof(W8MaterialRecord)).bytes == static_cast<std::size_t>((count - 1) * sizeof(W8MaterialRecord)));
        }
    }

    for (index = 0; index < count; ++index) {
        ClearMaterialRecordPadding(records.data() + index);
    }

    if (records.size() == g_read_mesh_material_records.size() &&
        memcmp(records.data(), g_read_mesh_material_records.data(),
               count * sizeof(W8MaterialRecord)) == 0) {
        return g_read_mesh_materials;
    }

    ReleaseReadMeshScratch();
    g_read_mesh_material_records = std::move(records);
    g_read_mesh_materials.reserve(count);
    g_read_mesh_materials.push_back(CreateDefaultMaterial());
    for (index = 1; index < count; ++index) {
        g_read_mesh_materials.push_back(
            LoadMaterial(info->bitmap_folder, g_read_mesh_material_records[index]));
    }
    return g_read_mesh_materials;
}

// FUNCTION: WIZ8 0x00485B20
unsigned char ReadSingleLevelMesh(W8ReadLevelInfo* info, srModelInstance** instance,
                                  int unused_first, int unused_second, const char* name,
                                  bool load_materials)
{
    if (name != 0) {
        if (std::shared_ptr<stMeshModel> model = g_level_meshes.find(name)) {
            stModelInstance* duplicate = CreateModelInstance(std::move(model));
            duplicate->setName("Read Mesh Duplicate Instance");
            *instance = duplicate;
            SkipSingleLevelMesh(info);
            return 1;
        }
    }

    return ReadSingleLevelMeshBody(info, instance, unused_first, unused_second, name,
                                   load_materials);
}

// FUNCTION: WIZ8 0x00485C10
unsigned char ReadSingleLevelMeshBody(W8ReadLevelInfo* info, srModelInstance** instance, int, int,
                                      const char* name, bool load_materials)
try
{
    /* Retail read mapping_count/value/key/compression_type uninitialised when
       a FileRead short-circuited; the recovery keeps that read. */
    std::vector<short> mapped_values;
    std::vector<short> mapped_keys;
    int version = 0;
    int vertex_count = 0;
    int face_count = 0;
    unsigned char flags = 0;
    unsigned int bytes_read;
    std::vector<srVector3T<float>> vertices;
    std::vector<std::vector<short>> compressed_vertices;
    short frame_count = 0;
    float compression_scale = 1000.0f;
    std::vector<W8ReadMeshFace> faces;
    std::vector<std::vector<int>> vertex_maps;
    unsigned int mesh_count = 0;
    std::shared_ptr<stMeshModel> first_model;

    if (info == 0) {
        srAssertFail("pInfo", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0xd7, 0);
    }
    wiz8::File* file = info->hFile;
    if (file == 0) {
        srAssertFail("hFile", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0xd9, 0);
    }

    file->read_exact(&version, sizeof(version));
    file->read_exact(&vertex_count, sizeof(vertex_count));
    unsigned char success = (file->read(&face_count, sizeof(face_count)).bytes == static_cast<std::size_t>(sizeof(face_count)));
    if (vertex_count < 1 || face_count < 1) {
        return 0;
    }

    srVector3T<float> location;
    srVector3T<float> scale;
    glm::mat3 rotation;
    if (version > 2) {
        success = (file->read(&flags, sizeof(flags)).bytes == static_cast<std::size_t>(sizeof(flags)));
    }
    if (version > 1) {
        ReadMeshTransform(file, &location, &rotation, &scale);
    }
    if (success == 0) {
        return 0;
    }

    if (version > 3) {
        signed char mapping_count;
        success = (file->read(&mapping_count, sizeof(mapping_count)).bytes == static_cast<std::size_t>(sizeof(mapping_count)));
        for (short index = 0; index < mapping_count; ++index) {
            short value = 0;
            short key = 0;
            /* Retail 0x00485e06/0x00485e1b: the mapping key (the vertex
               marker id GetCycleMappedPosition asks for) comes first in the
               file, then the original vertex index. */
            if (success == 0 || !(file->read(&key, sizeof(key)).bytes == static_cast<std::size_t>(sizeof(key))) ||
                !(file->read(&value, sizeof(value)).bytes == static_cast<std::size_t>(sizeof(value)))) {
                success = 0;
            }
            mapped_values.push_back(value);
            mapped_keys.push_back(key);
        }
    }
    if (success == 0) {
        return 0;
    }

    if ((flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        vertices.resize(vertex_count);
        success = ((bytes_read = file->read(vertices.data(), vertex_count * sizeof(srVector3T<float>)).bytes) == static_cast<std::size_t>(vertex_count * sizeof(srVector3T<float>)));
        if (success == 0) {
            srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x13b,
                         0);
        }
        for (int index = 0; index < vertex_count; ++index) {
            vertices[index] *= 500.0f;
        }
    } else {
        unsigned char compression_type;
        file->read_exact(&compression_type, sizeof(compression_type));
        file->read_exact(&frame_count, sizeof(frame_count));
        if (compression_type == 2) {
            file->read_exact(&compression_scale, sizeof(compression_scale));
        }
        if ((flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) == 0) {
            srAssertFail("FALSE", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x131,
                         "Uncompressed mesh, please re-export level with newer plugin");
        } else {
            compressed_vertices.resize(frame_count);
            for (short frame = 0; frame < frame_count; ++frame) {
                compressed_vertices[frame].resize(vertex_count * 3);
                success = ((bytes_read = file->read(compressed_vertices[frame].data(), vertex_count * 3 * sizeof(short)).bytes) == static_cast<std::size_t>(vertex_count * 3 * sizeof(short)));
                if (success == 0) {
                    srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp",
                                 0x118, 0);
                }
            }
        }
    }

    faces.resize(face_count);
    if ((flags & W8_LEVEL_MESH_COMPRESSED_FACES) == 0) {
        success = ((bytes_read = file->read(faces.data(), face_count * sizeof(W8ReadMeshFace)).bytes) == static_cast<std::size_t>(face_count * sizeof(W8ReadMeshFace)));
        if (success == 0) {
            srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x166,
                         0);
        }
    } else {
        std::vector<W8LevelFileCompressedFace> compressed_faces(face_count);
        success =
            ((bytes_read = file->read(compressed_faces.data(), face_count * sizeof(W8LevelFileCompressedFace)).bytes) == static_cast<std::size_t>(face_count * sizeof(W8LevelFileCompressedFace)));
        if (success == 0) {
            srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x152,
                         0);
        }
        for (int index = 0; index < face_count; ++index) {
            for (int vertex = 0; vertex < 3; ++vertex) {
                faces[index].vertices[vertex] = compressed_faces[index].vertex_indices[vertex];
                faces[index].texture_coordinates[vertex] =
                    compressed_faces[index].texture_coordinates[vertex];
            }
            faces[index].material_index = compressed_faces[index].material_index;
            faces[index].flags = compressed_faces[index].flags;
        }
    }

    const std::vector<W8MeshMaterial>& materials = ReadMeshMaterials(info);
    if (materials.empty()) {
        srAssertFail("uiMatCount", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x16c, 0);
    }
    for (const W8ReadMeshFace& face : faces) {
        if (face.material_index < 0 || face.material_index >= static_cast<int>(materials.size())) {
            throw std::runtime_error("Invalid mesh face material index");
        }
    }

    first_model = BuildSingleLevelMesh(face_count, faces.data(), vertex_count, materials,
                                       &mesh_count, vertex_maps, &mapped_values, &mapped_keys);
    if (first_model != 0) {
        first_model->setName(name ? name : "");
        stModelInstance* loaded_instance = CreateModelInstance(first_model);
        loaded_instance->setName("ReadSTMeshFromFile");
        if (version > 1 && loaded_instance != 0) {
            const double angle = 3.1415926;
            rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(1, 0, 0)));

            srVector3T<double> translated(location.x * 500.0, location.y * 500.0,
                                          location.z * 500.0);
            loaded_instance->setLocation(translated);
            loaded_instance->setRotation(rotation);
        }
        *instance = loaded_instance;
        CacheLevelMesh(name, first_model);
    }

    if ((flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        int mesh_index = 0;
        for (stMeshModel* model : stMeshModel::Meshes(first_model.get())) {
            srVector3T<float>* model_vertices = model->getVertexLoc();
            for (int index = 0; index < model->vertex_location_count; ++index) {
                model_vertices[index] = vertices[vertex_maps[mesh_index][index]];
            }
            OptimizeMeshOrder(model);
            ++mesh_index;
        }
    } else {
        for (short frame = 0; frame < frame_count; ++frame) {
            int mesh_index = 0;
            for (stMeshModel* model : stMeshModel::Meshes(first_model.get())) {
                model->InitializeVertexFrames(frame_count);
                model->vertex_compression_scale = 500.0f / compression_scale;
                short* model_vertices = model->GetVertex(frame);
                for (int index = 0; index < model->vertex_location_count; ++index) {
                    int source = vertex_maps[mesh_index][index];
                    model_vertices[index * 3] = compressed_vertices[frame][source * 3];
                    model_vertices[index * 3 + 1] = compressed_vertices[frame][source * 3 + 1];
                    model_vertices[index * 3 + 2] = compressed_vertices[frame][source * 3 + 2];
                }
                model->FinalizeVertexFrame(frame);
                ++mesh_index;
            }
        }
    }

    if (first_model != 0) {
        glm::vec3 minimum;
        glm::vec3 maximum;
        first_model->getBoundingBox(minimum, maximum);
    }
    return 1;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x00488240
unsigned char ReadMultipleLevelMeshes(W8ReadLevelInfo* info, srModelInstance** instances,
                                      w8_ulong count, const char* name)
try
{
    OctMeshModel reader;
    unsigned int mesh_count;
    unsigned int root_count;
    int terminator;

    if (info == 0) {
        srAssertFail("pInfo", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x31e, 0);
    }
    if (count == 0) {
        srAssertFail("uiNumMeshes", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x31f,
                     0);
    }
    if (info->hFile == 0) {
        srAssertFail("hFile", "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x323, 0);
    }

    info->hFile->read_exact(&mesh_count, sizeof(mesh_count));
    info->hFile->read_exact(&root_count, sizeof(root_count));
    if (mesh_count != count || root_count > mesh_count) {
        throw std::runtime_error("Invalid mesh or root count");
    }

    const std::vector<W8MeshMaterial>& materials = ReadMeshMaterials(info);
    if (materials.empty()) return false;

    info->hFile->read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) {
        srAssertFail("(uiTerminator == 0xffffffff)",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x32c,
                     "NewReadMesh: Material list length is incorrect.");
    }


    std::vector<stMeshModel*> meshes(count);
    std::vector<std::shared_ptr<stMeshModel>> roots(count);
    glm::vec3 minimum;
    glm::vec3 maximum;
    for (unsigned int mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
        if (g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT) {
            UpdatePleaseWaitLoadFrame();
        }
        auto owned_model = reader.Read(info->hFile, materials);
        stMeshModel* model = owned_model.get();
        /* A linked mesh continues the model whose last mesh is its predecessor. */
        if (reader.m_link_index >= 0) {
            if (static_cast<unsigned int>(reader.m_link_index) >= mesh_index) {
                throw std::runtime_error("Invalid linked mesh predecessor");
            }
            stMeshModel* predecessor = meshes[reader.m_link_index];
            stMeshModel* head = predecessor->head ? predecessor->head : predecessor;
            if (predecessor != (head->parts.empty() ? head : head->parts.back().get())) {
                throw std::runtime_error("Duplicate linked mesh successor");
            }
            head->AddPart(std::move(owned_model));
        } else {
            roots[mesh_index] = std::move(owned_model);
        }
        meshes[mesh_index] = model;
        model->setDirty(srMeshModel::DIRTY_BOUNDS);
        model->getBoundingBox(minimum, maximum);
    }

    for (unsigned int mesh_index = 0; mesh_index < root_count; ++mesh_index) {
        stMeshModel* model = meshes[mesh_index];
        model->setName(name ? name : "");
        if (model->head == nullptr) {
            stModelInstance* instance = CreateModelInstance(roots[mesh_index]);
            instance->setName("Multi Mesh Instance");
            instance->mesh_index = mesh_index;
            CacheLevelMesh(name, roots[mesh_index]);
            instances[mesh_index] = instance;
        }
    }

    info->hFile->read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) {
        srAssertFail("(uiTerminator == 0xffffffff)",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\ReadMesh.cpp", 0x35b,
                     "NewReadMesh: Incorrect offset in file at end of mesh.");
    }

    return 1;
} catch (const std::exception&) {
    return false;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00489980
void ClearMaterialRecordPadding(W8MaterialRecord* material)
{
    if (material == 0) {
        return;
    }
    /* Retail expands the four tail clears straight-line; VC6 does not unroll
       a counted loop, so these are four authored statements. */
    memset(material->texture_name, 0, sizeof(material->texture_name));
    memset(material->texture_names[0] + strlen(material->texture_names[0]), 0,
           sizeof(material->texture_names[0]) - strlen(material->texture_names[0]));
    memset(material->texture_names[1] + strlen(material->texture_names[1]), 0,
           sizeof(material->texture_names[1]) - strlen(material->texture_names[1]));
    memset(material->texture_names[2] + strlen(material->texture_names[2]), 0,
           sizeof(material->texture_names[2]) - strlen(material->texture_names[2]));
    memset(material->texture_names[3] + strlen(material->texture_names[3]), 0,
           sizeof(material->texture_names[3]) - strlen(material->texture_names[3]));
}

// FUNCTION: WIZ8 0x004881d0
void ReleaseReadMeshScratch()
{
    g_read_mesh_materials.clear();
    g_read_mesh_material_records.clear();
}

// FUNCTION: WIZ8 0x00487bd0
unsigned char SkipSingleLevelMesh(W8ReadLevelInfo* info)
try
{
    /* Retail read count/group_count uninitialised when a FileRead
       short-circuited; natively count starts at zero. */
    int version;
    int vertex_count;
    int face_count;
    unsigned char flags = 0;
    unsigned char count = 0;
    short item_count;
    short index;
    unsigned char success = 1;

    if (info == 0 || info->world == 0 || info->hFile == 0) {
        return 0;
    }
    if (!(info->hFile->read(&version, 4).bytes == static_cast<std::size_t>(4)) || !(info->hFile->read(&vertex_count, 4).bytes == static_cast<std::size_t>(4)) ||
        !(info->hFile->read(&face_count, 4).bytes == static_cast<std::size_t>(4))) {
        return 0;
    }
    if (vertex_count < 1 || face_count < 1) {
        return 0;
    }
    if (version > 2) {
        success = (info->hFile->read(&flags, 1).bytes == static_cast<std::size_t>(1));
    }
    if (version > 1) {
        (info->hFile->seek(0x28, wiz8::SeekOrigin::current), true);
    }
    if (version > 3) {
        if (success == 0 || !(info->hFile->read(&count, 1).bytes == static_cast<std::size_t>(1))) {
            success = 0;
        }
        if (count != 0) {
            (info->hFile->seek(static_cast<int>(static_cast<signed char>(count)) * 4, wiz8::SeekOrigin::current), true);
        }
    }
    if ((flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        vertex_count *= 0xc;
    } else {
        unsigned char ignored;
        short group_count;

        (info->hFile->read(&ignored, 1).bytes == static_cast<std::size_t>(1));
        (info->hFile->read(&group_count, 2).bytes == static_cast<std::size_t>(2));
        if ((flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) == 0) {
            vertex_count = group_count * vertex_count * 0xc;
        } else {
            vertex_count = group_count * vertex_count * 6;
        }
    }
    (info->hFile->seek(vertex_count, wiz8::SeekOrigin::current), true);
    if ((flags & W8_LEVEL_MESH_COMPRESSED_FACES) == 0) {
        face_count *= 0x29;
    } else {
        face_count *= 0x21;
    }
    (info->hFile->seek(face_count, wiz8::SeekOrigin::current), true);
    if ((info->hFile->read(&item_count, 2).bytes == static_cast<std::size_t>(2)) && item_count > 0) {
        for (index = 0; index < item_count; ++index) {
            (info->hFile->read(&count, 1).bytes == static_cast<std::size_t>(1));
            (info->hFile->seek(0x119, wiz8::SeekOrigin::current), true);
            if (count > 3) {
                (info->hFile->seek(0x10, wiz8::SeekOrigin::current), true);
            }
        }
    }
    if ((flags & W8_LEVEL_MESH_LOD_VERTICES) != 0) {
        success = 2;
    }
    return success;
} catch (const std::exception&) {
    return false;
}
