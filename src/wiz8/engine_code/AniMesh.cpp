#include "wiz8/engine_code/AniMesh.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "ReadMesh.h"
#include "glm/geometric.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/srModel.h"
#include "surrender/srNode.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"
#include "wiz8/sr_api.h"

/* VC6 places `float x = 0.0f` in .bss; retail keeps this slot in initialized
   .data next to g_float_one. Const storage lands in .rdata with physical
   zeros so datacmp agrees. */
// GLOBAL: WIZ8 0x005ebb34
const float g_float_zero = 0.0f;

#define ANI_MESH_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\AniMesh.cpp"

// GLOBAL: WIZ8 0x005ebe80
const double g_double_half = 0.5;
// GLOBAL: WIZ8 0x005ebb40
extern const double g_double_zero = 0.0;

/* AniMesh cache: a generation stamp, the loaded-byte total, the 16 MiB primary
   limit, the unused 1 MiB secondary limit the initializer still writes, and
   the list EnforceAniMeshMemoryLimit walks. */
// GLOBAL: WIZ8 0x0065be80
int g_animesh_cache_stamp;
// GLOBAL: WIZ8 0x0065be84
int g_animesh_cache_bytes;
// GLOBAL: WIZ8 0x0065be88
int g_animesh_cache_limit;
// GLOBAL: WIZ8 0x0065be8c
int g_animesh_cache_secondary_limit;
// GLOBAL: WIZ8 0x0065be90
std::vector<W8AniMesh*> g_animesh_cache_list;

/* The two caller-provided values override the original 16 MiB and 1 MiB
   defaults only when positive. Startup passes -1 for both. */
// FUNCTION: WIZ8 0x004b5780
void InitializeAniMeshCache(int primary_limit, int secondary_limit)
{
    g_animesh_cache_stamp = 0;
    g_animesh_cache_bytes = 0;
    g_animesh_cache_limit = 0x1000000;
    if (primary_limit > 0) {
        g_animesh_cache_limit = primary_limit;
    }
    g_animesh_cache_secondary_limit = 0x100000;
    if (secondary_limit > 0) {
        g_animesh_cache_secondary_limit = secondary_limit;
    }
    g_animesh_cache_list.clear();
    g_animesh_cache_list.reserve(10);
}

/* Releases every cached ani-mesh entry and clears the list. */
// FUNCTION: WIZ8 0x004B57D0
void FreeAniMeshCache(void)
{
    g_animesh_cache_list.clear();
}

// FUNCTION: WIZ8 0x004b57e0
W8AniMesh::W8AniMesh() = default;

W8AniMesh::W8AniMesh(const W8AniMesh& other)
    : flags(other.flags), frame_count(other.frame_count), bounds_minimum(other.bounds_minimum),
      bounds_maximum(other.bounds_maximum), radius(other.radius), loaded_bytes(other.loaded_bytes),
      list_index(other.list_index), bitmap_directory(other.bitmap_directory),
      filename(other.filename), file_offset(other.file_offset), world(other.world),
      last_used(other.last_used)
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

bool LoadAniMeshFrameCount(wiz8::File* file, W8AniMesh* mesh);

// FUNCTION: WIZ8 0x004b5b30
bool LoadAniMeshFromInfo(W8ReadLevelInfo* info, W8AniMesh* mesh, bool load_all)
{
    if (info == 0 || mesh == 0) {
        srAssertFail("pInfo&&pAniMesh", ANI_MESH_CPP, 0x105, 0);
    }
    if (info == 0 || mesh == 0) return 0;
    mesh->bitmap_directory = info->bitmap_folder;
    mesh->world = info->world;
    mesh->flags = {};
    mesh->last_used = 0;
    if (info->mesh_filename != 0) mesh->filename = info->mesh_filename;
    mesh->file_offset = info->hFile->tell();
    if (load_all == 0) {
        bool result = LoadAniMeshFrameCount(info->hFile, mesh);
        mesh->flags.frame_count_loaded = true;
        mesh->flags.keep_loaded = true;
        return result;
    }
    return LoadAniMesh(info->hFile, mesh, true);
}

// FUNCTION: WIZ8 0x004b5c10
float GetAniMeshFrameRadius(W8AniMesh* mesh, unsigned char frame)
{
    auto* instance = GetAniMeshFrame(mesh, frame);
    if (!instance || !instance->getModel()) return g_float_zero;
    const auto bounds = instance->getModel()->getBounds().box;
    return static_cast<float>(glm::distance(bounds.minimum, bounds.maximum) * g_double_half);
}

// FUNCTION: WIZ8 0x004b5d00
bool LoadAniMesh(wiz8::File* file, W8AniMesh* mesh, bool load_all)
try {
    std::unique_ptr<wiz8::File> opened;
    wiz8::File* handle = file;
    std::string instance_name;
    unsigned char frame_count;
    unsigned char frame_index;
    unsigned char loaded_count;
    std::unique_ptr<stModelInstance> loaded_instance;
    W8ReadLevelInfo info;

    if (handle == 0) {
        opened = [&]() {
            try {
                return wiz8::open_file(mesh->filename.c_str(), wiz8::OpenMode::read);
            } catch (const std::exception&) {
                return std::unique_ptr<wiz8::File>{};
            }
        }();
        handle = opened.get();
        if (handle == 0) {
            srAssertFail("0", ANI_MESH_CPP, 0x199, fmt::format("Couldn't open {}", mesh->filename));
            return 0;
        }
    }

    info.world = mesh->world;
    info.hFile = handle;
    info.bitmap_folder = mesh->bitmap_directory.c_str();
    if (file == 0) {
        handle->seek(mesh->file_offset, wiz8::SeekOrigin::begin);
    }

    if (!(handle->read(&frame_count, sizeof(frame_count)).bytes ==
          static_cast<std::size_t>(sizeof(frame_count)))) {
        srAssertFail("fSuccess", ANI_MESH_CPP, 0x1ad, 0);
        if (opened) opened->close();
        opened.reset();
        return 0;
    }
    if (frame_count == 0) return false;
    mesh->frame_count = frame_count;
    mesh->flags.frame_count_loaded = true;

    if (!mesh->filename.empty()) {
        instance_name = mesh->filename + "::0::" + std::to_string(mesh->list_index);
    }

    if (!(handle->read(&frame_index, sizeof(frame_index)).bytes ==
          static_cast<std::size_t>(sizeof(frame_index))) ||
        !(loaded_instance = ReadSingleLevelMesh(
              &info, instance_name.empty() ? nullptr : instance_name.c_str(), true))) {
        srAssertFail("fSuccess", ANI_MESH_CPP, 0x1c5, 0);

        if (opened) opened->close();
        opened.reset();
        return 0;
    }

    loaded_instance->setName("AniMeshReallyReadFromFile");
    auto instance = std::move(loaded_instance);
    stMeshModel* model = static_cast<stMeshModel*>(instance->getModel());

    if (model->frames.size() > 1) {
        mesh->flags.single_instance = true;
        mesh->meshes.clear();
        mesh->meshes.push_back(std::move(instance));
    } else {
        mesh->meshes.clear();
        mesh->meshes.resize(frame_count);
        mesh->meshes[0] = std::move(instance);

        loaded_count = 1;
        while (loaded_count < frame_count) {
            loaded_instance = 0;
            if (!instance_name.empty()) {
                instance_name = mesh->filename + "::" + std::to_string(loaded_count) +
                                "::" + std::to_string(mesh->list_index);
            }
            if (!load_all ||
                !(handle->read(&frame_index, sizeof(frame_index)).bytes ==
                  static_cast<std::size_t>(sizeof(frame_index))) ||
                !(loaded_instance = ReadSingleLevelMesh(
                      &info, instance_name.empty() ? nullptr : instance_name.c_str(), load_all))) {
                srAssertFail("fSuccess", ANI_MESH_CPP, 0x1f1, 0);

                if (opened) opened->close();
                opened.reset();
                return 0;
            }
            if (frame_index >= frame_count) {

                if (opened) opened->close();
                opened.reset();
                return 0;
            }
            loaded_instance->setName("AniMeshReallyReadFromFile");
            mesh->meshes[frame_index] = std::move(loaded_instance);
            ++loaded_count;
        }
    }

    mesh->flags.loaded = true;
    mesh->last_used = g_animesh_cache_stamp++;
    g_animesh_cache_bytes += mesh->loaded_bytes;

    mesh->radius = 0.0f;
    for (frame_index = 0; frame_index < mesh->frame_count; ++frame_index) {
        float radius = GetAniMeshFrameRadius(mesh, frame_index);
        if (mesh->radius <= radius) {
            mesh->radius = radius;
        }
    }

    srBoundingBox bounds;
    bool has_bounds = false;
    for (frame_index = 0; frame_index < mesh->frame_count; ++frame_index) {
        auto* instance = GetAniMeshFrame(mesh, frame_index);
        if (!instance || !instance->getModel()) continue;
        const auto frame_bounds = instance->getModel()->getBounds().box;
        if (has_bounds)
            bounds.include(frame_bounds);
        else
            bounds = frame_bounds;
        has_bounds = true;
    }
    mesh->bounds_minimum = bounds.minimum;
    mesh->bounds_maximum = bounds.maximum;
    mesh->flags.radius_loaded = true;

    if (file == 0) {
        if (opened) opened->close();
        opened.reset();
    }

    if (mesh->flags.keep_loaded) {
        g_animesh_cache_list.push_back(mesh);
    }
    EnforceAniMeshMemoryLimit(mesh);
    return 1;
} catch (const std::exception&) {
    return false;
}

/* Hands back the mesh's cached bounding box, loading the mesh first if it has
   not been. Touching it also stamps the storage clock, so asking for bounds
   counts as a use for the memory-pressure walk. */
// FUNCTION: WIZ8 0x004b6640
std::optional<srBoundingBox> GetAniMeshBounds(W8AniMesh* mesh)
{
    if (!mesh || (!mesh->flags.radius_loaded && !LoadAniMesh(0, mesh, true))) return std::nullopt;
    mesh->last_used = g_animesh_cache_stamp++;
    return srBoundingBox{mesh->bounds_minimum, mesh->bounds_maximum};
}

// FUNCTION: WIZ8 0x004b5880
W8AniMesh::~W8AniMesh()
{
    std::erase(g_animesh_cache_list, this);
    if (flags.loaded) UnloadAniMesh(this, true);
}

// FUNCTION: WIZ8 0x004b6290
bool LoadAniMeshFrameCount(wiz8::File* file, W8AniMesh* mesh)
try {
    std::unique_ptr<wiz8::File> opened;
    wiz8::File* handle = file;
    unsigned char frame_count, frame_index, loaded_count;
    bool success;
    W8ReadLevelInfo info;
    if (handle == 0) {
        opened = [&]() {
            try {
                return wiz8::open_file(mesh->filename.c_str(), wiz8::OpenMode::read);
            } catch (const std::exception&) {
                return std::unique_ptr<wiz8::File>{};
            }
        }();
        handle = opened.get();
        if (handle == 0) {
            srAssertFail("fi.hFile", ANI_MESH_CPP, 0x23f,
                         fmt::format("Couldn't open {}", mesh->filename));
            return 0;
        }
    }
    info.world = mesh->world;
    info.hFile = handle;
    info.bitmap_folder = mesh->bitmap_directory.c_str();
    info.mesh_filename = mesh->filename.c_str();
    if (file == 0) handle->seek(mesh->file_offset, wiz8::SeekOrigin::begin);
    success = (handle->read(&frame_count, 1).bytes == static_cast<std::size_t>(1));
    if (success == 0) {
        srAssertFail("fSuccess", ANI_MESH_CPP, 0x253, 0);
        if (opened) opened->close();
        opened.reset();
        return 0;
    }
    mesh->flags.frame_count_loaded = true;
    mesh->frame_count = frame_count;
    for (loaded_count = 0; loaded_count < frame_count; ++loaded_count) {
        if (success != 0)
            success = (handle->read(&frame_index, 1).bytes == static_cast<std::size_t>(1));
        if (success != 0 && SkipSingleLevelMesh(&info) == 2) break;
    }
    if (file == 0) {
        if (opened) opened->close();
        opened.reset();
    }
    return success;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004b63f0
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

// FUNCTION: WIZ8 0x004b6550
stModelInstance* GetAniMeshFrame(W8AniMesh* mesh, unsigned char frame)
{
    stModelInstance* instance;
    char message[0x80];

    if (mesh == 0 || frame >= mesh->frame_count) {
        sprintf(message, "AniMeshGetMeshForFrame error, frame %d, num %d", frame,
                mesh->frame_count);
        srAssertFail("0", ANI_MESH_CPP, 0x2e6, message);
        return 0;
    }
    if (!mesh->flags.loaded && LoadAniMesh(0, mesh, true) == 0) {
        srAssertFail("0", ANI_MESH_CPP, 0x2ee, 0);
        return 0;
    }
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

// FUNCTION: WIZ8 0x004b64f0
unsigned char AniMeshValue(W8AniMesh* mesh)
{
    if (mesh == 0) {
        srAssertFail("pAniMesh", ANI_MESH_CPP, 0x2be, 0);
        return 0xff;
    }
    if (!mesh->flags.frame_count_loaded) {
        if (LoadAniMesh(0, mesh, true) == 0) {
            srAssertFail("0", ANI_MESH_CPP, 0x2c6, 0);
            return 0xff;
        }
    }
    return mesh->frame_count;
}

// FUNCTION: WIZ8 0x004b66e0
bool AniMeshRadius(W8AniMesh* mesh, float* radius)
{
    if (mesh == 0 || radius == 0) {
        srAssertFail("pAniMesh&&pflRadius", ANI_MESH_CPP, 0x348, 0);
    }
    if (mesh != 0 && radius != 0) {
        if (!mesh->flags.radius_loaded) {
            if (LoadAniMesh(0, mesh, true) == 0) {
                srAssertFail("0", ANI_MESH_CPP, 0x350, 0);
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

// FUNCTION: WIZ8 0x004b6860
void SetAniMeshCacheProtected(W8AniMesh* mesh, bool enabled)
{
    if (mesh == 0) {
        srAssertFail("pAniMesh", ANI_MESH_CPP, 0x3b7, 0);
    }
    if (enabled) {
        mesh->flags.cache_protected = true;
        return;
    }
    mesh->flags.cache_protected = false;
}

// FUNCTION: WIZ8 0x004b6770
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
