#pragma once

#include <memory>
#include <vector>

namespace wiz8 {
class File;
}
struct W8MeshMaterial;
class Mesh;

// Metadata used while decoding one compiled OCT/PVL mesh.
class CompiledMesh {
public:
    CompiledMesh() = default;
    CompiledMesh(CompiledMesh&& other) noexcept = default;
    CompiledMesh& operator=(CompiledMesh&& other) noexcept = default;
    CompiledMesh(const CompiledMesh&) = delete;
    CompiledMesh& operator=(const CompiledMesh&) = delete;
    std::unique_ptr<Mesh> Read(wiz8::File* file, const std::vector<W8MeshMaterial>& materials);

    short version{};
    int m_link_index{};
    int next_link{};
    int m_material_index{};
    int m_map_count{};                       /* m_psrMap entry count */
    unsigned int m_packed_header{};
    int m_vertex_count{};
    int m_polygon_count{};
};
