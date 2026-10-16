#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "application.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "surrender/srClass.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srShader.h"
#include "wiz8/binary_reader.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/filesystem.h"

class srTexture;

/* Engine Code\OctSubMesh.cpp. Read at 0x0049E9A0 asserts this unit
   (lines 0x1ad and 0x1d2); the constructor and destructor immediately before
   it are the preceding attribution gap and stay here provisionally. */

/* The first Read captures a copy of material 0, its texture and render flags as
   the default state later used by the path visualization and trace models in
   OctPath.cpp and GameData.cpp. */
W8MeshMaterial g_oct_mesh_default;

void ReleaseOctMeshDefaults()
{
    g_oct_mesh_default = {};
}

/* The loader verifies every array the same way: a null getter result and a
   failed bulk read each stop with the call site's own diagnostic. */
template<class T>
void ReadMeshArray(wiz8::File& file, std::span<T> values, const char* get_message,
                   const char* read_message)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if (!values.data()) ShutdownWithErrorBox(get_message);
    try {
        if constexpr (std::is_same_v<T, glm::ivec3>) {
            std::vector<std::array<std::int32_t, 3>> records(values.size());
            file.read_exact(records.data(), records.size() * sizeof(records.front()));
            std::ranges::transform(records, values.begin(), [](const auto& record) {
                return glm::ivec3(record[0], record[1], record[2]);
            });
        } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                             std::is_same_v<T, glm::vec4>) {
            std::vector<std::array<float, T::length()>> records(values.size());
            file.read_exact(records.data(), records.size() * sizeof(records.front()));
            for (std::size_t i = 0; i < values.size(); ++i)
                for (int component = 0; component < T::length(); ++component)
                    values[i][component] = records[i][component];
        } else {
            file.read_exact(values.data(), values.size_bytes());
        }
    } catch (const std::exception&) {
        ShutdownWithErrorBox(read_message);
    }
}

// Keep raw integer write exceptions and vector diagnostics distinct. Both
// release their source only after successful I/O, in the field's disk order.
template<class T>
static void WriteAndConsumeMeshArray(wiz8::File& file, std::vector<T>& values, int count, int line,
                                     const char* message)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto write = [&] {
        if (count < 0 || std::cmp_greater(count, values.size()))
            throw std::runtime_error("Invalid mesh record count");
        const auto records = std::span<const T>(values).first(count);
        if constexpr (std::is_same_v<T, glm::ivec3>) {
            std::vector<std::array<std::int32_t, 3>> packed(records.size());
            std::ranges::transform(records, packed.begin(), [](const auto& triangle) {
                return std::array<std::int32_t, 3>{triangle.x, triangle.y, triangle.z};
            });
            file.write(packed.data(), packed.size() * sizeof(packed.front()));
        } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                             std::is_same_v<T, glm::vec4>) {
            std::vector<std::array<float, T::length()>> packed(records.size());
            for (std::size_t i = 0; i < records.size(); ++i)
                for (int component = 0; component < T::length(); ++component)
                    packed[i][component] = records[i][component];
            file.write(packed.data(), packed.size() * sizeof(packed.front()));
        } else {
            file.write(records.data(), records.size_bytes());
        }
    };
    if constexpr (std::is_same_v<T, int>) {
        write();
    } else {
        try {
            write();
        } catch (const std::exception&) {
            srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", line,
                         message);
        }
    }
    std::vector<T>{}.swap(values);
}

// FUNCTION: WIZ8 0x0049E5D0
bool OctMeshModel::Write(wiz8::File& hFile)
{
    m_packed_header |= static_cast<unsigned int>(version) << 8;
    hFile.write(&m_packed_header, 4);
    hFile.write(&m_vertex_count, 4);
    hFile.write(&m_map_count, 4);
    hFile.write(&m_polygon_count, 4);
    hFile.write(&m_link_index, 4);
    hFile.write(&next_link, 4);
    hFile.write(&m_material_index, 4);

    WriteAndConsumeMeshArray(hFile, m_vertex_locations, m_vertex_count, 0x109,
                             "OctMeshModel::Write -- Could not write m_psrVertLoc.\n");
    WriteAndConsumeMeshArray(hFile, m_vertex_map, m_map_count, 0x10f,
                             "OctMeshModel::Write -- Could not write m_psrMap.\n");
    if (m_material_index < 0) {
        WriteAndConsumeMeshArray(hFile, m_vertex_materials, m_vertex_count, 0x116,
                                 "OctMeshModel::Write -- Could not write m_plVertMats.\n");
    } else {
        m_vertex_materials.clear();
    }
    WriteAndConsumeMeshArray(hFile, m_poly_uv_index, m_polygon_count, 0x11f,
                             "OctMeshModel::Write -- Could not write m_psrPolyUVIndex.\n");
    WriteAndConsumeMeshArray(hFile, m_poly_vertices, m_polygon_count, 0x125,
                             "OctMeshModel::Write -- Could not write m_psrPolyVertex.\n");
    WriteAndConsumeMeshArray(hFile, m_poly_textures, m_polygon_count, 0x12a,
                             "OctMeshModel::Write -- Could not write m_plPolyTextures.\n");
    WriteAndConsumeMeshArray(hFile, m_vertex_normals, m_vertex_count, 0x130,
                             "OctMeshModel::Write -- Could not write m_pVertNorms.\n");
    WriteAndConsumeMeshArray(hFile, m_vertex_lights, m_vertex_count, 0x136,
                             "OctMeshModel::Write -- Could not write m_pVertLights.\n");
    WriteAndConsumeMeshArray(hFile, m_poly_equations, m_polygon_count, 0x13c,
                             "OctMeshModel::Write -- Could not write m_psrPolyEqtns.\n");

    if (version != 0) {
        hFile.write(m_sun_lights[0].data(), m_vertex_count * sizeof(float));
    }
    std::vector<std::vector<float>>{}.swap(m_sun_lights);

    int terminator = -1;
    hFile.write(&terminator, 4);
    return true;
}

// FUNCTION: WIZ8 0x0049E9A0
std::unique_ptr<stMeshModel> OctMeshModel::Read(wiz8::File* file,
                                                const std::vector<W8MeshMaterial>& materials)
{
    const int material_count = static_cast<int>(materials.size());
    if (materials.empty()) throw std::runtime_error("Missing mesh materials");
    wiz8::BinaryReader input(*file);
    m_packed_header = input.record<unsigned int>();
    m_vertex_count = input.record<int>();
    m_map_count = input.record<int>();
    m_polygon_count = input.record<int>();
    m_link_index = input.record<int>();
    next_link = input.record<int>();
    m_material_index = input.record<int>();

    unsigned int header = m_packed_header;
    bool unweighted = (header & 0xff) == 0;
    m_packed_header = header & 0xff;
    version = static_cast<short>((header >> 8) & 0xff);

    if (m_vertex_count < 0 || m_map_count < 0 || m_polygon_count < 0 || m_material_index < -1 ||
        m_material_index >= material_count) {
        throw std::runtime_error("Invalid mesh counts or material index");
    }
    if (m_vertex_count == 0 && m_material_index < 0)
        throw std::runtime_error("Missing mesh vertex material");
    const auto vertex_bytes = static_cast<std::uint64_t>(m_vertex_count) *
                              (36 + (m_material_index < 0 ? 4 : 0) + (version != 0 ? 4 : 0));
    const auto polygon_bytes = static_cast<std::uint64_t>(m_polygon_count) * 44;
    input.require(vertex_bytes + static_cast<std::uint64_t>(m_map_count) * 8 + polygon_bytes + 4);
    // Read destinations borrow the model's buffers. They must never become
    // reader-owned Write workspace, including when a read throws midway.
    std::vector<int> material_indices(std::max(m_vertex_count, m_polygon_count));
    int* indices = material_indices.data();
    glm::vec3* vertex_locations;
    glm::vec2* vertex_map;
    glm::ivec3* poly_uv_index;
    glm::ivec3* poly_vertices;
    glm::vec3* vertex_normals;
    glm::vec4* poly_equations;

    auto model = std::make_unique<stMeshModel>(m_polygon_count, m_vertex_count);
    if (unweighted) {
        model->flags &= ~W8_MESH_SORTED_RENDERING;
    } else {
        model->flags |= W8_MESH_SORTED_RENDERING;
    }

    vertex_locations = model->getVertexLoc();
    ReadMeshArray(*file, std::span(vertex_locations, m_vertex_count),
                  "OctMeshModel::Read -- Could not get vertex location array.\n",
                  "OctMeshModel::Read -- Could not read Integer fields.\n");

    model->setUVCount(m_map_count);
    vertex_map = model->getVertexTexCoords(0, 0, 1);
    ReadMeshArray(*file, std::span(vertex_map, static_cast<std::size_t>(m_map_count)),
                  "OctMeshModel::Read -- Could not get vertex mapping array.\n",
                  "OctMeshModel::Read -- Could not read m_psrMap.");

    int selected_material = m_material_index;
    int index;
    if (m_material_index < 0) {
        std::shared_ptr<srMaterial>* vertex_materials =
            model->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1);
        if (vertex_materials == 0) {
            ShutdownWithErrorBox("OctMeshModel::Read -- Could not get vertex material array.\n");
        }
        if (!(file->read(indices, m_vertex_count * sizeof(int)).bytes ==
              static_cast<std::size_t>(m_vertex_count * sizeof(int)))) {
            ShutdownWithErrorBox("OctMeshModel::Read -- Could not read m_plVertMats.\n");
        }
        selected_material = indices[0];
        for (index = 0; index < m_vertex_count; ++index) {
            int material_index = indices[index];
            if (material_index < 0 || material_index >= material_count) {
                throw std::runtime_error("Invalid mesh vertex material index");
            }
            vertex_materials[index] = materials[material_index].material;
        }
    } else {
        model->setMaterial(materials[m_material_index].material, 0, srMeshModel::SIDE_FRONT);
    }

    poly_uv_index = model->getPolyUVIndex(0, 1);
    ReadMeshArray(*file, std::span(poly_uv_index, static_cast<std::size_t>(m_polygon_count)),
                  "OctMeshModel::Read -- Could not get poly-UV array.\n",
                  "OctMeshModel::Read -- Could not read m_psrPolyUVIndex.\n");

    poly_vertices = model->getPolyVertex();
    ReadMeshArray(*file, std::span(poly_vertices, static_cast<std::size_t>(m_polygon_count)),
                  "OctMeshModel::Read -- Could not get poly-vertex array.\n",
                  "OctMeshModel::Read -- Could not read m_psrPolyVertex.\n");

    std::shared_ptr<srTexture>* polygon_textures = model->getPolyTexture(0, 0, 1);
    if (polygon_textures == 0) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not get poly texture array.\n");
    }
    if (!(file->read(indices, m_polygon_count * sizeof(int)).bytes ==
          static_cast<std::size_t>(m_polygon_count * sizeof(int)))) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not read m_plPolyTextures.\n");
    }
    for (index = 0; index < m_polygon_count; ++index) {
        int texture_index = indices[index];
        if (texture_index < 0 || texture_index >= material_count) {
            throw std::runtime_error("Invalid mesh polygon texture index");
        }
        polygon_textures[index] = materials[texture_index].texture;
    }

    vertex_normals = model->getVertexNormal();
    ReadMeshArray(*file, std::span(vertex_normals, m_vertex_count),
                  "OctMeshModel::Read -- Could not get vertex normal array.\n",
                  "OctMeshModel::Read -- Could not read m_pVertNorms.\n");

    ReadMeshArray(*file, model->GetVertexLights(true, -1),
                  "OctMeshModel::Read -- Could not get static lighting array.\n",
                  "OctMeshModel::Read -- Could not read m_pVertLights.\n");

    poly_equations = model->getPolyEq();
    ReadMeshArray(*file, std::span(poly_equations, static_cast<std::size_t>(m_polygon_count)),
                  "OctMeshModel::Read -- Could not get poly equation array.\n",
                  "OctMeshModel::Read -- Could not read m_psrPolyEqtns.\n");

    const auto weights = model->GetVertexSunlight(true);
    if (weights.empty()) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not allocate intensity array.\n");
    }
    if (version != 0 && !(file->read(weights.data(), weights.size_bytes()).bytes ==
                          static_cast<std::size_t>(m_vertex_count * sizeof(float)))) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not read Sunlight array.\n");
    }

    w8_ulong* shade_indices = model->getVertexShadeIndex(1);
    if (vertex_locations == 0) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not get vertex location array.\n");
    }
    for (w8_ulong shade_index = 0; shade_index < static_cast<w8_ulong>(m_vertex_count);
         ++shade_index) {
        shade_indices[shade_index] = shade_index;
    }

    int terminator;
    file->read_exact(&terminator, sizeof(terminator));
    if (terminator != -1) {
        ShutdownWithErrorBox("Mesh Model in .pvl file is wrong length.");
    }

    const srShader render_flags = materials.at(selected_material).render_flags;
    model->setShader(render_flags, 0);
    if ((render_flags.value & 0x6000) == 0x4000) {
        if (unweighted) {
            ShutdownWithErrorBox("OctMeshModel::Read -- Wrong shader type.\n");
        } else {
            model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
        }
    } else if (!unweighted) {
        model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
    }

    if (!g_oct_mesh_default.material) {
        auto material = std::make_shared<stMaterial>();
        material->srClass::operator=(*materials[0].material);
        g_oct_mesh_default = {std::move(material), materials[0].texture, materials[0].render_flags};
    }

    return model;
}
