#include "wiz8/world/automap.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "wiz8/checked_integer.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"
#include "wiz8/index_algorithms.h"
#include "wiz8/packed_bits.h"
#include "wiz8/render/map_view.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/unicode.h"
#include "wiz8/world/world.h"

namespace {
constexpr std::uint32_t kNotesSignature = 0xf00df00d;
constexpr std::size_t kMaxNotes = 200;
constexpr float kFollowDistanceSquared = 250000.0f;

/* Pack a grid-relative position into a cell key: eleven bits of z, then eleven of x,
   then ten of y, each scaled to grid cells. */
unsigned int CellKey(float size, const glm::vec3& position)
{
    return ((static_cast<unsigned int>(position.z / size) & 0x7ff) |
            static_cast<unsigned int>(position.x / size) << 11)
               << 10 |
           (static_cast<unsigned int>(position.y / size) & 0x3ff);
}

/* How far from a cell the map lights static vertices: wider on the open outdoor
   levels. */
float RevealRange(int level)
{
    return level == 0x18 || (level > 0x1a && level <= 0x22) ? 30000.0f : 10000.0f;
}
} // namespace

bool Automap::readCells(wiz8::File& file)
try {
    float cell_size;
    unsigned int count;
    file.read_exact(&cell_size, sizeof(cell_size));
    file.read_exact(&count, sizeof(count));
    if (count > static_cast<std::uint64_t>(file.size() - file.tell()) / sizeof(unsigned int))
        return false;
    std::vector<unsigned int> keys(count ? count : 1, 0);
    if (count) file.read_exact(keys.data(), count * sizeof(unsigned int));
    std::unordered_map<unsigned int, int> index;
    for (unsigned int i = 0; i < count; ++i) index.insert_or_assign(keys[i], i + 1);
    grid_cell_size_ = cell_size;
    visited_.assign(keys.size(), false);
    lit_.assign(keys.size(), false);
    keys_ = std::move(keys);
    index_ = std::move(index);
    return true;
} catch (const std::exception&) {
    return false;
}

AutomapGrid Automap::grid() const
{
    if (const auto* spatial = world_.compiledIndex()) {
        const auto bounds = spatial->m_spatial.GetClippedBounds();
        return {bounds.minimum, bounds.maximum};
    }
    return {glm::vec3(-250000.0f), glm::vec3(250000.0f)};
}

glm::vec3 Automap::cellPosition(std::size_t cell) const
{
    glm::vec3 position(0);
    if (cell < keys_.size()) {
        const unsigned int key = keys_[cell];
        const float size = grid_cell_size_;
        position = glm::vec3(static_cast<float>(key >> 0x15) * size,
                             static_cast<float>(key & 0x3ff) * size,
                             static_cast<float>((key >> 10) & 0x7ff) * size) +
                   size * 0.5f;
    }
    return position + grid().minimum;
}

int Automap::cellAt(glm::vec3 relative) const
{
    const int cell = LookupIndexValue(index_, CellKey(grid_cell_size_, relative));
    if (cell > 1) return cell;
    relative.y += grid_cell_size_;
    return LookupIndexValue(index_, CellKey(grid_cell_size_, relative));
}

bool Automap::visit(const glm::vec3& position)
{
    const int cell = cellAt(position - grid().minimum);
    return cell > 1 && !SetBit(visited_, cell - 1);
}

void Automap::follow(const glm::vec3& position)
{
    if (followed_) {
        const glm::vec3 offset = position - *followed_;
        if (glm::dot(offset, offset) <= kFollowDistanceSquared) return;
    }
    followed_ = position;
    visit(position);
}

std::size_t Automap::reveal(std::size_t limit)
{
    const auto* spatial = world_.compiledIndex();
    if (!spatial) return 0;
    const float range = RevealRange(world_.level_id);
    std::size_t revealed = 0;
    for (std::size_t cell = 0; cell < keys_.size() && revealed < limit; ++cell) {
        if (!TestBit(visited_, cell) || SetBit(lit_, cell)) continue;
        const glm::vec3 center = cellPosition(cell);
        for (auto* instance : spatial->modelsNearPoint(center, range))
            if (instance) world_.scene->map.reveal(*instance, center, range);
        ++revealed;
    }
    return revealed;
}

bool Automap::cameraSees(const glm::vec3& location) const
{
    const auto* spatial = world_.compiledIndex();
    const auto eye = glm::vec3(world_.scene->map.camera.transform.getLocation());
    return !spatial || spatial->hasLineOfSight(eye, location);
}

W8SavedAutomapNotes Automap::capture() const
{
    W8SavedAutomapNotes records{.visited = visited_};
    for (const auto& note : notes_) records.notes.push_back(std::make_unique<W8AutomapNote>(*note));
    return records;
}

void Automap::restore(W8SavedAutomapNotes records)
{
    if (keys_.size() > 1) {
        if (records.visited.size() == keys_.size())
            visited_ = std::move(records.visited);
        else
            visited_.assign(keys_.size(), false);
    }
    notes_ = std::move(records.notes);
}

W8AutomapNote* Automap::addNote(glm::vec2 position, int layer)
{
    if (notes_.size() >= kMaxNotes) return nullptr;
    return notes_.emplace_back(std::make_unique<W8AutomapNote>(W8AutomapNote{position, layer, "_"}))
        .get();
}

void Automap::eraseNote(const W8AutomapNote* note)
{
    std::erase_if(notes_, [note](const auto& owned) { return owned.get() == note; });
}

void W8SavedAutomapNotes::write(wiz8::File& handle) const
{
    if (!visited.empty()) SavePackedBits(handle, visited);
    handle.write(&kNotesSignature, sizeof(kNotesSignature));
    const auto count = wiz8::checked_integer<std::uint32_t>(notes.size());
    handle.write(&count, sizeof(count));
    for (const auto& note : notes) {
        const auto length =
            wiz8::checked_integer<std::uint32_t>(wiz8::text::to_utf16(note->text).size() + 1);
        std::vector<std::byte> bytes(length * 2);
        wiz8::text::to_utf16le(note->text, bytes);
        handle.write(&note->position.x, sizeof(note->position.x));
        handle.write(&note->position.y, sizeof(note->position.y));
        handle.write(&note->layer, sizeof(note->layer));
        handle.write(&length, sizeof(length));
        handle.write(bytes.data(), bytes.size());
    }
    handle.write(&kNotesSignature, sizeof(kNotesSignature));
}

W8SavedAutomapNotes W8SavedAutomapNotes::read(W8Chunk& file)
{
    W8SavedAutomapNotes result;
    const auto first = file.ReadValue<std::uint32_t>();
    file.RewindCurrentChunk();
    if (first == kPackedBitsMagic && !LoadPackedBits(file, result.visited))
        throw std::runtime_error("invalid automap visited cells");
    unsigned int signature, count;
    file.ReadBytes(&signature, sizeof(signature));
    if (signature != kNotesSignature) throw std::runtime_error("invalid automap notes signature");
    file.ReadBytes(&count, sizeof(count));
    if (count > file.RemainingBytes() / 18) throw std::runtime_error("invalid automap note count");
    result.notes.reserve(std::min<std::size_t>(count, kMaxNotes));
    for (unsigned int index = 0; index < count; ++index) {
        auto note = std::make_unique<W8AutomapNote>();
        int length;
        file.ReadBytes(&note->position.x, sizeof(float));
        file.ReadBytes(&note->position.y, sizeof(float));
        file.ReadBytes(&note->layer, sizeof(note->layer));
        file.ReadBytes(&length, sizeof(length));
        if (length < 1 || length > 40) throw std::runtime_error("invalid automap note length");
        std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 2);
        file.ReadBytes(bytes.data(), bytes.size());
        note->text = wiz8::text::from_utf16le(bytes);
        if (result.notes.size() < kMaxNotes) result.notes.push_back(std::move(note));
    }
    file.ReadBytes(&signature, sizeof(signature));
    if (signature != kNotesSignature) throw std::runtime_error("invalid automap notes terminator");
    return result;
}
