#include "wiz8/entities/animation_mesh.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "wiz8/filesystem.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/world/update.h"

AnimationMesh::AnimationMesh() = default;
AnimationMesh::~AnimationMesh() = default;

AnimationMesh::AnimationMesh(const AnimationMesh& other)
    : frame_count(other.frame_count), single_instance(other.single_instance), bounds(other.bounds),
      radius(other.radius), source_file(other.source_file)
{
    meshes.reserve(other.meshes.size());
    for (const auto& source : other.meshes) {
        auto copy = source ? DuplicateModelInstance(*source) : nullptr;
        if (copy) copy->setName("Ani Mesh Duplicate Instance");
        meshes.push_back(std::move(copy));
    }
}

// Decode the full frame sequence into detached ownership before publishing it.
// Source offsets identify resources; frame/channel labels are unnecessary.
std::unique_ptr<AnimationMesh> ReadAnimatedMesh(wiz8::File& file,
                                                const std::filesystem::path& texture_directory)
try {
    unsigned char count;
    file.read_exact(&count, sizeof(count));
    if (!count) return {};
    unsigned char first_index;
    file.read_exact(&first_index, sizeof(first_index));
    if (first_index >= count) return {};
    auto first = ReadSingleLevelMesh(file, texture_directory);
    if (!first) return {};
    auto* model = static_cast<Mesh*>(first->getModel());
    const bool single = model->frames.size() > 1;
    std::vector<std::unique_ptr<MeshInstance>> frames(single ? 1 : count);
    frames[single ? 0 : first_index] = std::move(first);
    if (!single) {
        for (unsigned int index = 1; index < count; ++index) {
            unsigned char frame;
            file.read_exact(&frame, sizeof(frame));
            if (frame >= count || frames[frame]) return {};
            frames[frame] = ReadSingleLevelMesh(file, texture_directory);
            if (!frames[frame]) return {};
        }
    }
    // Model bounds already include all vertex frames.
    auto mesh = std::make_unique<AnimationMesh>();
    mesh->bounds = frames.front()->getModel()->getBounds().box;
    for (const auto& instance : frames) {
        const auto frame_bounds = instance->getModel()->getBounds().box;
        mesh->radius = std::max(mesh->radius,
                                glm::distance(frame_bounds.minimum, frame_bounds.maximum) * 0.5f);
        mesh->bounds.include(frame_bounds);
    }
    mesh->meshes = std::move(frames);
    mesh->frame_count = count;
    mesh->single_instance = single;
    mesh->source_file = file.physical_path();
    return mesh;
} catch (const std::exception&) {
    return {};
}

MeshInstance* GetAniMeshFrame(AnimationMesh* mesh, unsigned char frame)
{
    assert(mesh);
    assert(frame < mesh->frame_count);
    if (!mesh->single_instance) return mesh->meshes[frame].get();
    MeshInstance* instance = mesh->meshes[0].get();
    instance->frame_index = frame;
    return instance;
}
