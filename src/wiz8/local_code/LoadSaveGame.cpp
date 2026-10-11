#include "wiz8/engine_code/stScript.h" // IWYU pragma: keep - complete script type used through the owned pointer.
#include <fmt/format.h>
#include <fmt/printf.h>
#include <SDL3/SDL_filesystem.h>
#include <optional>
#include <string_view>
#include <vector>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <limits>
#include <string>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <utility>

#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/containers.h"
#include "wiz8/compat/debug_heap.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/monster_generators.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/xstatus.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/notices.h"
#include "wiz8/chunk.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/sr_api.h"
#include "wiz8/save_game.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/fonts.h"
#include "wiz8/utility.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/Video2.h"
#include "surrender/srColorSurface.h"
#include "wiz8/world_cursor.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/float_constants.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/location_variables.h"
#include "wiz8/fact_state.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/cursor.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "timer.h"
#include "wiz8/file_time.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "soundman.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "AnimRep.hpp"
#include "Item.h"
#include "compat/native.h"
#include "conditions.h"
#include "gameplay_modifiers.h"
#include "input.h"
#include "item_spawning.h"
#include "layouts/character.h"
#include "layouts/gameplay_databases.h"
#include "layouts/item_instance.h"
#include "layouts/levels.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "monster_actions.h"
#include "monster_spell_icons.h"
#include "retail_records.h"
#include "surrender/srMath.h"
#include "surrender/srPixelConvert.h"

#define LOADSAVEGAME_CPP "C:\\Projects\\Wizardry 8\\Local Code\\LoadSaveGame.cpp"

// GLOBAL: WIZ8 0x00689f98
bool g_save_pending;

/* The save-file extension. Retail keeps it in writable .data. */
// GLOBAL: WIZ8 0x0061A144
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

constexpr std::string_view kCurrentGamePath = "Saves\\CurrentGame.SAV";
constexpr std::string_view kAutoSaveName = "AutoSave";

/* Monster groups and monsters beyond the live lists (encounter groups and
   unborn monsters) are addressed from this list index on. */
constexpr unsigned int kSecondaryListBase = 10000;

/* The fixed STAT record. Only the leading counters and the status block are
   established; the tail is written as zeros and ignored on load. */
struct W8StatusHeader {
    float version;
    int next_group_id;
    int next_monster_location_id;
    int next_world_item_id;
    int next_trigger_id;
    unsigned char status_block[0x100];
    unsigned char unknown_114[0x200];
};
static_assert(sizeof(W8StatusHeader) == 0x314);
constexpr float kStatusHeaderVersion = 2.0f;
/* g_status keeps the status block in three fields; their offsets in the block. */
constexpr std::size_t kStatusTurnsOffset = 0xd4;
constexpr std::size_t kStatusSuffixOffset = 0xd8;

std::string SaveSlotPath(std::string_view name)
{
    return fmt::format("Saves\\{}.{}", name, g_save_extension);
}

std::string QuickSaveName(int slot)
{
    return fmt::format("Quick {}", slot);
}

bool IsRegularFile(std::string_view path)
{
    const auto status = wiz8::file_status(path);
    return status && status->info.type == SDL_PATHTYPE_FILE;
}

bool IsCurrentGameFileName(std::string_view name)
{
    return CompareTextIgnoreAsciiCase(name, "CurrentGame.SAV") == 0;
}

/* Section writers and loaders report failure as false; inside a chunk body
   that abandons the whole operation. */
void RequireSection(bool succeeded, const char* section)
{
    if (!succeeded) throw std::runtime_error(std::string("save section failed: ") + section);
}

template<class F> class ScopeExit {
    F action_;
public:
    explicit ScopeExit(F action) : action_(std::move(action)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() { action_(); }
};

/* Writes a save through a staged file: the RIFF is finalized and only then
   published over the old file, so a failure leaves the old save intact. */
class SaveTransaction {
    wiz8::StagedFile staged_;
public:
    W8Chunk chunks;
    explicit SaveTransaction(std::string_view path) : staged_(path)
    {
        if (!chunks.OpenWrite(staged_.file())) throw std::runtime_error("open staged save");
    }
    void commit()
    {
        chunks.Close();
        staged_.commit();
    }
};

/* A status block read outside g_status, with party buffers of its own. */
struct DetachedStatus {
    W8GlobalStatus status{};
    W8Character characters[8]{};
    W8PartySlotRow party_rows[8]{};
    DetachedStatus()
    {
        status.buffers.Char = characters;
        status.buffers.XChar = party_rows;
    }
    DetachedStatus(const DetachedStatus&) = delete;
    DetachedStatus& operator=(const DetachedStatus&) = delete;
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
    if (count > static_cast<std::uint64_t>(chunk.RemainingBytes()) / sizeof(wiz8::retail::WorldItem))
        throw std::runtime_error("invalid item section count");
    return count;
}

/* CHAR chunks lead with the record's NUL-terminated 64-byte file name. */
constexpr int kCharacterNameBytes = 64;

bool ReadCharacterNameMatches(W8Chunk& chunk, std::string_view path)
{
    char name[kCharacterNameBytes];
    chunk.ReadBytes(name, sizeof(name));
    return memchr(name, '\0', sizeof(name)) && CompareTextIgnoreAsciiCase(name, path) == 0;
}

template<class F> void ForEachShownMessage(F&& visit)
{
    for (int box = 0; box < 4; ++box) {
        for (unsigned int line = 0; line < g_status.text_box_lines_shown[box]; ++line) {
            visit(g_message_storage[box][line]);
        }
    }
}

/* The STAT record mirrors the id counters and status block of g_status. */
W8StatusHeader CaptureStatusHeader()
{
    W8StatusHeader header{};
    header.version = kStatusHeaderVersion;
    header.next_group_id = g_status.next_group_id;
    header.next_monster_location_id = g_status.next_monster_location_id;
    header.next_world_item_id = g_status.next_world_item_id;
    header.next_trigger_id = g_status.next_trigger_id;
    std::memcpy(header.status_block, g_status.status_header_prefix, sizeof(g_status.status_header_prefix));
    std::memcpy(header.status_block + kStatusTurnsOffset, &g_status.uiTurnsElapsed,
                sizeof(g_status.uiTurnsElapsed));
    std::memcpy(header.status_block + kStatusSuffixOffset, g_status.status_header_suffix,
                sizeof(g_status.status_header_suffix));
    return header;
}

/* Publish a STAT record. A zero counter restarts at one. */
void ApplyStatusHeader(const W8StatusHeader& header)
{
    g_status.next_group_id = header.next_group_id ? header.next_group_id : 1;
    g_status.next_monster_location_id = header.next_monster_location_id ? header.next_monster_location_id : 1;
    g_status.next_world_item_id = header.next_world_item_id ? header.next_world_item_id : 1;
    g_status.next_trigger_id = header.next_trigger_id ? header.next_trigger_id : 1;
    std::memcpy(g_status.status_header_prefix, header.status_block, sizeof(g_status.status_header_prefix));
    std::memcpy(&g_status.uiTurnsElapsed, header.status_block + kStatusTurnsOffset,
                sizeof(g_status.uiTurnsElapsed));
    std::memcpy(g_status.status_header_suffix, header.status_block + kStatusSuffixOffset,
                sizeof(g_status.status_header_suffix));
}

/* The HYPN record: the monster-control effect's fields in file order. The
   pointer-list vectors and the result state are rebuilt, not saved. */
template<class Entry, class Field> void ForEachMonsterControlField(Entry& effect, Field&& field)
{
    static_assert(sizeof(W8TargetSource) == 0x34);
    field(effect.kind);
    field(effect.turns_remaining);
    field(effect.Source);
    field(effect.target);
    field(effect.OrigSource);
    field(effect.OrigTarget);
    field(effect.recast);
    field(effect.sustained);
    field(effect.missiles_pending);
    field(effect.targets_resolved);
    field(effect.definition);
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

    static SavedMonsterScript Capture(const W8Monster& monster)
    {
        SavedMonsterScript script;
        const auto& name = monster.script->getName();
        if (name.size() >= sizeof(script.name)) throw std::runtime_error("monster script name too long");
        std::copy(name.begin(), name.end(), script.name);
        script.wait = monster.script_wait;
        script.line = monster.script_line;
        for (const auto condition : monster.script_conditions) script.conditions.push_back(condition != 0);
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
        if (!memchr(script.name, '\0', sizeof(script.name))) throw std::runtime_error("unterminated monster script name");
        chunk.ReadValue(script.wait);
        chunk.ReadValue(script.line);
        const auto count = chunk.ReadValue<int>();
        if (count < 0 || count > chunk.RemainingBytes()) throw std::runtime_error("invalid monster script conditions");
        script.conditions.resize(count);
        chunk.ReadBytes(script.conditions.data(), script.conditions.size());
        return script;
    }
    void Apply(W8MonsterInfo& info, W8Monster& monster) const
    {
        if (name[0] == '\0') return;
        info.ai_mode |= W8_MONSTER_AI_RESTORE_SCRIPT;
        monster.SetScript(name, false);
        monster.script_wait = wait;
        monster.script_line = line;
        for (const auto condition : conditions) monster.script_conditions.push_back(condition != 0);
    }
};

/* Free (non-linked) navigator movement: height band, patrol home and target. */
struct SavedMonsterMovement {
    float minimum_height = 0;
    float maximum_height = 0;
    srVector3T<float> patrol_home{};
    srVector3T<float> target{};

    static SavedMonsterMovement Capture(const W8Monster& monster)
    {
        return {monster.minimum_height, monster.maximum_height, monster.patrol_home,
                monster.movement.attachment->path_destination};
    }
    void Write(W8Chunk& chunk) const
    {
        chunk.WriteValue(minimum_height);
        chunk.WriteValue(maximum_height);
        chunk.WriteValue(patrol_home);
        chunk.WriteValue(target);
    }
    static SavedMonsterMovement Read(W8Chunk& chunk)
    {
        SavedMonsterMovement movement;
        chunk.ReadValue(movement.minimum_height);
        chunk.ReadValue(movement.maximum_height);
        chunk.ReadValue(movement.patrol_home);
        chunk.ReadValue(movement.target);
        return movement;
    }
    void Apply(W8Monster& monster) const
    {
        auto target_point = target;
        monster.minimum_height = minimum_height;
        monster.maximum_height = maximum_height;
        monster.patrol_home = patrol_home;
        monster.movement.attachment->InitializeSegment(&monster.movement.position, &target_point);
        monster.movement_target = target;
        monster.flags |= kMonsterHasMovement;
        monster.movement.attachment->flags |= W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
        monster.movement.target_position = patrol_home;
        if (monster.SetMovementTarget(&monster.movement_target, true)) {
            monster.flags |= 0x6;
            monster.ClearMovementStopped();
            monster.halted = false;
            monster.movement_target = monster.movement.attachment->path_destination;
        }
    }
};

/* Orders and patrol state (record version 3), with the facing direction
   (version 4) and the face-party (6) and stay-home (7) flags. */
struct SavedMonsterOrders {
    bool defining_orders = false;
    W8MonsterOrderMode order_mode = 0;
    bool orders_finished = false;
    bool deaf = false;
    float patrol_distance = 0;
    float patrol_variation = 0;
    signed char patrol_index = 0;
    std::vector<srVector3T<float>> points;
    std::optional<srVector3T<float>> direction;
    std::optional<bool> face_party;
    std::optional<bool> stay_home;

    static SavedMonsterOrders Capture(const W8Monster& monster)
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
        srVector3T<float> direction;
        direction.Set(monster.direction_x, monster.direction_y, monster.direction_z);
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
        chunk.WriteBytes(points.data(), points.size() * sizeof(points[0]));
        chunk.WriteValue(direction.value());
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
        if (count < 0 || count > chunk.RemainingBytes() / std::int64_t{sizeof(srVector3T<float>)})
            throw std::runtime_error("invalid monster patrol points");
        orders.points.resize(count);
        chunk.ReadBytes(orders.points.data(), orders.points.size() * sizeof(orders.points[0]));
        if (version >= 4) orders.direction = chunk.ReadValue<srVector3T<float>>();
        if (version >= 6) orders.face_party = chunk.ReadValue<unsigned char>() != 0;
        if (version >= 7) orders.stay_home = chunk.ReadValue<unsigned char>() != 0;
        return orders;
    }
    void Apply(W8MonsterInfo& info, W8Monster& monster) const
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
void RestoreMonsterSpellIcons(const W8MonsterInfo& info, W8Monster* monster)
{
    if (info.highest_condition != W8_CONDITION_NONE) {
        for (int index = 0; index < W8_CONDITION_COUNT; ++index) {
            if (info.uiCondition[index] != 0)
                SetMonsterSpellIcon(monster, static_cast<W8MonsterSpellIconId>(index - 1), true);
        }
    }
    for (int index = 0; index < 8; ++index) {
        if (info.enchantments[index].turns != 0)
            SetMonsterSpellIcon(monster, static_cast<W8MonsterSpellIconId>(index + 0x10), true);
    }
    for (const auto& effect : info.effect_slots) {
        if (effect.duration != 0)
            SetMonsterSpellIcon(monster, g_effect_visual_table[effect.effect_id].monster_icon, true);
    }
    if (info.charm_strength > 0) SetMonsterSpellIcon(monster, SPELL_ICON_CHARMED, true);
    if (info.summoned != W8_MONSTER_SUMMON_NONE) SetMonsterSpellIcon(monster, SPELL_ICON_SUMMONED, true);
}

/* One monster record: the version, the size-prefixed W8MonsterInfo image with
   position and yaw refreshed while the monster is live, then the script,
   unborn, movement and order sections. */
// FUNCTION: WIZ8 0x005147a0
void WriteMonsterRecord(W8Chunk& chunk, unsigned int list_index)
{
    auto* info = MonsterGetScriptPartByLocationIndex(list_index);
    if (!info || !info->p3D) throw std::runtime_error("missing monster");
    W8Monster& monster = *info->p3D;
    if (info->fActive) {
        info->position = MonsterGetLocation(&monster);
        info->position.y = probeGround(info->position.glmValue()).position.y;
        info->derived = MonsterGetYaw(&monster);
    }
    chunk.WriteValue(kMonsterRecordVersion);
    wiz8::retail::write_sized(chunk, *info);

    const unsigned char has_script = monster.script != nullptr;
    chunk.WriteValue(has_script);
    if (has_script) SavedMonsterScript::Capture(monster).Write(chunk);

    const unsigned char unborn = std::ranges::contains(gXStatus.unborn_monsters, info);
    chunk.WriteValue(unborn);

    const unsigned char has_movement = !monster.linked_navigator && (monster.flags & kMonsterHasMovement) != 0;
    chunk.WriteValue(has_movement);
    if (has_movement) SavedMonsterMovement::Capture(monster).Write(chunk);

    SavedMonsterOrders::Capture(monster).Write(chunk);
}

/* Allocations shared with the C-style monster managers, which free them. */
template<class T> using MallocPtr = std::unique_ptr<T, decltype(&free)>;
template<class T> MallocPtr<T> AllocateZeroed()
{
    MallocPtr<T> owner(static_cast<T*>(calloc(1, sizeof(T))), &free);
    if (!owner) throw std::bad_alloc();
    return owner;
}

void WriteWorldItems(W8Chunk& chunks)
{
    const unsigned int count = gXStatus.world_items.size();
    chunks.WriteValue(count);
    for (unsigned int index = 0; index < count; ++index)
        RequireSection(SaveItemFile(chunks.Stream(), ItemInfo(index)), "world item");
}

/* Apply one child of the matching LVLS group. */
void LoadLevelSection(W8Chunk& chunk, unsigned int chunk_id)
{
    auto& file = chunk.Stream();
    switch (chunk_id) {
    case W8_CHUNK_STATUS_HEADER:
        RequireSection(LoadStatusHeader(&chunk), "status header");
        break;
    case W8_CHUNK_MONSTERS: {
        const auto group_count = chunk.ReadValue<unsigned int>();
        const auto monster_count = chunk.ReadValue<unsigned int>();
        // Each group is at least its size word, each monster its version, size and flag.
        if (std::uint64_t{group_count} * 4 + std::uint64_t{monster_count} * 9 >
            static_cast<std::uint64_t>(chunk.RemainingBytes()))
            throw std::runtime_error("invalid monster section counts");
        for (unsigned int index = 0; index < group_count; ++index)
            RequireSection(LoadMonsterGroup(&chunk), "monster group");
        for (unsigned int index = 0; index < monster_count; ++index)
            RequireSection(LoadMonster(&chunk), "monster");
        ReapplyMonsterGroupFormations();
        RepairMonsterGroupLeaderLinks();
        ApplyDefaultMonsterGroupSounds();
        break;
    }
    case W8_CHUNK_WORLD_ITEMS:
        for (auto count = ReadWorldItemCount(chunk); count > 0; --count)
            RequireSection(LoadItem(file, true) != nullptr, "world item");
        break;
    case W8_CHUNK_CURSOR_NODES:
        if (!g_level_status_loading) ReleaseWorldCursorNodes();
        LoadWorldCursorNodes(file);
        if (g_level_status_loading) LoadWorldCursorNodeStates(file);
        break;
    case W8_CHUNK_ENCOUNTERS:
        if (!g_level_status_loading) DestroyMonsterGenerators();
        MonGen::LoadAll(file);
        break;
    case W8_CHUNK_TRIGGER_LOCKS:
    case W8_CHUNK_TRIGGER_STATES: LoadTriggerRuntimeStates(file); break;
    case W8_CHUNK_TRIGGER_ACTIONS: LoadTriggerActionData(file); break;
    case W8_CHUNK_AUTOMAP_NOTES: LoadAutomapNotes(file); break;
    case W8_CHUNK_TRIGGERS: LoadWorldTriggers(g_world, file); break;
    case W8_CHUNK_PROPS: LoadWorldProps(g_world, file); break;
    case W8_CHUNK_CURSOR_NODE_STATES: LoadWorldCursorNodeStates(file); break;
    case W8_CHUNK_MONSTER_GENERATORS: LoadMonsterGenerators(file); break;
    case W8_CHUNK_AMBIENT_SOUNDS: LoadAmbientSoundList(file); break;
    case W8_CHUNK_PARTICLES: LoadParticleStates(file); break;
    case W8_CHUNK_LIGHTS: LoadLightStates(file); break;
    }
}

/* Copy the CHAR chunks and the other levels' LVLS groups from the running
   CurrentGame save into a new save. */
// FUNCTION: WIZ8 0x00514d50
void CopyCurrentGameChunks(W8Chunk& source, W8Chunk& destination)
{
    source.ForEachChunk([&](unsigned int id) {
        if (id == W8_CHUNK_LEVEL_STATUS) {
            if (PeekGroupLevel(source) == g_status.current_level) return;
        } else if (id != W8_CHUNK_CHARACTER) {
            return;
        }
        RequireSection(destination.CopyCurrentChunkFrom(&source), "save chunk copy");
    });
}

/* Autosaves wait for a quiet moment: no world-cursor lock, nobody dying, no
   combat, sight override, uninterruptible movement, dialogue or camp. */
bool CanAutoSaveNow()
{
    return g_status.world_cursor_gate == 0 && !AnyMonsterDying() && !gXStatus.fCombatMode &&
           !IsSightRangeOverridden() && CanInterruptLevelMovement() && !gXStatus.fNpcDialogueMode &&
           !gXStatus.fCampMode;
}

/* Iron Man games overwrite their own slot instead of the AutoSave slot. */
std::string_view AutoSaveSlotName()
{
    return g_status.iron_man ? GetLastSaveName() : kAutoSaveName;
}

void ShowMainGameNotice(int string_id)
{
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME)
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[string_id]);
}

} // namespace

/* 0x0061A134/0x0061A138: the masks an iron-man save XORs its file's creation
   time with before storing it in the status block. */
// GLOBAL: WIZ8 0x0061A134
static unsigned int g_save_filetime_xor_low = 0x6b24e9f0;
// GLOBAL: WIZ8 0x0061A138
static unsigned int g_save_filetime_xor_high = 0xe77c28c1;

/* The path of a loose character file. Before the game starts characters live
   under Saves\Characters (slot -1) or Saves\NPCs; in a started game, a slot
   whose flag is clear still reads Saves\NPCs and every other name is a CHAR
   record of the current save. */
// FUNCTION: WIZ8 0x00514fa0
std::string BuildCharacterFilePath(std::string_view filename, int slot)
{
    if (!g_status.game_started)
        return fmt::format("{}\\{}", slot == -1 ? "Saves\\Characters" : "Saves\\NPCs", filename);
    if (slot != -1 && !g_status.flags[slot])
        return fmt::format("Saves\\NPCs\\{}", filename);
    return std::string(filename);
}

// FUNCTION: WIZ8 0x00514ec0
std::string BuildCharacterPath(std::string_view name, int slot)
{
    return BuildCharacterFilePath(fmt::format("{}.CHR", name), slot);
}

/* Load one character record from its loose file, or from the current save's
   CHAR chunks in a started game. A failure clears the record and may report. */
// FUNCTION: WIZ8 0x005152b0
bool LoadCharacter(std::string_view name, W8Character* character, int slot, bool report_failure)
try
{
    bool loaded = false;
    try {
        const auto path = BuildCharacterFilePath(name, slot);
        if (g_status.game_started && (slot == -1 || g_status.flags[slot] != 0)) {
            loaded = LoadCharacterFromCurrentGame(path, character);
        } else {
            auto handle = wiz8::open_file(path);
            *character = {};
            W8Character saved{};
            wiz8::retail::read_sized(*handle, saved);
            *character = std::move(saved);
            loaded = true;
        }
    } catch (const std::exception&) {
        loaded = false;
    }
    if (!loaded && report_failure) {
        CreateMessageBox(fmt::sprintf(g_localized_strings[W8_NOTICE_CHARACTER_LOAD_FAILED], name),
                         g_small_font, 1, true, false, 0);
    }
    return loaded;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00511df0
void FillCurrentSaveSlot(W8SaveSlot& slot)
{
    slot.name.clear();
    slot.level_id = GetLoadedLevelID();
    slot.game_time_ms = g_status.game_time_ms;
    slot.game_time_days = g_status.game_time_days;
    slot.iron_man = g_status.iron_man;
    const auto now = wiz8::current_local_time();
    slot.timestamp = {
        now.year, static_cast<unsigned>(now.month), static_cast<unsigned>(now.day),
        static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute),
        static_cast<unsigned>(now.second), static_cast<unsigned>(now.nanosecond / 100),
        static_cast<unsigned>(now.day_of_week)};
    CaptureSaveScreenshot(slot.screenshot);
    slot.version_major = kCurrentSaveVersion.major;
    slot.version_minor = kCurrentSaveVersion.minor;
    slot.version_patch = kCurrentSaveVersion.patch;
}

/* Read one save's listing entry: its status, version and screenshot. Saves
   without a status, flagged saves and saves past the endgame are not listed. */
static std::unique_ptr<W8SaveSlot> ReadSaveSlot(const std::string& name, const wiz8::FileStatus& metadata,
                                                int utc_offset)
{
    W8Chunk chunks;
    if (!chunks.OpenRead("Saves\\" + name)) return nullptr;
    auto slot = std::make_unique<W8SaveSlot>();
    DetachedStatus saved;
    bool has_status = false;
    slot->screenshot.capture_result = 0;
    slot->version_major = kUnversionedSave.major;
    slot->version_minor = kUnversionedSave.minor;
    slot->version_patch = kUnversionedSave.patch;
    chunks.ForEachChunk([&](unsigned int id) {
        switch (id) {
        case W8_CHUNK_GAME_STATUS:
            LoadGameStatus(&chunks, &saved.status);
            has_status = true;
            slot->dev_flagged = saved.status.dev_flagged;
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
    const auto& status = saved.status;
    if (!has_status || status.flag || status.endgame_started) return nullptr;
    slot->name = name.substr(0, name.find_last_of('.'));
    slot->local_write_time = wiz8::file_time_with_legacy_local_bias(
        wiz8::file_time_from_sdl(metadata.info.modify_time), utc_offset);
    slot->timestamp = wiz8::file_time_to_utc(slot->local_write_time);
    slot->level_id = status.current_level;
    slot->game_time_ms = status.game_time_ms;
    slot->iron_man = status.iron_man;
    slot->game_time_days = status.game_time_days;
    return slot;
}

/* Append every readable save after the slots already present, newest first. */
// FUNCTION: WIZ8 0x00511e70
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots)
try
{
    if (!wiz8::file_status("Saves")) return true;
    const auto first = static_cast<std::ptrdiff_t>(slots.size());
    const auto utc_offset = wiz8::current_utc_offset_seconds();
    for (const auto& name : wiz8::list_directory("Saves", std::string("*.") + g_save_extension)) {
        const auto metadata = wiz8::file_status("Saves\\" + name);
        if (!metadata || metadata->info.type != SDL_PATHTYPE_FILE) continue;
        if (IsCurrentGameFileName(name) || name.size() >= kCharacterNameBytes) continue;
        std::unique_ptr<W8SaveSlot> slot;
        try {
            slot = ReadSaveSlot(name, *metadata, utc_offset);
        } catch (const std::exception&) {
            continue; // An unreadable save is left out of the list.
        }
        if (!slot) continue;
        const auto ticks = slot->local_write_time.ticks();
        const auto position = std::find_if(slots.begin() + first, slots.end(), [&](const auto& listed) {
            return ticks > listed->local_write_time.ticks();
        });
        slots.insert(position, std::move(slot));
    }
    return true;
}
catch (const std::exception&) { return false; }

/* The level a save was made on, read from its status chunk alone; 0 when the
   save cannot be read. */
// FUNCTION: WIZ8 0x00512290
int GetSaveGameLevel(std::string_view slot_name)
try
{
    W8Chunk chunks;
    if (!chunks.OpenRead(SaveSlotPath(slot_name))) return 0;
    DetachedStatus saved;
    bool found = false;
    chunks.ForEachChunk([&](unsigned int id) {
        if (id != W8_CHUNK_GAME_STATUS) return true;
        LoadGameStatus(&chunks, &saved.status);
        found = true;
        return false;
    });
    chunks.Close();
    return found ? saved.status.current_level : 0;
}
catch (const std::exception&) { return 0; }

/* Fold the live session into g_status before it is written: the camera
   anchor, each shown message's remaining countdown, the master functions and
   the difficulty. Iron-man saves also record the file's masked creation time. */
static void PrepareStatusForSave(const wiz8::File& file)
{
    g_status.pending_move_location = cameraState(*GetWorld());
    ForEachShownMessage([](auto& message) {
        message.saved_remaining_ms = std::uint32_t(TimeUntilDeadline(message.clock));
    });
    gXStatus.gameplay_timer->Restart();
    DestroyUngroupedMonsters();
    SaveMasterFunctions();
    g_status.buffers.save_version = 1.1f;
    g_status.difficulty = g_settings.difficulty;
    if (g_status.iron_man) {
        const auto creation_time = file.times().created;
        g_status.save_filetime_xor[0] = creation_time.low ^ g_save_filetime_xor_low;
        g_status.save_filetime_xor[1] = creation_time.high ^ g_save_filetime_xor_high;
    }
}

/* Write one whole save: the CHAR and other-level chunks carried over from
   CurrentGame (unless this is CurrentGame), the status, version, screenshot
   (captured now when the caller has none), the message, location, NPC, fact,
   faction, journal and monster-control sections, and the current level's
   LVLS group last. */
static void WriteGame(std::string_view name, const W8SaveScreenshot* screenshot, W8Chunk& chunks)
{
    if (CompareTextIgnoreAsciiCase(name, "CurrentGame") != 0 && wiz8::file_status(kCurrentGamePath)) {
        W8Chunk current_game;
        if (!current_game.OpenRead(kCurrentGamePath)) throw std::runtime_error("open current game");
        CopyCurrentGameChunks(current_game, chunks);
        current_game.Close();
    }
    PrepareStatusForSave(chunks.Stream());
    {
        // The text-line cursor's transient state 2 is saved as 0.
        const short cursor = g_status.text_line_cursor;
        ScopeExit restore([&] { g_status.text_line_cursor = cursor; });
        if (cursor == 2) g_status.text_line_cursor = 0;
        SaveGlobalStatus(&chunks, &g_status);
    }
    chunks.WriteChunk(W8_CHUNK_GAME_VERSION, [&] {
        chunks.WriteValue(kCurrentSaveVersion.major);
        chunks.WriteValue(kCurrentSaveVersion.minor);
        chunks.WriteValue(kCurrentSaveVersion.patch);
    });
    W8SaveScreenshot captured;
    if (!screenshot) {
        CaptureSaveScreenshot(captured);
        screenshot = &captured;
    }
    chunks.WriteChunk(W8_CHUNK_SCREENSHOT, [&] { chunks.WriteValue(*screenshot); });

    auto& file = chunks.Stream();
    chunks.WriteChunk(W8_CHUNK_MESSAGE_STORAGE, [&] {
        RequireSection(SaveMessageStorage(file), "message storage");
    });
    if (!g_location_variable_values.empty())
        chunks.WriteChunk(W8_CHUNK_LOCATION_VARIABLES, [&] { SaveLocationVariables(file); });
    chunks.WriteChunk(W8_CHUNK_NPC_TRANSCRIPT, [&] {
        RequireSection(SaveNpcDialogueTranscript(file), "NPC transcript");
    });
    chunks.WriteChunk(W8_CHUNK_NPC_STATES, [&] { RequireSection(SaveNpcStates(file), "NPC states"); });
    chunks.WriteChunk(W8_CHUNK_FACT_STATE, [&] { SaveFactState(file); });
    chunks.WriteChunk(W8_CHUNK_FACTION_STATE, [&] { SaveFactionState(file); });
    chunks.WriteChunk(W8_CHUNK_JOURNAL, [&] { SaveFactJournal(file); });
    if (FindMonsterControlSpellEffect())
        chunks.WriteChunk(W8_CHUNK_MONSTER_CONTROL, [&] { SaveMonsterControlSpellEffect(&chunks); });
    RequireSection(SaveStatusHeader(&chunks), "level status");
}

// FUNCTION: WIZ8 0x005123F0
bool SaveGame(std::string_view name, W8SaveScreenshot* screenshot)
try
{
    if (name.empty()) return false;
    SaveTransaction transaction(SaveSlotPath(name));
    WriteGame(name, screenshot, transaction.chunks);
    transaction.commit();
    return true;
}
catch (const std::exception&) { return false; }

/* The shipped status file for a level. Levels 0-55 index the level folder
   table; retail's table has 47 entries, so levels 47-55 read past it exactly
   as the original does. Level 56 is the default test level and later levels
   are lettered test levels. */
// FUNCTION: WIZ8 0x00512e80
std::string BuildLevelStatusPath(unsigned int level)
{
    if (level == 56) return "Levels\\Test\\DefaultLevel.STS";
    if (level < 56)
        return fmt::format("Levels\\{}\\{}.STS", g_level_folders[level].folder_name,
                           g_level_folders[level].level_name);
    return fmt::format("Levels\\Test\\Level{:c}.STS", static_cast<char>(level - 56));
}

/* Read the STAT record and publish its counters and status block. Any version
   other than 2.0 is refused. */
// FUNCTION: WIZ8 0x00513090
bool LoadStatusHeader(W8Chunk* chunk)
try
{
    const auto header = chunk->ReadValue<W8StatusHeader>();
    if (header.version != kStatusHeaderVersion) return false;
    InitializeMonsterManagerState();
    InitializeItemManagerState();
    ResetNextTriggerId();
    ApplyStatusHeader(header);
    return true;
}
catch (const std::exception&) { return false; }

/* Rewrite a status file with the current level's group replaced and every
   other level's group kept. */
// FUNCTION: WIZ8 0x00513160
bool SaveLevelStatus(const char* path)
try
{
    if (!path) return false;
    SaveTransaction transaction(path);
    if (wiz8::file_status(path)) {
        W8Chunk source;
        if (!source.OpenRead(path)) return false;
        source.ForEachChunk([&](unsigned int id) {
            if (id == W8_CHUNK_LEVEL_STATUS && PeekGroupLevel(source) == g_status.current_level) return;
            RequireSection(transaction.chunks.CopyCurrentChunkFrom(&source), "level status copy");
        });
        source.Close();
    }
    if (!SaveStatusHeader(&transaction.chunks)) return false;
    transaction.commit();
    return true;
}
catch (const std::exception&) { return false; }

/* Write the current level's LVLS group: the level number, STAT, MONS and ITEM,
   then either the four transient sections a level restore needs or the full
   set an ordinary save keeps. */
// FUNCTION: WIZ8 0x00513260
bool SaveStatusHeader(W8Chunk* chunks)
try
{
    DestroyUngroupedMonsters();
    auto& file = chunks->Stream();
    chunks->WriteGroup(W8_CHUNK_LEVEL_STATUS, [&] {
        chunks->WriteValue(g_status.current_level);
        chunks->WriteChunk(W8_CHUNK_STATUS_HEADER, [&] { chunks->WriteValue(CaptureStatusHeader()); });
        chunks->WriteChunk(W8_CHUNK_MONSTERS, [&] { RequireSection(SaveMonsterStatus(chunks), "monster status"); });
        chunks->WriteChunk(W8_CHUNK_WORLD_ITEMS, [&] { WriteWorldItems(*chunks); });

        if (g_level_status_loading) {
            chunks->WriteChunk(W8_CHUNK_CURSOR_NODES, [&] {
                SaveWorldCursorNodes(file);
                SaveWorldCursorNodeStates(file);
            });
            chunks->WriteChunk(W8_CHUNK_ENCOUNTERS, [&] { SaveEncounterState(file); });
            chunks->WriteChunk(W8_CHUNK_TRIGGER_LOCKS, [&] {
                SaveTriggerRuntimeStates(g_world, file, g_level_status_loading);
            });
            chunks->WriteChunk(W8_CHUNK_TRIGGER_ACTIONS, [&] { SaveTriggerActionData(g_world, file); });
            return;
        }

        chunks->WriteChunk(W8_CHUNK_AUTOMAP_NOTES, [&] { SaveAutomapNotes(file); });
        if (!g_world->triggers.empty())
            chunks->WriteChunk(W8_CHUNK_TRIGGERS, [&] { SaveWorldTriggers(g_world, file); });
        chunks->WriteChunk(W8_CHUNK_PROPS, [&] { SaveWorldProps(g_world, file); });
        chunks->WriteChunk(W8_CHUNK_CURSOR_NODE_STATES, [&] { SaveWorldCursorNodeStates(file); });
        chunks->WriteChunk(W8_CHUNK_MONSTER_GENERATORS, [&] { SaveMonsterGenerators(file); });
        chunks->WriteChunk(W8_CHUNK_TRIGGER_STATES, [&] {
            SaveTriggerRuntimeStates(g_world, file, g_level_status_loading);
        });
        chunks->WriteChunk(W8_CHUNK_AMBIENT_SOUNDS, [&] { SaveAmbientSoundList(file); });
        chunks->WriteChunk(W8_CHUNK_PARTICLES, [&] { SaveParticleStates(file); });
        chunks->WriteChunk(W8_CHUNK_LIGHTS, [&] { SaveLightStates(file); });
    });
    return true;
}
catch (const std::exception&) { return false; }

/* The MONS section: group and monster counts, every group record (stamped
   version 3) with its encounter-list flag, then every monster record. Live
   list entries come first, then encounter groups and unborn monsters. */
// FUNCTION: WIZ8 0x005145a0
bool SaveMonsterStatus(W8Chunk* chunks)
try
{
    const auto list_index = [](unsigned int index, std::size_t live) {
        return index < live ? index : static_cast<unsigned int>(index - live + kSecondaryListBase);
    };
    const unsigned int group_count = gXStatus.monster_groups.size() + gXStatus.encounter_groups.size();
    const unsigned int monster_count = gXStatus.monsters.size() + gXStatus.unborn_monsters.size();
    chunks->WriteValue(group_count);
    chunks->WriteValue(monster_count);
    for (unsigned int index = 0; index < group_count; ++index) {
        W8MonsterGroup* group = GetMonsterGroupByListIndex(list_index(index, gXStatus.monster_groups.size()));
        if (!group) return false;
        group->version = kMonsterGroupRecordVersion;
        wiz8::retail::write_sized(*chunks, *group);
        const unsigned char encountered = std::ranges::contains(gXStatus.encounter_groups, group);
        chunks->WriteValue(encountered);
    }
    for (unsigned int index = 0; index < monster_count; ++index)
        WriteMonsterRecord(*chunks, list_index(index, gXStatus.monsters.size()));
    return true;
}
catch (const std::exception&) { return false; }

/* Apply a status file's group for `level`. */
// FUNCTION: WIZ8 0x005135d0
bool LoadLevelStatus(const char* path, int level)
try
{
    W8Chunk chunk;
    if (!chunk.OpenRead(path)) return false;
    const bool loaded = LoadItemStatus(&chunk, level);
    chunk.Close();
    return loaded;
}
catch (const std::exception&) { return false; }

/* Apply the saved group for `level` from an open status file. On a save load
   the level's shipped status is folded in first, so the saved sections land
   on top of the level's baseline. */
// FUNCTION: WIZ8 0x00513650
bool LoadItemStatus(W8Chunk* chunk, int level)
try
{
    return WithLevelGroup(*chunk, level, [&] {
        if (!g_level_status_loading) LoadDefaultLevelStatus(level);
        chunk->ForEachChunk([&](unsigned int section) { LoadLevelSection(*chunk, section); });
    });
}
catch (const std::exception&) { return false; }

/* Fold a level's shipped status file into the live state: only its cursor
   nodes, encounter generators and trigger records. */
// FUNCTION: WIZ8 0x005139c0
bool LoadDefaultLevelStatus(unsigned int level)
try
{
    W8LevelInfo info;
    if (!LevelBuildInfoByID(level, &info)) {
        srAssertFail("LevelFilesExist(ulLevel, &LevelName)", LOADSAVEGAME_CPP, 0x366, 0);
    }
    W8Chunk chunk;
    if (!chunk.OpenRead(BuildLevelStatusPath(level))) return false;
    WithLevelGroup(chunk, std::nullopt, [&] {
        auto& file = chunk.Stream();
        chunk.ForEachChunk([&](unsigned int section) {
            switch (section) {
            case W8_CHUNK_TRIGGER_LOCKS: LoadTriggerRuntimeStates(file); break;
            case W8_CHUNK_CURSOR_NODES: LoadWorldCursorNodes(file); break;
            case W8_CHUNK_ENCOUNTERS: MonGen::LoadAll(file); break;
            case W8_CHUNK_TRIGGER_ACTIONS: LoadTriggerActionData(file); break;
            }
        });
    });
    chunk.Close();
    return true;
}
catch (const std::exception&) { return false; }

/* Read one saved monster group and file it under the monster or encounter
   groups. A group whose species was deleted from the database is dropped. */
// FUNCTION: WIZ8 0x00513c20
bool LoadMonsterGroup(W8Chunk* chunk)
try
{
    auto owner = std::make_unique<W8MonsterGroup>();
    auto& group = *owner;
    wiz8::retail::read_sized(*chunk, group);
    const bool is_encounter = group.version >= 2 && chunk->ReadValue<unsigned char>() != 0;
    if (group.version < 3) group.forced_neutral = false;

    if (static_cast<unsigned int>(group.monster_id) >= std::size(gXStatus.monster_record_cache)) return false;
    const auto* record = MonsterDBFromSpecies(group.monster_id);
    if (!record) return false;
    if (record->deleted) return true;

    auto& list = is_encounter ? gXStatus.encounter_groups : gXStatus.monster_groups;
    list.reserve(list.size() + 1);
    // Members re-register as their monster records load.
    group.monsters.clear();
    group.member_count = 0;
    group.active_member_count = 0;
    group.members_active = false;
    group.fInCombat = false;
    list.push_back(owner.release());
    ActivateGroupMembers(&group, W8_MONSTER_LOAD_ALL_CYCLES);
    if (group.encounter_registered && group.leader_group_id == 0) RegisterActiveEncounterGroup(&group);
    return true;
}
catch (const std::exception&) { return false; }

/* Read one monster record (see WriteMonsterRecord), adopt the monster into the
   live or unborn list and its group, activate it and restore its icons,
   movement, orders and script. A monster whose species was deleted is
   dropped; one with no hit points starts dying. */
// FUNCTION: WIZ8 0x00513d80
bool LoadMonster(W8Chunk* chunk)
try
{
    auto owner = AllocateZeroed<W8MonsterInfo>();
    auto& info = *owner;
    const auto version = chunk->ReadValue<unsigned int>();
    wiz8::retail::read_sized(*chunk, info);
    std::optional<SavedMonsterScript> script;
    if (chunk->ReadValue<unsigned char>()) script = SavedMonsterScript::Read(*chunk);
    info.fActive = false;
    info.p3D = nullptr;
    info.fInCombat = false;
    info.pCombat = nullptr;
    const bool unborn = version >= 5 && chunk->ReadValue<unsigned char>() != 0;
    std::optional<SavedMonsterMovement> movement;
    if (version >= 2 && chunk->ReadValue<unsigned char>()) movement = SavedMonsterMovement::Read(*chunk);
    std::optional<SavedMonsterOrders> orders;
    if (version >= 3) orders = SavedMonsterOrders::Read(*chunk, version);

    for (const auto& effect : info.effect_slots) {
        if (effect.duration && static_cast<unsigned int>(effect.effect_id) >= std::size(g_effect_visual_table))
            return false;
    }
    if (info.monster_species >= std::size(gXStatus.monster_record_cache)) return false;
    const auto* record = MonsterDBFromSpecies(info.monster_species);
    if (!record) return false;
    if (record->deleted) return true;
    auto* group = GetMonsterGroupByListIndex(
        GetMonsterGroupIndexByID(0x698, LOADSAVEGAME_CPP, info.monster_group_id, true));
    if (!group) return false;

    auto& monsters = unborn ? gXStatus.unborn_monsters : gXStatus.monsters;
    monsters.reserve(monsters.size() + 1);
    group->monsters.push_back(info.location_id);
    monsters.push_back(owner.release());
    const auto leader = static_cast<unsigned int>(group->leader_location_id);
    if (leader == WIZ8_DEBUG_UNINITIALIZED_HEAP_PATTERN || leader < static_cast<unsigned int>(info.location_id))
        group->leader_location_id = info.location_id;
    ++group->member_count;
    RequestRedrawParty();
    if (info.highest_condition < W8_CONDITION_TURNCOAT) ++group->active_member_count;

    ActivateMonster(&info, W8_MONSTER_LOAD_ALL_CYCLES);
    ActivateMonsterInWorld(&info);
    W8Monster* monster = info.p3D;
    RestoreMonsterSpellIcons(info, monster);
    if (movement) movement->Apply(*monster);
    if (orders) orders->Apply(info, *monster);
    if (script) script->Apply(info, *monster);
    if (info.hp_current == 0) MonsterStartsDying(&info, true);
    return true;
}
catch (const std::exception&) { return false; }

/* Tear down the live session before a new game or a save load replaces it. */
// FUNCTION: WIZ8 0x00512c40
void ResetLiveSessionForLoad(void)
{
    if (g_status.current_level != -1) {
        UnloadLevel("");
        SoundEmptyCache();
    }
    if (gXStatus.character_event_queue) gXStatus.character_event_queue->DestroyAllEvents();
    ResetMainGameScreenState();
    ClearNpcMessageQueue();
    ResetMainScreenStateBlock();
    wiz8::remove_all_and_delete(g_spell_effects);
    ReleaseAllTriggers();
    ResetGameplayStatusBlock();
}

/* Create the writable save directories in the user overlay. */
// FUNCTION: WIZ8 0x00512d00
bool VerifyDataSubdirs(void)
try
{
    for (const char* directory : {"Saves", "Saves\\Characters", "Saves\\NPCs"}) {
        wiz8::create_directory(directory);
        const auto entry = wiz8::file_status(directory);
        if (!entry || entry->info.type != SDL_PATHTYPE_DIRECTORY || !entry->writable) return false;
    }
    return true;
}
catch (const std::exception&) { return false; }

/* Write an item and its siblings, each with its live position and flags. */
// FUNCTION: WIZ8 0x00514be0
bool SaveItemFile(wiz8::File& handle, W8WorldItem* item_info)
try
{
    for (auto* item = item_info; item; item = item->next) {
        item->saved_marker = 1;
        if (item->fActive) {
            item->position = item->p3D->m_pRep->GetLocation();
            item->entity_flags = static_cast<W8ItemRep*>(item->p3D->m_pRep)->flags;
        }
        if (g_level_status_loading) item->entity_flags &= ~W8_ITEM_ENTITY_RADAR_SEEN;
        wiz8::retail::write(handle, *item);
    }
    return true;
}
catch (const std::exception&) { return false; }

/* Read an item chain. A saved next pointer only marks that another record
   follows. The whole chain is staged before it is published to the item list
   and the search registry. */
// FUNCTION: WIZ8 0x00514c80
W8WorldItem* LoadItem(wiz8::File& handle, bool add_to_list)
try
{
    std::vector<W8WorldItem> staged;
    bool has_next;
    do {
        const auto image = wiz8::retail::read_image<W8WorldItem>(handle);
        has_next = image.next != 0;
        W8WorldItem& item = staged.emplace_back();
        wiz8::retail::decode_image(image, item);
        item.sector_id = -2;
        item.fActive = false;
        if (g_level_status_loading) item.entity_flags &= ~W8_ITEM_ENTITY_RADAR_SEEN;
    } while (has_next);

    // Allocate everything that can fail before anything is published.
    std::vector<MallocPtr<W8WorldItem>> owners;
    std::vector<std::unique_ptr<W8Searchable>> searchables;
    for (const auto& item : staged) {
        auto& owner = owners.emplace_back(static_cast<W8WorldItem*>(malloc(sizeof(W8WorldItem))), &free);
        if (!owner) throw std::bad_alloc();
        *owner = item;
        if (ItemHasFlags(&item, W8_WORLD_ITEM_HIDDEN)) searchables.push_back(std::make_unique<W8Searchable>());
    }
    g_searchables.reserve(g_searchables.size() + searchables.size());
    if (add_to_list) gXStatus.world_items.reserve(gXStatus.world_items.size() + 1);

    std::vector<W8WorldItem*> chain;
    chain.reserve(owners.size());
    for (auto& owner : owners) chain.push_back(owner.release());
    auto searchable = searchables.begin();
    for (std::size_t index = 0; index < chain.size(); ++index) {
        if (index + 1 < chain.size()) chain[index]->next = chain[index + 1];
        if (ItemHasFlags(chain[index], W8_WORLD_ITEM_HIDDEN)) {
            (*searchable)->world_item = chain[index];
            g_searchables.push_back(searchable->release());
            ++searchable;
        }
    }
    if (add_to_list) gXStatus.world_items.push_back(chain.front());
    return chain.front();
}
catch (const std::exception&) { return nullptr; }

/* Whether any save other than CurrentGame exists. */
// FUNCTION: WIZ8 0x00512fb0
bool SaveGameExists(void)
{
    const auto saves = wiz8::list_directory("Saves", std::string("*.") + g_save_extension);
    return std::ranges::any_of(saves, [](const auto& name) { return !IsCurrentGameFileName(name); });
}

/* Write a character record. Before the game starts it is a loose file (its
   read-only bit cleared first); afterwards a CHAR chunk of the current save.
   On failure either the save-failed notice is shown, taking the continuation
   with it, or the continuation runs at once. */
// FUNCTION: WIZ8 0x00515090
bool SaveCharacter(W8Character* character, int slot, bool report_failure,
                   void (*continuation)(void))
try
{
    character->record_version = 1;
    const auto path = BuildCharacterPath(character->name, slot);
    bool saved = false;
    try {
        if (g_status.game_started) {
            saved = SaveCharacterToCurrentGame(path, slot, character);
        } else {
            const auto status = wiz8::file_status(path);
            if (status && status->read_only) wiz8::clear_read_only(path);
            wiz8::StagedFile staged(path);
            wiz8::retail::write_sized(*staged.file(), *character);
            staged.commit();
            saved = true;
        }
    } catch (const std::exception&) {
        saved = false;
    }
    if (saved) return true;
    if (report_failure) {
        CreateMessageBox(fmt::sprintf(g_localized_strings[W8_NOTICE_CHARACTER_SAVE_FAILED], character->name),
                         g_small_font, 1, true, false, continuation);
    } else if (continuation) {
        continuation();
    }
    return false;
}
catch (const std::exception&) { return false; }

/* Replace every CHAR chunk with this name in the current save by one new
   record. Malformed CHAR payloads are skipped by loading and can be replaced. */
// FUNCTION: WIZ8 0x005155b0
bool SaveCharacterToCurrentGame(std::string_view path, int /*slot*/, W8Character* character)
try
{
    if (path.empty() || !character || path.size() >= kCharacterNameBytes) return false;
    W8Chunk source;
    if (!source.OpenRead(kCurrentGamePath)) return false;
    SaveTransaction transaction(kCurrentGamePath);
    source.ForEachChunk([&](unsigned int id) {
        if (id == W8_CHUNK_CHARACTER && source.CurrentChunkExtent() >= kCharacterNameBytes) {
            if (ReadCharacterNameMatches(source, path)) return;
            source.RewindCurrentChunk();
        }
        RequireSection(transaction.chunks.CopyCurrentChunkFrom(&source), "character save copy");
    });
    source.Close();
    transaction.chunks.WriteChunk(W8_CHUNK_CHARACTER, [&] {
        char name[kCharacterNameBytes]{};
        std::ranges::copy(path, name);
        transaction.chunks.WriteBytes(name, sizeof(name));
        wiz8::retail::write_sized(transaction.chunks, *character);
    });
    transaction.commit();
    return true;
}
catch (const std::exception&) { return false; }

/* Load the last complete CHAR record with this name from the current save. */
// FUNCTION: WIZ8 0x005156c0
bool LoadCharacterFromCurrentGame(std::string_view path, W8Character* character)
try
{
    if (path.empty() || !character) return false;
    W8Chunk chunk;
    if (!chunk.OpenRead(kCurrentGamePath)) return false;
    std::optional<W8Character> found;
    chunk.ForEachChunk([&](unsigned int id) {
        if (id != W8_CHUNK_CHARACTER || chunk.CurrentChunkExtent() < kCharacterNameBytes + 4) return;
        const bool matches = ReadCharacterNameMatches(chunk, path);
        const auto size = chunk.ReadValue<unsigned int>();
        if (!matches || size > W8_CHARACTER_SERIALIZED_SIZE || size > chunk.RemainingBytes()) return;
        W8Character saved{};
        wiz8::retail::read_exact(chunk, saved, size);
        found = std::move(saved);
    });
    chunk.Close();
    if (!found) return false;
    *character = std::move(*found);
    return true;
}
catch (const std::exception&) { return false; }

/* Render the world into the screenshot's 80x60 ARGB1555 pixels. Auto-flip is
   suspended so the capture is never presented, and the frame is redrawn
   afterwards. */
// FUNCTION: WIZ8 0x00515840
void CaptureSaveScreenshot(W8SaveScreenshot& screenshot)
{
    screenshot.version = 1.0f;
    W8ScreenRect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = 640;
    rect.bottom = 480;
    srColorSurface surface(srPixelConvert::SURFACE_ARGB1555, screenshot.pixels, 80, 60, 160);
    SetRendererAutoFlipEnabled(false);
    ScopeExit restore([] { SetRendererAutoFlipEnabled(true); });
    screenshot.capture_result = RenderWorldToSurface(&surface, &rect, true);
    RenderFrame();
}

/* Delete the last-used slot and the CurrentGame save. */
// FUNCTION: WIZ8 0x00515920
void DeleteCurrentSaveFiles(void)
{
    wiz8::remove_file(SaveSlotPath(GetLastSaveName()));
    wiz8::remove_file(kCurrentGamePath);
}

/* Save now if autosaving is enabled (or the game is Iron Man) and the moment
   allows it. A declined autosave counts as success. */
// FUNCTION: WIZ8 0x005159e0
bool AutoSaveIfAllowed(bool forced)
{
    gXStatus.save_notice_shown = false;
    const bool wanted = (g_settings.auto_save != 0 && !forced) || g_status.iron_man;
    if (!wanted || !CanAutoSaveNow()) return true;
    return SaveGame(AutoSaveSlotName(), nullptr);
}

/* Take the pending-save flag, clearing it. */
// FUNCTION: WIZ8 0x00515910
bool TakePendingSaveFlag(void)
{
    return std::exchange(g_save_pending, false);
}

// FUNCTION: WIZ8 0x00512f70
unsigned char SaveSlotFileExists(std::string_view slot_name)
{
    return IsRegularFile(SaveSlotPath(slot_name));
}

/* Announce an autosave; the next ProcessMainGameAutoSave tick performs it. */
// FUNCTION: WIZ8 0x00515ac0
void ReportSaveFailed(bool quiet)
{
    if (quiet && !g_status.iron_man) return;
    gXStatus.save_notice_shown = true;
    ShowMainGameNotice(W8_STRING_AUTOSAVE_STARTING);
}

/* The timed main-game autosave runs over two frames: once the gameplay timer
   has elapsed the first eligible frame announces it, and the next eligible
   frame saves and reports the result. */
// FUNCTION: WIZ8 0x00515b00
void ProcessMainGameAutoSave(void)
{
    if ((g_settings.auto_save == 0 && !g_status.iron_man) || !CanAutoSaveNow()) return;
    if (!gXStatus.save_notice_shown) {
        if (gXStatus.gameplay_timer->GetProgress() <= g_float_one) return;
        gXStatus.save_notice_shown = true;
        ShowMainGameNotice(W8_STRING_AUTOSAVE_STARTING);
        gXStatus.gameplay_timer->Restart();
        return;
    }
    gXStatus.save_notice_shown = false;
    const bool saved = SaveGame(AutoSaveSlotName(), nullptr);
    ShowMainGameNotice(saved ? W8_STRING_AUTOSAVE_SUCCEEDED : W8_STRING_AUTOSAVE_FAILED);
}

/* Write the live monster-control effect as the HYPN record. */
// FUNCTION: WIZ8 0x00516580
void SaveMonsterControlSpellEffect(W8Chunk* chunks)
{
    W8SpellEffectEntry* lure = FindMonsterControlSpellEffect();
    if (!lure) srAssertFail("pLure", LOADSAVEGAME_CPP, 0xdb3, 0);
    ForEachMonsterControlField(*lure, [&]<class Field>(const Field& field) {
        if constexpr (std::is_same_v<Field, W8CombatSlot>) wiz8::retail::write(*chunks, field);
        else chunks->WriteValue(field);
    });
}

/* The quick-save slot to write next: the first missing one, otherwise the
   one written longest ago. */
// FUNCTION: WIZ8 0x00516670
std::optional<std::string> SelectQuickSaveSlotForWrite()
try
{
    std::optional<std::pair<int, std::uint64_t>> oldest;
    for (int slot = 1; slot <= 3; ++slot) {
        const auto path = SaveSlotPath(QuickSaveName(slot));
        if (!wiz8::file_status(path)) return QuickSaveName(slot);
        const auto ticks = wiz8::open_file(path)->times().modified.ticks();
        if (!oldest || ticks < oldest->second) oldest.emplace(slot, ticks);
    }
    return QuickSaveName(oldest->first);
}
catch (const std::exception&) { return {}; }

/* The newest numbered quick save, for command-line startup. The unnumbered
   Quick slot is used only when none of the numbered ones exists. */
// FUNCTION: WIZ8 0x00516740
std::optional<std::string> FindStartupQuickSave()
try
{
    std::optional<std::pair<int, std::uint64_t>> newest;
    for (int slot = 1; slot <= 3; ++slot) {
        const auto path = SaveSlotPath(QuickSaveName(slot));
        if (!IsRegularFile(path)) continue;
        const auto ticks = wiz8::open_file(path)->times().modified.ticks();
        if (!newest || ticks > newest->second) newest.emplace(slot, ticks);
    }
    if (newest) return QuickSaveName(newest->first);
    if (IsRegularFile(SaveSlotPath("Quick"))) return "Quick";
    return {};
}
catch (const std::exception&) { return {}; }

/* Read the GSTA section: the status block followed by eight characters and
   eight party rows, each size-prefixed. The live buffer pointers are kept.
   Old status blocks get the message-box migration, pre-v2 characters their
   profession repair, and the live party rows rebuild their item pointers
   rather than trusting saved addresses. */
// FUNCTION: WIZ8 0x00515cf0
void LoadGameStatus(W8Chunk* chunks, W8GlobalStatus* status)
{
    W8Character* characters = status->buffers.Char;
    W8PartySlotRow* party_rows = status->buffers.XChar;
    if (!characters || !party_rows) throw std::runtime_error("missing status buffers");

    W8GlobalStatus saved{};
    W8Character saved_characters[8]{};
    W8PartySlotRow saved_rows[8]{};
    wiz8::retail::read_sized(*chunks, saved);
    if (saved.buffers.save_version < 1.1f) {
        // Before 1.1 there were three message boxes, stored in a legacy pair.
        for (int box = 0; box < 3; ++box) {
            saved.text_box_lines_used[box] = saved.legacy_text_box_lines[0][box];
            saved.text_box_lines_shown[box] = saved.legacy_text_box_lines[1][box];
        }
        saved.text_box_lines_used[3] = 0;
        saved.text_box_lines_shown[3] = 0;
    }
    for (int box = 0; box < 4; ++box) {
        if (saved.text_box_lines_used[box] > std::size(g_message_storage[box]) ||
            saved.text_box_lines_shown[box] > std::size(g_message_storage[box]))
            throw std::runtime_error("invalid message storage count");
    }
    for (auto& character : saved_characters) {
        wiz8::retail::read_sized(*chunks, character);
        if (character.record_version < 2 && character.original_profession == W8_PROFESSION_FIGHTER &&
            character.profession_levels[W8_PROFESSION_FIGHTER] == 0)
            character.original_profession = character.iProfession;
    }
    for (auto& party_row : saved_rows) wiz8::retail::read_sized(*chunks, party_row);

    saved.buffers.Char = characters;
    saved.buffers.XChar = party_rows;
    std::ranges::move(saved_characters, characters);
    std::ranges::copy(saved_rows, party_rows);
    *status = std::move(saved);
    RebuildPartyStatus(&status->formation, party_rows);
    if (status != &g_status) return;

    for (int slot = 0; slot < 8; ++slot) {
        auto& party_row = party_rows[slot];
        const auto origin = static_cast<signed char>(party_row.item_origin);
        const auto item_slot = static_cast<short>(party_row.item_slot);
        W8ItemInstance* item = nullptr;
        if (party_row.fOccupied && party_row.pending_action == W8_ACTION_USE_ITEM && origin != -1 &&
            item_slot != -1) {
            item = FindCharacterItemAt(slot, static_cast<unsigned char>(origin),
                                      static_cast<unsigned short>(item_slot));
        }
        party_row.pending_action_detail.item_use.item = item;
        party_row.action_detail1.item_use.item = nullptr;
        party_row.spell_target.pPCItem = nullptr;
        party_row.item_target.pPCItem = nullptr;
        party_row.breath_target.pPCItem = nullptr;
    }
    RebuildPartyStatus(&status->formation, party_rows);
}

/* Write the GSTA section; see LoadGameStatus. */
// FUNCTION: WIZ8 0x00515fa0
void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status)
{
    chunks->WriteChunk(W8_CHUNK_GAME_STATUS, [&] {
        wiz8::retail::write_sized(*chunks, *status);
        for (int slot = 0; slot != 8; ++slot) wiz8::retail::write_sized(*chunks, status->buffers.Char[slot]);
        for (int slot = 0; slot != 8; ++slot) wiz8::retail::write_sized(*chunks, status->buffers.XChar[slot]);
    });
}

/* Load one level's world items from the current save without listing them. */
// FUNCTION: WIZ8 0x00516070
bool LoadSavedLevelItems(int level, std::vector<W8WorldItem*>& items)
try
{
    W8Chunk chunk;
    if (!chunk.OpenRead(kCurrentGamePath)) return false;
    const bool found = WithLevelGroup(chunk, level, [&] {
        chunk.ForEachChunk([&](unsigned int section) {
            if (section != W8_CHUNK_WORLD_ITEMS) return;
            const auto count = ReadWorldItemCount(chunk);
            if (count > static_cast<unsigned int>(std::numeric_limits<int>::max() - items.size()))
                throw std::runtime_error("too many world items");
            items.reserve(items.size() + count);
            for (unsigned int index = 0; index < count; ++index) {
                W8WorldItem* item = LoadItem(chunk.Stream(), false);
                RequireSection(item != nullptr, "world item");
                items.push_back(item);
            }
        });
    });
    chunk.Close();
    return found;
}
catch (const std::exception&) { return false; }

/* The first free name of Ending, Ending1 ... Ending20. */
// FUNCTION: WIZ8 0x00516890
std::optional<std::string> FindFreeEndingSaveName()
try
{
    for (int index = 0; index <= 20; ++index) {
        auto name = index == 0 ? std::string("Ending") : fmt::format("Ending{}", index);
        if (!IsRegularFile(SaveSlotPath(name))) return name;
    }
    return {};
}
catch (const std::exception&) { return {}; }

/* Load a save slot: tear down the live session, make the slot the new
   CurrentGame, apply each section, then re-arm the message countdowns, the
   gameplay timer and the held-item cursor and normalize the loaded items. */
// FUNCTION: WIZ8 0x00512920
bool LoadGame(std::string_view slot_name)
try
{
    ResetLiveSessionForLoad();
    wiz8::copy_file(SaveSlotPath(slot_name), kCurrentGamePath, wiz8::CopyMode::replace);
    W8Chunk chunks;
    if (!chunks.OpenRead(kCurrentGamePath)) return false;
    auto& file = chunks.Stream();
    chunks.ForEachChunk([&](unsigned int id) {
        switch (id) {
        case W8_CHUNK_GAME_STATUS: LoadGameStatus(&chunks, &g_status); break;
        case W8_CHUNK_MESSAGE_STORAGE: LoadMessageStorage(file); break;
        case W8_CHUNK_LOCATION_VARIABLES: LoadLocationVariables(file); break;
        case W8_CHUNK_NPC_TRANSCRIPT: LoadNpcDialogueTranscript(file); break;
        case W8_CHUNK_NPC_STATES: LoadNpcStates(file); break;
        case W8_CHUNK_FACT_STATE: LoadFactState(file); break;
        case W8_CHUNK_FACTION_STATE: LoadFactionState(file); break;
        case W8_CHUNK_JOURNAL: LoadJournalEntries(file); break;
        case W8_CHUNK_MONSTER_CONTROL: LoadMonsterControlSpellEffect(&chunks); break;
        }
    });
    chunks.Close();
    ForEachShownMessage([](auto& message) { message.clock = w8_get_ticks() + message.saved_remaining_ms; });
    gXStatus.gameplay_timer->Restart();
    ResetMainGameScreenState();
    if (g_status.item_in_hand.iItemNo == -1) {
        ClearHeldItemDisplay();
    } else {
        SetItemCursor(0);
    }
    SanitizeLoadedItems();
    return true;
}
catch (const std::exception&) { return false; }

/* Read the HYPN record into a new spell effect. */
// FUNCTION: WIZ8 0x00516310
void LoadMonsterControlSpellEffect(W8Chunk* chunks)
{
    auto effect = std::make_unique<W8SpellEffectEntry>();
    ForEachMonsterControlField(*effect, [&]<class Field>(Field& field) {
        if constexpr (std::is_same_v<Field, W8CombatSlot>) wiz8::retail::read_exact(*chunks, field);
        else chunks->ReadValue(field);
    });
    AddSpellEffect(effect.release());
}
