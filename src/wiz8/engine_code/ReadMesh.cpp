#include "wiz8/engine_code/ReadMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/fwd.hpp"
#include "resource_cache.h"
#include "surrender/mesh_resources.h"
#include "surrender/srDebug.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srShader.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"

class srMaterial;
class srTexture;

/* Loaded level meshes shared by name; animated textures keep per-load state. */
static ResourceCache<stMeshModel> g_level_meshes;

static std::string MeshResourceKey(const W8ReadLevelInfo& info, const char* name)
{
    if (!name || !*name) return {};
    return std::string(info.mesh_filename ? info.mesh_filename : "") + '\0' + info.bitmap_folder +
           '\0' + name;
}

static void CacheLevelMesh(std::string_view name, const std::shared_ptr<stMeshModel>& model)
{
    if (name.empty()) {
        return;
    }
    if (MeshHasAnimatedTexture(model.get())) {
        g_level_meshes.erase(name);
    } else {
        g_level_meshes.insert(std::string(name), model);
    }
}

static bool ReadMeshFaceNeedsSplit(const W8ReadMeshFace& face,
                                   const std::vector<W8MeshMaterial>& materials)
{
    return (face.flags & 4) != 0 ||
           (static_cast<stMaterial*>(materials[face.material_index].material.get())
                ->m_surface_flags &
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
    std::vector<const MeshResources<srTexture>*> texture_tables;
    std::vector<const MeshResources<srMaterial>*> material_tables;
    for (w8_long pass = 0; pass < model->pass_count; ++pass) {
        if (const srShader* shaders = model->getPolyShader(pass, 0)) {
            shader_tables.push_back(shaders);
        }
        for (w8_long layer = 0; layer < 2; ++layer) {
            if (const auto* textures = model->getPolygonTextureTable(pass, layer, false)) {
                texture_tables.push_back(textures);
            }
        }
        for (int side = 0; side < 2; ++side) {
            if (const auto* materials = model->getVertexMaterialTable(
                    pass, static_cast<srMeshModel::e_side>(side), false)) {
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
            if ((*textures)[left] != (*textures)[right]) {
                return std::less<>{}((*textures)[left].get(), (*textures)[right].get());
            }
        }
        return false;
    });

    std::vector<w8_ulong> first_use(vertex_count, 0);
    w8_ulong used = 0;
    const glm::ivec3* polygon_vertices = model->getPolyVertex();
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
            if ((*materials)[left] != (*materials)[right]) {
                return std::less<>{}((*materials)[left].get(), (*materials)[right].get());
            }
        }
        return first_use[left] < first_use[right];
    });

    model->reindexPolygons(polygons);
    model->reindexVertices(vertices);
}

// FUNCTION: WIZ8 0x00488650
std::shared_ptr<stMeshModel>
BuildSingleLevelMesh(int face_count, W8ReadMeshFace* faces, int vertex_count,
                     const std::vector<W8MeshMaterial>& materials, unsigned int* mesh_count,
                     std::vector<std::vector<int>>& vertex_maps, std::vector<short>* mapped_values,
                     std::vector<short>* mapped_keys)
{
    std::vector<srShader> polygon_types;
    /* Each (material, source vertex) pair becomes one vertex per polygon type. */
    std::map<std::pair<const srMaterial*, int>, int> vertex_indices[8];
    std::vector<int> duplicated_from[8];
    std::vector<int> duplicated_to[8];
    std::vector<short> mapped_meshes;
    std::vector<short> mapped_vertices;
    int capacities[8];
    int polygon_counts[8];
    int vertex_counts[8];
    int extra_uv_counts[8];
    std::array<std::vector<std::shared_ptr<srMaterial>>, 8> vertex_materials;
    std::array<std::vector<std::shared_ptr<srTexture>>, 8> polygon_textures;
    std::array<std::vector<glm::vec2>, 8> vertex_uvs;
    std::array<std::vector<glm::vec2>, 8> extra_uvs;
    std::array<std::vector<glm::ivec3>, 8> polygon_vertices;
    std::array<std::vector<glm::ivec3>, 8> polygon_shades;
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
                if (materials[face.material_index].render_flags.value ==
                    material.render_flags.value) {
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
        glm::ivec3& poly = polygon_vertices[type][polygon_counts[type]];
        glm::ivec3& shade = polygon_shades[type][polygon_counts[type]];
        for (int corner = 0; corner < 3; ++corner) {
            int original_vertex = face.vertices[corner];
            auto [found, inserted] = vertex_indices[type].try_emplace(
                std::pair{material.material.get(), original_vertex}, vertex_counts[type]);
            int vertex = found->second;
            if (inserted) {
                vertex_materials[type][vertex] = material.material;
                vertex_uvs[type][vertex] = face.texture_coordinates[corner];
                poly[corner] = vertex;
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
                poly[corner] = vertex;
            } else {
                poly[corner] = -1 - extra_uv_counts[type];
                extra_uvs[type][extra_uv_counts[type]] = face.texture_coordinates[corner];
                ++extra_uv_counts[type];
            }
            shade[corner] = vertex;
        }
        polygon_textures[type][polygon_counts[type]] = material.texture;
        ++polygon_counts[type];

        if (ReadMeshFaceNeedsSplit(face, materials)) {
            glm::ivec3& split_poly = polygon_vertices[type][polygon_counts[type]];
            glm::ivec3& split_shade = polygon_shades[type][polygon_counts[type]];
            for (int corner = 0; corner < 3; ++corner) {
                int source_vertex = shade[corner];
                int vertex = vertex_counts[type];
                vertex_materials[type][vertex] = vertex_materials[type][source_vertex];
                vertex_shades[type][vertex] = vertex;
                vertex_maps[type][vertex] = vertex_maps[type][source_vertex];
                vertex_uvs[type][vertex] = face.texture_coordinates[corner];
                duplicated_from[type].push_back(source_vertex);
                duplicated_to[type].push_back(vertex);
                split_poly[corner] = vertex;
                split_shade[corner] = vertex;
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

        for (short mapped_mesh : mapped_meshes) {
            if (mapped_mesh == type) {
                for (int entry = 0; entry < static_cast<int>(mapped_values->size()); ++entry) {
                    model->SetMappedVertex(mapped_vertices[entry], (*mapped_keys)[entry]);
                }
                break;
            }
        }

        MeshResources<srTexture>* model_textures = model->getPolygonTextureTable(0, 0, true);
        for (int polygon = 0; polygon < polygon_counts[type]; ++polygon) {
            model_textures->set(polygon, polygon_textures[type][polygon]);
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
            MeshResources<srMaterial>* model_materials =
                model->getVertexMaterialTable(0, srMeshModel::SIDE_FRONT, true);
            for (int vertex = 0; vertex < vertex_counts[type]; ++vertex) {
                model_materials->set(vertex, vertex_materials[type][vertex]);
            }
        }

        glm::ivec3* model_polygons = model->getPolyVertex();
        model->setUVCount(vertex_counts[type] + extra_uv_counts[type]);
        glm::vec2* model_uvs = model->getVertexTexCoords(0, 0, 1);
        memcpy(model_uvs, vertex_uvs[type].data(), vertex_counts[type] * sizeof(glm::vec2));
        memcpy(model_uvs + vertex_counts[type], extra_uvs[type].data(),
               extra_uv_counts[type] * sizeof(glm::vec2));
        glm::ivec3* model_uv_indices = model->getPolyUVIndex(0, 1);
        for (int uv_polygon = 0; uv_polygon < polygon_counts[type]; ++uv_polygon) {
            for (int uv_corner = 0; uv_corner < 3; ++uv_corner) {
                int index = polygon_vertices[type][uv_polygon][uv_corner];
                if (index < 0) {
                    index = vertex_counts[type] + (-1 - index);
                }
                model_uv_indices[uv_polygon][uv_corner] = index;
            }
        }
        w8_ulong* model_shades = model->getVertexShadeIndex(1);
        memcpy(model_polygons, polygon_shades[type].data(),
               polygon_counts[type] * sizeof(glm::ivec3));
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

static void ValidateMeshMaterialNames(const W8MaterialRecord& material);

// FUNCTION: WIZ8 0x00487E10
static std::vector<W8MaterialRecord> ReadMeshMaterialRecords(wiz8::File& file)
{
    short count = 0;
    short index;
    file.read_exact(&count, sizeof(count));
    if (count < 1) {
        return {};
    }

    if (static_cast<std::int64_t>(count) * 0x11a > file.size() - file.tell()) {
        throw std::runtime_error("ReadMeshMaterials: Invalid material count.");
    }
    std::vector<W8MaterialRecord> records(count);
    file.read_exact(records.data(), 0x11a);
    if (records[0].version < 4) {
        for (index = 1; index < count; ++index) {
            file.read_exact(records.data() + index, 0x11a);
        }
    } else {
        file.read_exact(records[0].texture_modes, sizeof(records[0].texture_modes));
        file.read_exact(records.data() + 1,
                        static_cast<std::size_t>(count - 1) * sizeof(W8MaterialRecord));
    }

    for (index = 0; index < count; ++index) {
        ValidateMeshMaterialNames(records[index]);
    }

    return records;
}

static std::vector<W8MeshMaterial> LoadMeshMaterials(W8ReadLevelInfo* info,
                                                     const std::vector<W8MaterialRecord>& records)
{
    if (!info->bitmap_folder) throw std::invalid_argument("missing mesh texture directory");
    const auto count = records.size();
    if (count == 0) return {};
    std::vector<W8MeshMaterial> staged;
    staged.reserve(count);
    staged.push_back(CreateDefaultMaterial());
    for (std::size_t index = 1; index < count; ++index) {
        staged.push_back(LoadMaterial(info->bitmap_folder, records[index]));
    }
    return staged;
}

namespace {
struct SingleLevelMeshRecord {
    std::vector<short> mapped_values;
    std::vector<short> mapped_keys;
    int version = 0;
    int vertex_count = 0;
    int face_count = 0;
    unsigned char flags = 0;
    std::vector<glm::vec3> vertices;
    std::vector<std::vector<short>> compressed_vertices;
    short frame_count = 0;
    float compression_scale = 1000.0f;
    std::vector<W8ReadMeshFace> faces;
    glm::vec3 location{0};
    glm::mat3 rotation{1};
    std::vector<W8MaterialRecord> materials;
};

SingleLevelMeshRecord ReadSingleLevelMeshRecord(wiz8::File& input)
{
    SingleLevelMeshRecord record;
    auto& [mapped_values, mapped_keys, version, vertex_count, face_count, flags, vertices,
           compressed_vertices, frame_count, compression_scale, faces, location, rotation,
           materials] = record;
    auto* file = &input;

    file->read_exact(&version, sizeof(version));
    file->read_exact(&vertex_count, sizeof(vertex_count));
    file->read_exact(&face_count, sizeof(face_count));
    if (vertex_count < 1 || vertex_count > 100000 || face_count < 1 || face_count > 200000) {
        throw std::runtime_error("invalid mesh record");
    }

    if (version > 2) {
        file->read_exact(&flags, sizeof(flags));
    }
    if (version > 1) {
        // Ten disk floats: location, angle/axis, then the authored scale.
        // Retail consumes the scale but does not apply it to this instance.
        std::array<float, 10> transform;
        file->read_exact(transform.data(), transform.size() * sizeof(float));
        location = {transform[0], transform[1], transform[2]};
        const glm::vec3 axis(transform[4], transform[5], transform[6]);
        rotation = glm::mat3_cast(glm::angleAxis(transform[3], normalizedOrZero(axis)));
    }

    if (version > 3) {
        signed char mapping_count;
        file->read_exact(&mapping_count, sizeof(mapping_count));
        if (mapping_count < 0) throw std::runtime_error("invalid mesh record");
        for (short index = 0; index < mapping_count; ++index) {
            short value = 0;
            short key = 0;
            /* Retail 0x00485e06/0x00485e1b: the mapping key (the vertex
               marker id GetCycleMappedPosition asks for) comes first in the
               file, then the original vertex index. */
            file->read_exact(&key, sizeof(key));
            file->read_exact(&value, sizeof(value));
            if (value < 0 || value >= vertex_count) {
                throw std::runtime_error("invalid mesh record");
            }
            mapped_values.push_back(value);
            mapped_keys.push_back(key);
        }
    }

    if ((flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
        vertices.resize(vertex_count);
        std::vector<float> coordinates(static_cast<std::size_t>(vertex_count) * 3);
        file->read_exact(coordinates.data(), coordinates.size() * sizeof(float));
        for (int index = 0; index < vertex_count; ++index)
            vertices[index] = glm::vec3(coordinates[index * 3], coordinates[index * 3 + 1],
                                        coordinates[index * 3 + 2]) *
                              500.0f;
    } else {
        unsigned char compression_type;
        file->read_exact(&compression_type, sizeof(compression_type));
        file->read_exact(&frame_count, sizeof(frame_count));
        if (compression_type == 2) {
            file->read_exact(&compression_scale, sizeof(compression_scale));
        }
        const bool shorts = (flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) != 0;
        const std::uint64_t bytes_per_vertex = shorts ? 6 : 12;
        if (frame_count <= 0 || !std::isfinite(compression_scale) || compression_scale <= 0 ||
            static_cast<std::uint64_t>(frame_count) * vertex_count * bytes_per_vertex >
                static_cast<std::uint64_t>(file->size() - file->tell()))
            throw std::runtime_error("invalid mesh vertex frames");
        if (!shorts) {
            // Historical float frames can be inspected by the animation reader;
            // runtime mesh construction still requires the short-frame format.
            std::vector<float> legacy_frames(static_cast<std::size_t>(frame_count) * vertex_count *
                                             3);
            file->read_exact(legacy_frames.data(), legacy_frames.size() * sizeof(float));
        } else {
            compressed_vertices.resize(frame_count);
            for (auto& frame : compressed_vertices) {
                frame.resize(static_cast<std::size_t>(vertex_count) * 3);
                file->read_exact(frame.data(), frame.size() * sizeof(short));
            }
        }
    }

    faces.resize(face_count);
    if ((flags & W8_LEVEL_MESH_COMPRESSED_FACES) == 0) {
        std::vector<W8SerializedMeshFace> records(face_count);
        file->read_exact(records.data(), records.size() * sizeof(W8SerializedMeshFace));
        for (int index = 0; index < face_count; ++index)
            faces[index] = DecodeMeshFace(records[index]);
    } else {
        std::vector<W8LevelFileCompressedFace> compressed_faces(face_count);
        file->read_exact(compressed_faces.data(),
                         compressed_faces.size() * sizeof(W8LevelFileCompressedFace));
        for (int index = 0; index < face_count; ++index) {
            for (int vertex = 0; vertex < 3; ++vertex) {
                faces[index].vertices[vertex] = compressed_faces[index].vertex_indices[vertex];
                faces[index].texture_coordinates[vertex] = {
                    compressed_faces[index].texture_coordinates[vertex][0],
                    compressed_faces[index].texture_coordinates[vertex][1]};
            }
            faces[index].material_index = compressed_faces[index].material_index;
            faces[index].flags = compressed_faces[index].flags;
        }
    }

    materials = ReadMeshMaterialRecords(input);
    if (materials.empty()) throw std::runtime_error("mesh has no materials");
    for (const auto& face : faces) {
        if (face.material_index < 0 ||
            static_cast<std::size_t>(face.material_index) >= materials.size())
            throw std::runtime_error("invalid mesh material index");
        for (const int vertex : face.vertices)
            if (vertex < 0 || vertex >= vertex_count)
                throw std::runtime_error("invalid mesh vertex index");
    }
    return record;
}
} // namespace

bool ReadSingleLevelMeshHasVertexFrames(W8ReadLevelInfo& info)
{
    return (ReadSingleLevelMeshRecord(*info.hFile).flags & W8_LEVEL_MESH_LOD_VERTICES) != 0;
}

std::unique_ptr<stModelInstance> ReadSingleLevelMesh(W8ReadLevelInfo* info, const char* name,
                                                     bool load_materials)
try {
    if (!info || !info->hFile || !info->bitmap_folder) return {};
    auto record = ReadSingleLevelMeshRecord(*info->hFile);
    auto& [mapped_values, mapped_keys, version, vertex_count, face_count, flags, vertices,
           compressed_vertices, frame_count, compression_scale, faces, location, rotation,
           material_records] = record;
    const auto resource_key = MeshResourceKey(*info, name);
    auto first_model = resource_key.empty() ? nullptr : g_level_meshes.find(resource_key);
    if (!first_model) {
        if ((flags & W8_LEVEL_MESH_LOD_VERTICES) && !(flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES))
            throw std::runtime_error("runtime mesh requires short vertex frames");
        const auto materials = LoadMeshMaterials(info, material_records);
        unsigned int mesh_count = 0;
        std::vector<std::vector<int>> vertex_maps;
        std::vector<w8_ulong> shaders;
        for (const auto& material : materials) {
            if (!std::ranges::contains(shaders, material.render_flags.value)) {
                shaders.push_back(material.render_flags.value);
            }
        }
        if (shaders.size() > 8) {
            return {};
        }
        first_model = BuildSingleLevelMesh(face_count, faces.data(), vertex_count, materials,
                                           &mesh_count, vertex_maps, &mapped_values, &mapped_keys);
        if ((flags & W8_LEVEL_MESH_LOD_VERTICES) == 0) {
            int mesh_index = 0;
            for (stMeshModel* model : stMeshModel::Meshes(first_model.get())) {
                glm::vec3* model_vertices = model->getVertexLoc();
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

        first_model->getBounds();
        CacheLevelMesh(resource_key, first_model);
    }
    if (first_model != 0) {
        first_model->setName(name ? name : "");
        auto loaded_instance = CreateModelInstance(first_model);
        loaded_instance->setName("ReadSTMeshFromFile");
        if (version > 1 && loaded_instance != 0) {
            const double angle = 3.1415926;
            rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(1, 0, 0)));

            glm::dvec3 translated(location.x * 500.0, location.y * 500.0, location.z * 500.0);
            loaded_instance->setLocation(translated);
            loaded_instance->setRotation(rotation);
        }
        return loaded_instance;
    }

    return {};
} catch (const std::exception&) {
    return {};
}

// FUNCTION: WIZ8 0x00488240
std::vector<std::unique_ptr<stModelInstance>>
ReadMultipleLevelMeshes(W8ReadLevelInfo* info, std::size_t expected_count, const char* name)
try {
    OctMeshModel reader;
    unsigned int mesh_count;
    unsigned int root_count;
    int terminator;

    if (info == 0) {
        return {};
    }
    if (expected_count == 0) {
        return {};
    }
    if (info->hFile == 0) {
        return {};
    }

    info->hFile->read_exact(&mesh_count, sizeof(mesh_count));
    info->hFile->read_exact(&root_count, sizeof(root_count));
    if (mesh_count != expected_count || root_count > mesh_count) {
        throw std::runtime_error("Invalid mesh or root count");
    }

    const auto materials = LoadMeshMaterials(info, ReadMeshMaterialRecords(*info->hFile));
    if (materials.empty()) return {};

    info->hFile->read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) return {};

    if (mesh_count > static_cast<std::uint64_t>(info->hFile->size() - info->hFile->tell())) {
        return {};
    }
    std::vector<stMeshModel*> meshes(mesh_count);
    std::vector<std::shared_ptr<stMeshModel>> roots(mesh_count);
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
        model->getBounds();
    }

    info->hFile->read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) return {};

    std::vector<std::unique_ptr<stModelInstance>> pending(mesh_count);
    for (unsigned int mesh_index = 0; mesh_index < root_count; ++mesh_index) {
        stMeshModel* model = meshes[mesh_index];
        model->setName(name ? name : "");
        if (model->head == nullptr) {
            auto instance = CreateModelInstance(roots[mesh_index]);
            instance->setName("Multi Mesh Instance");
            instance->mesh_index = mesh_index;
            CacheLevelMesh(MeshResourceKey(*info, name), roots[mesh_index]);
            pending[mesh_index] = std::move(instance);
        }
    }

    return pending;
} catch (const std::exception&) {
    return {};
}

static void ValidateMeshMaterialNames(const W8MaterialRecord& material)
{
    for (const auto& name : material.texture_names) {
        if (memchr(name, '\0', sizeof(name)) == nullptr)
            throw std::runtime_error("unterminated mesh texture name");
    }
}
