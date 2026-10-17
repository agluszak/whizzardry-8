#include "wiz8/engine_code/ReadMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/fwd.hpp"
#include "surrender/mesh_resources.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srShader.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"

class srMaterial;
class srTexture;

namespace {
// Runtime math objects have natural alignment; disk records contain only values.
struct W8ReadMeshFace {
    int vertices[3];
    glm::vec2 texture_coordinates[3];
    int material_index;
    unsigned char flags;
};
W8ReadMeshFace DecodeMeshFace(const W8SerializedMeshFace& record)
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

struct SingleLevelMeshRecord {
    std::vector<std::pair<short, short>> mapped_vertices; // marker key, source vertex
    int version = 0;
    unsigned char flags = 0;
    std::vector<glm::vec3> vertices;
    std::vector<std::vector<short>> compressed_vertices;
    float compression_scale = 1000.0f;
    std::vector<W8ReadMeshFace> faces;
    glm::vec3 location{0};
    glm::mat3 rotation{1};
    std::vector<W8MaterialRecord> materials;
};

// A record has an identity before it is decoded. Placement and debug labels
// cannot change which geometry resource that record denotes.
struct MeshResourceKey {
    std::filesystem::path source;
    std::int64_t offset;
    std::filesystem::path texture_directory;
    auto operator<=>(const MeshResourceKey&) const = default;
};
std::map<MeshResourceKey, std::weak_ptr<stMeshModel>> level_meshes;
} // namespace

static bool ReadMeshFaceNeedsSplit(const W8ReadMeshFace& face,
                                   const std::vector<W8MeshMaterial>& materials)
{
    return (face.flags & 4) != 0 || materials[face.material_index].two_sided;
}

/* Reorder polygons so equal shader/texture keys are adjacent, then vertices so
   equal materials are adjacent and, within them, follow first use. */
// FUNCTION: WIZ8 0x004867F0
static void OptimizeMeshOrder(stMeshModel& mesh)
{
    auto* model = &mesh;
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
    std::vector<w8_ulong> inverse(vertex_count);
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
        inverse[vertices[vertex]] = vertex;
    for (auto& [key, vertex] : mesh.mapped_vertices) vertex = static_cast<short>(inverse[vertex]);
}

namespace {
// Build one complete shader partition. Source-to-renderer indices stay local
// to construction; callers never receive an unfinished mesh and a repair map.
std::unique_ptr<stMeshModel> BuildMeshPart(const SingleLevelMeshRecord& record,
                                           const std::vector<W8MeshMaterial>& materials,
                                           srShader shader)
{
    std::map<std::tuple<const srMaterial*, bool, int>, int> vertex_indices;
    std::vector<int> source_vertices;
    std::vector<std::shared_ptr<srMaterial>> vertex_materials;
    std::vector<std::shared_ptr<srTexture>> polygon_textures;
    std::vector<glm::vec2> uvs;
    std::vector<int> first_uvs;
    std::vector<glm::ivec3> polygon_vertices, polygon_uvs;
    std::map<short, short> mapped_vertices;

    for (const auto& face : record.faces) {
        const auto& material = materials[face.material_index];
        if (material.render_flags.value != shader.value) continue;
        glm::ivec3 polygon, uv;
        for (int corner = 0; corner < 3; ++corner) {
            const int source = face.vertices[corner];
            auto [entry, inserted] = vertex_indices.try_emplace(
                std::tuple{material.material.get(), material.two_sided, source},
                source_vertices.size());
            const int vertex = entry->second;
            polygon[corner] = vertex;
            if (inserted) {
                source_vertices.push_back(source);
                vertex_materials.push_back(material.material);
                first_uvs.push_back(static_cast<int>(uvs.size()));
                uvs.push_back(face.texture_coordinates[corner]);
                for (const auto& [key, mapped_source] : record.mapped_vertices) {
                    if (mapped_source == source) {
                        mapped_vertices.insert_or_assign(key, static_cast<short>(vertex));
                        break;
                    }
                }
            }
            if (uvs[first_uvs[vertex]] == face.texture_coordinates[corner]) {
                uv[corner] = first_uvs[vertex];
            } else {
                uv[corner] = static_cast<int>(uvs.size());
                uvs.push_back(face.texture_coordinates[corner]);
            }
        }
        polygon_vertices.push_back(polygon);
        polygon_uvs.push_back(uv);
        polygon_textures.push_back(material.texture);
        if (ReadMeshFaceNeedsSplit(face, materials)) {
            glm::ivec3 split, split_uv;
            for (int corner = 0; corner < 3; ++corner) {
                split[corner] = static_cast<int>(source_vertices.size());
                source_vertices.push_back(face.vertices[corner]);
                vertex_materials.push_back(material.material);
                split_uv[corner] = static_cast<int>(uvs.size());
                first_uvs.push_back(split_uv[corner]);
                uvs.push_back(face.texture_coordinates[corner]);
            }
            polygon_vertices.push_back(split);
            polygon_uvs.push_back(split_uv);
            polygon_textures.push_back(material.texture);
        }
    }
    if (polygon_vertices.empty()) return {};

    const auto vertex_count = source_vertices.size();
    auto model = std::make_unique<stMeshModel>(polygon_vertices.size(), vertex_count);
    model->mapped_vertices.insert(mapped_vertices.begin(), mapped_vertices.end());
    auto* textures = model->getPolygonTextureTable(0, 0, true);
    for (std::size_t polygon = 0; polygon < polygon_textures.size(); ++polygon)
        textures->set(polygon, polygon_textures[polygon]);

    if (std::ranges::all_of(vertex_materials, [&](const auto& material) {
            return material == vertex_materials.front();
        })) {
        model->setMaterial(vertex_materials.front(), 0, srMeshModel::SIDE_FRONT);
    } else {
        auto* table = model->getVertexMaterialTable(0, srMeshModel::SIDE_FRONT, true);
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex)
            table->set(vertex, vertex_materials[vertex]);
    }

    std::ranges::copy(polygon_vertices, model->getPolyVertex());
    model->setUVCount(uvs.size());
    std::ranges::copy(uvs, model->getVertexTexCoords(0, 0, 1));
    std::ranges::copy(polygon_uvs, model->getPolyUVIndex(0, 1));
    auto* shades = model->getVertexShadeIndex(1);
    std::iota(shades, shades + vertex_count, 0);
    model->setShader(shader, 0);
    if ((shader.value & 0x6000) == 0x4000) {
        model->flags |= W8_MESH_SORTED_RENDERING;
        model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
    }

    if (record.compressed_vertices.empty()) {
        auto* vertices = model->getVertexLoc();
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex)
            vertices[vertex] = record.vertices[source_vertices[vertex]];
        OptimizeMeshOrder(*model);
    } else {
        model->InitializeVertexFrames(record.compressed_vertices.size());
        model->vertex_compression_scale = 500.0f / record.compression_scale;
        for (std::size_t frame = 0; frame < record.compressed_vertices.size(); ++frame) {
            auto* vertices = model->GetVertex(frame);
            for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
                const auto source = source_vertices[vertex];
                std::copy_n(record.compressed_vertices[frame].data() + source * 3, 3,
                            vertices + vertex * 3);
            }
            model->FinalizeVertexFrame(frame);
        }
    }
    model->setDirtyAll();
    return model;
}
} // namespace

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

static std::vector<W8MeshMaterial> LoadMeshMaterials(const std::filesystem::path& texture_directory,
                                                     const std::vector<W8MaterialRecord>& records)
{
    const auto count = records.size();
    if (count == 0) return {};
    std::vector<W8MeshMaterial> staged;
    staged.reserve(count);
    staged.push_back(CreateDefaultMaterial());
    for (std::size_t index = 1; index < count; ++index) {
        staged.push_back(LoadMaterial(texture_directory, records[index]));
    }
    return staged;
}

namespace {

SingleLevelMeshRecord ReadSingleLevelMeshRecord(wiz8::File& input)
{
    SingleLevelMeshRecord record;
    auto& [mapped_vertices, version, flags, vertices, compressed_vertices, compression_scale, faces,
           location, rotation, materials] = record;
    int vertex_count = 0;
    int face_count = 0;
    short frame_count = 0;
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
            mapped_vertices.emplace_back(key, value);
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
        std::vector<W8CompressedMeshFace> compressed_faces(face_count);
        file->read_exact(compressed_faces.data(),
                         compressed_faces.size() * sizeof(W8CompressedMeshFace));
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

namespace {
std::shared_ptr<stMeshModel> BuildMeshResource(const SingleLevelMeshRecord& record,
                                               const std::filesystem::path& texture_directory)
{
    if ((record.flags & W8_LEVEL_MESH_LOD_VERTICES) &&
        !(record.flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES))
        throw std::runtime_error("runtime mesh requires short vertex frames");
    const auto materials = LoadMeshMaterials(texture_directory, record.materials);
    std::vector<w8_ulong> shaders;
    std::shared_ptr<stMeshModel> model;
    for (const auto& material : materials) {
        if (std::ranges::contains(shaders, material.render_flags.value)) continue;
        shaders.push_back(material.render_flags.value);
        auto part = BuildMeshPart(record, materials, material.render_flags);
        if (!part) continue;
        if (model)
            model->AddPart(std::move(part));
        else
            model = std::move(part);
    }
    model->setDirty(srMeshModel::DIRTY_BOUNDS);
    model->getBounds();
    model->setName("Level mesh resource");
    return model;
}

std::shared_ptr<stMeshModel> ReuseOrBuildMeshResource(const SingleLevelMeshRecord& record,
                                                      const MeshResourceKey& key)
{
    auto& cached = level_meshes[key];
    if (auto model = cached.lock()) return model;
    auto model = BuildMeshResource(record, key.texture_directory);
    if (!model) return {};
    // Every part must be shareable. A static head can have an animated child.
    for (auto* part : stMeshModel::Meshes(model.get())) {
        if (MeshHasAnimatedTexture(part)) return model;
    }
    cached = model;
    return model;
}

std::unique_ptr<stModelInstance> PlaceMeshInstance(const SingleLevelMeshRecord& record,
                                                   std::shared_ptr<stMeshModel> resource)
{
    if (!resource) return {};
    auto instance = CreateModelInstance(std::move(resource));
    instance->setName("Level mesh instance");
    if (record.version > 1) {
        const auto rotation =
            record.rotation * glm::mat3_cast(glm::angleAxis(float(3.1415926), glm::vec3(1, 0, 0)));
        instance->setLocation(glm::dvec3(record.location) * 500.0);
        instance->setRotation(rotation);
    }
    return instance;
}
} // namespace

bool ReadSingleLevelMeshHasVertexFrames(wiz8::File& file)
{
    return (ReadSingleLevelMeshRecord(file).flags & W8_LEVEL_MESH_LOD_VERTICES) != 0;
}

std::unique_ptr<stModelInstance> ReadSingleLevelMesh(wiz8::File& file,
                                                     const std::filesystem::path& texture_directory)
try {
    const MeshResourceKey key{file.physical_path(), file.tell(), texture_directory};
    auto record = ReadSingleLevelMeshRecord(file);
    return PlaceMeshInstance(record, ReuseOrBuildMeshResource(record, key));
} catch (const std::exception&) {
    return {};
}

// FUNCTION: WIZ8 0x00488240
std::vector<std::unique_ptr<stModelInstance>>
ReadMultipleLevelMeshes(wiz8::File& file, std::size_t expected_count,
                        const std::filesystem::path& texture_directory)
try {
    OctMeshModel reader;
    unsigned int mesh_count;
    unsigned int root_count;
    int terminator;

    if (expected_count == 0) {
        return {};
    }

    file.read_exact(&mesh_count, sizeof(mesh_count));
    file.read_exact(&root_count, sizeof(root_count));
    if (mesh_count != expected_count || root_count > mesh_count) {
        throw std::runtime_error("Invalid mesh or root count");
    }

    const auto materials = LoadMeshMaterials(texture_directory, ReadMeshMaterialRecords(file));
    if (materials.empty()) return {};

    file.read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) return {};

    if (mesh_count > static_cast<std::uint64_t>(file.size() - file.tell())) {
        return {};
    }
    std::vector<stMeshModel*> meshes(mesh_count);
    std::vector<std::shared_ptr<stMeshModel>> roots(mesh_count);
    for (unsigned int mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
        if (g_current_screen_state.id == W8_SCREEN_PLEASE_WAIT) {
            UpdatePleaseWaitLoadFrame();
        }
        auto owned_model = reader.Read(&file, materials);
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

    file.read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) return {};

    std::vector<std::unique_ptr<stModelInstance>> pending(mesh_count);
    for (unsigned int mesh_index = 0; mesh_index < root_count; ++mesh_index) {
        stMeshModel* model = meshes[mesh_index];
        model->setName("Level mesh resource");
        if (model->head == nullptr) {
            auto instance = CreateModelInstance(roots[mesh_index]);
            instance->setName("Multi Mesh Instance");
            instance->mesh_index = mesh_index;
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
