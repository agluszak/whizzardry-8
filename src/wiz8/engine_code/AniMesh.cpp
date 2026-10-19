#include "wiz8/engine_code/AniMesh.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "ReadMesh.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "surrender/srModel.h"
#include "surrender/srNode.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"

/* AniMesh cache: a generation stamp, the loaded-byte total, the 16 MiB primary
   limit, the unused 1 MiB secondary limit the initializer still writes, and
   the list EnforceAniMeshMemoryLimit walks. */
int g_animesh_cache_stamp;
int g_animesh_cache_bytes;
int g_animesh_cache_limit;
std::vector<W8AniMesh*> g_animesh_cache_list;

/* The two caller-provided values override the original 16 MiB and 1 MiB
   defaults only when positive. Startup passes -1 for both. */
void InitializeAniMeshCache(int primary_limit, int secondary_limit)
{
    g_animesh_cache_stamp = 0;
    g_animesh_cache_bytes = 0;
    g_animesh_cache_limit = 0x1000000;
    if (primary_limit > 0) {
        g_animesh_cache_limit = primary_limit;
    }
    if (secondary_limit > 0) {
    }
    g_animesh_cache_list.clear();
    g_animesh_cache_list.reserve(10);
}

/* Releases every cached ani-mesh entry and clears the list. */
void FreeAniMeshCache(void)
{
    g_animesh_cache_list.clear();
}

W8AniMesh::W8AniMesh() = default;

W8AniMesh::W8AniMesh(const W8AniMesh& other)
    : flags(other.flags), frame_count(other.frame_count), bounds_minimum(other.bounds_minimum),
      bounds_maximum(other.bounds_maximum), radius(other.radius), loaded_bytes(other.loaded_bytes),
      bitmap_directory(other.bitmap_directory), source_file(other.source_file),
      file_offset(other.file_offset), last_used(other.last_used)
{
    meshes.reserve(other.meshes.size());
    for (const auto& source : other.meshes) {
        auto copy = source ? DuplicateModelInstance(*source) : nullptr;
        if (copy) copy->setName("Ani Mesh Duplicate Instance");
        meshes.push_back(std::move(copy));
    }
    if (flags.loaded) {
        if (flags.keep_loaded) g_animesh_cache_list.push_back(this);
        g_animesh_cache_bytes += loaded_bytes;
    }
}

namespace {
bool ReadAnimatedFrames(wiz8::File& file, W8AniMesh& mesh);
bool ReloadAnimatedMesh(W8AniMesh& mesh);
}

std::unique_ptr<W8AniMesh>
ReadAnimatedMesh(wiz8::File& file, const std::filesystem::path& texture_directory, bool load_all)
try {
    auto mesh = std::make_unique<W8AniMesh>();
    mesh->bitmap_directory = texture_directory;
    mesh->source_file = file.physical_path();
    mesh->file_offset = file.tell();
    if (load_all) {
        if (!ReadAnimatedFrames(file, *mesh)) return {};
    } else {
        unsigned char count;
        file.read_exact(&count, sizeof(count));
        if (!count) return {};
        for (unsigned int index = 0; index < count; ++index) {
            unsigned char frame;
            file.read_exact(&frame, sizeof(frame));
            if (frame >= count) return {};
            if (ReadSingleLevelMeshHasVertexFrames(file)) break;
        }
        mesh->frame_count = count;
        mesh->flags.frame_count_loaded = true;
        mesh->flags.keep_loaded = true;
    }
    return mesh;
} catch (const std::exception&) {
    return {};
}

namespace {
// Decode the full frame sequence into detached ownership before publishing it.
// Source offsets identify resources; frame/channel labels are unnecessary.
bool ReadAnimatedFrames(wiz8::File& file, W8AniMesh& mesh)
try {
    unsigned char count;
    file.read_exact(&count, sizeof(count));
    if (!count) return false;
    unsigned char first_index;
    file.read_exact(&first_index, sizeof(first_index));
    if (first_index >= count) return false;
    auto first = ReadSingleLevelMesh(file, mesh.bitmap_directory);
    if (!first) return false;
    auto* model = static_cast<stMeshModel*>(first->getModel());
    const bool single = model->frames.size() > 1;
    std::vector<std::unique_ptr<stModelInstance>> frames(single ? 1 : count);
    frames[single ? 0 : first_index] = std::move(first);
    if (!single) {
        for (unsigned int index = 1; index < count; ++index) {
            unsigned char frame;
            file.read_exact(&frame, sizeof(frame));
            if (frame >= count || frames[frame]) return false;
            frames[frame] = ReadSingleLevelMesh(file, mesh.bitmap_directory);
            if (!frames[frame]) return false;
        }
    }
    // Model bounds already include all vertex frames. Inspect the detached
    // instances directly: selecting frames here would change playback and LRU
    // state merely to compute this summary.
    srBoundingBox bounds;
    bool has_bounds = false;
    float radius = 0;
    for (const auto& instance : frames) {
        const auto frame_bounds = instance->getModel()->getBounds().box;
        radius = std::max(radius, glm::distance(frame_bounds.minimum, frame_bounds.maximum) * 0.5f);
        if (has_bounds)
            bounds.include(frame_bounds);
        else
            bounds = frame_bounds;
        has_bounds = true;
    }

    mesh.meshes = std::move(frames);
    mesh.frame_count = count;
    mesh.flags.single_instance = single;
    mesh.flags.frame_count_loaded = true;
    mesh.flags.loaded = true;
    mesh.last_used = g_animesh_cache_stamp++;
    g_animesh_cache_bytes += mesh.loaded_bytes;
    mesh.radius = radius;
    mesh.bounds_minimum = bounds.minimum;
    mesh.bounds_maximum = bounds.maximum;
    mesh.flags.radius_loaded = true;
    if (mesh.flags.keep_loaded) g_animesh_cache_list.push_back(&mesh);
    EnforceAniMeshMemoryLimit(&mesh);
    return true;
} catch (const std::exception&) {
    return false;
}

bool ReloadAnimatedMesh(W8AniMesh& mesh)
try {
    auto file = wiz8::open_file(mesh.source_file);
    file->seek(mesh.file_offset, wiz8::SeekOrigin::begin);
    return ReadAnimatedFrames(*file, mesh);
} catch (const std::exception&) {
    return false;
}
} // namespace

/* Hands back the mesh's cached bounding box, loading the mesh first if it has
   not been. Touching it also stamps the storage clock, so asking for bounds
   counts as a use for the memory-pressure walk. */
std::optional<srBoundingBox> GetAniMeshBounds(W8AniMesh* mesh)
{
    if (!mesh || (!mesh->flags.radius_loaded && !ReloadAnimatedMesh(*mesh))) return std::nullopt;
    mesh->last_used = g_animesh_cache_stamp++;
    return srBoundingBox{mesh->bounds_minimum, mesh->bounds_maximum};
}

W8AniMesh::~W8AniMesh()
{
    std::erase(g_animesh_cache_list, this);
    if (flags.loaded) UnloadAniMesh(this, true);
}

bool UnloadAniMesh(W8AniMesh* mesh, bool force)
{
    if (!mesh || (!force && !mesh->flags.keep_loaded)) return false;
    if (!mesh->flags.single_instance) {
        for (const auto& instance : mesh->meshes) {
            if (!instance) continue;
            instance->setParent(nullptr, 1);
            instance->setFlag(srNode::FLAG_DISABLE);
        }
    }
    if (mesh->flags.loaded) g_animesh_cache_bytes -= mesh->loaded_bytes;
    mesh->meshes.clear();
    mesh->flags.loaded = false;
    return true;
}

stModelInstance* GetAniMeshFrame(W8AniMesh* mesh, unsigned char frame)
{
    if (!mesh) {
        Fail("mesh");
        return nullptr;
    }
    if (frame >= mesh->frame_count) {
        const auto message =
            fmt::format("AniMeshGetMeshForFrame error, frame {}, num {}", frame, mesh->frame_count);
        Fail("frame < mesh->frame_count", message.c_str());
        return nullptr;
    }
    if (!mesh->flags.loaded && !ReloadAnimatedMesh(*mesh)) {
        Fail("0");
        return 0;
    }
    stModelInstance* instance;
    if (mesh->flags.single_instance) {
        instance = mesh->meshes[0].get();
        instance->frame_index = frame;
    } else {
        instance = mesh->meshes[frame].get();
    }
    mesh->last_used = g_animesh_cache_stamp;
    ++g_animesh_cache_stamp;
    return instance;
}

unsigned char AniMeshValue(W8AniMesh* mesh)
{
    if (mesh == 0) {
        Fail("pAniMesh");
        return 0xff;
    }
    if (!mesh->flags.frame_count_loaded) {
        if (!ReloadAnimatedMesh(*mesh)) {
            Fail("0");
            return 0xff;
        }
    }
    return mesh->frame_count;
}

bool AniMeshRadius(W8AniMesh* mesh, float* radius)
{
    if (mesh == 0 || radius == 0) {
        Fail("pAniMesh&&pflRadius");
    }
    if (mesh != 0 && radius != 0) {
        if (!mesh->flags.radius_loaded) {
            if (!ReloadAnimatedMesh(*mesh)) {
                Fail("0");
                return false;
            }
        }
        *radius = mesh->radius;
        mesh->last_used = g_animesh_cache_stamp;
        ++g_animesh_cache_stamp;
        return true;
    }
    return false;
}

void SetAniMeshCacheProtected(W8AniMesh* mesh, bool enabled)
{
    if (mesh == 0) {
        Fail("pAniMesh");
    }
    if (enabled) {
        mesh->flags.cache_protected = true;
        return;
    }
    mesh->flags.cache_protected = false;
}

void EnforceAniMeshMemoryLimit(W8AniMesh* current)
{
    while (g_animesh_cache_bytes > g_animesh_cache_limit) {
        W8AniMesh* oldest = nullptr;
        for (auto* candidate : g_animesh_cache_list) {
            if (!candidate || candidate == current || candidate->flags.cache_protected) continue;
            if (!oldest || static_cast<unsigned int>(candidate->last_used) <
                               static_cast<unsigned int>(oldest->last_used)) {
                oldest = candidate;
            }
        }
        if (!oldest || !UnloadAniMesh(oldest, false)) return;
        std::erase(g_animesh_cache_list, oldest);
    }
}
