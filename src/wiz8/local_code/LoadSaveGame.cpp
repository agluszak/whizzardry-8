#include "wiz8/local_code/LoadSaveGame.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_time.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "glm/ext/vector_float3.hpp"
#include "item_spawning.h"
#include "monster_actions.h"
#include "monster_spell_icons.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/checked_integer.h"
#include "wiz8/chunk.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/design_rect.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/saved_world_sections.h"
#include "wiz8/entities/script.h" // IWYU pragma: keep - complete script type used through the owned pointer.
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/file_time.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/save_screenshot.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/level_state.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/message_log.h"
#include "wiz8/monster_generators.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/save_codec.h"
#include "wiz8/save_game.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/surface_storage.h"
#include "wiz8/unicode.h"
#include "wiz8/world/automap.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"
#include "wiz8/world_view.h"

bool g_save_pending;

/* The save-file extension. Retail keeps it in writable .data. */
char g_save_extension[] = "SAV";

namespace {

/* Save and level-status chunk ids. */
constexpr unsigned int W8_CHUNK_GAME_STATUS = W8ChunkId("GSTA");
constexpr unsigned int W8_CHUNK_GAME_VERSION = W8ChunkId("GVER");
constexpr unsigned int W8_CHUNK_SCREENSHOT = W8ChunkId("SHOT");
constexpr unsigned int W8_CHUNK_MESSAGE_STORAGE = W8ChunkId("TEXT");
constexpr unsigned int W8_CHUNK_LOCATION_VARIABLES = W8ChunkId("TVAR");
constexpr unsigned int W8_CHUNK_NPC_TRANSCRIPT = W8ChunkId("NPCI");
constexpr unsigned int W8_CHUNK_NPC_STATES = W8ChunkId("NPCT");
constexpr unsigned int W8_CHUNK_FACT_STATE = W8ChunkId("NPCF");
constexpr unsigned int W8_CHUNK_FACTION_STATE = W8ChunkId("FATA");
constexpr unsigned int W8_CHUNK_JOURNAL = W8ChunkId("JRNL");
constexpr unsigned int W8_CHUNK_MONSTER_CONTROL = W8ChunkId("HYPN");
constexpr unsigned int W8_CHUNK_CHARACTER = W8ChunkId("CHAR");
constexpr unsigned int W8_CHUNK_LEVEL_STATUS = W8ChunkId("LVLS");
constexpr unsigned int W8_CHUNK_STATUS_HEADER = W8ChunkId("STAT");
constexpr unsigned int W8_CHUNK_MONSTERS = W8ChunkId("MONS");
constexpr unsigned int W8_CHUNK_WORLD_ITEMS = W8ChunkId("ITEM");
constexpr unsigned int W8_CHUNK_CURSOR_NODES = W8ChunkId("CUBE");
constexpr unsigned int W8_CHUNK_ENCOUNTERS = W8ChunkId("MONG");
constexpr unsigned int W8_CHUNK_TRIGGER_LOCKS = W8ChunkId("LOCK");
constexpr unsigned int W8_CHUNK_TRIGGER_ACTIONS = W8ChunkId("TRES");
constexpr unsigned int W8_CHUNK_AUTOMAP_NOTES = W8ChunkId("AUTO");
constexpr unsigned int W8_CHUNK_TRIGGERS = W8ChunkId("TRIG");
constexpr unsigned int W8_CHUNK_PROPS = W8ChunkId("APST");
constexpr unsigned int W8_CHUNK_CURSOR_NODE_STATES = W8ChunkId("CUBS");
constexpr unsigned int W8_CHUNK_MONSTER_GENERATORS = W8ChunkId("MGNS");
constexpr unsigned int W8_CHUNK_TRIGGER_STATES = W8ChunkId("LCKS");
constexpr unsigned int W8_CHUNK_AMBIENT_SOUNDS = W8ChunkId("AMBS");
constexpr unsigned int W8_CHUNK_PARTICLES = W8ChunkId("PART");
constexpr unsigned int W8_CHUNK_LIGHTS = W8ChunkId("LGHT");

/* Localized strings the autosave notices use. */
constexpr int W8_STRING_AUTOSAVE_STARTING = 0x783;
constexpr int W8_STRING_AUTOSAVE_SUCCEEDED = 0x784;
constexpr int W8_STRING_AUTOSAVE_FAILED = 0x785;

/* The game version written to GVER; a save without GVER predates it. */
struct SaveVersion {
    int major, minor, patch;
};
constexpr SaveVersion kCurrentSaveVersion{1, 2, 4};
constexpr SaveVersion kUnversionedSave{1, 0, 0};

constexpr std::string_view kAutoSaveName = "AutoSave";

std::string SaveSlotPath(std::string_view name)
{
    return fmt::format("saves\\{}.{}", name, g_save_extension);
}

std::string QuickSaveName(int slot)
{
    return fmt::format("Quick {}", slot);
}

bool IsRegularFile(std::string_view path)
{
    const auto status = wiz8::file_status(wiz8::user_path(path));
    return status && status->info.type == SDL_PATHTYPE_FILE;
}

// Earlier versions kept their working file among the saves; it is not a slot.
bool IsCurrentGameFileName(std::string_view name)
{
    return CompareTextIgnoreAsciiCase(name, "CurrentGame.SAV") == 0;
}

// The rest of the current chunk, for the codecs that decode a section whole.
std::vector<std::byte> ReadChunkPayload(W8Chunk& chunk)
{
    std::vector<std::byte> payload(static_cast<std::size_t>(chunk.RemainingBytes()));
    chunk.ReadBytes(payload.data(), payload.size());
    return payload;
}

void WriteChunkPayload(W8Chunk& chunks, unsigned int id, std::span<const std::byte> payload)
{
    chunks.WriteChunk(id, [&] { chunks.WriteBytes(payload.data(), payload.size()); });
}

/* Section writers and loaders report failure as false; inside a chunk body
   that abandons the whole operation. */
void RequireSection(bool succeeded, const char* section)
{
    if (!succeeded) throw std::runtime_error(std::string("save section failed: ") + section);
}

/* Writes a save through a staged file: the RIFF is finalized and only then
   published over the old file, so a failure leaves the old save intact. */
class SaveTransaction {
    wiz8::StagedFile staged_;

public:
    W8Chunk chunks;
    explicit SaveTransaction(std::string_view path) : staged_(wiz8::user_path(path))
    {
        if (!chunks.OpenWrite(staged_.file())) throw std::runtime_error("open staged save");
    }
    void commit()
    {
        chunks.Close();
        staged_.commit();
    }
};

/* Find the top-level LVLS group saved for `level` (the first group when no
   level is given) and run `body` inside it, after the group's level number.
   Returns whether a group was found. */
template<class F> bool WithLevelGroup(W8Chunk& chunk, std::optional<int> level, F&& body)
{
    bool found = false;
    chunk.ForEachChunk([&](unsigned int id) {
        if (id != W8_CHUNK_LEVEL_STATUS) return true;
        chunk.ReadGroup([&] {
            const auto saved_level = chunk.ReadValue<int>();
            if (level && saved_level != *level) return;
            found = true;
            body();
        });
        return !found;
    });
    return found;
}

/* Read the current LVLS group's level number and rewind to the start of the
   chunk, ready for it to be copied whole. */
int PeekGroupLevel(W8Chunk& chunk)
{
    int level;
    chunk.ReadGroup([&] { chunk.ReadValue(level); });
    chunk.RewindCurrentChunk();
    return level;
}

/* The ITEM section's record count, checked against the bytes left. */
unsigned int ReadWorldItemCount(W8Chunk& chunk)
{
    const auto count = chunk.ReadValue<unsigned int>();
    if (count >
        static_cast<std::uint64_t>(chunk.RemainingBytes()) / sizeof(wiz8::retail::WorldItem))
        throw std::runtime_error("invalid item section count");
    return count;
}

/* A size-prefixed record image; an older writer's shorter image is zero-extended. */
template<class Disk> void ReadSizedImage(W8Chunk& chunk, Disk& image)
{
    const auto size = chunk.ReadValue<std::uint32_t>();
    if (size > sizeof(image)) throw std::runtime_error("Oversized retail record");
    image = {};
    chunk.ReadBytes(&image, size);
}

/* Save names and stored characters' file names fit a 64-byte field with its terminator. */
constexpr int kCharacterNameBytes = 64;

// The stored character with this file name.
wiz8::save::StoredCharacter*
FindStoredCharacter(std::vector<wiz8::save::StoredCharacter>& characters,
                    std::string_view file_name)
{
    const auto found = std::ranges::find_if(characters, [&](const auto& stored) {
        return CompareTextIgnoreAsciiCase(stored.file_name, file_name) == 0;
    });
    return found == characters.end() ? nullptr : &*found;
}

// Keep a character under its file name, replacing one stored under the same name.
void StoreCharacter(std::vector<wiz8::save::StoredCharacter>& characters,
                    wiz8::save::StoredCharacter stored)
{
    if (auto* existing = FindStoredCharacter(characters, stored.file_name))
        *existing = std::move(stored);
    else
        characters.push_back(std::move(stored));
}

/* The STAT record mirrors the id counters and status block of the game's status. */
wiz8::save::LevelStatusHeader CaptureStatusHeader(const GameStatus& status)
{
    return {
        .next_group_id = status.next_group_id,
        .next_monster_location_id = status.next_monster_location_id,
        .next_world_item_id = status.next_world_item_id,
        .next_trigger_id = status.next_trigger_id,
        .turns_elapsed = status.uiTurnsElapsed,
        .status_prefix = status.retail.status_header_prefix,
        .status_suffix = status.retail.status_header_suffix,
    };
}

/* Publish a STAT record. A zero counter restarts at one. */
void ApplyStatusHeader(const wiz8::save::LevelStatusHeader& header, GameStatus& status)
{
    const auto counter = [](int saved) { return saved ? saved : 1; };
    status.next_group_id = counter(header.next_group_id);
    status.next_monster_location_id = counter(header.next_monster_location_id);
    status.next_world_item_id = counter(header.next_world_item_id);
    status.next_trigger_id = counter(header.next_trigger_id);
    status.uiTurnsElapsed = header.turns_elapsed;
    status.retail.status_header_prefix = header.status_prefix;
    status.retail.status_header_suffix = header.status_suffix;
}

/* Each text box's used lines, with the time left on the countdowns of the lines still
   shown. */
wiz8::save::MessageBoxes CaptureMessageBoxes(const Game& game)
{
    wiz8::save::MessageBoxes boxes;
    for (std::size_t box = 0; box < boxes.size(); ++box) {
        const auto used =
            std::min<std::size_t>(game.status.text_box_lines_used[box], W8_TEXT_BOX_LINES);
        for (std::size_t index = 0; index < used; ++index) {
            auto& line = boxes[box].emplace_back(game.message_log[box][index]);
            if (index < game.status.text_box_lines_shown[box])
                line.saved_remaining_ms = static_cast<std::uint32_t>(TimeUntilDeadline(line.clock));
        }
    }
    return boxes;
}

/* Install a save's message lines; the load re-arms the countdowns of those still shown. */
void InstallMessageBoxes(wiz8::save::MessageBoxes boxes, Game& game)
{
    for (std::size_t box = 0; box < boxes.size(); ++box) {
        auto& lines = boxes[box];
        game.status.text_box_lines_used[box] = static_cast<unsigned int>(lines.size());
        std::ranges::move(lines, game.message_log[box].begin());
    }
}

/* The story's location variables with the level script's state folded in, which leaving
   the level would record. */
std::vector<W8LocationVariable> CaptureLocationVariables(const Game& game, const World* world)
{
    auto variables = game.story.locationVariables();
    if (!world || !world->script) return variables;
    const int level = world->level_id;
    for (const auto& [name, value] : world->script->captureState()) {
        const auto found = std::ranges::find_if(variables, [&](const auto& variable) {
            return variable.level == level && CompareTextIgnoreAsciiCase(variable.name, name) == 0;
        });
        if (found == variables.end())
            variables.push_back({name, value, level});
        else
            found->value = value;
    }
    return variables;
}

wiz8::save::DialogueTranscript CaptureDialogueTranscript(const Story& story,
                                                         const W8NpcInteractionState& state)
{
    wiz8::save::DialogueTranscript transcript;
    transcript.keywords = story.keywords;
    transcript.view = {state.dialogue_category_filter, story.keywords_sorted};
    return transcript;
}

void InstallDialogueTranscript(wiz8::save::DialogueTranscript transcript, Story& story,
                               W8NpcInteractionState& state)
{
    story.keywords = std::move(transcript.keywords);
    if (transcript.view) {
        state.dialogue_category_filter = transcript.view->category_filter;
        story.keywords_sorted = transcript.view->sorted;
    }
}

/* Monster save records follow the W8MonsterInfo image with versioned
   sections. Version 7 writes all of them. */
constexpr unsigned int kMonsterRecordVersion = 7;
constexpr unsigned int kMonsterGroupRecordVersion = 3;
constexpr unsigned int kMonsterHasMovement = 0x20000000;

struct SavedMonsterScript {
    char name[0x40]{};
    W8MonsterScriptCommand wait{};
    int line = 0;
    std::vector<unsigned char> conditions;

    static SavedMonsterScript Capture(const Monster& monster)
    {
        SavedMonsterScript script;
        const auto& name = monster.script->getName();
        if (name.size() >= sizeof(script.name))
            throw std::runtime_error("monster script name too long");
        std::copy(name.begin(), name.end(), script.name);
        script.wait = monster.script_wait;
        script.line = monster.script_line;
        for (const auto condition : monster.script_conditions)
            script.conditions.push_back(condition != 0);
        return script;
    }
    void Write(W8Chunk& chunk) const
    {
        chunk.WriteBytes(name, sizeof(name));
        chunk.WriteValue(wait);
        chunk.WriteValue(line);
        chunk.WriteValue(static_cast<int>(conditions.size()));
        chunk.WriteBytes(conditions.data(), conditions.size());
    }
    static SavedMonsterScript Read(W8Chunk& chunk)
    {
        SavedMonsterScript script;
        chunk.ReadBytes(script.name, sizeof(script.name));
        if (!memchr(script.name, '\0', sizeof(script.name)))
            throw std::runtime_error("unterminated monster script name");
        chunk.ReadValue(script.wait);
        chunk.ReadValue(script.line);
        const auto count = chunk.ReadValue<int>();
        if (count < 0 || count > chunk.RemainingBytes())
            throw std::runtime_error("invalid monster script conditions");
        script.conditions.resize(count);
        chunk.ReadBytes(script.conditions.data(), script.conditions.size());
        return script;
    }
    void Apply(W8MonsterInfo& info, Monster& monster, std::shared_ptr<Script> resource) const
    {
        if (name[0] == '\0') return;
        info.ai_mode |= W8_MONSTER_AI_RESTORE_SCRIPT;
        monster.UseScript(std::move(resource), false);
        monster.script_wait = wait;
        monster.script_line = line;
        for (const auto condition : conditions) monster.script_conditions.push_back(condition != 0);
    }
};

/* Free (non-linked) navigator movement: height band, patrol home and target. */
struct SavedMonsterMovement {
    float minimum_height = 0;
    float maximum_height = 0;
    glm::vec3 patrol_home{0.0f};
    glm::vec3 target{0.0f};

    static SavedMonsterMovement Capture(const Monster& monster)
    {
        return {monster.nav.minimum_height, monster.nav.maximum_height, monster.nav.patrol_home,
                monster.nav.movement.attachment->path_destination};
    }
    void Write(W8Chunk& chunk) const
    {
        chunk.WriteValue(minimum_height);
        chunk.WriteValue(maximum_height);
        wiz8::retail::write(chunk, patrol_home);
        wiz8::retail::write(chunk, target);
    }
    static SavedMonsterMovement Read(W8Chunk& chunk)
    {
        SavedMonsterMovement movement;
        chunk.ReadValue(movement.minimum_height);
        chunk.ReadValue(movement.maximum_height);
        wiz8::retail::read_exact(chunk, movement.patrol_home);
        wiz8::retail::read_exact(chunk, movement.target);
        return movement;
    }
    void Apply(Monster& monster) const
    {
        auto target_point = target;
        monster.nav.minimum_height = minimum_height;
        monster.nav.maximum_height = maximum_height;
        monster.nav.patrol_home = patrol_home;
        monster.nav.movement.attachment->InitializeSegment(&monster.nav.movement.position,
                                                           &target_point);
        monster.nav.movement_target = target;
        monster.nav.flags |= kMonsterHasMovement;
        monster.nav.movement.attachment->flags |= W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
        monster.nav.movement.target_position = patrol_home;
        if (monster.nav.SetMovementTarget(&monster.nav.movement_target, true)) {
            monster.nav.flags |= 0x6;
            monster.nav.ClearMovementStopped();
            monster.nav.halted = false;
            monster.nav.movement_target = monster.nav.movement.attachment->path_destination;
        }
    }
};

/* Orders and patrol state (record version 3), with the facing direction
   (version 4) and the face-party (6) and stay-home (7) flags. */
struct SavedMonsterOrders {
    bool defining_orders = false;
    W8MonsterOrderMode order_mode{};
    bool orders_finished = false;
    bool deaf = false;
    float patrol_distance = 0;
    float patrol_variation = 0;
    signed char patrol_index = 0;
    std::vector<glm::vec3> points;
    std::optional<glm::vec3> direction;
    std::optional<bool> face_party;
    std::optional<bool> stay_home;

    static SavedMonsterOrders Capture(const Monster& monster)
    {
        SavedMonsterOrders orders;
        orders.defining_orders = monster.defining_orders;
        orders.order_mode = monster.order_mode;
        orders.orders_finished = monster.orders_finished;
        orders.deaf = monster.deaf;
        orders.patrol_distance = monster.patrol_distance;
        orders.patrol_variation = monster.patrol_variation;
        orders.patrol_index = monster.patrol_index;
        orders.points = monster.vector;
        glm::vec3 direction;
        direction = glm::vec3(monster.direction_x, monster.direction_y, monster.direction_z);
        orders.direction = direction;
        orders.face_party = monster.face_party;
        orders.stay_home = monster.stay_home;
        return orders;
    }
    void Write(W8Chunk& chunk) const
    {
        chunk.WriteValue(defining_orders);
        chunk.WriteValue(order_mode);
        chunk.WriteValue(orders_finished);
        chunk.WriteValue(deaf);
        chunk.WriteValue(patrol_distance);
        chunk.WriteValue(patrol_variation);
        chunk.WriteValue(patrol_index);
        chunk.WriteValue(static_cast<int>(points.size()));
        for (const auto& point : points) wiz8::retail::write(chunk, point);
        if (direction) wiz8::retail::write(chunk, *direction);
        if (face_party) chunk.WriteValue(*face_party);
        if (stay_home) chunk.WriteValue(*stay_home);
    }
    static SavedMonsterOrders Read(W8Chunk& chunk, unsigned int version)
    {
        SavedMonsterOrders orders;
        orders.defining_orders = chunk.ReadValue<unsigned char>() != 0;
        chunk.ReadValue(orders.order_mode);
        orders.orders_finished = chunk.ReadValue<unsigned char>() != 0;
        orders.deaf = chunk.ReadValue<unsigned char>() != 0;
        chunk.ReadValue(orders.patrol_distance);
        chunk.ReadValue(orders.patrol_variation);
        chunk.ReadValue(orders.patrol_index);
        const auto count = chunk.ReadValue<int>();
        if (count < 0 || count > chunk.RemainingBytes() / std::int64_t{sizeof(wiz8::retail::Vec3f)})
            throw std::runtime_error("invalid monster patrol points");
        orders.points.resize(count);
        for (auto& point : orders.points) wiz8::retail::read_exact(chunk, point);
        if (version >= 4) wiz8::retail::read_exact(chunk, orders.direction.emplace(0.0f));
        if (version >= 6) orders.face_party = chunk.ReadValue<unsigned char>() != 0;
        if (version >= 7) orders.stay_home = chunk.ReadValue<unsigned char>() != 0;
        return orders;
    }
    void Apply(W8MonsterInfo& info, Monster& monster) const
    {
        monster.defining_orders = defining_orders;
        monster.order_mode = order_mode;
        monster.orders_finished = orders_finished;
        monster.deaf = deaf;
        monster.patrol_distance = patrol_distance;
        monster.patrol_variation = patrol_variation;
        monster.patrol_index = patrol_index;
        monster.vector.insert(monster.vector.end(), points.begin(), points.end());
        if (direction) {
            monster.direction_x = direction->x;
            monster.direction_y = direction->y;
            monster.direction_z = direction->z;
        }
        if (face_party) monster.face_party = *face_party;
        if (stay_home) monster.stay_home = *stay_home;
        info.ai_mode |= W8_MONSTER_AI_REAPPLY_MODE;
    }
};

/* Rebuild the icons for the conditions, enchantments and effects a loaded
   monster carries. */
void RestoreMonsterSpellIcons(World& world, const W8MonsterInfo& info, Monster& monster)
{
    if (info.highest_condition != W8_CONDITION_NONE) {
        for (int index = 0; index < W8_CONDITION_COUNT; ++index) {
            if (info.uiCondition[index] != 0)
                AddMonsterSpellIcon(world, monster, static_cast<W8MonsterSpellIconId>(index - 1));
        }
    }
    for (int index = 0; index < 8; ++index) {
        if (info.enchantments[index].turns != 0)
            AddMonsterSpellIcon(world, monster, static_cast<W8MonsterSpellIconId>(index + 0x10));
    }
    for (const auto& effect : info.effect_slots) {
        if (effect.duration != 0)
            AddMonsterSpellIcon(world, monster,
                                g_effect_visual_table[effect.effect_id].monster_icon);
    }
    if (info.charm_strength > 0) AddMonsterSpellIcon(world, monster, SPELL_ICON_CHARMED);
    if (info.summoned != W8_MONSTER_SUMMON_NONE)
        AddMonsterSpellIcon(world, monster, SPELL_ICON_SUMMONED);
}

/* A MONS group record: its size-prefixed W8MonsterGroup image and, from version 2,
   whether it was on the encounter list. */
struct SavedMonsterGroup {
    wiz8::retail::MonsterGroup image{};
    bool encounter = false;

    void Write(W8Chunk& chunk) const
    {
        chunk.WriteValue(static_cast<std::uint32_t>(sizeof(image)));
        chunk.WriteBytes(&image, sizeof(image));
        if (image.version >= 2) chunk.WriteValue(static_cast<unsigned char>(encounter));
    }
    static SavedMonsterGroup Read(W8Chunk& chunk)
    {
        SavedMonsterGroup saved;
        ReadSizedImage(chunk, saved.image);
        saved.encounter = saved.image.version >= 2 && chunk.ReadValue<unsigned char>() != 0;
        if (static_cast<unsigned int>(saved.image.monster_id) >= kMaxMonsterSpecies)
            throw std::runtime_error("invalid monster group species");
        return saved;
    }
    // The group, without members until its monsters join it.
    std::unique_ptr<W8MonsterGroup> Build() const
    {
        auto group = std::make_unique<W8MonsterGroup>();
        wiz8::retail::decode_image(image, *group);
        if (group->version < 3) group->forced_neutral = false;
        group->monsters.clear();
        group->members_active = false;
        group->fInCombat = false;
        return group;
    }
};

/* One monster record: the version, the size-prefixed W8MonsterInfo image with position
   and yaw refreshed while the monster is live, then the script, unborn (version 5),
   movement (version 2) and order (version 3) sections. */
struct SavedMonster {
    unsigned int version = kMonsterRecordVersion;
    wiz8::retail::MonsterInfo image{};
    std::optional<SavedMonsterScript> script;
    bool unborn = false;
    std::optional<SavedMonsterMovement> movement;
    std::optional<SavedMonsterOrders> orders;

    static SavedMonster Capture(const W8MonsterInfo& info, bool unborn)
    {
        if (!info.entity) throw std::runtime_error("missing monster");
        Monster& monster = *info.entity;
        SavedMonster saved{.image = wiz8::retail::encode_image(info), .unborn = unborn};
        if (info.fActive) {
            auto position = monster.GetRepresentationLocation();
            position.y = probeGround(position).position.y;
            saved.image.position = {position.x, position.y, position.z};
            saved.image.derived = monster.nav.GetYaw();
        }
        if (monster.script) saved.script = SavedMonsterScript::Capture(monster);
        if (!monster.nav.linked_navigator && (monster.nav.flags & kMonsterHasMovement) != 0)
            saved.movement = SavedMonsterMovement::Capture(monster);
        saved.orders = SavedMonsterOrders::Capture(monster);
        return saved;
    }
    void Write(W8Chunk& chunk) const
    {
        chunk.WriteValue(version);
        chunk.WriteValue(static_cast<std::uint32_t>(sizeof(image)));
        chunk.WriteBytes(&image, sizeof(image));
        chunk.WriteValue(static_cast<unsigned char>(script.has_value()));
        if (script) script->Write(chunk);
        if (version >= 5) chunk.WriteValue(static_cast<unsigned char>(unborn));
        if (version >= 2) {
            chunk.WriteValue(static_cast<unsigned char>(movement.has_value()));
            if (movement) movement->Write(chunk);
        }
        if (orders) orders->Write(chunk);
    }
    static SavedMonster Read(W8Chunk& chunk)
    {
        SavedMonster saved;
        saved.version = chunk.ReadValue<unsigned int>();
        ReadSizedImage(chunk, saved.image);
        if (chunk.ReadValue<unsigned char>()) saved.script = SavedMonsterScript::Read(chunk);
        saved.unborn = saved.version >= 5 && chunk.ReadValue<unsigned char>() != 0;
        if (saved.version >= 2 && chunk.ReadValue<unsigned char>())
            saved.movement = SavedMonsterMovement::Read(chunk);
        if (saved.version >= 3) saved.orders = SavedMonsterOrders::Read(chunk, saved.version);
        for (const auto& effect : saved.image.effect_slots) {
            if (effect.duration &&
                static_cast<unsigned int>(effect.effect_id) >= std::size(g_effect_visual_table))
                throw std::runtime_error("invalid monster status species or effect");
        }
        if (saved.image.monster_species >= kMaxMonsterSpecies)
            throw std::runtime_error("invalid monster status species or effect");
        return saved;
    }
    // The inactive record, without its entity or combat state.
    std::unique_ptr<W8MonsterInfo> Build() const
    {
        auto info = std::make_unique<W8MonsterInfo>();
        wiz8::retail::decode_image(image, *info);
        info->fActive = false;
        return info;
    }
};

} // namespace

/* The sections of one level's LVLS group: the status header, the monster groups and
   monsters, the world items and automap, the trigger, prop, lock and action sections, the
   cursor node and generator states, and the ambient sounds, particles and lights. The
   level's asset status also holds its cursor nodes and encounters, which a save does not
   repeat. */
struct LevelState::Data {
    int level = 0;
    // Read from the level's asset status rather than from a save.
    bool from_assets = false;
    std::optional<wiz8::save::LevelStatusHeader> header;
    std::vector<SavedMonsterGroup> groups;
    std::vector<SavedMonster> monsters;
    std::vector<SavedWorldItem> items;
    std::optional<W8SavedCursorNodes> cursors;
    std::optional<W8SavedCursorNodeStates> cursor_states;
    std::optional<W8EncounterState> encounters;
    std::optional<W8MonsterGeneratorStates> generator_states;
    std::optional<W8SavedAutomapNotes> automap;
    std::vector<W8AmbientSoundState> ambient;
    std::vector<W8ParticleState> particles;
    std::vector<W8LightState> lights;
    // The trigger, prop, lock and action sections, in the order they were read.
    std::vector<SavedWorldSection> world_sections;
};

LevelState::LevelState() : data_(std::make_unique<Data>()) {}
LevelState::LevelState(LevelState&&) noexcept = default;
LevelState& LevelState::operator=(LevelState&&) noexcept = default;
LevelState::~LevelState() = default;

// What entering a built level installs: its status header, NPC bindings and pending deaths.
struct W8LoadedLevelStatus::Data {
    std::optional<wiz8::save::LevelStatusHeader> header;
    W8PreparedNpcBindings npc_bindings;
    std::vector<int> pending_deaths;
};

W8LoadedLevelStatus::W8LoadedLevelStatus() : data(std::make_unique<Data>()) {}
W8LoadedLevelStatus::~W8LoadedLevelStatus() = default;

namespace {
/* Decode one section of a level group into the group's values. The asset status's cursor
   section carries the nodes' bytes too, except when it is the base of a saved level. */
void ReadLevelSection(LevelState::Data& data, W8Chunk& chunk, unsigned int chunk_id,
                      bool from_assets, bool baseline = false)
{
    switch (chunk_id) {
    case W8_CHUNK_STATUS_HEADER:
        data.header = wiz8::save::decode_level_status_header(ReadChunkPayload(chunk));
        break;
    case W8_CHUNK_MONSTERS: {
        const auto group_count = chunk.ReadValue<unsigned int>();
        const auto monster_count = chunk.ReadValue<unsigned int>();
        // Each group is at least its size word, each monster its version, size and flag.
        if (std::uint64_t{group_count} * 4 + std::uint64_t{monster_count} * 9 >
            static_cast<std::uint64_t>(chunk.RemainingBytes()))
            throw std::runtime_error("invalid monster section counts");
        auto& groups = data.groups;
        auto& monsters = data.monsters;
        groups.reserve(groups.size() + group_count);
        monsters.reserve(monsters.size() + monster_count);
        for (unsigned int index = 0; index < group_count; ++index)
            groups.push_back(SavedMonsterGroup::Read(chunk));
        for (unsigned int index = 0; index < monster_count; ++index)
            monsters.push_back(SavedMonster::Read(chunk));
        break;
    }
    case W8_CHUNK_WORLD_ITEMS: {
        const auto count = ReadWorldItemCount(chunk);
        data.items.reserve(data.items.size() + count);
        for (unsigned int index = 0; index < count; ++index)
            data.items.push_back(ReadSavedWorldItem(chunk, from_assets));
        break;
    }
    case W8_CHUNK_CURSOR_NODES:
        data.cursors = ReadWorldCursorNodes(chunk);
        if (from_assets && !baseline)
            ApplyWorldCursorNodeStates(*data.cursors, ReadWorldCursorNodeStates(chunk));
        break;
    case W8_CHUNK_ENCOUNTERS:
        data.encounters = ReadEncounterState(chunk);
        break;
    case W8_CHUNK_TRIGGER_LOCKS:
    case W8_CHUNK_TRIGGER_STATES:
        data.world_sections.emplace_back(DecodeSavedTriggerLocks(chunk));
        break;
    case W8_CHUNK_TRIGGER_ACTIONS:
        data.world_sections.emplace_back(DecodeSavedTriggerActions(chunk));
        break;
    case W8_CHUNK_AUTOMAP_NOTES:
        data.automap = W8SavedAutomapNotes::read(chunk);
        break;
    case W8_CHUNK_TRIGGERS:
        data.world_sections.emplace_back(DecodeSavedTriggers(chunk, from_assets));
        break;
    case W8_CHUNK_PROPS:
        data.world_sections.emplace_back(DecodeSavedProps(chunk));
        break;
    case W8_CHUNK_CURSOR_NODE_STATES:
        data.cursor_states = ReadWorldCursorNodeStates(chunk);
        break;
    case W8_CHUNK_MONSTER_GENERATORS:
        data.generator_states = ReadMonsterGeneratorStates(chunk);
        break;
    case W8_CHUNK_AMBIENT_SOUNDS:
        std::ranges::move(ReadAmbientSoundStates(chunk), std::back_inserter(data.ambient));
        break;
    case W8_CHUNK_PARTICLES:
        std::ranges::move(ReadParticleStates(chunk), std::back_inserter(data.particles));
        break;
    case W8_CHUNK_LIGHTS:
        std::ranges::move(ReadLightStates(chunk), std::back_inserter(data.lights));
        break;
    }
}

/* A level's LVLS group as a RIFF image of its own, copied from `source`'s current chunk. */
std::vector<std::byte> CopyLevelGroupImage(W8Chunk& source)
{
    const std::shared_ptr<wiz8::File> file = wiz8::open_memory_writer();
    W8Chunk image;
    RequireSection(image.OpenWrite(file), "level image");
    RequireSection(image.CopyCurrentChunkFrom(&source), "level image copy");
    image.Close();
    return file->written_memory();
}

// Open a level image for reading.
void OpenLevelImage(W8Chunk& chunk, std::span<const std::byte> image)
{
    RequireSection(chunk.OpenRead(wiz8::open_memory({image.begin(), image.end()})), "level image");
}

/* Autosaves wait for a quiet moment: no world-cursor lock, nobody dying, no
   combat, sight override, uninterruptible movement, dialogue or camp. */
bool CanAutoSaveNow()
{
    return CurrentGame().status.world_cursor_gate == 0 && !AnyMonsterDying() &&
           !CurrentGame().combat.has_value() && !IsSightRangeOverridden() &&
           CanInterruptLevelMovement() && !CurrentGame().activity.conversation &&
           !CurrentGame().activity.conversation_aside;
}

/* Iron Man games overwrite their own slot instead of the AutoSave slot. */
std::string_view AutoSaveSlotName()
{
    return CurrentGame().status.iron_man ? GetLastSaveName() : kAutoSaveName;
}

void ShowMainGameNotice(int string_id)
{
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME)
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[string_id]);
}

} // namespace

/* 0x0061A134/0x0061A138: the masks an iron-man save XORs its file's creation
   time with before storing it in the status block. */
static unsigned int g_save_filetime_xor_low = 0x6b24e9f0;
static unsigned int g_save_filetime_xor_high = 0xe77c28c1;

/* The path of a loose character file. Before the game starts characters live
   under Saves\Characters (slot -1) or Saves\NPCs; in a started game, a slot
   whose flag is clear still reads Saves\NPCs and every other name is one of the
   game's stored characters. */
std::string BuildCharacterFilePath(std::string_view filename, int slot)
{
    if (!GameStarted())
        return fmt::format("{}\\{}", slot == -1 ? "saves\\characters" : "saves\\npcs", filename);
    if (slot != -1 && !CurrentGame().status.flags[slot])
        return fmt::format("saves\\npcs\\{}", filename);
    return std::string(filename);
}

std::string BuildCharacterPath(std::string_view name, int slot)
{
    return BuildCharacterFilePath(fmt::format("{}.CHR", name), slot);
}

/* Load one character record from its loose file, or from the game's stored characters in
   a started game. */
bool LoadCharacter(std::string_view name, W8Character* character, int slot)
try {
    bool loaded = false;
    try {
        const auto path = BuildCharacterFilePath(name, slot);
        if (GameStarted() && (slot == -1 || CurrentGame().status.flags[slot] != 0)) {
            const auto* stored = FindStoredCharacter(CurrentGame().stored_characters, path);
            if (stored) *character = stored->character;
            loaded = stored != nullptr;
        } else {
            auto handle =
                wiz8::open_file(slot == -1 ? wiz8::user_path(path) : wiz8::asset_path(path));
            *character = {};
            W8Character saved{};
            wiz8::retail::read_sized(*handle, saved);
            *character = std::move(saved);
            loaded = true;
        }
    } catch (const std::exception&) {
        loaded = false;
    }
    return loaded;
} catch (const std::exception&) {
    return false;
}

void FillCurrentSaveSlot(W8SaveSlot& slot)
{
    slot.name.clear();
    slot.level_id = GetLoadedLevelID();
    slot.game_time_ms = CurrentGame().status.game_time_ms;
    slot.game_time_days = CurrentGame().status.game_time_days;
    slot.iron_man = CurrentGame().status.iron_man;
    const auto now = wiz8::current_local_time();
    slot.timestamp = now;
    CaptureSaveScreenshot(CurrentGame(), slot.screenshot);
    slot.version_major = kCurrentSaveVersion.major;
    slot.version_minor = kCurrentSaveVersion.minor;
    slot.version_patch = kCurrentSaveVersion.patch;
}

/* Read one save's listing entry: its status, version and screenshot. Saves
   without a status, flagged saves and saves past the endgame are not listed. */
static std::unique_ptr<W8SaveSlot> ReadSaveSlot(const std::string& name,
                                                const wiz8::FileStatus& metadata)
{
    W8Chunk chunks;
    if (!chunks.OpenRead(wiz8::user_path("saves\\" + name))) return nullptr;
    auto slot = std::make_unique<W8SaveSlot>();
    GameStatus saved{};
    bool has_status = false;
    slot->screenshot.capture_result = W8ScreenshotCaptureResult::failed;
    slot->version_major = kUnversionedSave.major;
    slot->version_minor = kUnversionedSave.minor;
    slot->version_patch = kUnversionedSave.patch;
    chunks.ForEachChunk([&](unsigned int id) {
        switch (id) {
        case W8_CHUNK_GAME_STATUS:
            saved = wiz8::save::decode_game_state(ReadChunkPayload(chunks)).status;
            has_status = true;
            break;
        case W8_CHUNK_GAME_VERSION:
            chunks.ReadValue(slot->version_major);
            chunks.ReadValue(slot->version_minor);
            chunks.ReadValue(slot->version_patch);
            break;
        case W8_CHUNK_SCREENSHOT:
            chunks.ReadValue(slot->screenshot);
            break;
        }
    });
    chunks.Close();
    const auto& status = saved;
    if (!has_status || status.flag || status.endgame_started) return nullptr;
    slot->name = name.substr(0, name.find_last_of('.'));
    slot->write_time = metadata.info.modify_time;
    if (!SDL_TimeToDateTime(slot->write_time, &slot->timestamp, true))
        throw std::runtime_error(SDL_GetError());
    slot->level_id = status.resume_level;
    slot->game_time_ms = status.game_time_ms;
    slot->iron_man = status.iron_man;
    slot->game_time_days = status.game_time_days;
    return slot;
}

/* Append every readable save after the slots already present, newest first. */
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots)
try {
    if (!wiz8::file_status(wiz8::user_path("saves"))) return true;
    const auto first = static_cast<std::ptrdiff_t>(slots.size());
    for (const auto& name :
         wiz8::list_directory(wiz8::user_path("saves"), std::string("*.") + g_save_extension)) {
        const auto metadata = wiz8::file_status(wiz8::user_path("saves\\" + name));
        if (!metadata || metadata->info.type != SDL_PATHTYPE_FILE) continue;
        if (IsCurrentGameFileName(name) || name.size() >= kCharacterNameBytes) continue;
        std::unique_ptr<W8SaveSlot> slot;
        try {
            slot = ReadSaveSlot(name, *metadata);
        } catch (const std::exception&) {
            continue; // An unreadable save is left out of the list.
        }
        if (!slot) continue;
        const auto ticks = slot->write_time;
        const auto position =
            std::find_if(slots.begin() + first, slots.end(),
                         [&](const auto& listed) { return ticks > listed->write_time; });
        slots.insert(position, std::move(slot));
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* The level a save was made on, read from its status chunk alone; 0 when the
   save cannot be read. */
int GetSaveGameLevel(std::string_view slot_name)
try {
    W8Chunk chunks;
    if (!chunks.OpenRead(wiz8::user_path(SaveSlotPath(slot_name)))) return 0;
    GameStatus saved{};
    bool found = false;
    chunks.ForEachChunk([&](unsigned int id) {
        if (id != W8_CHUNK_GAME_STATUS) return true;
        saved = wiz8::save::decode_game_state(ReadChunkPayload(chunks)).status;
        found = true;
        return false;
    });
    chunks.Close();
    return found ? saved.resume_level : 0;
} catch (const std::exception&) {
    return 0;
}

/* A save's copy of the game state, stamped with what the save records about itself: the
   camera, the status version, the difficulty and an Iron Man save's file time. */
static wiz8::save::GameState CaptureStateForSave(const Game& game, const wiz8::File& file)
{
    const auto* held = game.inventory.held();
    wiz8::save::GameState snapshot{game.status,
                                   game.party,
                                   {game.inventory.pool().begin(), game.inventory.pool().end()},
                                   held ? std::optional{held->item} : std::nullopt,
                                   game.search.requested()};
    auto& status = snapshot.status;
    status.pending_move_location = game.level.primary->party.pose();
    status.retail.save_version = 1.1f;
    status.difficulty = g_settings.difficulty;
    if (status.iron_man) {
        const auto creation_time = file.times().created;
        status.retail.save_filetime_xor = {creation_time.low ^ g_save_filetime_xor_low,
                                           creation_time.high ^ g_save_filetime_xor_high};
    }
    return snapshot;
}

LevelState LevelState::capture(const Level& level, const GameStatus& status)
{
    const World& world = *level.primary;
    LevelState captured;
    auto& state = *captured.data_;
    state.level = world.level_id;
    state.header = CaptureStatusHeader(status);
    auto& population = *world.population;
    // Live list entries come first, then encounter groups and unborn monsters.
    for (const auto* partition : {&population.monster_groups, &population.encounter_groups}) {
        for (const auto& group : *partition) {
            auto& saved = state.groups.emplace_back(SavedMonsterGroup{
                wiz8::retail::encode_image(*group), partition == &population.encounter_groups});
            saved.image.version = kMonsterGroupRecordVersion;
            saved.image.active_member_count =
                wiz8::checked_integer<std::int32_t>(population.activeMemberCount(*group));
        }
    }
    for (const auto* partition :
         {&population.monster_roster.live(), &population.monster_roster.unborn()})
        for (const auto& monster : *partition)
            if (monster->monster_group_id != 0)
                state.monsters.push_back(SavedMonster::Capture(
                    *monster, population.monster_roster.isUnborn(monster.get())));
    for (const auto& item : population.world_items) state.items.push_back(CaptureWorldItem(*item));
    state.automap = world.automap.capture();
    if (!world.triggers.empty()) {
        auto& triggers = state.world_sections.emplace_back(std::vector<SavedTrigger>{});
        for (const auto& trigger : world.triggers)
            std::get<std::vector<SavedTrigger>>(triggers).push_back(trigger->Capture());
    }
    auto& props = std::get<std::vector<SavedProp>>(
        state.world_sections.emplace_back(std::vector<SavedProp>{}));
    for (const auto& prop : world.plsProps)
        props.push_back({prop->name, prop->CaptureAnimationState()});
    auto& cursor_states = state.cursor_states.emplace();
    for (const auto& node : world.cursor_nodes) {
        auto& entry = cursor_states.entries.emplace_back();
        std::ranges::copy(node->name, entry.name.begin());
        entry.userdata = node->userdata;
    }
    state.generator_states = world.encounters.savedGeneratorStates();
    auto& locks =
        std::get<SavedTriggerLocks>(state.world_sections.emplace_back(SavedTriggerLocks{}));
    for (const auto& trigger : world.triggers)
        if (auto lock = trigger->CaptureLock()) locks.locks.push_back(std::move(*lock));
    state.ambient = world.audio.savedStates();
    for (const auto& particle : world.particles)
        if (particle->persisted)
            state.particles.push_back({particle->getName(), particle->emitting});
    // A trigger can toggle a light of the sky as well as of the level.
    for (const auto* lit : {level.primary.get(), level.sky.get()})
        if (lit && lit->scene)
            for (const auto* light : lit->scene->lights)
                if (light->m_save_marked)
                    state.lights.push_back({light->getName(), light->visible});
    return captured;
}

/* The LVLS group: the level number, STAT, MONS and ITEM, then the automap, the trigger and
   prop sections, the cursor node and generator states, the lock and action sections, and
   the ambient sounds, particles and lights, in the order retail writes them. A save does
   not repeat the asset status's cursor nodes and encounters. */
int LevelState::level() const
{
    return data_->level;
}

void LevelState::encode(W8Chunk& chunks) const
{
    const auto& state = *data_;
    auto& file = chunks.WriteStream();
    const auto sections = [&]<class Section>(unsigned int id) {
        for (const auto& section : state.world_sections)
            if (const auto* value = std::get_if<Section>(&section))
                chunks.WriteChunk(id, [&] {
                    if constexpr (std::is_same_v<Section, std::vector<SavedTrigger>>)
                        WriteSavedTriggers(file, *value);
                    else if constexpr (std::is_same_v<Section, std::vector<SavedProp>>)
                        WriteSavedProps(file, *value);
                    else if constexpr (std::is_same_v<Section, SavedTriggerLocks>)
                        WriteSavedTriggerLocks(file, *value);
                    else
                        WriteSavedTriggerActions(file, *value);
                });
    };
    chunks.WriteGroup(W8_CHUNK_LEVEL_STATUS, [&] {
        chunks.WriteValue(state.level);
        if (state.header)
            WriteChunkPayload(chunks, W8_CHUNK_STATUS_HEADER, wiz8::save::encode(*state.header));
        chunks.WriteChunk(W8_CHUNK_MONSTERS, [&] {
            chunks.WriteValue(wiz8::checked_integer<std::uint32_t>(state.groups.size()));
            chunks.WriteValue(wiz8::checked_integer<std::uint32_t>(state.monsters.size()));
            for (const auto& group : state.groups) group.Write(chunks);
            for (const auto& monster : state.monsters) monster.Write(chunks);
        });
        chunks.WriteChunk(W8_CHUNK_WORLD_ITEMS, [&] {
            chunks.WriteValue(wiz8::checked_integer<std::uint32_t>(state.items.size()));
            for (const auto& item : state.items) WriteSavedWorldItem(file, item);
        });
        if (state.automap)
            chunks.WriteChunk(W8_CHUNK_AUTOMAP_NOTES, [&] { state.automap->write(file); });
        sections.operator()<std::vector<SavedTrigger>>(W8_CHUNK_TRIGGERS);
        sections.operator()<std::vector<SavedProp>>(W8_CHUNK_PROPS);
        if (state.cursor_states)
            chunks.WriteChunk(W8_CHUNK_CURSOR_NODE_STATES,
                              [&] { WriteWorldCursorNodeStates(file, *state.cursor_states); });
        if (state.generator_states)
            chunks.WriteChunk(W8_CHUNK_MONSTER_GENERATORS,
                              [&] { WriteMonsterGeneratorStates(file, *state.generator_states); });
        sections.operator()<SavedTriggerLocks>(W8_CHUNK_TRIGGER_STATES);
        sections.operator()<std::vector<SavedTriggerAction>>(W8_CHUNK_TRIGGER_ACTIONS);
        chunks.WriteChunk(W8_CHUNK_AMBIENT_SOUNDS,
                          [&] { WriteAmbientSoundStates(file, state.ambient); });
        chunks.WriteChunk(W8_CHUNK_PARTICLES, [&] { WriteParticleStates(file, state.particles); });
        chunks.WriteChunk(W8_CHUNK_LIGHTS, [&] { WriteLightStates(file, state.lights); });
    });
}

/* Write one whole save: the CHAR and other-level chunks carried over from
   CurrentGame (unless this is CurrentGame), the status, version, screenshot
   (captured now when the caller has none), the message, location, NPC, fact,
   faction, journal and monster-control sections, and the current level's
   LVLS group last. */
static void WriteGame(std::string_view name, const W8SaveScreenshot* screenshot, W8Chunk& chunks)
{
    auto& game = CurrentGame();
    // The levels the party left, as they were left; the current level is written live below.
    game.left_levels.write(chunks, game.level.primary->level_id);
    for (const auto& stored : game.stored_characters)
        WriteChunkPayload(chunks, W8_CHUNK_CHARACTER, wiz8::save::encode(stored));
    {
        auto snapshot = CaptureStateForSave(game, chunks.WriteStream());
        // Normalize transient UI state in the save image, without changing the game.
        if (snapshot.status.text_line_cursor == 2) snapshot.status.text_line_cursor = 0;
        snapshot.status.resume_level = GetLoadedLevelID();
        WriteChunkPayload(chunks, W8_CHUNK_GAME_STATUS, wiz8::save::encode(snapshot));
    }
    chunks.WriteChunk(W8_CHUNK_GAME_VERSION, [&] {
        chunks.WriteValue(kCurrentSaveVersion.major);
        chunks.WriteValue(kCurrentSaveVersion.minor);
        chunks.WriteValue(kCurrentSaveVersion.patch);
    });
    W8SaveScreenshot captured{};
    if (!screenshot) {
        CaptureSaveScreenshot(game, captured);
        screenshot = &captured;
    }
    chunks.WriteChunk(W8_CHUNK_SCREENSHOT, [&] { chunks.WriteValue(*screenshot); });

    const auto& story = game.story;
    WriteChunkPayload(chunks, W8_CHUNK_MESSAGE_STORAGE,
                      wiz8::save::encode(CaptureMessageBoxes(game)));
    if (const auto variables = CaptureLocationVariables(game, GetWorld()); !variables.empty())
        WriteChunkPayload(chunks, W8_CHUNK_LOCATION_VARIABLES, wiz8::save::encode(variables));
    WriteChunkPayload(chunks, W8_CHUNK_NPC_TRANSCRIPT,
                      wiz8::save::encode(CaptureDialogueTranscript(CurrentGame().story,
                                                                   CurrentGame().npc_interaction)));
    WriteChunkPayload(chunks, W8_CHUNK_NPC_STATES, wiz8::save::encode(story.npcs));
    WriteChunkPayload(chunks, W8_CHUNK_FACT_STATE, wiz8::save::encode(story.facts));
    WriteChunkPayload(
        chunks, W8_CHUNK_FACTION_STATE,
        wiz8::save::encode(wiz8::save::Factions{story.faction_relations, story.factions}));
    WriteChunkPayload(chunks, W8_CHUNK_JOURNAL, wiz8::save::encode(story.journal));
    if (const auto* control = game.level.loaded().monsterControlEffect())
        WriteChunkPayload(chunks, W8_CHUNK_MONSTER_CONTROL, wiz8::save::encode(*control));
    LevelState::capture(game.level, game.status).encode(chunks);
}

bool SaveGame(std::string_view name, const W8SaveScreenshot* screenshot)
try {
    if (name.empty()) return false;
    SaveTransaction transaction(SaveSlotPath(name));
    WriteGame(name, screenshot, transaction.chunks);
    transaction.commit();
    CurrentGame().autosave_due = Game::kAutosaveInterval;
    return true;
} catch (const std::exception&) {
    return false;
}

/* The shipped status file for a level. Levels 0-55 index the level folder
   table; retail's table has 47 entries, so levels 47-55 read past it exactly
   as the original does. Level 56 is the default test level and later levels
   are lettered test levels. */
std::string BuildLevelStatusPath(unsigned int level)
{
    if (level == 56) return "Levels\\Test\\DefaultLevel.STS";
    if (level < 56)
        return fmt::format("Levels\\{}\\{}.STS", g_level_folders[level].folder_name,
                           g_level_folders[level].level_name);
    return fmt::format("Levels\\Test\\Level{:c}.STS", static_cast<char>(level - 56));
}

void LeftLevels::store(const LevelState& state)
{
    const std::shared_ptr<wiz8::File> file = wiz8::open_memory_writer();
    W8Chunk image;
    RequireSection(image.OpenWrite(file), "level image");
    state.encode(image);
    image.Close();
    images_[state.level()] = file->written_memory();
}

void LeftLevels::keep(int level, std::vector<std::byte> image)
{
    images_[level] = std::move(image);
}

std::optional<LevelState> LeftLevels::decode(int level) const
{
    const auto left = images_.find(level);
    if (left == images_.end()) return std::nullopt;
    return LevelState::decode(left->second, level);
}

void LeftLevels::write(W8Chunk& chunks, int current) const
{
    for (const auto& [level, image] : images_) {
        if (level == current) continue;
        W8Chunk source;
        OpenLevelImage(source, image);
        source.ForEachChunk([&](unsigned int id) {
            if (id == W8_CHUNK_LEVEL_STATUS)
                RequireSection(chunks.CopyCurrentChunkFrom(&source), "save chunk copy");
        });
        source.Close();
    }
}

/* Level `level`'s group in `chunk`, or nothing when the file has none. Group and monster
   identities must be unique. */
std::optional<LevelState> LevelState::decode(W8Chunk& chunk, int level, bool from_assets)
{
    LevelState decoded;
    auto& state = *decoded.data_;
    state.level = level;
    state.from_assets = from_assets;
    const bool found = WithLevelGroup(chunk, level, [&] {
        chunk.ForEachChunk(
            [&](unsigned int section) { ReadLevelSection(state, chunk, section, from_assets); });
    });
    if (!found) return std::nullopt;
    std::unordered_set<int> group_ids, monster_ids;
    for (const auto& group : state.groups)
        if (!group_ids.insert(group.image.group_id).second)
            throw std::runtime_error("duplicate saved monster group");
    for (const auto& saved : state.monsters)
        if (!monster_ids.insert(saved.image.location_id).second)
            throw std::runtime_error("duplicate saved monster identity");
    return decoded;
}

namespace {
/* A saved level takes the sections a save does not repeat (locks, cursor nodes, encounters
   and trigger actions) from the level's asset status, ahead of its own; then the saved
   cursor node and generator states update the nodes and generators. */
void ApplyLevelBase(LevelState::Data& state)
{
    if (!state.from_assets) {
        LevelState::Data base;
        W8Chunk baseline;
        if (baseline.OpenRead(wiz8::asset_path(BuildLevelStatusPath(state.level)))) {
            WithLevelGroup(baseline, std::nullopt, [&] {
                baseline.ForEachChunk([&](unsigned int section) {
                    if (section == W8_CHUNK_TRIGGER_LOCKS || section == W8_CHUNK_CURSOR_NODES ||
                        section == W8_CHUNK_ENCOUNTERS || section == W8_CHUNK_TRIGGER_ACTIONS)
                        ReadLevelSection(base, baseline, section, true, true);
                });
            });
        }
        state.cursors = std::move(base.cursors);
        state.encounters = std::move(base.encounters);
        state.world_sections.insert(state.world_sections.begin(),
                                    std::make_move_iterator(base.world_sections.begin()),
                                    std::make_move_iterator(base.world_sections.end()));
    }
    if (state.cursor_states) {
        if (!state.cursors) state.cursors.emplace();
        ApplyWorldCursorNodeStates(*state.cursors, std::move(*state.cursor_states));
    }
    if (state.generator_states) {
        if (!state.encounters) state.encounters.emplace();
        state.encounters->applyGeneratorStates(*state.generator_states);
    }
}

/* The scripts building a level loads for its monsters: the encounter default, kept until
   activation assigns it, and each saved monster's own, by the monster's index. */
struct LevelScripts {
    std::shared_ptr<Script> default_script;
    std::vector<std::shared_ptr<Script>> saved;
};

/* Install the decoded groups, items and monsters in `world`'s population, link them, and
   restore the encounters, automap, ambient sounds, particles and lights. */
void PrepareLevelPopulation(Game& game, World& world, LevelState::Data& data,
                            std::vector<std::unique_ptr<W8MonsterInfo>> infos, LevelScripts scripts,
                            W8LoadedLevelStatus::Data& entry)
{
    auto& population = *world.population;
    for (const auto& saved : data.groups) {
        if (game.data.monsters.at(saved.image.monster_id).deleted) continue;
        auto& list = saved.encounter ? population.encounter_groups : population.monster_groups;
        list.push_back(saved.Build());
    }
    for (const auto& item : data.items) population.world_items.push_back(BuildWorldItem(item));
    std::vector<W8MonsterInfo*> records;
    records.reserve(data.monsters.size());
    for (std::size_t index = 0; index < data.monsters.size(); ++index) {
        auto& saved = data.monsters[index];
        auto* info = infos[index].get();
        if (!info) {
            records.push_back(nullptr);
            continue;
        }
        auto* group = population.findGroup(info->monster_group_id);
        if (!group) throw std::runtime_error("missing prepared monster group");
        group->monsters.push_back(info->location_id);
        if (static_cast<unsigned int>(group->leader_location_id) <
            static_cast<unsigned int>(info->location_id))
            group->leader_location_id = info->location_id;
        records.push_back(population.monster_roster.add(std::move(infos[index]), saved.unborn));
    }
    for (size_t index = 0; index < records.size(); ++index) {
        if (auto* info = records[index]) {
            info->configureBody(game.rng, game.status.level_progress[world.level_id].visited != 0);
            world.prepareMonsterBody(*info, game.rng, data.monsters[index].unborn);
        }
    }
    entry.npc_bindings = game.prepareNpcBindings(world);
    for (size_t index = 0; index < records.size(); ++index) {
        auto* info = records[index];
        if (!info) continue;
        auto& saved = data.monsters[index];
        if (saved.movement) saved.movement->Apply(*info->entity);
        if (saved.orders) saved.orders->Apply(*info, *info->entity);
        if (saved.script)
            saved.script->Apply(*info, *info->entity, std::move(scripts.saved[index]));
        if (info->hp_current == 0) entry.pending_deaths.push_back(info->location_id);
    }
    // Resolve durable links against this population, preserving stored member order.
    for (auto& group : population.monster_groups) {
        if (group->monsters.empty()) throw std::runtime_error("empty prepared monster group");
        if (!population.findMonster(group->leader_location_id))
            group->leader_location_id = group->monsters.back();
        if (group->leader_group_id == group->group_id) group->leader_group_id = 0;
        if (group->leader_group_id && !population.findGroup(group->leader_group_id))
            group->leader_group_id = 0;
        for (int& ally : group->allied_group_ids)
            if (ally == group->group_id || (ally && !population.findGroup(ally))) ally = 0;
        Monster& leader = *population.findMonster(group->leader_location_id)->entity;
        leader.formation = group->formation;
        if (group->encounter_registered && !leader.script)
            leader.UseScript(scripts.default_script, true);
        auto* lead_group = group.get();
        size_t remaining = population.monster_groups.size();
        while (lead_group->leader_group_id) {
            if (remaining-- == 0) throw std::runtime_error("cyclic monster group leaders");
            lead_group = population.findGroup(lead_group->leader_group_id);
            if (!lead_group) throw std::runtime_error("missing monster group leader");
        }
        auto* formation_leader_info = population.findMonster(lead_group->leader_location_id);
        if (!formation_leader_info) throw std::runtime_error("missing formation leader");
        Monster& formation_leader = *formation_leader_info->entity;
        for (int id : group->MemberIds()) {
            Monster& member = *population.findMonster(id)->entity;
            member.nav.LinkGroupNavigator(
                &member == &formation_leader ? nullptr : &formation_leader.nav, false);
        }
    }
    for (const auto& group : population.monster_groups) {
        if (group->leader_group_id) continue;
        Monster& leader = *population.findMonster(group->leader_location_id)->entity;
        // Restored levels start outside combat. Propagate the saved path using
        // this population, independently of the old world's combat clock.
        leader.nav.CopyPathToGroup();
        if (!leader.nav.movement_stopped) {
            for (const auto& member : population.monster_roster.live())
                if (member->entity->nav.linked_navigator == &leader.nav)
                    member->entity->nav.movement_stopped = false;
        }
    }
    if (data.encounters) world.encounters = std::move(*data.encounters);
    if (data.automap) world.automap.restore(std::move(*data.automap));
    ApplyAmbientSoundStates(world, data.ambient);
    ApplyParticleStates(world, data.particles);
    ApplyLightStates(world, data.lights);
}
} // namespace

std::optional<LevelState> LevelState::decode(const std::filesystem::path& path, int level,
                                             bool from_assets)
{
    W8Chunk chunk;
    if (!chunk.OpenRead(path)) return std::nullopt;
    return decode(chunk, level, from_assets);
}

std::optional<LevelState> LevelState::decode(std::span<const std::byte> image, int level)
{
    W8Chunk chunk;
    OpenLevelImage(chunk, image);
    return decode(chunk, level, false);
}

std::vector<std::unique_ptr<W8WorldItem>> LevelState::buildItems() const
{
    std::vector<std::unique_ptr<W8WorldItem>> items;
    for (const auto& item : data_->items) items.push_back(BuildWorldItem(item));
    return items;
}

// A monster of a deleted species is dropped, so only the others need their group.
void LevelState::checkSpecies(const GameData& data) const
{
    const auto& state = *data_;
    for (const auto& group : state.groups)
        if (group.image.monster_id < 0 ||
            std::cmp_greater_equal(group.image.monster_id, data.monsters.size()))
            throw std::runtime_error("missing saved monster group species");
    for (const auto& saved : state.monsters) {
        const auto species = saved.image.monster_species;
        if (species >= data.monsters.size())
            throw std::runtime_error("missing saved monster species");
        if (data.monsters[species].deleted) continue;
        const auto group = std::ranges::find_if(state.groups, [&](const auto& saved_group) {
            return saved_group.image.group_id == saved.image.monster_group_id;
        });
        if (group == state.groups.end() || data.monsters.at(group->image.monster_id).deleted)
            throw std::runtime_error("missing saved monster group");
    }
}

std::unique_ptr<W8LoadedLevelStatus> LevelState::build(Game& game, World& world) &&
{
    auto& state = *data_;
    ApplyLevelBase(state);
    checkSpecies(game.data);
    // Retail drew the generators' jittered intervals as it read them, before building anything.
    if (state.encounters) state.encounters->rearmDecodedGenerators(game.rng);
    auto status = std::make_unique<W8LoadedLevelStatus>();
    status->data->header = state.header;
    for (auto& section : state.world_sections) ApplySavedSection(world, section);
    LevelScripts scripts{.saved = std::vector<std::shared_ptr<Script>>(state.monsters.size())};
    // A monster of a deleted species has no record.
    std::vector<std::unique_ptr<W8MonsterInfo>> infos(state.monsters.size());
    for (std::size_t index = 0; index < state.monsters.size(); ++index) {
        const auto& saved = state.monsters[index];
        if (game.data.monsters.at(saved.image.monster_species).deleted) continue;
        auto& info = *(infos[index] = saved.Build());
        const auto group = std::ranges::find_if(state.groups, [&](const auto& saved_group) {
            return saved_group.image.group_id == info.monster_group_id;
        });
        const auto& record = game.data.monsters.at(info.monster_species);
        auto model = world.loadMonsterBody(record, info.location_id, game.rng);
        info.entity = std::move(model);
        RestoreMonsterSpellIcons(world, info, *info.entity);
        if (!info.entity->script && !scripts.default_script && group->image.encounter_registered)
            scripts.default_script = LoadMonsterScript("Default.MSF");
        if (saved.script && saved.script->name[0] != '\0')
            scripts.saved[index] = LoadMonsterScript(saved.script->name);
    }
    if (state.cursors) BuildWorldCursorNodes(world, std::move(*state.cursors));
    PrepareLevelPopulation(game, world, state, std::move(infos), std::move(scripts), *status->data);
    return status;
}

// Publish shared session references and scalar state only. The replacement
// already owns every record, representation, membership and identity link.
void InstallLevelSessionState(Game& game, World& world, W8LoadedLevelStatus* status)
{
    game.status.next_group_id = game.status.next_monster_location_id =
        game.status.next_world_item_id = 1;
    game.status.next_trigger_id = world.next_trigger_id;
    if (!status) return;
    auto& data = *status->data;
    if (data.header) ApplyStatusHeader(*data.header, game.status);
    game.story.installNpcBindings(std::move(data.npc_bindings));
}

void BeginRestoredLevelActivity(World& world, W8LoadedLevelStatus* status)
{
    StartLevelEncounters(world);
    if (!status) return;
    auto& data = *status->data;
    for (int id : data.pending_deaths)
        if (auto* info = FindMonster(id, false)) MonsterStartsDying(info, true);
}

/* Create the writable save directories in the user directory. */
bool VerifyDataSubdirs(void)
try {
    const char* directories[]{"saves", "saves\\characters", "saves\\npcs"};
    return std::ranges::all_of(directories, [](const char* directory) {
        wiz8::create_directory(wiz8::user_path(directory));
        const auto entry = wiz8::file_status(wiz8::user_path(directory));
        return entry && entry->info.type == SDL_PATHTYPE_DIRECTORY;
    });
} catch (const std::exception&) {
    return false;
}

/* The retail flat sequence: each record marks the saved slot and whether another
   follows. An active item's position and flags come from its entity. */
// FUNCTION: WIZ8 0x00514be0
SavedWorldItem CaptureWorldItem(const W8WorldItem& item_info)
{
    SavedWorldItem records;
    for (std::size_t index = 0; index <= item_info.contents.size(); ++index) {
        const auto* item = index == 0 ? &item_info : item_info.contents[index - 1].get();
        auto& image = records.emplace_back(wiz8::retail::encode_image(*item));
        image.saved_marker = 1;
        image.next = index < item_info.contents.size() ? 1 : 0;
        if (item->entity) {
            const auto position = item->entity->GetLocation();
            image.position = {position.x, position.y, position.z};
            image.entity_flags = item->entity->flags;
        }
    }
    return records;
}

void WriteSavedWorldItem(wiz8::File& handle, const SavedWorldItem& item)
{
    handle.write(item.data(), item.size() * sizeof(item[0]));
}

/* A saved next pointer only marks that another record follows. */
// FUNCTION: WIZ8 0x00514c80
SavedWorldItem ReadSavedWorldItem(W8Chunk& handle, bool from_assets)
{
    SavedWorldItem records;
    do {
        auto& image = records.emplace_back(wiz8::retail::read_image<W8WorldItem>(handle));
        if (from_assets) image.entity_flags &= ~W8_ITEM_ENTITY_RADAR_SEEN;
    } while (records.back().next != 0);
    return records;
}

/* All records are staged before publication to the item list and search registry. */
std::unique_ptr<W8WorldItem> BuildWorldItem(const SavedWorldItem& records)
{
    std::unique_ptr<W8WorldItem> head;
    for (const auto& image : records) {
        auto entry = std::make_unique<W8WorldItem>();
        wiz8::retail::decode_image(image, *entry);
        entry->sector_id = -2;
        if (!head)
            head = std::move(entry);
        else
            head->contents.push_back(std::move(entry));
    }
    return head;
}

/* Whether any save other than CurrentGame exists. */
bool SaveGameExists(void)
{
    const auto saves =
        wiz8::list_directory(wiz8::user_path("saves"), std::string("*.") + g_save_extension);
    return std::ranges::any_of(saves,
                               [](const auto& name) { return !IsCurrentGameFileName(name); });
}

/* Write a character record. Before the game starts it is a loose file (its
   read-only bit cleared first); afterwards one of the game's stored characters. */
bool SaveCharacter(const W8Character* character, int slot)
try {
    auto snapshot = *character;
    snapshot.record_version = 1;
    const auto path = BuildCharacterPath(character->name, slot);
    bool saved = false;
    try {
        if (GameStarted()) {
            // The CHAR section's name field holds the name and its terminator.
            if (path.empty() || path.size() >= kCharacterNameBytes) return false;
            StoreCharacter(CurrentGame().stored_characters, {path, snapshot});
            saved = true;
        } else {
            const auto status = wiz8::file_status(wiz8::user_path(path));
            if (status && status->read_only) wiz8::clear_read_only(wiz8::user_path(path));
            wiz8::StagedFile staged(wiz8::user_path(path));
            wiz8::retail::write_sized(*staged.file(), snapshot);
            staged.commit();
            saved = true;
        }
    } catch (const std::exception&) {
        saved = false;
    }
    return saved;
} catch (const std::exception&) {
    return false;
}

/* Capture the world offscreen into the save's 80x60 ARGB1555 image. */
void CaptureSaveScreenshot(Game& game, W8SaveScreenshot& screenshot)
{
    screenshot.version = 1.0f;
    DesignRect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = 640;
    rect.bottom = 480;
    auto surface = CreateCpuStorage(80, 60, SDL_PIXELFORMAT_ARGB1555, screenshot.pixels, 160);
    screenshot.capture_result = CaptureGameView(game, *surface, rect)
                                    ? W8ScreenshotCaptureResult::succeeded
                                    : W8ScreenshotCaptureResult::failed;
}

/* Delete the last-used slot. */
void DeleteCurrentSaveFiles(void)
{
    wiz8::remove_file(wiz8::user_path(SaveSlotPath(GetLastSaveName())));
}

/* Save now if autosaving is enabled (or the game is Iron Man) and the moment
   allows it. A declined autosave counts as success. */
bool AutoSaveIfAllowed(bool forced)
{
    CurrentGame().save_notice_shown = false;
    const bool wanted = (g_settings.auto_save && !forced) || CurrentGame().status.iron_man;
    if (!wanted || !CanAutoSaveNow()) return true;
    return SaveGame(AutoSaveSlotName(), nullptr);
}

/* Take the pending-save flag, clearing it. */
bool TakePendingSaveFlag(void)
{
    return std::exchange(g_save_pending, false);
}

bool SaveSlotFileExists(std::string_view slot_name)
{
    return IsRegularFile(SaveSlotPath(slot_name));
}

/* Announce an autosave; the next ProcessMainGameAutoSave tick performs it. */
void ReportSaveFailed(bool quiet)
{
    if (quiet && !CurrentGame().status.iron_man) return;
    CurrentGame().save_notice_shown = true;
    ShowMainGameNotice(W8_STRING_AUTOSAVE_STARTING);
}

/* The timed main-game autosave runs over two frames: once the gameplay timer
   has elapsed the first eligible frame announces it, and the next eligible
   frame saves and reports the result. */
void ProcessMainGameAutoSave(void)
{
    if ((!g_settings.auto_save && !CurrentGame().status.iron_man) || !CanAutoSaveNow()) return;
    if (!CurrentGame().save_notice_shown) {
        if (CurrentGame().autosave_due >= std::chrono::milliseconds::zero()) return;
        CurrentGame().save_notice_shown = true;
        ShowMainGameNotice(W8_STRING_AUTOSAVE_STARTING);
        CurrentGame().autosave_due = Game::kAutosaveInterval;
        return;
    }
    CurrentGame().save_notice_shown = false;
    const bool saved = SaveGame(AutoSaveSlotName(), nullptr);
    ShowMainGameNotice(saved ? W8_STRING_AUTOSAVE_SUCCEEDED : W8_STRING_AUTOSAVE_FAILED);
}

/* The quick-save slot to write next: the first missing one, otherwise the
   one written longest ago. */
std::optional<std::string> SelectQuickSaveSlotForWrite()
try {
    std::optional<std::pair<int, SDL_Time>> oldest;
    for (int slot = 1; slot <= 3; ++slot) {
        const auto path = SaveSlotPath(QuickSaveName(slot));
        const auto metadata = wiz8::file_status(wiz8::user_path(path));
        if (!metadata) return QuickSaveName(slot);
        const auto ticks = metadata->info.modify_time;
        if (!oldest || ticks < oldest->second) oldest.emplace(slot, ticks);
    }
    if (!oldest) return {};
    return QuickSaveName(oldest->first);
} catch (const std::exception&) {
    return {};
}

/* The newest numbered quick save, for command-line startup. The unnumbered
   Quick slot is used only when none of the numbered ones exists. */
std::optional<std::string> FindStartupQuickSave()
try {
    std::optional<std::pair<int, SDL_Time>> newest;
    for (int slot = 1; slot <= 3; ++slot) {
        const auto path = SaveSlotPath(QuickSaveName(slot));
        const auto metadata = wiz8::file_status(wiz8::user_path(path));
        if (!metadata || metadata->info.type != SDL_PATHTYPE_FILE) continue;
        const auto ticks = metadata->info.modify_time;
        if (!newest || ticks > newest->second) newest.emplace(slot, ticks);
    }
    if (newest) return QuickSaveName(newest->first);
    if (IsRegularFile(SaveSlotPath("Quick"))) return "Quick";
    return {};
} catch (const std::exception&) {
    return {};
}

/* Load the world items of a level the party left, without listing them. */
bool LoadSavedLevelItems(int level, std::vector<std::unique_ptr<W8WorldItem>>& items)
try {
    auto state = CurrentGame().left_levels.decode(level);
    if (!state) return false;
    std::ranges::move(state->buildItems(), std::back_inserter(items));
    return true;
} catch (const std::exception&) {
    return false;
}

/* The first free name of Ending, Ending1 ... Ending20. */
std::optional<std::string> FindFreeEndingSaveName()
try {
    for (int index = 0; index <= 20; ++index) {
        auto name = index == 0 ? std::string("Ending") : fmt::format("Ending{}", index);
        if (!IsRegularFile(SaveSlotPath(name))) return name;
    }
    return {};
} catch (const std::exception&) {
    return {};
}

/* Decode a save slot before the game in progress or its presentation is released, so a
   malformed section, nested level groups included, rejects the load and the current game
   resumes. */
std::optional<SavedGame> ReadSavedGame(const GameData& data, std::string_view slot_name)
try {
    W8Chunk chunks;
    if (!chunks.OpenRead(wiz8::user_path(SaveSlotPath(slot_name)))) return std::nullopt;
    SavedGame saved;
    bool has_state = false;
    // The message boxes' encoding follows the save version the status section names.
    auto save_version = GameStatus{}.retail.save_version;
    chunks.ForEachChunk([&](unsigned int id) {
        switch (id) {
        case W8_CHUNK_LEVEL_STATUS: {
            const int level = PeekGroupLevel(chunks);
            auto image = CopyLevelGroupImage(chunks);
            if (auto state = LevelState::decode(image, level)) state->checkSpecies(data);
            saved.levels.keep(level, std::move(image));
            break;
        }
        case W8_CHUNK_GAME_STATUS:
            saved.state = wiz8::save::decode_game_state(ReadChunkPayload(chunks));
            save_version = saved.state.status.retail.save_version;
            has_state = true;
            break;
        case W8_CHUNK_CHARACTER:
            // A malformed record is skipped; the last complete one under a name wins.
            try {
                StoreCharacter(saved.characters,
                               wiz8::save::decode_stored_character(ReadChunkPayload(chunks)));
            } catch (const std::runtime_error&) {
            }
            break;
        case W8_CHUNK_MESSAGE_STORAGE:
            saved.messages =
                wiz8::save::decode_message_boxes(ReadChunkPayload(chunks), save_version);
            break;
        case W8_CHUNK_LOCATION_VARIABLES:
            std::ranges::move(wiz8::save::decode_location_variables(ReadChunkPayload(chunks)),
                              std::back_inserter(saved.location_variables));
            break;
        case W8_CHUNK_NPC_TRANSCRIPT:
            saved.transcript = wiz8::save::decode_dialogue_transcript(ReadChunkPayload(chunks));
            break;
        case W8_CHUNK_NPC_STATES:
            saved.npcs = wiz8::save::decode_npc_states(ReadChunkPayload(chunks), data.npcs.size());
            break;
        case W8_CHUNK_FACT_STATE:
            saved.facts = wiz8::save::decode_facts(ReadChunkPayload(chunks));
            break;
        case W8_CHUNK_FACTION_STATE:
            saved.factions = wiz8::save::decode_factions(ReadChunkPayload(chunks));
            // The party's standing with itself is not saved.
            saved.factions->states[W8_FACTION_PARTY].disposition_score = 100;
            break;
        case W8_CHUNK_JOURNAL:
            saved.journal = wiz8::save::decode_journal(ReadChunkPayload(chunks));
            break;
        case W8_CHUNK_MONSTER_CONTROL:
            saved.spell_effects.push_back(std::make_unique<W8SpellEffectEntry>(
                wiz8::save::decode_monster_control(ReadChunkPayload(chunks))));
            break;
        }
    });
    chunks.Close();
    if (!has_state) throw std::runtime_error("save has no game status");
    return saved;
} catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot load save %.*s: %s",
                 static_cast<int>(slot_name.size()), slot_name.data(), error.what());
    return std::nullopt;
}

void SavedGame::install(Game& game) &&
{
    game.left_levels = std::move(levels);
    game.stored_characters = std::move(characters);
    game.status = std::move(state.status);
    game.party = std::move(state.party);
    game.inventory.restore(std::move(state.pool), state.held);
    game.search.restore(state.search_mode);
    if (messages) InstallMessageBoxes(std::move(*messages), game);
    game.story.restoreLocationVariables(std::move(location_variables));
    if (transcript)
        InstallDialogueTranscript(std::move(*transcript), game.story, game.npc_interaction);
    if (npcs) {
        // Bind each state to its record. The game has no level yet, so grouped NPCs wait for
        // the saved level to bind their monsters.
        auto& states = game.story.npcs;
        states.assign(std::make_move_iterator(npcs->begin()), std::make_move_iterator(npcs->end()));
        for (std::size_t index = 0; index < states.size(); ++index) {
            auto& npc = *states[index];
            npc.partner_index = static_cast<unsigned char>(index);
            npc.script_file.reset();
            npc.has_monster = npc.is_grouped;
            npc.record = &game.data.npcs[npc.name_style];
            if (npc.is_grouped) {
                npc.is_present = false;
                npc.level_band = game.levelBand(-1);
                npc.bound_level = -1;
                npc.script_file = LoadNpcScriptResources(game.story, npc, nullptr);
            }
            if (npc.spawned == 0xffff) npc.spawned = 0;
        }
        // The unflagged records no saved state claims start as new runtime nodes.
        for (unsigned int npc_id = 0; npc_id < game.data.npcs.size(); ++npc_id) {
            const auto claimed = std::ranges::any_of(states, [&](const auto& npc) {
                return npc->record->kind == static_cast<int>(npc_id);
            });
            if (game.data.npcs[npc_id].monster_bound == 0 && !claimed)
                game.createNpcRuntimeNode(npc_id);
        }
    }
    if (facts) {
        // Re-apply the consequences of the facts that do not survive a save.
        const auto& stored = game.story.facts = *facts;
        if (stored[W8_FACT_RFS81_HAS_BEEN_FIXED]) {
            const auto npc = std::ranges::find_if(
                game.story.npcs, [](const auto& state) { return state->record->kind == 0x20; });
            if (npc != game.story.npcs.end() && (*npc)->has_monster)
                (*npc)->script_file = LoadNpcScriptResources(game.story, **npc, nullptr);
        }
        // The derived import fact keeps its side effects, which act on the game just begun.
        if (!stored[W8_FACT_IMPORT_TRANG] && !stored[W8_FACT_IMPORT_UMPANI] &&
            !stored[W8_FACT_VIRGIN])
            SetFact(W8_FACT_IMPORT_NOALIGN, 1, false);
    }
    if (factions) {
        game.story.faction_relations = factions->relations;
        game.story.factions = factions->states;
    }
    if (journal) game.story.journal = std::move(*journal);
    for (std::size_t box = 0; box < game.message_log.size(); ++box) {
        for (std::size_t line = 0; line < game.status.text_box_lines_shown[box]; ++line) {
            auto& message = game.message_log[box][line];
            message.clock = MonotonicMilliseconds() + message.saved_remaining_ms;
        }
    }
    // Drop equipment a character can no longer wear, then normalize every carried item's
    // quantity byte.
    SettleActiveParty(game);
    const auto normalize = [](std::span<W8ItemInstance> items) {
        for (auto& item : items)
            if (item.iItemNo != -1) NormalizeItemQuantityKind(&item);
    };
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!game.party.members[slot].has_value()) continue;
        normalize(game.party.characters[slot].EquippedItem);
        normalize(game.party.characters[slot].backpack);
    }
    for (unsigned int index = 0; index < game.inventory.pool().size(); ++index) {
        game.inventory.editItem({W8ItemStorage::Pool, 0, index},
                                [](W8ItemInstance& item) { NormalizeItemQuantityKind(&item); });
    }
}
