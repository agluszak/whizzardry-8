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
#include <functional>
#include <iterator>
#include <limits>
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
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/AutomapScreen.h"
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
#include "wiz8/rules/targeting.h"
#include "wiz8/save_codec.h"
#include "wiz8/save_game.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/surface_storage.h"
#include "wiz8/unicode.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

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

/* Save names and stored characters' file names fit a 64-byte field with its terminator. */
constexpr int kCharacterNameBytes = 64;

// The game's stored character with this file name.
wiz8::save::StoredCharacter* FindStoredCharacter(Game& game, std::string_view file_name)
{
    const auto found = std::ranges::find_if(game.stored_characters, [&](const auto& stored) {
        return CompareTextIgnoreAsciiCase(stored.file_name, file_name) == 0;
    });
    return found == game.stored_characters.end() ? nullptr : &*found;
}

// Keep a character under its file name, replacing one stored under the same name.
void StoreCharacter(Game& game, wiz8::save::StoredCharacter stored)
{
    if (auto* existing = FindStoredCharacter(game, stored.file_name))
        *existing = std::move(stored);
    else
        game.stored_characters.push_back(std::move(stored));
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
    auto variables = game.story.location_variables;
    if (!world || !world->script) return variables;
    const int level = GetLoadedLevelID();
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
    std::shared_ptr<Script> resource;
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
    void Apply(W8MonsterInfo& info, Monster& monster) const
    {
        if (name[0] == '\0') return;
        info.ai_mode |= W8_MONSTER_AI_RESTORE_SCRIPT;
        monster.UseScript(resource, false);
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
        wiz8::retail::write(chunk, direction.value());
        chunk.WriteValue(face_party.value());
        chunk.WriteValue(stay_home.value());
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

/* One monster record: the version, the size-prefixed W8MonsterInfo image with
   position and yaw refreshed while the monster is live, then the script,
   unborn, movement and order sections. */
void WriteMonsterRecord(W8Chunk& chunk, const W8MonsterInfo* info)
{
    if (!info || !info->entity) throw std::runtime_error("missing monster");
    Monster& monster = *info->entity;
    auto image = wiz8::retail::encode_image(*info);
    if (info->fActive) {
        auto position = monster.GetRepresentationLocation();
        position.y = probeGround(position).position.y;
        image.position = {position.x, position.y, position.z};
        image.derived = monster.nav.GetYaw();
    }
    chunk.WriteValue(kMonsterRecordVersion);
    chunk.WriteValue(static_cast<std::uint32_t>(sizeof(image)));
    chunk.WriteBytes(&image, sizeof(image));

    const unsigned char has_script = monster.script != nullptr;
    chunk.WriteValue(has_script);
    if (has_script) SavedMonsterScript::Capture(monster).Write(chunk);

    const unsigned char unborn = CurrentLevelPopulation().monster_roster.isUnborn(info);
    chunk.WriteValue(unborn);

    const unsigned char has_movement =
        !monster.nav.linked_navigator && (monster.nav.flags & kMonsterHasMovement) != 0;
    chunk.WriteValue(has_movement);
    if (has_movement) SavedMonsterMovement::Capture(monster).Write(chunk);

    SavedMonsterOrders::Capture(monster).Write(chunk);
}

void WriteWorldItems(W8Chunk& chunks)
{
    const auto count =
        wiz8::checked_integer<std::uint32_t>(CurrentLevelPopulation().world_items.size());
    chunks.WriteValue(count);
    for (unsigned int index = 0; index < count; ++index)
        SaveItemFile(chunks.WriteStream(), *ItemInfo(index));
}

struct SavedMonsterGroup {
    std::unique_ptr<W8MonsterGroup> group;
    bool encounter;
};
struct SavedMonster {
    std::unique_ptr<W8MonsterInfo> info = std::make_unique<W8MonsterInfo>();
    bool unborn = false;
    std::optional<SavedMonsterScript> script;
    std::optional<SavedMonsterMovement> movement;
    std::optional<SavedMonsterOrders> orders;
};

SavedMonsterGroup ReadMonsterGroup(W8Chunk& chunk)
{
    auto owner = std::make_unique<W8MonsterGroup>();
    auto& group = *owner;
    wiz8::retail::read_sized(chunk, group);
    const bool is_encounter = group.version >= 2 && chunk.ReadValue<unsigned char>() != 0;
    if (group.version < 3) group.forced_neutral = false;

    if (static_cast<unsigned int>(group.monster_id) >= kMaxMonsterSpecies)
        throw std::runtime_error("invalid monster group species");

    return {std::move(owner), is_encounter};
}

SavedMonster ReadMonster(W8Chunk& chunk)
{
    SavedMonster saved;
    auto& owner = saved.info;
    auto& info = *owner;
    const auto version = chunk.ReadValue<unsigned int>();
    wiz8::retail::read_sized(chunk, info);
    auto& script = saved.script;
    if (chunk.ReadValue<unsigned char>()) script = SavedMonsterScript::Read(chunk);
    info.fActive = false;
    saved.unborn = version >= 5 && chunk.ReadValue<unsigned char>() != 0;
    auto& movement = saved.movement;
    if (version >= 2 && chunk.ReadValue<unsigned char>())
        movement = SavedMonsterMovement::Read(chunk);
    auto& orders = saved.orders;
    if (version >= 3) orders = SavedMonsterOrders::Read(chunk, version);

    for (const auto& effect : info.effect_slots) {
        if (effect.duration &&
            static_cast<unsigned int>(effect.effect_id) >= std::size(g_effect_visual_table))
            throw std::runtime_error("invalid monster status species or effect");
    }
    if (info.monster_species >= kMaxMonsterSpecies)
        throw std::runtime_error("invalid monster status species or effect");
    return saved;
}

} // namespace

namespace {
/* One level's saved state as its LVLS group holds it, decoded without the level's world:
   the status header, the monster groups and monsters, the world items, cursor nodes,
   encounters and automap, the trigger, prop, lock and action sections, and the ambient
   sounds, particles and lights. */
struct LevelState {
    std::optional<wiz8::save::LevelStatusHeader> header;
    std::vector<SavedMonsterGroup> groups;
    std::vector<SavedMonster> monsters;
    std::vector<std::unique_ptr<W8WorldItem>> items;
    std::optional<W8SavedCursorNodes> cursors;
    std::optional<W8EncounterState> encounters;
    std::optional<W8SavedAutomapNotes> automap;
    std::vector<W8AmbientSoundState> ambient;
    std::vector<W8ParticleState> particles;
    std::vector<W8LightState> lights;
    // The trigger, prop, lock and action sections, in the order they were read.
    std::vector<SavedWorldSection> world_sections;
};
} // namespace

// A level's decoded state with what building its world prepared from it.
struct W8LoadedLevelStatus::Data : LevelState {
    std::shared_ptr<Script> default_script;
    W8PreparedNpcBindings npc_bindings;
    std::vector<int> pending_deaths;
};

W8LoadedLevelStatus::W8LoadedLevelStatus() : data(std::make_unique<Data>()) {}
W8LoadedLevelStatus::~W8LoadedLevelStatus() = default;

namespace {
// Decode one section of a level group into the group's values.
void ReadLevelSection(LevelState& data, W8Chunk& chunk, unsigned int chunk_id, bool from_assets,
                      bool baseline = false)
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
            groups.push_back(ReadMonsterGroup(chunk));
        for (unsigned int index = 0; index < monster_count; ++index)
            monsters.push_back(ReadMonster(chunk));
        break;
    }
    case W8_CHUNK_WORLD_ITEMS: {
        const auto count = ReadWorldItemCount(chunk);
        auto& items = data.items;
        items.reserve(items.size() + count);
        for (unsigned int index = 0; index < count; ++index) {
            auto item = LoadItem(chunk, from_assets);
            items.push_back(std::move(item));
        }
        break;
    }
    case W8_CHUNK_CURSOR_NODES:
        data.cursors = ReadWorldCursorNodes(chunk);
        if (from_assets && !baseline) ReadWorldCursorNodeStates(chunk, *data.cursors);
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
        data.automap = ReadAutomapNotes(chunk);
        break;
    case W8_CHUNK_TRIGGERS:
        data.world_sections.emplace_back(DecodeSavedTriggers(chunk, from_assets));
        break;
    case W8_CHUNK_PROPS:
        data.world_sections.emplace_back(DecodeSavedProps(chunk));
        break;
    case W8_CHUNK_CURSOR_NODE_STATES:
        if (!data.cursors) data.cursors.emplace();
        ReadWorldCursorNodeStates(chunk, *data.cursors);
        break;
    case W8_CHUNK_MONSTER_GENERATORS:
        if (!data.encounters) data.encounters.emplace();
        ReadMonsterGeneratorStates(chunk, *data.encounters);
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
void OpenLevelImage(W8Chunk& chunk, const std::vector<std::byte>& image)
{
    RequireSection(chunk.OpenRead(wiz8::open_memory(image)), "level image");
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
            const auto* stored = FindStoredCharacter(CurrentGame(), path);
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
    CaptureSaveScreenshot(slot.screenshot);
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
    wiz8::save::GameState snapshot{game.status, game.party};
    auto& status = snapshot.status;
    status.pending_move_location = partyPose(*GetWorld());
    status.retail.save_version = 1.1f;
    status.difficulty = g_settings.difficulty;
    if (status.iron_man) {
        const auto creation_time = file.times().created;
        status.retail.save_filetime_xor = {creation_time.low ^ g_save_filetime_xor_low,
                                           creation_time.high ^ g_save_filetime_xor_high};
    }
    return snapshot;
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
    for (const auto& [level, image] : game.left_levels) {
        if (level == GetLoadedLevelID()) continue;
        W8Chunk source;
        OpenLevelImage(source, image);
        source.ForEachChunk([&](unsigned int id) {
            if (id == W8_CHUNK_LEVEL_STATUS)
                RequireSection(chunks.CopyCurrentChunkFrom(&source), "save chunk copy");
        });
        source.Close();
    }
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
        CaptureSaveScreenshot(captured);
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
    if (const auto* control = FindMonsterControlSpellEffect())
        WriteChunkPayload(chunks, W8_CHUNK_MONSTER_CONTROL, wiz8::save::encode(*control));
    RequireSection(SaveStatusHeader(&chunks), "level status");
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

bool StoreLeftLevel(Game& game)
try {
    const std::shared_ptr<wiz8::File> file = wiz8::open_memory_writer();
    W8Chunk image;
    if (!image.OpenWrite(file) || !SaveStatusHeader(&image)) return false;
    image.Close();
    game.left_levels[GetLoadedLevelID()] = file->written_memory();
    return true;
} catch (const std::exception&) {
    return false;
}

/* Write the current level's LVLS group: the level number, STAT, MONS and ITEM,
   then the world sections a save keeps. */
bool SaveStatusHeader(W8Chunk* chunks)
try {
    auto& file = chunks->WriteStream();
    chunks->WriteGroup(W8_CHUNK_LEVEL_STATUS, [&] {
        chunks->WriteValue(GetLoadedLevelID());
        WriteChunkPayload(*chunks, W8_CHUNK_STATUS_HEADER,
                          wiz8::save::encode(CaptureStatusHeader(CurrentGame().status)));
        chunks->WriteChunk(W8_CHUNK_MONSTERS,
                           [&] { RequireSection(SaveMonsterStatus(chunks), "monster status"); });
        chunks->WriteChunk(W8_CHUNK_WORLD_ITEMS, [&] { WriteWorldItems(*chunks); });

        chunks->WriteChunk(W8_CHUNK_AUTOMAP_NOTES, [&] { SaveAutomapNotes(file); });
        if (!GetWorld()->triggers.empty())
            chunks->WriteChunk(W8_CHUNK_TRIGGERS, [&] { SaveWorldTriggers(file); });
        chunks->WriteChunk(W8_CHUNK_PROPS, [&] { SaveWorldProps(file); });
        chunks->WriteChunk(W8_CHUNK_CURSOR_NODE_STATES, [&] { SaveWorldCursorNodeStates(file); });
        chunks->WriteChunk(W8_CHUNK_MONSTER_GENERATORS, [&] { SaveMonsterGenerators(file); });
        chunks->WriteChunk(W8_CHUNK_TRIGGER_STATES, [&] { SaveTriggerRuntimeStates(file); });
        chunks->WriteChunk(W8_CHUNK_AMBIENT_SOUNDS, [&] { SaveAmbientSoundList(file); });
        chunks->WriteChunk(W8_CHUNK_PARTICLES, [&] { SaveParticleStates(file); });
        chunks->WriteChunk(W8_CHUNK_LIGHTS, [&] { SaveLightStates(file); });
    });
    return true;
} catch (const std::exception&) {
    return false;
}

/* The MONS section: group and monster counts, every group record (stamped
   version 3) with its encounter-list flag, then every monster record. Live
   list entries come first, then encounter groups and unborn monsters. */
bool SaveMonsterStatus(W8Chunk* chunks)
try {
    const auto group_count =
        wiz8::checked_integer<std::uint32_t>(CurrentLevelPopulation().monster_groups.size() +
                                             CurrentLevelPopulation().encounter_groups.size());
    const auto persistent = [](const auto& monster) { return monster->monster_group_id != 0; };
    const auto monster_count = wiz8::checked_integer<std::uint32_t>(
        std::ranges::count_if(CurrentLevelPopulation().monster_roster.live(), persistent) +
        std::ranges::count_if(CurrentLevelPopulation().monster_roster.unborn(), persistent));
    chunks->WriteValue(group_count);
    chunks->WriteValue(monster_count);
    for (const auto* partition :
         {&CurrentLevelPopulation().monster_groups, &CurrentLevelPopulation().encounter_groups}) {
        const unsigned char encountered = partition == &CurrentLevelPopulation().encounter_groups;
        for (const auto& group : *partition) {
            auto image = wiz8::retail::encode_image(*group);
            image.version = kMonsterGroupRecordVersion;
            image.active_member_count = wiz8::checked_integer<std::int32_t>(
                CurrentLevelPopulation().activeMemberCount(*group));
            chunks->WriteValue(static_cast<std::uint32_t>(sizeof(image)));
            chunks->WriteBytes(&image, sizeof(image));
            chunks->WriteValue(encountered);
        }
    }
    for (const auto* partition : {&CurrentLevelPopulation().monster_roster.live(),
                                  &CurrentLevelPopulation().monster_roster.unborn()})
        for (const auto& monster : *partition)
            if (persistent(monster)) WriteMonsterRecord(*chunks, monster.get());
    return true;
} catch (const std::exception&) {
    return false;
}

namespace {
/* Decode level `level`'s group from `chunk`, or nothing when the file has none. A saved
   group takes the sections a save does not repeat (locks, cursor nodes, encounters and
   trigger actions) from the level's asset status first. */
std::optional<LevelState> DecodeLevelState(W8Chunk& chunk, int level, bool from_assets)
{
    LevelState state;
    const bool found = WithLevelGroup(chunk, level, [&] {
        if (!from_assets) {
            W8Chunk baseline;
            if (baseline.OpenRead(wiz8::asset_path(BuildLevelStatusPath(level)))) {
                WithLevelGroup(baseline, std::nullopt, [&] {
                    baseline.ForEachChunk([&](unsigned int section) {
                        if (section == W8_CHUNK_TRIGGER_LOCKS || section == W8_CHUNK_CURSOR_NODES ||
                            section == W8_CHUNK_ENCOUNTERS || section == W8_CHUNK_TRIGGER_ACTIONS)
                            ReadLevelSection(state, baseline, section, true, true);
                    });
                });
            }
        }
        chunk.ForEachChunk(
            [&](unsigned int section) { ReadLevelSection(state, chunk, section, from_assets); });
    });
    if (!found) return std::nullopt;
    return state;
}

std::unique_ptr<W8LoadedLevelStatus> ReadLevelStatus(World& world, W8Chunk& chunk, int level,
                                                     bool from_assets)
{
    auto state = DecodeLevelState(chunk, level, from_assets);
    if (!state) return nullptr;
    auto status = std::make_unique<W8LoadedLevelStatus>();
    auto& data = *status->data;
    static_cast<LevelState&>(data) = std::move(*state);
    for (auto& section : data.world_sections) ApplySavedSection(world, section);
    std::unordered_set<int> group_ids, monster_ids;
    for (const auto& group : data.groups) {
        if (!group_ids.insert(group.group->group_id).second)
            throw std::runtime_error("duplicate saved monster group");
        if (!MonsterDBFromSpecies(group.group->monster_id))
            throw std::runtime_error("missing saved monster group species");
    }
    // Loading graphics and scripts belongs to construction, not gameplay entry.
    for (auto& saved : data.monsters) {
        auto& info = *saved.info;
        if (!monster_ids.insert(info.location_id).second)
            throw std::runtime_error("duplicate saved monster identity");
        const auto* record = MonsterDBFromSpecies(info.monster_species);
        if (!record) throw std::runtime_error("missing saved monster species");
        if (record->deleted) continue;
        const auto group =
            std::find_if(data.groups.begin(), data.groups.end(), [&](const auto& saved_group) {
                return saved_group.group->group_id == info.monster_group_id;
            });
        if (group == data.groups.end() || MonsterDBFromSpecies(group->group->monster_id)->deleted)
            throw std::runtime_error("missing saved monster group");
        auto model = ReadMonsterRepresentation(world, &info);
        info.entity.attach(*model);
        world.monsters.push_back(std::move(model));
        RestoreMonsterSpellIcons(world, info, *info.entity);
        // Keep the encounter default script cached until activation assigns it.
        if (group->group->encounter_registered && !info.entity->script && !data.default_script)
            data.default_script = LoadMonsterScript("Default.MSF");
        if (saved.script && saved.script->name[0] != '\0')
            saved.script->resource = LoadMonsterScript(saved.script->name);
    }
    if (data.cursors) BuildWorldCursorNodes(world, std::move(*data.cursors));
    PrepareLevelPopulation(world, *status);
    return status;
}
} // namespace

std::unique_ptr<W8LoadedLevelStatus>
ReadLevelStatus(World& world, const std::filesystem::path& path, int level, bool from_assets)
{
    W8Chunk chunk;
    if (!chunk.OpenRead(path)) return nullptr;
    return ReadLevelStatus(world, chunk, level, from_assets);
}

std::unique_ptr<W8LoadedLevelStatus> ReadLeftLevelStatus(World& world, const Game& game, int level)
{
    const auto left = game.left_levels.find(level);
    if (left == game.left_levels.end()) return nullptr;
    W8Chunk chunk;
    OpenLevelImage(chunk, left->second);
    return ReadLevelStatus(world, chunk, level, false);
}

void PrepareLevelPopulation(World& world, W8LoadedLevelStatus& status)
{
    auto& data = *status.data;
    auto& population = *world.population;
    for (auto& saved : data.groups) {
        auto& group = *saved.group;
        if (MonsterDBFromSpecies(group.monster_id)->deleted) continue;
        group.monsters.clear();
        group.members_active = false;
        group.fInCombat = false;
        auto& list = saved.encounter ? population.encounter_groups : population.monster_groups;
        list.push_back(std::move(saved.group));
    }
    population.world_items = std::move(data.items);
    std::vector<W8MonsterInfo*> records;
    records.reserve(data.monsters.size());
    for (auto& saved : data.monsters) {
        auto* info = saved.info.get();
        if (MonsterDBFromSpecies(info->monster_species)->deleted) {
            records.push_back(nullptr);
            continue;
        }
        auto* group = population.findGroup(info->monster_group_id);
        if (!group) throw std::runtime_error("missing prepared monster group");
        group->monsters.push_back(info->location_id);
        if (static_cast<unsigned int>(group->leader_location_id) <
            static_cast<unsigned int>(info->location_id))
            group->leader_location_id = info->location_id;
        records.push_back(population.monster_roster.add(std::move(saved.info), saved.unborn));
    }
    for (size_t index = 0; index < records.size(); ++index) {
        if (auto* info = records[index]) {
            ConfigureMonsterRepresentation(world, info);
            PrepareMonsterInWorld(world, *info, data.monsters[index].unborn);
        }
    }
    data.npc_bindings = PrepareNpcBindings(world);
    for (size_t index = 0; index < records.size(); ++index) {
        auto* info = records[index];
        if (!info) continue;
        auto& saved = data.monsters[index];
        if (saved.movement) saved.movement->Apply(*info->entity);
        if (saved.orders) saved.orders->Apply(*info, *info->entity);
        if (saved.script) saved.script->Apply(*info, *info->entity);
        if (info->hp_current == 0) data.pending_deaths.push_back(info->location_id);
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
            leader.UseScript(data.default_script, true);
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
    if (data.automap) ApplyAutomapNotes(world, std::move(*data.automap));
    ApplyAmbientSoundStates(world, data.ambient);
    ApplyParticleStates(world, data.particles);
    ApplyLightStates(world, data.lights);
}

// Publish shared session references and scalar state only. The replacement
// already owns every record, representation, membership and identity link.
void InstallLevelSessionState(World& world, W8LoadedLevelStatus* status)
{
    CurrentGame().status.next_group_id = CurrentGame().status.next_monster_location_id =
        CurrentGame().status.next_world_item_id = 1;
    CurrentGame().status.next_trigger_id = world.next_trigger_id;
    CurrentGame().item_manager_pending = 0;
    if (!status) return;
    auto& data = *status->data;
    if (data.header) ApplyStatusHeader(*data.header, CurrentGame().status);
    InstallNpcBindings(std::move(data.npc_bindings));
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

/* Write the container and its contents as the retail flat sequence. Each record's
   marker word is one, and its next word says whether another record follows. */
// FUNCTION: WIZ8 0x00514be0
void SaveItemFile(wiz8::File& handle, const W8WorldItem& item_info)
{
    for (std::size_t index = 0; index <= item_info.contents.size(); ++index) {
        const auto* item = index == 0 ? &item_info : item_info.contents[index - 1].get();
        auto image = wiz8::retail::encode_image(*item);
        image.saved_marker = 1;
        image.next = index < item_info.contents.size() ? 1 : 0;
        if (item->fActive) {
            const auto position = item->entity->GetLocation();
            image.position = {position.x, position.y, position.z};
            image.entity_flags = item->entity->flags;
        }
        handle.write(&image, sizeof(image));
    }
}

/* Read the retail flat sequence into a container and its owned contents.
   A saved next pointer only marks that another record follows. All records
   are staged before publication to the item list and search registry. */
// FUNCTION: WIZ8 0x00514c80
std::unique_ptr<W8WorldItem> LoadItem(W8Chunk& handle, bool from_assets)
{
    std::unique_ptr<W8WorldItem> head;
    bool has_next;
    do {
        const auto image = wiz8::retail::read_image<W8WorldItem>(handle);
        has_next = image.next != 0;
        auto entry = std::make_unique<W8WorldItem>();
        auto& item = *entry;
        wiz8::retail::decode_image(image, item);
        item.sector_id = -2;
        item.fActive = false;
        if (from_assets) item.entity_flags &= ~W8_ITEM_ENTITY_RADAR_SEEN;
        if (!head)
            head = std::move(entry);
        else
            head->contents.push_back(std::move(entry));
    } while (has_next);
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
            StoreCharacter(CurrentGame(), {path, snapshot});
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
void CaptureSaveScreenshot(W8SaveScreenshot& screenshot)
{
    screenshot.version = 1.0f;
    DesignRect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = 640;
    rect.bottom = 480;
    auto surface = CreateCpuStorage(80, 60, SDL_PIXELFORMAT_ARGB1555, screenshot.pixels, 160);
    screenshot.capture_result = RenderWorldToSurface(surface.get(), &rect)
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
    const bool wanted = (g_settings.auto_save != 0 && !forced) || CurrentGame().status.iron_man;
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
    if ((g_settings.auto_save == 0 && !CurrentGame().status.iron_man) || !CanAutoSaveNow()) return;
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
    const auto& left = CurrentGame().left_levels;
    const auto image = left.find(level);
    if (image == left.end()) return false;
    W8Chunk chunk;
    OpenLevelImage(chunk, image->second);
    const bool found = WithLevelGroup(chunk, level, [&] {
        chunk.ForEachChunk([&](unsigned int section) {
            if (section != W8_CHUNK_WORLD_ITEMS) return;
            const auto count = ReadWorldItemCount(chunk);
            if (count > static_cast<unsigned int>(std::numeric_limits<int>::max() - items.size()))
                throw std::runtime_error("too many world items");
            items.reserve(items.size() + count);
            for (unsigned int index = 0; index < count; ++index) {
                auto item = LoadItem(chunk);
                items.push_back(std::move(item));
            }
        });
    });
    chunk.Close();
    return found;
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

/* Load a save slot. Every game section is decoded first, so a malformed one fails the load
   before the game in progress is replaced; then a new game begins and the sections are
   installed in the order the file holds them. The game keeps each level's group as bytes,
   which a save copies and entering the level decodes. The message countdowns, the gameplay
   timer and the held item are re-armed and the loaded items normalized last. */
bool LoadGame(GameSession& games, std::string_view slot_name)
try {
    W8Chunk chunks;
    if (!chunks.OpenRead(wiz8::user_path(SaveSlotPath(slot_name)))) return false;
    std::vector<std::move_only_function<void(Game&)>> installs;
    // The message boxes' encoding follows the save version the status section names.
    auto save_version = GameStatus{}.retail.save_version;
    chunks.ForEachChunk([&](unsigned int id) {
        switch (id) {
        case W8_CHUNK_LEVEL_STATUS: {
            const int level = PeekGroupLevel(chunks);
            installs.emplace_back([level, image = CopyLevelGroupImage(chunks)](Game& game) mutable {
                game.left_levels[level] = std::move(image);
            });
            break;
        }
        case W8_CHUNK_GAME_STATUS: {
            auto saved = wiz8::save::decode_game_state(ReadChunkPayload(chunks));
            save_version = saved.status.retail.save_version;
            installs.emplace_back([saved = std::move(saved)](Game& game) mutable {
                game.status = std::move(saved.status);
                game.party = std::move(saved.party);
            });
            break;
        }
        case W8_CHUNK_CHARACTER:
            // A malformed record is skipped; the last complete one under a name wins.
            try {
                installs.emplace_back(
                    [stored = wiz8::save::decode_stored_character(ReadChunkPayload(chunks))](
                        Game& game) mutable { StoreCharacter(game, std::move(stored)); });
            } catch (const std::runtime_error&) {
            }
            break;
        case W8_CHUNK_MESSAGE_STORAGE:
            installs.emplace_back(
                [boxes = wiz8::save::decode_message_boxes(ReadChunkPayload(chunks), save_version)](
                    Game& game) mutable { InstallMessageBoxes(std::move(boxes), game); });
            break;
        case W8_CHUNK_LOCATION_VARIABLES:
            installs.emplace_back([variables = wiz8::save::decode_location_variables(
                                       ReadChunkPayload(chunks))](Game& game) mutable {
                std::ranges::move(variables, std::back_inserter(game.story.location_variables));
            });
            break;
        case W8_CHUNK_NPC_TRANSCRIPT:
            installs.emplace_back([transcript = wiz8::save::decode_dialogue_transcript(
                                       ReadChunkPayload(chunks))](Game& game) mutable {
                InstallDialogueTranscript(std::move(transcript), game.story, game.npc_interaction);
            });
            break;
        case W8_CHUNK_NPC_STATES:
            installs.emplace_back([npcs = wiz8::save::decode_npc_states(ReadChunkPayload(chunks),
                                                                        Data().npcs.size())](
                                      Game&) mutable { InstallNpcStates(std::move(npcs)); });
            break;
        case W8_CHUNK_FACT_STATE:
            installs.emplace_back([facts = wiz8::save::decode_facts(ReadChunkPayload(chunks))](
                                      Game&) { InstallFacts(facts); });
            break;
        case W8_CHUNK_FACTION_STATE:
            installs.emplace_back(
                [factions = wiz8::save::decode_factions(ReadChunkPayload(chunks))](Game& game) {
                    game.story.faction_relations = factions.relations;
                    game.story.factions = factions.states;
                    game.story.factions[W8_FACTION_PARTY].disposition_score = 100;
                });
            break;
        case W8_CHUNK_JOURNAL:
            installs.emplace_back(
                [journal = wiz8::save::decode_journal(ReadChunkPayload(chunks))](
                    Game& game) mutable { game.story.journal = std::move(journal); });
            break;
        case W8_CHUNK_MONSTER_CONTROL:
            installs.emplace_back([control = wiz8::save::decode_monster_control(
                                       ReadChunkPayload(chunks))](Game& game) mutable {
                game.spell_effects.push_back(
                    std::make_unique<W8SpellEffectEntry>(std::move(control)));
            });
            break;
        }
    });
    chunks.Close();
    auto& game = games.begin();
    for (auto& install : installs) install(game);
    for (std::size_t box = 0; box < game.message_log.size(); ++box) {
        for (std::size_t line = 0; line < game.status.text_box_lines_shown[box]; ++line) {
            auto& message = game.message_log[box][line];
            message.clock = MonotonicMilliseconds() + message.saved_remaining_ms;
        }
    }
    game.autosave_due = Game::kAutosaveInterval;
    SanitizeLoadedItems();
    return true;
} catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot load save %.*s: %s",
                 static_cast<int>(slot_name.size()), slot_name.data(), error.what());
    return false;
}
