#include "wiz8/world/automap.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
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

/* Pack a grid-relative position into a cell key: eleven bits of z, then eleven of x,
   then ten of y, each scaled to grid cells. */
unsigned int AutomapNodeKey(const W8Automap& automap, const glm::vec3& position)
{
    const float size = automap.grid_cell_size;
    return ((static_cast<unsigned int>(position.z / size) & 0x7ff) |
            static_cast<unsigned int>(position.x / size) << 11)
               << 10 |
           (static_cast<unsigned int>(position.y / size) & 0x3ff);
}

// The cell under `position`, one-based; zero and one name no cell the map keeps.
int AutomapCellAt(const W8Automap& automap, glm::vec3 relative)
{
    const int cell = LookupIndexValue(automap.index, AutomapNodeKey(automap, relative));
    if (cell > 1) return cell;
    relative.y += automap.grid_cell_size;
    return LookupIndexValue(automap.index, AutomapNodeKey(automap, relative));
}

/* How far from a cell the map lights static vertices: wider on the open outdoor
   levels. */
float AutomapRevealRange(int level)
{
    return level == 0x18 || (level > 0x1a && level <= 0x22) ? 30000.0f : 10000.0f;
}
} // namespace

AutomapGrid automapGrid(const World& world)
{
    if (const auto* spatial = world.compiledIndex()) {
        const auto bounds = spatial->m_spatial.GetClippedBounds();
        return {bounds.minimum, bounds.maximum};
    }
    return {glm::vec3(-250000.0f), glm::vec3(250000.0f)};
}

glm::vec3 automapCellPosition(const World& world, std::size_t cell)
{
    const auto& automap = world.automap;
    glm::vec3 position(0);
    if (cell < automap.keys.size()) {
        const unsigned int key = automap.keys[cell];
        const float size = automap.grid_cell_size;
        position = glm::vec3(static_cast<float>(key >> 0x15) * size,
                             static_cast<float>(key & 0x3ff) * size,
                             static_cast<float>((key >> 10) & 0x7ff) * size) +
                   size * 0.5f;
    }
    return position + automapGrid(world).minimum;
}

bool VisitAutomapCell(World& world, const glm::vec3& position)
{
    const int cell = AutomapCellAt(world.automap, position - automapGrid(world).minimum);
    return cell > 1 && !SetBit(world.automap.visited, cell - 1);
}

std::size_t RevealAutomapCells(World& world, std::size_t limit)
{
    auto& automap = world.automap;
    const auto* spatial = world.compiledIndex();
    if (!spatial) return 0;
    const float range = AutomapRevealRange(world.level_id);
    std::size_t revealed = 0;
    for (std::size_t cell = 0; cell < automap.keys.size() && revealed < limit; ++cell) {
        if (!TestBit(automap.visited, cell) || SetBit(automap.lit, cell)) continue;
        const glm::vec3 center = automapCellPosition(world, cell);
        for (auto* instance : spatial->modelsNearPoint(center, range))
            if (instance) world.scene->map.reveal(*instance, center, range);
        ++revealed;
    }
    return revealed;
}

bool ReadAutomapNodes(wiz8::File& file, W8Automap& automap)
try {
    W8Automap decoded;
    unsigned int count;
    file.read_exact(&decoded.grid_cell_size, sizeof(decoded.grid_cell_size));
    file.read_exact(&count, sizeof(count));
    if (count > static_cast<std::uint64_t>(file.size() - file.tell()) / sizeof(unsigned int))
        return false;
    decoded.keys.resize(count ? count : 1, 0);
    if (count) file.read_exact(decoded.keys.data(), count * sizeof(unsigned int));
    decoded.visited.resize(decoded.keys.size(), false);
    decoded.lit.resize(decoded.keys.size(), false);
    for (unsigned int i = 0; i < count; ++i) decoded.index.insert_or_assign(decoded.keys[i], i + 1);
    automap = std::move(decoded);
    return true;
} catch (const std::exception&) {
    return false;
}

W8SavedAutomapNotes CaptureAutomapNotes(const W8Automap& automap)
{
    W8SavedAutomapNotes records{.visited = automap.visited};
    for (const auto& note : automap.notes)
        records.notes.push_back(std::make_unique<W8AutomapNote>(*note));
    return records;
}

/* The visited-cell bitmap, when the map has cells, then every note's position, layer and
   text between two 0xF00DF00D signatures. Write failures throw. */
void WriteAutomapNotes(wiz8::File& handle, const W8SavedAutomapNotes& records)
{
    if (!records.visited.empty()) SavePackedBits(handle, records.visited);
    handle.write(&kNotesSignature, sizeof(kNotesSignature));
    const auto count = wiz8::checked_integer<std::uint32_t>(records.notes.size());
    handle.write(&count, sizeof(count));
    for (const auto& note : records.notes) {
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

/* The visited-cell bitmap, present when the level's map had cells, then the notes. */
W8SavedAutomapNotes ReadAutomapNotes(W8Chunk& file)
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
    result.notes.reserve(std::min(count, 200u));
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
        if (result.notes.size() < 200) result.notes.push_back(std::move(note));
    }
    file.ReadBytes(&signature, sizeof(signature));
    if (signature != kNotesSignature) throw std::runtime_error("invalid automap notes terminator");
    return result;
}

/* The saved visited cells replace the level's when they describe its cells; the map
   view reveals them over the following frames. */
void ApplyAutomapNotes(W8Automap& automap, W8SavedAutomapNotes records)
{
    if (automap.keys.size() > 1) {
        if (records.visited.size() == automap.keys.size())
            automap.visited = std::move(records.visited);
        else
            automap.visited.assign(automap.keys.size(), false);
    }
    automap.notes = std::move(records.notes);
}
