#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "wiz8/render/mesh_loader.h"
#include "wiz8/spatial/bounding_box.h"

class Mesh;
class MeshInstance;
namespace wiz8 {
class File;
}

/* One animated part's frames: shared meshes that are never placed in a scene
   themselves. The actor that plays the animation shows a frame on its own
   model instance. */
struct AnimationMesh {
    unsigned char frame_count{};
    // One mesh that carries every vertex frame, or one mesh per frame.
    bool vertex_frames = false;
    std::vector<PlacedMesh> frames;
    BoundingBox bounds;
    float radius{};
    // The file the frames came from; a death cycle's name marks it as a gib.
    std::filesystem::path source_file;
};

// A frame of an animation: the mesh to draw, the vertex frame within it, and
// where its record places it.
struct AnimationFrame {
    std::shared_ptr<Mesh> mesh;
    unsigned int vertex_frame = 0;
    const Transform3D* placement = nullptr;
    explicit operator bool() const { return mesh != nullptr; }
};

std::optional<AnimationMesh> ReadAnimatedMesh(wiz8::File& file,
                                              const std::filesystem::path& texture_directory);
AnimationFrame GetAniMeshFrame(const AnimationMesh& mesh, unsigned char frame);
// Draw `frame` with `instance`. Changing to another mesh starts the instance from
// that mesh's placement, as a separate instance per frame did.
void ShowFrame(MeshInstance& instance, const AnimationFrame& frame);
