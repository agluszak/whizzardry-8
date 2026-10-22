#include "wiz8/entities/animation_mesh.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "wiz8/filesystem.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/spatial/bounding_box.h"

// Decode the full frame sequence into detached ownership before publishing it.
// Source offsets identify resources; frame/channel labels are unnecessary.
std::optional<AnimationMesh> ReadAnimatedMesh(wiz8::File& file,
                                              const std::filesystem::path& texture_directory)
try {
    unsigned char count;
    file.read_exact(&count, sizeof(count));
    if (!count) return {};
    unsigned char first_index;
    file.read_exact(&first_index, sizeof(first_index));
    if (first_index >= count) return {};
    auto first = ReadPlacedMesh(file, texture_directory);
    if (!first.mesh) return {};
    const bool vertex_frames = first.mesh->frames.size() > 1;
    std::vector<PlacedMesh> frames(vertex_frames ? 1 : count);
    frames[vertex_frames ? 0 : first_index] = std::move(first);
    if (!vertex_frames) {
        for (unsigned int index = 1; index < count; ++index) {
            unsigned char frame;
            file.read_exact(&frame, sizeof(frame));
            if (frame >= count || frames[frame].mesh) return {};
            frames[frame] = ReadPlacedMesh(file, texture_directory);
            if (!frames[frame].mesh) return {};
        }
    }
    // Model bounds already include all vertex frames.
    AnimationMesh mesh;
    mesh.bounds = frames.front().mesh->getBounds().box;
    for (const auto& frame : frames) {
        const auto frame_bounds = frame.mesh->getBounds().box;
        mesh.radius =
            std::max(mesh.radius, glm::distance(frame_bounds.minimum, frame_bounds.maximum) * 0.5f);
        mesh.bounds.include(frame_bounds);
    }
    mesh.frames = std::move(frames);
    mesh.frame_count = count;
    mesh.vertex_frames = vertex_frames;
    mesh.source_file = file.physical_path();
    return mesh;
} catch (const std::exception&) {
    return {};
}

AnimationFrame GetAniMeshFrame(const AnimationMesh& mesh, unsigned char frame)
{
    assert(frame < mesh.frame_count);
    const auto& shown = mesh.frames[mesh.vertex_frames ? 0 : frame];
    return {shown.mesh, mesh.vertex_frames ? frame : 0u, &shown.placement};
}

void ShowFrame(MeshInstance& instance, const AnimationFrame& frame)
{
    if (instance.getModel() != frame.mesh.get()) {
        instance.setModel(frame.mesh);
        if (frame.placement) instance.transform = *frame.placement;
    }
    instance.frame_index = frame.vertex_frame;
}
