#include "wiz8/engine_code/LevelFile.h"

#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "materials.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/evidence/LevelFile_layout.inc"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"
#include "wiz8/local_screens/AutomapScreen.h"

#define LEVELFILE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\LevelFile.cpp"

namespace {

/* Build limits the level builder enforces on trigger parts. */
constexpr std::size_t kMaxSwitchTriggers = 1000;
constexpr std::size_t kMaxTriggerPlanes = 1000;
constexpr std::size_t kMaxLinkedRecords = 100;
constexpr int kMaxMeshVertices = 100000;
constexpr int kMaxMeshFaces = 200000;
constexpr int kMaxTransformMaterials = 500;
/* Material records before version 4 stop short of the texture modes. */
constexpr std::size_t kMaterialRecordV3Bytes = offsetof(W8MaterialRecord, texture_modes);
/* Particle systems before version 2 stop short of the attachment key. */
constexpr std::size_t kParticleSystemV1Bytes =
    offsetof(W8LevelFileParticleSystem, particle) + offsetof(W8LevelParticleRecord, attachment_key);

std::runtime_error LevelError(const std::string& message)
{
    return std::runtime_error("Level file: " + message);
}

/* Each record needs at least one byte, and the total must stay addressable. */
void ValidateRecordCount(std::int64_t count, std::size_t stride, const wiz8::File& file)
{
    const auto remaining = file.size() - file.tell();
    if (count < 0 || remaining < 0 || count > remaining ||
        static_cast<std::uint64_t>(count) > static_cast<std::uint64_t>(INT_MAX) / stride)
        throw LevelError("invalid record count");
}

/* Narrow a container size to its on-disk count type. */
template<class Count> Count DiskCount(std::size_t size)
{
    if (size > static_cast<std::size_t>(std::numeric_limits<Count>::max()))
        throw LevelError("too many records to write");
    return static_cast<Count>(size);
}

/* The two directions of a field-by-field transfer. Record transfers are
   written once against either; a writer sees its records as const. */
struct LevelReader {
    static constexpr bool reading = true;
    wiz8::File& file;

    void field(glm::vec3& value)
    {
        field(value.x);
        field(value.y);
        field(value.z);
    }
    void array(std::vector<glm::vec3>& values, std::int64_t count)
    {
        ValidateRecordCount(count, 3 * sizeof(float), file);
        values.resize(static_cast<std::size_t>(count));
        for (auto& value : values) field(value);
    }
    template<class T> void field(T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        file.read_exact(&value, sizeof(value));
    }
    template<class T> void bytes(T& value, std::size_t size)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        file.read_exact(&value, size);
    }
    template<std::size_t N> void text(char (&value)[N])
    {
        field(value);
        if (!std::memchr(value, '\0', N)) throw LevelError("unterminated text");
    }
    template<class T> void array(std::vector<T>& values, std::int64_t count)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        ValidateRecordCount(count, sizeof(T), file);
        values.resize(static_cast<std::size_t>(count));
        file.read_exact(values.data(), values.size() * sizeof(T));
    }
    /* Make room for `count` records each transferred by the caller. */
    template<class T> void records(std::vector<T>& values, std::int64_t count)
    {
        ValidateRecordCount(count, sizeof(T), file);
        values.resize(static_cast<std::size_t>(count));
    }
    template<class T> T& present(std::optional<T>& value) { return value.emplace(); }
};

struct LevelWriter {
    static constexpr bool reading = false;
    wiz8::File& file;

    void field(const glm::vec3& value)
    {
        field(value.x);
        field(value.y);
        field(value.z);
    }
    void array(const std::vector<glm::vec3>& values, std::int64_t count)
    {
        if (static_cast<std::int64_t>(values.size()) != count)
            throw LevelError("record count mismatch");
        for (const auto& value : values) field(value);
    }
    template<class T> void field(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        file.write(&value, sizeof(value));
    }
    template<class T> void bytes(const T& value, std::size_t size) { file.write(&value, size); }
    template<std::size_t N> void text(const char (&value)[N]) { field(value); }
    template<class T> void array(const std::vector<T>& values, std::int64_t count)
    {
        if (static_cast<std::int64_t>(values.size()) != count)
            throw LevelError("record count mismatch");
        file.write(values.data(), values.size() * sizeof(T));
    }
    template<class T> void records(const std::vector<T>&, std::int64_t) {}
    template<class T> const T& present(const std::optional<T>& value)
    {
        if (!value) throw LevelError("flagged record is missing");
        return *value;
    }
};

/* A count field followed by its fixed-width records. */
template<class Count, class IO, class Values> void TransferArray(IO& io, Values& values)
{
    Count count{};
    if constexpr (!IO::reading) count = DiskCount<Count>(values.size());
    io.field(count);
    io.array(values, count);
}

/* A count field followed by records the caller transfers one by one. */
template<class Count, class IO, class Values, class Transfer>
void TransferRecords(IO& io, Values& values, Transfer&& transfer)
{
    Count count{};
    if constexpr (!IO::reading) count = DiskCount<Count>(values.size());
    io.field(count);
    io.records(values, count);
    for (auto& value : values) transfer(io, value);
}

template<class Record, class Transfer> bool ReadRecord(wiz8::File& file, Record& record,
                                                       Transfer&& transfer)
try {
    Record loaded{};
    LevelReader io{file};
    transfer(io, loaded);
    record = std::move(loaded);
    return true;
} catch (const std::exception&) {
    record = Record{};
    return false;
}

template<class Record, class Transfer>
bool WriteRecord(wiz8::File& file, const Record& record, Transfer&& transfer)
try {
    LevelWriter io{file};
    transfer(io, record);
    return true;
} catch (const std::exception&) {
    return false;
}

template<class IO, class Material> void TransferMaterial(IO& io, Material& material)
{
    io.bytes(material, kMaterialRecordV3Bytes);
    if (material.version >= 4) io.field(material.texture_modes);
}

template<class IO, class Path> void TransferPathAI(IO& io, Path& path)
{
    io.field(path.version);
    io.field(path.scaled);
    io.field(path.position);
    io.field(path.unknown_06);
    if (path.IsScaled()) {
        TransferArray<int>(io, path.scaled_paths);
    } else {
        TransferArray<int>(io, path.paths);
    }
}

template<class IO, class Frames> void TransferLods(IO& io, Frames& frames, std::int64_t frame_size)
{
    for (auto& frame : frames) io.array(frame, frame_size);
}

template<class IO, class Mesh> void TransferMesh(IO& io, Mesh& mesh)
{
    io.field(mesh.version);
    io.field(mesh.num_vertices);
    io.field(mesh.num_faces);
    if constexpr (IO::reading) {
        ValidateRecordCount(mesh.num_vertices, 6 * sizeof(float), io.file);
        ValidateRecordCount(mesh.num_faces, 2 * sizeof(W8SerializedMeshFace), io.file);
        if (mesh.num_vertices <= 0 || mesh.num_vertices > kMaxMeshVertices) {
            ReportBuildStatus(7, fmt::format("Invalid number of vertices in mesh ({} vertices).\n",
                                             mesh.num_vertices));
            throw LevelError("invalid mesh vertex count");
        }
        if (mesh.num_faces <= 0 || mesh.num_faces > kMaxMeshFaces) {
            ReportBuildStatus(
                7, fmt::format("Invalid number of faces in mesh ({} faces).\n", mesh.num_faces));
            throw LevelError("invalid mesh face count");
        }
    }
    if (mesh.version >= 3) io.field(mesh.flags);
    if (mesh.version >= 2) {
        io.field(mesh.location);
        io.field(mesh.rotation_angle);
        io.field(mesh.rotation_axis);
        io.field(mesh.scale);
    }
    if (mesh.version >= 4) {
        io.field(mesh.mapping_count);
        // One mapping fits the record; more would need an expanded one.
        if (mesh.mapping_count < 0 || mesh.mapping_count > 1)
            throw LevelError("unsupported mesh mappings");
        if (mesh.mapping_count != 0) {
            io.field(mesh.mapped_value);
            io.field(mesh.mapped_key);
        }
    }

    if (!mesh.HasLods()) {
        io.array(mesh.vertices, mesh.num_vertices);
    } else {
        io.field(mesh.lod_mode);
        short lod_count{};
        if constexpr (!IO::reading) lod_count = DiskCount<short>(mesh.LodCount());
        io.field(lod_count);
        if constexpr (IO::reading) {
            ValidateRecordCount(lod_count, sizeof(void*), io.file);
            if (lod_count == 0) throw LevelError("LOD mesh without LODs");
        }
        // The reader takes the LOD scale only for short LODs; the .pvl writer
        // emits it for every LOD mode >= 2.
        if (mesh.lod_mode >= 2 && (!IO::reading || mesh.HasShortLods())) {
            io.field(mesh.lod_scale);
            if constexpr (IO::reading) {
                if (!std::isfinite(mesh.lod_scale) || mesh.lod_scale <= 0)
                    throw LevelError("invalid LOD scale");
            }
        }
        if (mesh.HasShortLods()) {
            if constexpr (IO::reading) mesh.lod_shorts.resize(lod_count);
            TransferLods(io, mesh.lod_shorts, std::int64_t{mesh.num_vertices} * 3);
        } else {
            if constexpr (IO::reading) mesh.lods.resize(lod_count);
            TransferLods(io, mesh.lods, mesh.num_vertices);
        }
    }

    if (mesh.HasCompressedFaces()) {
        io.array(mesh.compressed_faces, mesh.num_faces);
        if constexpr (IO::reading) {
            for (const auto& face : mesh.compressed_faces)
                for (auto vertex : face.vertex_indices)
                    if (vertex >= mesh.num_vertices) throw LevelError("face vertex out of range");
        }
    } else {
        io.array(mesh.faces, mesh.num_faces);
        if constexpr (IO::reading) {
            for (const auto& face : mesh.faces)
                for (int vertex : face.vertices)
                    if (vertex < 0 || vertex >= mesh.num_vertices)
                        throw LevelError("face vertex out of range");
        }
    }
}

template<class IO, class Light> void TransferLight(IO& io, Light& light)
{
    io.field(light.version);
    io.field(light.create);
    io.field(light.visible);
    io.field(light.flags);
    io.field(light.unknown_06);
    io.field(light.position);
    io.field(light.colour);
    io.field(light.intensity);
    io.field(light.range);
    if (light.version < 2) return;
    io.field(light.name);
    if ((light.flags & W8_LEVEL_LIGHT_HAS_DEFINITION) == 0) return;
    if constexpr (IO::reading) light.create = 1; // A defined light is always a created one.
    auto& extra = io.present(light.extra);
    io.field(extra);
    if ((extra.flags & W8_PARAM_LIGHT_HAS_PATH) != 0) TransferPathAI(io, io.present(light.path_ai));
}

template<class IO, class Light> void TransferAnimLight(IO& io, Light& light)
{
    io.field(light.version);
    io.field(light.position);
    io.field(light.color);
    io.field(light.intensity);
    io.field(light.range);
    if (light.version >= 2) io.field(io.present(light.extra));
}

template<class IO, class Door> void TransferDoorRecord(IO& io, Door& door)
{
    io.field(door.version);
    io.field(door.flags);
    io.field(door.item);
    io.field(door.has_position);
    io.field(door.position);
    io.field(door.linked_trigger);
}

/* A door reference's kind byte has already been transferred. */
template<class IO, class DoorRef> void TransferDoor(IO& io, DoorRef& ref)
{
    if (ref.kind == 1) TransferDoorRecord(io, io.present(ref.door));
}

template<class IO, class Record> void TransferLinkedRecord(IO& io, Record& record)
{
    io.field(record.kind);
    io.field(record.vertices);
    io.field(record.linked_face);
    io.field(record.flag);
}

template<class IO, class Switch> void TransferSwitch(IO& io, Switch& trigger)
{
    io.field(trigger.version);
    io.field(trigger.cycle_bounce);
    io.field(trigger.state_count);
    io.field(trigger.animate_states);
    io.field(trigger.range);
    io.field(trigger.action);
    io.field(trigger.value);
    io.field(trigger.animate_action);
    io.field(trigger.packed_flags);
    io.field(trigger.enabled);
    io.text(trigger.name);
    io.text(trigger.recipients);
    io.field(trigger.sound);
    if constexpr (IO::reading)
        ReportBuildStatus(5, fmt::format("Switch Trigger: {}, recipients: {}\n", trigger.name,
                                         trigger.recipients));
    if (trigger.version > 1) {
        io.field(trigger.minimum_range);
        io.text(trigger.surface_id);
        if constexpr (IO::reading)
            ReportBuildStatus(5, fmt::format("Switch Trigger name: {}\n", trigger.surface_id));
    }
    if (trigger.version > 2) {
        io.field(trigger.has_door_trigger);
        if (trigger.has_door_trigger != 0) {
            io.field(trigger.door.kind);
            TransferDoor(io, trigger.door);
        }
    }
    if (trigger.version > 3) io.field(trigger.action_value);
}

template<class IO, class Invisible> void TransferInvisible(IO& io, Invisible& trigger)
{
    io.field(trigger.version);
    io.field(trigger.range);
    io.field(trigger.position);
    io.field(trigger.action);
    io.field(trigger.searchable);
    io.field(trigger.fire_linked);
    io.field(trigger.enabled);
    io.text(trigger.name);
    io.text(trigger.recipients);
    if constexpr (IO::reading)
        ReportBuildStatus(5, fmt::format("Invisible Trigger: {}, recipients: {}\n", trigger.name,
                                         trigger.recipients));
    if (trigger.version > 1) {
        io.field(trigger.plane_flag);
        io.field(io.present(trigger.plane));
    }
    if (trigger.version > 2) {
        io.field(trigger.angle);
        io.field(trigger.direction);
        io.field(trigger.unused);
        io.field(trigger.action_string);
    }
    if (trigger.version > 3) {
        io.field(trigger.flag);
        io.field(trigger.action_value);
    }
    if (trigger.version > 4) {
        io.field(trigger.has_legacy_geometry);
        if (trigger.has_legacy_geometry != 0) io.field(trigger.geometry_kind);
    }
}

template<class IO, class Sound> void TransferSound(IO& io, Sound& trigger)
{
    io.field(trigger.version);
    io.field(trigger.volume_min);
    io.field(trigger.volume_max);
    io.field(trigger.speed_min);
    io.field(trigger.speed_max);
    io.field(trigger.time_min);
    io.field(trigger.time_max);
    io.field(trigger.unbounded);
    io.field(trigger.radius);
    io.field(trigger.position);
    io.field(trigger.region_u);
    io.field(trigger.region_v);
    io.field(trigger.wave);
    if (trigger.version > 1) {
        io.field(trigger.has_position);
        io.field(trigger.looping);
    }
    if (trigger.version > 2) {
        io.field(trigger.region_center);
        io.field(trigger.region_angle);
        io.field(trigger.region_min);
        io.field(trigger.region_max);
    }
    if (trigger.version > 3) {
        io.field(trigger.name);
        if constexpr (IO::reading)
            ReportBuildStatus(5, fmt::format("Sound Trigger: {}\n", trigger.name));
    }
    if (trigger.version > 4) io.field(trigger.shared);
}

template<class IO, class Super> void TransferSuper(IO& io, Super& trigger)
{
    io.field(trigger.version);
    io.text(trigger.name);
    io.field(trigger.flags);
    io.field(trigger.active);
    io.field(trigger.kind);
    io.field(trigger.when_active);
    io.field(trigger.prop_index);
    io.field(trigger.activation_count);
    io.field(trigger.inactive_count);
    io.field(trigger.trigger);
    io.field(trigger.trigger_on);
    io.field(trigger.trigger_off);
    io.text(trigger.recipients);
    io.field(trigger.ataxia_or_cure);
    io.field(trigger.ps_events);
    io.field(trigger.allow_save);
    io.field(trigger.price);
    io.field(trigger.door_kind);
    io.field(trigger.animation);
    if constexpr (IO::reading)
        ReportBuildStatus(5, fmt::format("Super Trigger: {}, recipients: {}\n", trigger.name,
                                         trigger.recipients));
    if (trigger.version >= 2) {
        io.field(trigger.size);
        io.field(trigger.direction);
        io.field(trigger.wait0);
        io.field(trigger.wait1);
        io.field(trigger.wait2);
        io.field(trigger.loop);
        io.field(trigger.speed);
        io.field(trigger.unknown_4b3);
    }
    io.field(trigger.ignore);
    io.field(trigger.group);
    io.field(trigger.set_group);
    io.field(trigger.groups);
    io.field(trigger.objects);
    io.field(trigger.close_door);
    io.field(trigger.wait3);
    io.field(trigger.field);
    io.field(trigger.event);
    io.field(trigger.normal_scale);
    if (trigger.version >= 3) io.field(trigger.particle_system);

    if ((trigger.flags & 1) == 0) {
        io.field(trigger.placement_kind);
        if (trigger.placement_kind == 1) {
            io.field(io.present(trigger.position));
        } else if (trigger.placement_kind == 2) {
            io.field(io.present(trigger.plane));
        }
        unsigned char has_hotspot = trigger.hotspot.has_value();
        io.field(has_hotspot);
        if (has_hotspot) io.field(io.present(trigger.hotspot));
    }

    io.field(trigger.has_door);
    if (trigger.has_door == 0) return;
    io.field(trigger.door.kind);
    if (trigger.door.kind == 2) {
        auto& record = io.present(trigger.record);
        TransferLinkedRecord(io, record);
        if constexpr (IO::reading) {
            record.normal_scale = trigger.normal_scale;
            record.forward_scale = trigger.direction;
        }
    } else {
        TransferDoor(io, trigger.door);
    }
}

/* The record a trigger's type selects, after the version and type bytes. */
template<class IO, class Trigger> void TransferTriggerBody(IO& io, Trigger& trigger)
{
    const auto transfer = [&]<class Record>(auto body) {
        if constexpr (IO::reading) {
            body(trigger.data.template emplace<Record>());
        } else {
            const auto* record = std::get_if<Record>(&trigger.data);
            if (!record) throw LevelError("trigger record does not match its type");
            body(*record);
        }
    };
    switch (trigger.type) {
    case W8_LEVEL_TRIGGER_SWITCH:
        transfer.template operator()<W8LevelFileSwitch>(
            [&](auto& record) { TransferSwitch(io, record); });
        break;
    case W8_LEVEL_TRIGGER_INVISIBLE:
        transfer.template operator()<W8LevelFileInvisible>(
            [&](auto& record) { TransferInvisible(io, record); });
        break;
    case W8_LEVEL_TRIGGER_SOUND:
        transfer.template operator()<W8LevelFileSound>(
            [&](auto& record) { TransferSound(io, record); });
        break;
    case W8_LEVEL_TRIGGER_SUPER:
        transfer.template operator()<W8LevelFileSuperTrigger>(
            [&](auto& record) { TransferSuper(io, record); });
        break;
    }
}

template<class IO, class Trigger> void TransferTrigger(IO& io, Trigger& trigger)
{
    io.field(trigger.version);
    io.field(trigger.type);
    TransferTriggerBody(io, trigger);
}

enum class FrameKind { morph, transform };

/* A channel's frames. A morph whose first frame is an LOD mesh stores only
   that frame; transforms store every frame. */
template<class IO, class Frames> void TransferFrames(IO& io, Frames& frames, int count,
                                                     FrameKind kind)
{
    io.records(frames, count);
    for (auto& frame : frames) {
        io.field(frame.flags);
        TransferMesh(io, frame.mesh);
        short texture_count{};
        if constexpr (!IO::reading) texture_count = DiskCount<short>(frame.textures.size());
        io.field(texture_count);
        if constexpr (IO::reading) {
            if (kind == FrameKind::transform && texture_count > kMaxTransformMaterials) {
                ReportBuildStatus(
                    7, fmt::format("Invalid number of materials in mesh ({} materials).\n",
                                   texture_count));
                throw LevelError("too many frame materials");
            }
        }
        io.records(frame.textures, texture_count);
        for (auto& material : frame.textures) TransferMaterial(io, material);
        if (kind == FrameKind::morph && frame.mesh.HasLods()) break;
    }
}

template<class IO, class AnimObj> void TransferAnimObj(IO& io, AnimObj& anim)
{
    io.field(anim.version);
    char animation_count{};
    if constexpr (!IO::reading) animation_count = DiskCount<char>(anim.channel_counts.size());
    io.field(animation_count);
    if constexpr (IO::reading)
        ValidateRecordCount(animation_count, sizeof(W8LevelFileMorph), io.file);
    io.field(anim.animation_playing);
    io.field(anim.frame_method);
    io.field(anim.behaviour);
    io.field(anim.cycle);
    io.field(anim.path_lists);
    if (anim.version >= 3) io.field(anim.playback_scale);
    if (anim.version >= 5) io.field(anim.start_frame);
    if (anim.version >= 6) {
        io.field(anim.random_play);
        io.field(anim.play_chance);
    }
    io.field(anim.discarded);
    io.array(anim.channel_counts, animation_count);
    if (anim.version >= 7) TransferArray<unsigned char>(io, anim.bounds);
    if (anim.version >= 8) {
        TransferRecords<signed char>(io, anim.anim_lights,
                                     [](auto& io, auto& light) { TransferAnimLight(io, light); });
    }
    if (anim.path_lists == 0) {
        if (anim.version >= 9) {
            unsigned char has_path_ai = anim.path_ai.has_value();
            io.field(has_path_ai);
            if (has_path_ai) TransferPathAI(io, io.present(anim.path_ai));
        }
        if constexpr (IO::reading) {
            anim.morphs.resize(static_cast<std::size_t>(animation_count));
        } else if (anim.morphs.size() != anim.channel_counts.size()) {
            throw LevelError("morph and animation counts differ");
        }
        for (auto& morph : anim.morphs) {
            io.field(morph.channel);
            io.field(morph.num_frames);
            TransferFrames(io, morph.frames, morph.num_frames, FrameKind::morph);
        }
    } else {
        TransferRecords<char>(io, anim.transforms, [](auto& io, auto& transform) {
            io.field(transform.channel);
            io.field(transform.num_frames);
            TransferFrames(io, transform.frames, transform.num_frames, FrameKind::transform);
            TransferPathAI(io, transform.pathAI);
        });
    }
}

template<class IO, class Prop> void TransferProp(IO& io, Prop& prop)
{
    io.field(prop.version);
    io.field(prop.frame_count);
    if (prop.version >= 5) {
        io.field(prop.option);
        io.field(prop.position);
    }
    if (prop.version >= 6) io.field(prop.flags);
    if (prop.version >= 7) {
        io.field(prop.name);
        if constexpr (IO::reading) ReportBuildStatus(5, fmt::format("Prop: {}\n", prop.name));
    }
    if (prop.version >= 8) TransferArray<char>(io, prop.frame_positions);
    TransferAnimObj(io, prop.anim_obj);
    char has_trigger = prop.trigger.has_value();
    io.field(has_trigger);
    if (has_trigger) TransferTrigger(io, io.present(prop.trigger));
    if (prop.version >= 9) {
        io.field(prop.has_footsteps);
        if (prop.has_footsteps != 0) {
            io.field(prop.footstep_surface);
            io.field(prop.footstep_material);
        }
    }
}

template<class IO, class System> void TransferParticleSystem(IO& io, System& system)
{
    io.bytes(system, kParticleSystemV1Bytes);
    auto& particle = system.particle;
    if (system.version > 1) io.field(particle.attachment_key);
    if (system.version > 2) {
        io.field(particle.emission_limit);
        io.field(particle.requires_sorted_renderer);
    }
    if (system.version > 3) {
        io.field(particle.start_frame);
        io.field(particle.end_frame);
    }
    if constexpr (IO::reading) {
        ReportBuildStatus(5, fmt::format("Particle System: {}, Position {:f}, {:f}, {:f}\n",
                                         particle.name, particle.location.x * g_world_scale,
                                         particle.location.y * g_world_scale,
                                         particle.location.z * g_world_scale));
    }
}

template<class IO, class Block> void TransferBlock(IO& io, Block& block)
{
    io.field(block.fog_enabled);
    io.field(block.environment_red);
    io.field(block.environment_green);
    io.field(block.environment_blue);
    io.field(block.intensity);
    io.field(block.view_distance);
    io.field(block.camera_mode);
    if (block.camera_mode >= 1) io.field(block.camera_position);
    if (block.camera_mode >= 2) {
        io.field(block.camera_angle);
        io.field(block.camera_axis);
    }
    io.field(block.has_light_colours);
    if (block.has_light_colours != 0) io.field(block.light_colours);
    io.field(block.has_environment_colours);
    if (block.has_environment_colours != 0) io.field(block.environment_colours);
}

template<class IO, class Camera> void TransferCamera(IO& io, Camera& camera)
{
    io.field(camera.positional0);
    io.field(camera.positional1);
    io.field(camera.has_scale);
    io.field(camera.name);
    if (camera.has_scale != 0) io.field(camera.scale);
    TransferPathAI(io, camera.pathAI);
}

template<class IO, class Monster> void TransferMonster(IO& io, Monster& monster)
{
    W8LevelFileMonsterHeader header = monster.header;
    if constexpr (!IO::reading) header.path_node_count = DiskCount<int>(monster.path.size());
    io.field(header);
    if constexpr (IO::reading) monster.header = header;
    io.array(monster.path, header.path_node_count);
}

/* Invisible triggers before version 2 have no plane; retail registered a null
   plane for them, which the builder could not use. */
template<class Visit> void ForEachTrigger(W8LevelFile& level, Visit&& visit)
{
    for (auto* props : {&level.props, &level.bitmaps})
        for (auto& prop : *props)
            if (prop.trigger) visit(*prop.trigger);
    for (auto& trigger : level.triggers) visit(trigger);
}

} // namespace

W8LevelFileTriggerParts CollectTriggerParts(W8LevelFile& level)
{
    W8LevelFileTriggerParts parts;
    ForEachTrigger(level, [&](W8LevelFileTrigger& trigger) {
        if (auto* record = std::get_if<W8LevelFileSwitch>(&trigger.data)) {
            parts.switches.push_back(record);
        } else if (auto* record = std::get_if<W8LevelFileInvisible>(&trigger.data)) {
            if (record->plane) parts.planes.push_back(&*record->plane);
        } else if (auto* record = std::get_if<W8LevelFileSuperTrigger>(&trigger.data)) {
            if (record->plane) parts.planes.push_back(&*record->plane);
            if (record->record) parts.linked_records.push_back(&*record->record);
        }
    });
    return parts;
}

// FUNCTION: WIZ8 0x004CFDC0
std::unique_ptr<W8LevelFile> ReadLevelFile(wiz8::File& file)
try {
    auto level = std::make_unique<W8LevelFile>();
    LevelReader io{file};
    TransferMesh(io, level->mesh);

    short texture_count;
    io.field(texture_count);
    if (texture_count == 0) return nullptr;
    io.records(level->textures, texture_count);
    for (auto& material : level->textures) TransferMaterial(io, material);

    TransferRecords<short>(io, level->lights,
                           [](auto& io, auto& light) { TransferLight(io, light); });
    TransferRecords<int>(io, level->monsters,
                         [](auto& io, auto& monster) { TransferMonster(io, monster); });
    TransferArray<int>(io, level->items);
    io.field(level->missile_count);
    ValidateRecordCount(level->missile_count, 1, file);
    TransferRecords<int>(io, level->props, [](auto& io, auto& prop) { TransferProp(io, prop); });
    TransferRecords<int>(io, level->bitmaps, [](auto& io, auto& prop) { TransferProp(io, prop); });
    TransferRecords<int>(io, level->cameras,
                         [](auto& io, auto& camera) { TransferCamera(io, camera); });
    io.field(level->has_block);
    if (level->has_block != 0) TransferBlock(io, level->block);
    TransferRecords<int>(io, level->triggers,
                         [](auto& io, auto& trigger) { TransferTrigger(io, trigger); });
    io.field(level->camera_mode);

    int clipping_plane_count;
    io.field(clipping_plane_count);
    ValidateRecordCount(clipping_plane_count, sizeof(W8LevelFileClippingPlaneRecord), file);
    if (clipping_plane_count != 0) {
        io.field(level->clipping_plane_version);
        io.array(level->clipping_planes, clipping_plane_count);
    }

    io.field(level->environment_offset);
    TransferRecords<int>(io, level->particle_systems,
                         [](auto& io, auto& system) { TransferParticleSystem(io, system); });
    TransferArray<int>(io, level->named_positions);
    for (const auto& position : level->named_positions) {
        if (!std::memchr(position.name, '\0', sizeof(position.name))) return nullptr;
        ReportBuildStatus(5, fmt::format("Named Position: {} ({:f}, {:f}, {:f})\n", position.name,
                                         position.position.x, position.position.y,
                                         position.position.z));
    }
    io.field(level->unknown_6b9);
    level->read_end_position = static_cast<int>(file.tell());

    const auto parts = CollectTriggerParts(*level);
    if (parts.switches.size() > kMaxSwitchTriggers || parts.planes.size() > kMaxTriggerPlanes ||
        parts.linked_records.size() > kMaxLinkedRecords)
        return nullptr;
    return level;
} catch (const std::exception&) {
    return nullptr;
}

/* Write the .pvl header the builder produces: the submesh models in place of
   the source mesh, every other level section separated by the texture count,
   then the rest of the source file copied unchanged. */
// FUNCTION: WIZ8 0x004D07C0
bool WriteLevelFile(wiz8::File& file, wiz8::File& remainder, const W8LevelFile& level,
                    std::span<OctMeshModel> models, int mesh_count)
try {
    LevelWriter io{file};
    io.field(DiskCount<unsigned int>(models.size()));
    io.field(mesh_count);
    const short texture_count = DiskCount<short>(level.textures.size());
    io.field(texture_count);
    if (texture_count == 0) return false;
    for (const auto& material : level.textures) TransferMaterial(io, material);
    const int separator = texture_count;
    io.field(separator);

    for (auto& model : models) model.Write(file);
    io.field(separator);
    TransferRecords<short>(io, level.lights,
                           [](auto& io, auto& light) { TransferLight(io, light); });
    io.field(separator);
    TransferRecords<int>(io, level.monsters,
                         [](auto& io, auto& monster) { TransferMonster(io, monster); });
    io.field(separator);
    TransferArray<int>(io, level.items);
    io.field(separator);
    io.field(level.missile_count);
    io.field(separator);
    TransferRecords<int>(io, level.props, [](auto& io, auto& prop) { TransferProp(io, prop); });
    io.field(separator);
    TransferRecords<int>(io, level.bitmaps, [](auto& io, auto& prop) { TransferProp(io, prop); });
    io.field(separator);
    TransferRecords<int>(io, level.cameras,
                         [](auto& io, auto& camera) { TransferCamera(io, camera); });
    io.field(separator);
    io.field(level.has_block);
    if (level.has_block != 0) TransferBlock(io, level.block);
    io.field(separator);
    TransferRecords<int>(io, level.triggers,
                         [](auto& io, auto& trigger) { TransferTrigger(io, trigger); });
    io.field(separator);
    io.field(level.camera_mode);
    io.field(DiskCount<int>(level.clipping_planes.size()));
    if (!level.clipping_planes.empty()) {
        io.field(level.clipping_plane_version);
        io.array(level.clipping_planes, static_cast<std::int64_t>(level.clipping_planes.size()));
    }
    io.field(separator);
    io.field(level.environment_offset);
    TransferRecords<int>(io, level.particle_systems,
                         [](auto& io, auto& system) { TransferParticleSystem(io, system); });
    io.field(separator);
    TransferArray<int>(io, level.named_positions);
    io.field(separator);
    io.field(GetAutomapGridCellSize());
    TransferArray<int>(io, level.automap_nodes);
    io.field(separator);
    io.field(level.unknown_6b9);

    char buffer[0x400];
    for (;;) {
        const auto copied = remainder.read(buffer, sizeof(buffer)).bytes;
        file.write(buffer, copied);
        if (copied < sizeof(buffer)) break;
    }
    return true;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004D1110
bool ReadMeshFile(wiz8::File& file, W8LevelFileMesh& mesh)
{
    return ReadRecord(file, mesh, [](auto& io, auto& record) { TransferMesh(io, record); });
}

// FUNCTION: WIZ8 0x004D1510
bool WriteMeshFile(wiz8::File& file, const W8LevelFileMesh& mesh)
{
    return WriteRecord(file, mesh, [](auto& io, auto& record) { TransferMesh(io, record); });
}

// FUNCTION: WIZ8 0x004D1820
bool ReadLightFile(wiz8::File& file, W8LevelFileLight& light)
{
    return ReadRecord(file, light, [](auto& io, auto& record) { TransferLight(io, record); });
}

// FUNCTION: WIZ8 0x004D1960
bool WriteLightFile(wiz8::File& file, const W8LevelFileLight& light)
{
    return WriteRecord(file, light, [](auto& io, auto& record) { TransferLight(io, record); });
}

// FUNCTION: WIZ8 0x004D1A90
bool ReadAnimLightFile(wiz8::File& file, W8LevelFileAnimLight& light)
{
    return ReadRecord(file, light, [](auto& io, auto& record) { TransferAnimLight(io, record); });
}

// FUNCTION: WIZ8 0x004D1B50
bool WriteAnimLightFile(wiz8::File& file, const W8LevelFileAnimLight& light)
{
    return WriteRecord(file, light, [](auto& io, auto& record) { TransferAnimLight(io, record); });
}

// FUNCTION: WIZ8 0x004D1C10
bool ReadTriggerFile(wiz8::File& file, W8LevelFileTrigger& trigger)
{
    return ReadRecord(file, trigger, [](auto& io, auto& record) { TransferTrigger(io, record); });
}

// FUNCTION: WIZ8 0x004D23F0
bool WriteTriggerFile(wiz8::File& file, const W8LevelFileTrigger& trigger)
{
    return WriteRecord(file, trigger, [](auto& io, auto& record) { TransferTrigger(io, record); });
}

// FUNCTION: WIZ8 0x004D2A30
bool ReadSuperTriggerFile(wiz8::File& file, W8LevelFileTrigger& trigger)
{
    return ReadRecord(file, trigger, [](auto& io, auto& record) {
        record.type = W8_LEVEL_TRIGGER_SUPER;
        TransferTriggerBody(io, record);
    });
}

// FUNCTION: WIZ8 0x004D3000
bool WriteSuperTriggerFile(wiz8::File& file, const W8LevelFileTrigger& trigger)
{
    return WriteRecord(file, trigger, [](auto& io, auto& record) {
        if (record.type != W8_LEVEL_TRIGGER_SUPER) throw LevelError("not a super trigger");
        TransferTriggerBody(io, record);
    });
}

// FUNCTION: WIZ8 0x004D3540
bool ReadDoorTriggerFile(wiz8::File& file, W8LevelFileDoorRef& door)
{
    return ReadRecord(file, door, [](auto& io, auto& record) {
        record.kind = 1;
        TransferDoor(io, record);
    });
}

// FUNCTION: WIZ8 0x004D3660
bool WriteDoorTriggerFile(wiz8::File& file, const W8LevelFileDoorRef& door)
{
    return WriteRecord(file, door, [](auto& io, auto& record) {
        TransferDoorRecord(io, io.present(record.door));
    });
}

// FUNCTION: WIZ8 0x004D3770
bool ReadPathAIFile(wiz8::File& file, W8LevelFilePathAI& path)
{
    return ReadRecord(file, path, [](auto& io, auto& record) { TransferPathAI(io, record); });
}

// FUNCTION: WIZ8 0x004D38E0
bool WritePathAIFile(wiz8::File& file, const W8LevelFilePathAI& path)
{
    return WriteRecord(file, path, [](auto& io, auto& record) { TransferPathAI(io, record); });
}

// FUNCTION: WIZ8 0x004D3A10
bool ReadAnimObjFile(wiz8::File& file, W8LevelFileAnimObj& anim_obj)
{
    return ReadRecord(file, anim_obj, [](auto& io, auto& record) { TransferAnimObj(io, record); });
}

// FUNCTION: WIZ8 0x004D4480
bool WriteAnimObjFile(wiz8::File& file, const W8LevelFileAnimObj& anim_obj)
{
    return WriteRecord(file, anim_obj, [](auto& io, auto& record) { TransferAnimObj(io, record); });
}

// FUNCTION: WIZ8 0x004D4CB0
bool ReadPropsFile(wiz8::File& file, int count, std::vector<W8LevelFileProp>& props)
{
    return ReadRecord(file, props, [count](auto& io, auto& records) {
        io.records(records, count);
        for (auto& prop : records) TransferProp(io, prop);
    });
}

// FUNCTION: WIZ8 0x004D4FC0
bool WritePropsFile(wiz8::File& file, const std::vector<W8LevelFileProp>& props)
{
    return WriteRecord(file, props, [](auto& io, auto& records) {
        for (auto& prop : records) TransferProp(io, prop);
    });
}

// FUNCTION: WIZ8 0x004D5240
bool ReadParticleSystemFile(wiz8::File& file, W8LevelFileParticleSystem& system)
{
    return ReadRecord(file, system,
                      [](auto& io, auto& record) { TransferParticleSystem(io, record); });
}

// FUNCTION: WIZ8 0x004D5370
bool WriteParticleSystemFile(wiz8::File& file, const W8LevelFileParticleSystem& system)
{
    return WriteRecord(file, system,
                       [](auto& io, auto& record) { TransferParticleSystem(io, record); });
}

// FUNCTION: WIZ8 0x004D5430
bool ReadLevelFileBlock(wiz8::File& file, W8LevelFileBlock& block)
{
    return ReadRecord(file, block, [](auto& io, auto& record) { TransferBlock(io, record); });
}

// FUNCTION: WIZ8 0x004D5580
bool WriteLevelFileBlock(wiz8::File& file, const W8LevelFileBlock& block)
{
    return WriteRecord(file, block, [](auto& io, auto& record) { TransferBlock(io, record); });
}
