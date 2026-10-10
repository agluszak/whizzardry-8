#include "wiz8/filesystem.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/Octree.h"

#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/sr_api.h"
#include "wiz8/utility.h"
#include "wiz8/virtual_file.h"
#include "surrender/srMaterial.h"
#include <memory>
#include "surrender/srTexture.h"

#include <stdlib.h>

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
template <class T>
void ReadMeshArray(wiz8::File* file, T* values, int count, const char* get_message,
                   const char* read_message)
{
    if (values == 0) {
        ShutdownWithErrorBox(get_message);
    }
    if (!ReadVectorArray(file, values, count)) {
        ShutdownWithErrorBox(read_message);
    }
}

// FUNCTION: WIZ8 0x0049E4C0
OctMeshModel::OctMeshModel()
    : version(0), m_link_index(0), next_link(0), m_material_index(0), m_map_count(0),
      m_vertex_locations(0), m_vertex_map(0), m_vertex_materials(0), m_poly_vertices(0),
      m_poly_uv_index(0), m_poly_textures(0), m_vertex_normals(0), m_vertex_lights(0),
      m_poly_equations(0), m_sun_lights(0), m_packed_header(0), m_vertex_count(0),
      m_polygon_count(0)
{
}

// FUNCTION: WIZ8 0x0049E500
OctMeshModel::~OctMeshModel()
{
    if (m_vertex_locations != 0) {
        std::free(m_vertex_locations);
    }
    if (m_vertex_map != 0) {
        std::free(m_vertex_map);
    }
    if (m_vertex_materials != 0) {
        free(m_vertex_materials);
    }
    if (m_poly_vertices != 0) {
        std::free(m_poly_vertices);
    }
    if (m_poly_uv_index != 0) {
        std::free(m_poly_uv_index);
    }
    if (m_poly_textures != 0) {
        free(m_poly_textures);
    }
    if (m_vertex_normals != 0) {
        std::free(m_vertex_normals);
    }
    if (m_vertex_lights != 0) {
        std::free(m_vertex_lights);
    }
    if (m_poly_equations != 0) {
        std::free(m_poly_equations);
    }
    if (m_sun_lights != 0) {
        for (short index = 0; index < version; ++index) {
            free(m_sun_lights[index]);
        }
    }
}

// FUNCTION: WIZ8 0x0049E5D0
bool OctMeshModel::Write(wiz8::File* hFile)
{
    unsigned char success;
    unsigned char write_result;
    int index;

    m_packed_header |= static_cast<unsigned int>(version) << 8;
    success = (hFile->write(&m_packed_header, 4), true);
    success &= (hFile->write(&m_vertex_count, 4), true);
    success &= (hFile->write(&m_map_count, 4), true);
    success &= (hFile->write(&m_polygon_count, 4), true);
    success &= (hFile->write(&m_link_index, 4), true);
    success &= (hFile->write(&next_link, 4), true);
    success &= (hFile->write(&m_material_index, 4), true);
    if (success == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x105,
                     "OctMeshModel::Write -- Could not write INT32 fields.\n");
    }

    write_result = WriteVectorArray(hFile, m_vertex_locations, m_vertex_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x109,
                     "OctMeshModel::Write -- Could not write m_psrVertLoc.\n");
    }
    std::free(m_vertex_locations);
    m_vertex_locations = 0;

    write_result = WriteVectorArray(hFile, m_vertex_map, m_map_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x10f,
                     "OctMeshModel::Write -- Could not write m_psrMap.\n");
    }
    std::free(m_vertex_map);
    m_vertex_map = 0;

    if (m_material_index < 0) {
        write_result = (hFile->write(m_vertex_materials, m_vertex_count * sizeof(int)), true);
        if (write_result == 0) {
            srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x116,
                         "OctMeshModel::Write -- Could not write m_plVertMats.\n");
        }
    }
    if (m_vertex_materials != 0) {
        free(m_vertex_materials);
    }
    m_vertex_materials = 0;

    write_result = WriteVectorArray(hFile, m_poly_uv_index, m_polygon_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x11f,
                     "OctMeshModel::Write -- Could not write m_psrPolyUVIndex.\n");
    }
    std::free(m_poly_uv_index);
    m_poly_uv_index = 0;

    write_result = WriteVectorArray(hFile, m_poly_vertices, m_polygon_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x125,
                     "OctMeshModel::Write -- Could not write m_psrPolyVertex.\n");
    }
    std::free(m_poly_vertices);
    m_poly_vertices = 0;

    write_result = (hFile->write(m_poly_textures, m_polygon_count * sizeof(int)), true);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x12a,
                     "OctMeshModel::Write -- Could not write m_plPolyTextures.\n");
    }
    free(m_poly_textures);
    m_poly_textures = 0;

    write_result = WriteVectorArray(hFile, m_vertex_normals, m_vertex_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x130,
                     "OctMeshModel::Write -- Could not write m_pVertNorms.\n");
    }
    std::free(m_vertex_normals);
    m_vertex_normals = 0;

    write_result = WriteVectorArray(hFile, m_vertex_lights, m_vertex_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x136,
                     "OctMeshModel::Write -- Could not write m_pVertLights.\n");
    }
    std::free(m_vertex_lights);
    m_vertex_lights = 0;

    write_result = WriteVectorArray(hFile, m_poly_equations, m_polygon_count);
    if (write_result == 0) {
        srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x13c,
                     "OctMeshModel::Write -- Could not write m_psrPolyEqtns.\n");
    }
    std::free(m_poly_equations);
    m_poly_equations = 0;

    if (version != 0) {
        write_result = (hFile->write(m_sun_lights[0], m_vertex_count * sizeof(float)), true);
        if (write_result == 0) {
            srAssertFail("fSuccess", "C:\\Projects\\Wizardry 8\\Engine Code\\OctSubMesh.cpp", 0x143,
                         "OctMeshModel::Write -- Could not write m_ppflSunLights.\n");
        }
        for (index = 0; index < version; ++index) {
            free(m_sun_lights[index]);
        }
        free(m_sun_lights);
    }
    m_sun_lights = 0;

    int terminator = -1;
    write_result = (hFile->write(&terminator, 4), true);
    return write_result & success;
}

// FUNCTION: WIZ8 0x0049E9A0
std::unique_ptr<stMeshModel> OctMeshModel::Read(wiz8::File* file,
                                                const std::vector<W8MeshMaterial>& materials)
{
    const int material_count = static_cast<int>(materials.size());
    if (g_oct_mesh_default.material == 0) {
        /* Retail copies only the base identity (the name) of material 0. */
        auto material = std::make_shared<stMaterial>();
        material->srClass::operator=(*materials[0].material);
        g_oct_mesh_default = {std::move(material), materials[0].texture, materials[0].render_flags};
    }

    unsigned char read_ok = 1;
    read_ok &= (file->read(&m_packed_header, 4).bytes == static_cast<std::size_t>(4));
    read_ok &= (file->read(&m_vertex_count, 4).bytes == static_cast<std::size_t>(4));
    read_ok &= (file->read(&m_map_count, 4).bytes == static_cast<std::size_t>(4));
    read_ok &= (file->read(&m_polygon_count, 4).bytes == static_cast<std::size_t>(4));
    read_ok &= (file->read(&m_link_index, 4).bytes == static_cast<std::size_t>(4));
    read_ok &= (file->read(&next_link, 4).bytes == static_cast<std::size_t>(4));
    read_ok &= (file->read(&m_material_index, 4).bytes == static_cast<std::size_t>(4));
    if (read_ok == 0) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not read Integer fields.\n");
    }

    unsigned int header = m_packed_header;
    bool unweighted = (header & 0xff) == 0;
    m_packed_header = header & 0xff;
    version = static_cast<short>((header >> 8) & 0xff);

    if (m_vertex_count < 0 || m_map_count < 0 || m_polygon_count < 0 ||
        m_material_index < -1 || m_material_index >= material_count) {
        throw std::runtime_error("Invalid mesh counts or material index");
    }
    // Read destinations borrow the model's buffers. They must never become
    // reader-owned Write workspace, including when a read throws midway.
    std::vector<int> material_indices(std::max(m_vertex_count, m_polygon_count));
    int* indices = material_indices.data();
    srVector3T<float>* vertex_locations;
    srVector2T<float>* vertex_map;
    srVector3i* poly_uv_index;
    srVector3i* poly_vertices;
    srVector3T<float>* vertex_normals;
    srVector3T<float>* vertex_lights;
    srVector4T<float>* poly_equations;

    auto model = std::make_unique<stMeshModel>(m_polygon_count, m_vertex_count);
    if (unweighted) {
        model->flags &= ~W8_MESH_SORTED_RENDERING;
    } else {
        model->flags |= W8_MESH_SORTED_RENDERING;
    }

    vertex_locations = model->getVertexLoc();
    ReadMeshArray(file, vertex_locations, m_vertex_count,
                  "OctMeshModel::Read -- Could not get vertex location array.\n",
                  "OctMeshModel::Read -- Could not read Integer fields.\n");

    model->setUVCount(m_map_count);
    vertex_map = model->getVertexTexCoords(0, 0, 1);
    ReadMeshArray(file, vertex_map, m_map_count,
                  "OctMeshModel::Read -- Could not get vertex mapping array.\n",
                  "OctMeshModel::Read -- Could not read m_psrMap.");

    int selected_material = m_material_index;
    int index;
    if (m_material_index < 0) {
        std::shared_ptr<srMaterialIFace>* vertex_materials =
            model->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1);
        if (vertex_materials == 0) {
            ShutdownWithErrorBox("OctMeshModel::Read -- Could not get vertex material array.\n");
        }
        if (!(file->read(indices, m_vertex_count * sizeof(int)).bytes == static_cast<std::size_t>(m_vertex_count * sizeof(int)))) {
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
    ReadMeshArray(file, poly_uv_index, m_polygon_count,
                  "OctMeshModel::Read -- Could not get poly-UV array.\n",
                  "OctMeshModel::Read -- Could not read m_psrPolyUVIndex.\n");

    poly_vertices = model->getPolyVertex();
    ReadMeshArray(file, poly_vertices, m_polygon_count,
                  "OctMeshModel::Read -- Could not get poly-vertex array.\n",
                  "OctMeshModel::Read -- Could not read m_psrPolyVertex.\n");

    std::shared_ptr<srTextureIFace>* polygon_textures = model->getPolyTexture(0, 0, 1);
    if (polygon_textures == 0) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not get poly texture array.\n");
    }
    if (!(file->read(indices, m_polygon_count * sizeof(int)).bytes == static_cast<std::size_t>(m_polygon_count * sizeof(int)))) {
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
    ReadMeshArray(file, vertex_normals, m_vertex_count,
                  "OctMeshModel::Read -- Could not get vertex normal array.\n",
                  "OctMeshModel::Read -- Could not read m_pVertNorms.\n");

    vertex_lights = model->GetVertexLights(true, -1);
    ReadMeshArray(file, vertex_lights, m_vertex_count,
                  "OctMeshModel::Read -- Could not get static lighting array.\n",
                  "OctMeshModel::Read -- Could not read m_pVertLights.\n");

    poly_equations = model->getPolyEq();
    ReadMeshArray(file, poly_equations, m_polygon_count,
                  "OctMeshModel::Read -- Could not get poly equation array.\n",
                  "OctMeshModel::Read -- Could not read m_psrPolyEqtns.\n");

    float* weights = model->GetVertexSunlight(true);
    if (weights == 0) {
        ShutdownWithErrorBox("OctMeshModel::Read -- Could not allocate intensity array.\n");
    }
    if (version != 0 && !(file->read(weights, m_vertex_count * sizeof(float)).bytes == static_cast<std::size_t>(m_vertex_count * sizeof(float)))) {
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

    return model;
}
