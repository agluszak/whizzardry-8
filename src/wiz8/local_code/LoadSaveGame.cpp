#include "wiz8/engine_code/stScript.h" // IWYU pragma: keep - complete script type used through the owned pointer.
#include <fmt/format.h>
#include <fmt/printf.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <optional>
#include <string_view>
#include <vector>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <limits>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <utility>

#include "compat/compiler.h"
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
#include "Types.h"
#include "compat/native.h"
#include "compat/ptr32.h"
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

/* 0x004F8130, ItemManager.cpp line 998: asserts the item is non-null, then
   reports whether the flag word at +0x29 has any of the caller's bits set. */
/* Set to 1 by LoadLevel around its restore call at
   0x005135D0 and cleared immediately after, and read only from the save and
   load paths. It gates the bit-3 clear below. The meaning is not established
   beyond "a level restore is in progress", so the name stays positional. */

#define LOADSAVEGAME_CPP "C:\\Projects\\Wizardry 8\\Local Code\\LoadSaveGame.cpp"

/* The fixed 0x314-byte header every save begins with. Only the fields
   LoadStatusHeader forwards are established; the rest is read and kept. */
struct W8StatusHeader {
    float version;
    int next_group_id;
    int next_monster_location_id;
    int next_world_item_id;
    int next_trigger_id;
    unsigned char status_block[0x100];
    unsigned char unknown_114[0x200];
};

W8_ABI_ASSERT(sizeof(W8StatusHeader) == 0x314, "W8StatusHeader_must_be_0x314");

/* Same-unit bodies SaveGame reaches before their definitions. */
void ReadSaveChunks(W8Chunk* source, W8Chunk* destination);
void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status);

static bool SaveMonsterRecord(W8Chunk* chunks, unsigned int index);

/* 0x0061A134/0x0061A138: the two XOR masks SaveGame applies to the file's
   creation-time pair before it lands in the status block. */
// GLOBAL: WIZ8 0x0061A134
static unsigned int g_save_filetime_xor_low = 0x6b24e9f0;
// GLOBAL: WIZ8 0x0061A138
static unsigned int g_save_filetime_xor_high = 0xe77c28c1;


/* 0x0068517C selects where characters live, and flags is a per-slot byte
   consulted only when it is set. The failure notice comes out of the shared
   notice array, and 0x00683678 is passed alongside; neither is established
   beyond that, so both keep positional names. */

/* Build the loose character/NPC path in the two forms used by the save code.
   The first accepts an already formatted filename or wildcard; the second
   appends the canonical CHR extension to a character's wide name first.  When
   characters are being supplied by an archive, a flagged slot or the external
   character sentinel keeps the caller's name unqualified. */
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

/* Loads one character record, either from a loose file under Saves\Characters
   or Saves\NPCs, or through LoadCharacterFromCurrentGame when 0x0068517C says
   characters are not loose. The two spellings of the path share one sprintf:
   the branch that already has a directory literal jumps into the arm that
   formats one, which is what writing the call in both arms compiles to.
   The record is cleared before the read, and the read is two calls: a four-byte
   length and then that many bytes. A short or failed second read leaves the
   record cleared and reports failure, and the file is closed either way. */
// FUNCTION: WIZ8 0x005152b0
bool LoadCharacter(std::string_view name, W8Character* character, int slot, bool report_failure)
try
{
    std::string path;
    bool loaded = false;

    try {
        path = BuildCharacterFilePath(name, slot);
        if (g_status.game_started && (slot == -1 || g_status.flags[slot] != 0)) {
            loaded = LoadCharacterFromCurrentGame(path, character);
        } else {
            auto handle = wiz8::open_file(path);
            *character = {};
            unsigned int size;
            handle->read_exact(&size, sizeof(size));
            if (size <= W8_CHARACTER_SERIALIZED_SIZE) {
                W8Character saved{};
                wiz8::retail::read_exact(*handle, saved, size);
                *character = std::move(saved);
                loaded = true;
            }
        }
    } catch (const std::exception&) {
        loaded = false;
    }
    if (loaded) {
        return true;
    }
    if (report_failure) {
        CreateMessageBox(fmt::sprintf(g_localized_strings[W8_NOTICE_CHARACTER_LOAD_FAILED], name),
                         g_small_font, 1, true, false, 0);
    }
    return false;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00511df0
void FillCurrentSaveSlot(W8SaveSlot* slot)
{
    slot->name[0] = 0;
    slot->level_id = GetLoadedLevelID();
    slot->game_time_ms = g_status.game_time_ms;
    slot->game_time_days = g_status.game_time_days;
    slot->iron_man = g_status.iron_man;
    const auto now = wiz8::current_local_time();
    slot->timestamp = {
        now.year, static_cast<unsigned>(now.month), static_cast<unsigned>(now.day),
        static_cast<unsigned>(now.hour), static_cast<unsigned>(now.minute),
        static_cast<unsigned>(now.second), static_cast<unsigned>(now.nanosecond / 100),
        static_cast<unsigned>(now.day_of_week)};
    CaptureSaveScreenshot(&slot->screenshot);
    slot->version_major = 1;
    slot->version_minor = 2;
    slot->version_patch = 4;
}

// FUNCTION: WIZ8 0x00511e70
bool EnumerateSaveSlots(std::vector<W8SaveSlot*>& slots)
try
{
    W8Chunk chunks;
    int first = static_cast<int>(slots.size());
    try {
        if (!wiz8::file_status("Saves")) return true;
        const auto utc_offset = wiz8::current_utc_offset_seconds();
        for (const auto& name : wiz8::list_directory("Saves", std::string("*.") + g_save_extension)) {
            std::string path = "Saves\\" + name;
            const auto metadata = wiz8::file_status(path);
            if (!metadata || metadata->info.type != SDL_PATHTYPE_FILE) continue;
            if (_stricmp(name.c_str(), "CurrentGame.SAV") != 0 && name.size() < 64 &&
                chunks.OpenRead(path.data())) {
                auto slot_owner = std::make_unique<W8SaveSlot>();
                W8SaveSlot* slot = slot_owner.get();
                W8GlobalStatus status{};
                W8Character characters[8]{};
                W8PartySlotRow party_rows[8]{};
                status.buffers.Char = characters;
                status.buffers.XChar = party_rows;
                bool has_status = false;
                slot->screenshot.capture_result = 0;
                slot->version_major = 1;
                slot->version_minor = 0;
                slot->version_patch = 0;
                int count = chunks.ChunkCount();
                for (int index = 0; index < count; ++index) {
                    chunks.OpenChunk(0, 0);
                    if (!chunks.CurrentChunkAtEnd()) {
                        switch (chunks.CurrentChunkId()) {
                        case 0x41545347:
                            LoadGameStatus(&chunks, &status);
                            has_status = true;
                            slot->dev_flagged = status.dev_flagged;
                            break;
                        case 0x52455647:
                            chunks.m_hFile->read_exact(&slot->version_major, 4);
                            chunks.m_hFile->read_exact(&slot->version_minor, 4);
                            chunks.m_hFile->read_exact(&slot->version_patch, 4);
                            break;
                        case 0x544f4853:
                            chunks.m_hFile->read_exact(&slot->screenshot, 0x2588);
                            break;
                        }
                    }
                    chunks.SkipCurrentChunk();
                    chunks.ReleaseCurrentChunk();
                }
                chunks.Close();
                if (has_status && !status.flag && !status.endgame_started) {
                    const auto stem = name.substr(0, name.find_last_of('.'));
                    slot->name = stem;
                    const auto local_time = wiz8::file_time_with_legacy_local_bias(
                        wiz8::file_time_from_sdl(metadata->info.modify_time), utc_offset);
                    slot->local_write_time = local_time;
                    slot->timestamp = wiz8::file_time_to_utc(local_time);
                    slot->level_id = status.current_level;
                    slot->game_time_ms = status.game_time_ms;
                    slot->iron_man = status.iron_man;
                    slot->game_time_days = status.game_time_days;
                    int position;
                    for (position = first; position < static_cast<int>(slots.size()); ++position) {
                        if (slot->local_write_time.ticks() >
                            slots[position]->local_write_time.ticks()) {
                            break;
                        }
                    }
                    slots.insert(slots.begin() + position, slot);
                    (void)slot_owner.release();
                }
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}
catch (const std::exception&) { return false; }

/* Open one save slot and read only its game-status chunk. Startup needs the
   saved level before it commits to the full load, so every other top-level
   chunk is skipped and released without being materialized. */
// FUNCTION: WIZ8 0x00512290
int GetSaveGameLevel(std::string_view slot_name)
try
{
    W8Chunk chunks;
    std::string path;
    W8GlobalStatus status{};
    W8Character characters[8]{};
    W8PartySlotRow party_rows[8]{};
    status.buffers.Char = characters;
    status.buffers.XChar = party_rows;
    int count;
    int index;

    path = fmt::format("{}\\{}.{}", "Saves", slot_name, g_save_extension);
    if (chunks.OpenRead(path)) {
        count = chunks.ChunkCount();
        for (index = 0; index < count; ++index) {
            chunks.OpenChunk(0, 0);
            if (!chunks.CurrentChunkAtEnd() && chunks.CurrentChunkId() == 0x41545347) {
                LoadGameStatus(&chunks, &status);
                chunks.Close();
                return status.current_level;
            }
            chunks.SkipCurrentChunk();
            chunks.ReleaseCurrentChunk();
        }
        chunks.Close();
    }
    return 0;
}
catch (const std::exception&) { return 0; }

/* Write one whole save file: the live status block, the version triple, the
   SHOT screenshot (rendered through an offscreen surface when the caller did
   not supply one), then the TEXT, TVAR, NPCI, NPCT, NPCF, FATA, JRNL and HYPN
   sections, and the header last. Before any of that the live state is folded
   into the status block: the camera position goes to pending_move_location,
   every shown message line snapshots whether its countdown was still ticking,
   the gameplay timer restarts, ungrouped monsters are destroyed, the master
   functions save, and an iron-man game additionally stores the file's creation
   time XOR-masked by the two data constants. A save of anything but
   CurrentGame first absorbs the loose character chunks from
   Saves\CurrentGame.SAV through ReadSaveChunks. Every write and finalization failure aborts
   the staged replacement. */
// FUNCTION: WIZ8 0x005123F0
namespace {
// The save transaction owns both RIFF finalization and publication.
class SaveTransaction {
    wiz8::StagedFile staged;
public:
    W8Chunk chunks;
    explicit SaveTransaction(std::string_view path) : staged(path) {
        if (!chunks.OpenWrite(staged.file())) throw std::runtime_error("open staged save");
    }
    void commit() { chunks.Close(); staged.commit(); }
};
}
static bool WriteGame(std::string_view name, W8SaveScreenshot* screenshot,
                      W8Chunk& chunks)
try
{
    W8Chunk current_game;
    W8ScreenRect bounds;
    std::unique_ptr<W8SaveScreenshot> generated_screenshot;
    short cursor;
    int version_major;
    int version_minor;
    int version_patch;
    int region;
    unsigned int index;

    if (CompareTextIgnoreAsciiCase(name, "CurrentGame") != 0 && wiz8::file_status("Saves\\CurrentGame.SAV")) {
        if (!current_game.OpenRead("Saves\\CurrentGame.SAV")) return false;
        ReadSaveChunks(&current_game, &chunks);
        current_game.Close();
    }
    g_status.pending_move_location = cameraState(*GetWorld());
    for (region = 0; region != 4; ++region) {
        for (index = 0; index < g_status.text_box_lines_shown[region]; ++index) {
            g_message_storage[region][index].saved_remaining_ms =
                UINT32(TimeUntilDeadline(g_message_storage[region][index].clock));
        }
    }
    gXStatus.gameplay_timer->Restart();
    DestroyUngroupedMonsters();
    SaveMasterFunctions();
    g_status.buffers.save_version = 1.1f;
    g_status.difficulty = g_settings.difficulty;
    if (g_status.iron_man) {
        const auto creation_time = chunks.m_hFile->times().created;
        g_status.save_filetime_xor[0] = creation_time.low ^ g_save_filetime_xor_low;
        g_status.save_filetime_xor[1] = creation_time.high ^ g_save_filetime_xor_high;
    }
    cursor = g_status.text_line_cursor;
    if (cursor == 2) {
        g_status.text_line_cursor = 0;
    }
    try {
        SaveGlobalStatus(&chunks, &g_status);
    } catch (...) {
        g_status.text_line_cursor = cursor;
        throw;
    }
    g_status.text_line_cursor = cursor;
    chunks.OpenChunk(0x52455647, 0); /* GVER */
    version_major = 1;
    version_minor = 2;
    version_patch = 4;
    chunks.m_hFile->write(&version_major, 4);
    chunks.m_hFile->write(&version_minor, 4);
    chunks.m_hFile->write(&version_patch, 4);
    chunks.ReleaseCurrentChunk();
    if (!screenshot) {
        generated_screenshot = std::make_unique<W8SaveScreenshot>();
        screenshot = generated_screenshot.get();
        screenshot->version = 1.0f;
        bounds.left = 0;
        bounds.top = 0;
        bounds.right = 0x280;
        bounds.bottom = 0x1e0;
        auto surface = std::make_unique<srColorSurface>(
            srPixelConvert::SURFACE_ARGB1555, screenshot->pixels, 0x50, 0x3c, 0xa0);
        SetRendererAutoFlipEnabled(false);
        try {
            screenshot->capture_result = RenderWorldToSurface(surface.get(), &bounds, true);
            RenderFrame();
        } catch (...) {
            SetRendererAutoFlipEnabled(true);
            throw;
        }
        SetRendererAutoFlipEnabled(true);
    }
    chunks.OpenChunk(0x544f4853, 0); /* SHOT */
    chunks.m_hFile->write(screenshot, 0x2588);
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x54584554, 0); /* TEXT */
    if (!SaveMessageStorage(chunks.m_hFile.get())) return false;
    chunks.ReleaseCurrentChunk();
    if (!g_location_variable_values.empty()) {
        chunks.OpenChunk(0x52415654, 0); /* TVAR */
        SaveLocationVariables(chunks.m_hFile.get());
        chunks.ReleaseCurrentChunk();
    }
    chunks.OpenChunk(0x4943504e, 0); /* NPCI */
    if (SaveNpcDialogueTranscript(chunks.m_hFile.get()) == 0) {
        chunks.ReleaseCurrentChunk();
        return false;
    }
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x5443504e, 0); /* NPCT */
    if (!SaveNpcStates(&chunks)) return false;
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x4643504e, 0); /* NPCF */
    SaveFactState(chunks.m_hFile.get());
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x41544146, 0); /* FATA */
    SaveFactionState(chunks.m_hFile.get());
    chunks.ReleaseCurrentChunk();
    chunks.OpenChunk(0x4c4e524a, 0); /* JRNL */
    SaveFactJournal(chunks.m_hFile.get());
    chunks.ReleaseCurrentChunk();
    if (FindMonsterControlSpellEffect() != 0) {
        chunks.OpenChunk(0x4e505948, 0); /* HYPN */
        SaveMonsterControlSpellEffect(&chunks);
        chunks.ReleaseCurrentChunk();
    }
    if (!SaveStatusHeader(&chunks)) {
        return false;
    }
    return true;
}
catch (const std::exception&) { return false; }

bool SaveGame(std::string_view name, W8SaveScreenshot* screenshot)
try
{
    if (name.empty()) return false;
    const std::string path = fmt::format("Saves\\{}.{}", name, g_save_extension);
    SaveTransaction transaction(path);
    if (!WriteGame(name, screenshot, transaction.chunks)) return false;
    transaction.commit();
    return true;
}
catch (const std::exception&) { return false; }

/* Build the level-specific status path the save code falls back to when the
   current-game save has no matching level section. The regular levels use the
   database row's own folder and level names and level 56 is the shared default
   test level. The binary is explicit here: CMP ESI,0x39 branches
   at level < 57, CMP ESI,0x38 handles 56, and every other level indexes the
   table at 0x00604478 with stride 0x6B. That table has 47 entries, so levels
   47-55 read the adjacent rdata, even though LevelBuildInfoByID treats those
   ten slots as test levels. The recovered units disagree exactly as retail
   does; no non-OOB branch exists at the call site. */
// FUNCTION: WIZ8 0x00512e80
std::string BuildLevelStatusPath(unsigned int level)
{
    if (level == 56) return "Levels\\Test\\DefaultLevel.STS";
    if (level < 56)
        return fmt::format("Levels\\{}\\{}.STS", g_level_folders[level].folder_name,
                           g_level_folders[level].level_name);
    return fmt::format("Levels\\Test\\Level{:c}.STS", static_cast<char>(level - 56));
}

/* Reads and validates the header, then publishes the four counts and the block
   it carries. The version gate is an equality test against 2.0f held in .rdata,
   not a range, so a save written by any other version is refused outright.
   Each count is published first and only then corrected, rather than being
   tested before the store: the canonical writes all four globals, loads 1 once,
   and revisits each that turned out to be zero. */
// FUNCTION: WIZ8 0x00513090
bool LoadStatusHeader(W8Chunk* chunk)
try
{
    W8StatusHeader header;

    chunk->m_hFile->read_exact(&header, sizeof(header));
    if (header.version != 2.0f) {
        return false;
    }
    InitializeMonsterManagerState();
    InitializeItemManagerState();
    ResetNextTriggerId();
    g_status.next_group_id = header.next_group_id;
    g_status.next_monster_location_id = header.next_monster_location_id;
    g_status.next_world_item_id = header.next_world_item_id;
    g_status.next_trigger_id = header.next_trigger_id;
    if (header.next_group_id == 0) {
        g_status.next_group_id = 1;
    }
    if (header.next_monster_location_id == 0) {
        g_status.next_monster_location_id = 1;
    }
    if (header.next_world_item_id == 0) {
        g_status.next_world_item_id = 1;
    }
    if (header.next_trigger_id == 0) {
        g_status.next_trigger_id = 1;
    }
    // The STAT payload spans three fields in the retail record. Native
    // adjacency is not a serialization contract.
    std::memcpy(g_status.status_header_prefix, header.status_block, sizeof(g_status.status_header_prefix));
    std::memcpy(&g_status.uiTurnsElapsed, header.status_block + 0xd4, sizeof(g_status.uiTurnsElapsed));
    std::memcpy(g_status.status_header_suffix, header.status_block + 0xd8, sizeof(g_status.status_header_suffix));
    return true;
}
catch (const std::exception&) { return false; }

/* Stage a compacted status file, retaining active records for other levels.
   Finalize the RIFF before replacing the published save. */
// FUNCTION: WIZ8 0x00513160
bool SaveLevelStatus(const char* path)
try
{
    if (!path) return false;
    SaveTransaction transaction(path);
    W8Chunk source;
    if (wiz8::file_status(path)) {
        if (!source.OpenRead(path)) return false;
        for (int index = 0; index < source.ChunkCount(); ++index) {
            source.OpenChunk(0, 0);
            bool replace = source.CurrentChunkAtEnd();
            if (source.CurrentChunkId() == 0x534c564c) {
                source.OpenGroup();
                int level;
                source.m_hFile->read_exact(&level, sizeof(level));
                replace |= level == int(g_status.current_level);
                source.ReleaseGroup();
                source.RewindCurrentChunk();
            }
            if (!replace && !transaction.chunks.CopyCurrentChunkFrom(&source)) return false;
            source.SkipCurrentChunk(); source.ReleaseCurrentChunk();
        }
        source.Close();
    }
    if (!SaveStatusHeader(&transaction.chunks)) return false;
    transaction.commit();
    return true;
}
catch (const std::exception&) { return false; }

/* Serialize the complete per-level group. LVLS is a grouped chunk: its level
   id leads a sequence of ordinary child chunks. The restore path deliberately
   writes the four transient sections only; an ordinary save writes the live
   automation, trigger, prop, cube, generator, lock, ambient, particle and
   light sections. */
// FUNCTION: WIZ8 0x00513260
bool SaveStatusHeader(W8Chunk* chunks)
try
{
    W8StatusHeader header;
    unsigned int count;
    unsigned int index;

    DestroyUngroupedMonsters();
    chunks->OpenChunk(0x534c564c, 0); /* LVLS */
    chunks->OpenGroup();
    chunks->m_hFile->write(&g_status.current_level, sizeof(g_status.current_level));

    chunks->OpenChunk(0x54415453, 0); /* STAT */
    memset(&header, 0, sizeof(header));
    header.version = 2.0f;
    header.next_group_id = g_status.next_group_id;
    header.next_monster_location_id = g_status.next_monster_location_id;
    header.next_world_item_id = g_status.next_world_item_id;
    header.next_trigger_id = g_status.next_trigger_id;
    std::memcpy(header.status_block, g_status.status_header_prefix, sizeof(g_status.status_header_prefix));
    std::memcpy(header.status_block + 0xd4, &g_status.uiTurnsElapsed, sizeof(g_status.uiTurnsElapsed));
    std::memcpy(header.status_block + 0xd8, g_status.status_header_suffix, sizeof(g_status.status_header_suffix));
    chunks->m_hFile->write(&header, sizeof(header));
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x534e4f4d, 0); /* MONS */
    if (!SaveMonsterStatus(chunks)) {
        return false;
    }
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x4d455449, 0); /* ITEM */
    count = gXStatus.world_items.size();
    chunks->Write(&count, sizeof(count), 0);
    for (index = 0; index < count; ++index) {
        if (!SaveItemFile(chunks->m_hFile.get(), ItemInfo(index))) {
            return false;
        }
    }
    chunks->ReleaseCurrentChunk();

    if (g_level_status_loading) {
        chunks->OpenChunk(0x45425543, 0); /* CUBE */
        SaveWorldCursorNodes(chunks->m_hFile.get());
        SaveWorldCursorNodeStates(chunks->m_hFile.get());
        chunks->ReleaseCurrentChunk();

        chunks->OpenChunk(0x474e4f4d, 0); /* MONG */
        SaveEncounterState(chunks->m_hFile.get());
        chunks->ReleaseCurrentChunk();

        chunks->OpenChunk(0x4b434f4c, 0); /* LOCK */
        SaveTriggerRuntimeStates(g_world, chunks->m_hFile.get(), g_level_status_loading);
        chunks->ReleaseCurrentChunk();

        chunks->OpenChunk(0x53455254, 0); /* TRES */
        SaveTriggerActionData(g_world, chunks->m_hFile.get());
        chunks->ReleaseCurrentChunk();
        chunks->ReleaseGroup();
        chunks->ReleaseCurrentChunk();
        return true;
    }

    chunks->OpenChunk(0x4f545541, 0); /* AUTO */
    SaveAutomapNotes(chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    if (!g_world->triggers->empty()) {
        chunks->OpenChunk(0x47495254, 0); /* TRIG */
        SaveWorldTriggers(g_world, chunks->m_hFile.get());
        chunks->ReleaseCurrentChunk();
    }

    chunks->OpenChunk(0x54535041, 0); /* APST */
    SaveWorldProps(g_world, chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x53425543, 0); /* CUBS */
    SaveWorldCursorNodeStates(chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x534e474d, 0); /* MGNS */
    SaveMonsterGenerators(chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x534b434c, 0); /* LCKS */
    SaveTriggerRuntimeStates(g_world, chunks->m_hFile.get(), g_level_status_loading);
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x53424d41, 0); /* AMBS */
    SaveAmbientSoundList(chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x54524150, 0); /* PART */
    SaveParticleStates(chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    chunks->OpenChunk(0x5448474c, 0); /* LGHT */
    SaveLightStates(chunks->m_hFile.get());
    chunks->ReleaseCurrentChunk();

    chunks->ReleaseGroup();
    chunks->ReleaseCurrentChunk();
    return true;
}
catch (const std::exception&) { return false; }

/* MONS chunk: the group and monster totals, then every group's 0x12b-byte
   record restamped to save version 3 with an "encountered" byte, then every
   monster record. A missing group or a failed monster record releases the
   chunk and fails the section. */
// FUNCTION: WIZ8 0x005145a0
bool SaveMonsterStatus(W8Chunk* chunks)
try
{
    unsigned int group_count;
    unsigned int monster_count;
    unsigned int index;
    unsigned int record_size;
    W8MonsterGroup* group;

    group_count =
        gXStatus.monster_groups.size() +
        gXStatus.encounter_groups.size();
    monster_count = gXStatus.monsters.size() +
                    gXStatus.unborn_monsters.size();
    chunks->Write(&group_count, 4, 0);
    chunks->Write(&monster_count, 4, 0);
    for (index = 0; index < group_count; ++index) {
        if (index < gXStatus.monster_groups.size()) {
            unsigned char encountered;

            group = GetMonsterGroupByListIndex(index);
            if (group == 0) {
                goto fail;
            }
            group->version = 3;
            record_size = 0x12b;
            chunks->Write(&record_size, 4, 0);
            chunks->Write(group, record_size, 0);
            encountered = std::ranges::contains(gXStatus.encounter_groups, group);
            chunks->Write(&encountered, 1, 0);
        } else {
            unsigned char encountered;

            group = GetMonsterGroupByListIndex(
                index - gXStatus.monster_groups.size() +
                0x2710);
            if (group == 0) {
                goto fail;
            }
            group->version = 3;
            record_size = 0x12b;
            chunks->Write(&record_size, 4, 0);
            chunks->Write(group, record_size, 0);
            encountered = std::ranges::contains(gXStatus.encounter_groups, group);
            chunks->Write(&encountered, 1, 0);
        }
    }
    for (index = 0; index < monster_count; ++index) {
        if (index < gXStatus.monsters.size()) {
            if (!SaveMonsterRecord(chunks, index)) {
                goto fail;
            }
        } else {
            if (!SaveMonsterRecord(
                    chunks, index -
                                gXStatus.monsters.size() +
                                0x2710)) {
                goto fail;
            }
        }
    }
    return true;

fail:
    chunks->ReleaseCurrentChunk();
    return false;
}
catch (const std::exception&) { return false; }

/* One monster's save record: the version-7 tag, the 0x425-byte W8MonsterInfo
   with position/angle refreshed while the entry is live, then the optional
   script name and pending conditions, the unborn flag, the navigator movement
   state and the order/patrol fields the loader reads back in record-version
   order. */
// FUNCTION: WIZ8 0x005147a0
static bool SaveMonsterRecord(W8Chunk* chunks, unsigned int index)
try
{
    auto* info = MonsterGetScriptPartByLocationIndex(index);
    if (!info || !info->p3D) {
        return false;
    }
    W8Monster* monster = info->p3D;
    const unsigned int record_version = 7;
    const unsigned int record_size = sizeof(*info);
    if (info->fActive) {
        MonsterGetLocation(monster, &info->position);
        info->position.y = probeGround(info->position.glmValue()).position.y;
        info->derived = MonsterGetYaw(monster);
    }
    chunks->m_hFile->write(&record_version, sizeof(record_version));
    chunks->m_hFile->write(&record_size, sizeof(record_size));
    chunks->m_hFile->write(info, record_size);
    const unsigned char has_script = monster->script != nullptr;
    chunks->m_hFile->write(&has_script, sizeof(has_script));
    if (has_script) {
        char script_name[0x40]{};
        const auto& name = monster->script->getName();
        if (name.size() >= sizeof(script_name)) {
            return false;
        }
        memcpy(script_name, name.c_str(), name.size());
        chunks->m_hFile->write(script_name, sizeof(script_name));
        chunks->m_hFile->write(&monster->script_wait, sizeof(monster->script_wait));
        chunks->m_hFile->write(&monster->script_line, sizeof(monster->script_line));
        const int queue_count = static_cast<int>(monster->script_conditions.size());
        chunks->m_hFile->write(&queue_count, sizeof(queue_count));
        for (int i = 0; i < queue_count; ++i) {
            const bool script_flag = monster->script_conditions[i];
            chunks->m_hFile->write(&script_flag, sizeof(script_flag));
        }
    }
    const unsigned char unborn = std::ranges::contains(gXStatus.unborn_monsters, info);
    chunks->m_hFile->write(&unborn, sizeof(unborn));
    const unsigned char has_movement =
        !monster->linked_navigator && (monster->flags & 0x20000000) != 0;
    chunks->m_hFile->write(&has_movement, sizeof(has_movement));
    if (has_movement) {
        chunks->m_hFile->write(&monster->minimum_height, sizeof(monster->minimum_height));
        chunks->m_hFile->write(&monster->maximum_height, sizeof(monster->maximum_height));
        chunks->m_hFile->write(&monster->patrol_home, sizeof(monster->patrol_home));
        chunks->m_hFile->write(&monster->movement.attachment->path_destination,
                             sizeof(monster->movement.attachment->path_destination));
    }
    chunks->m_hFile->write(&monster->defining_orders, sizeof(monster->defining_orders));
    chunks->m_hFile->write(&monster->order_mode, sizeof(monster->order_mode));
    chunks->m_hFile->write(&monster->orders_finished, sizeof(monster->orders_finished));
    chunks->m_hFile->write(&monster->deaf, sizeof(monster->deaf));
    chunks->m_hFile->write(&monster->patrol_distance, sizeof(monster->patrol_distance));
    chunks->m_hFile->write(&monster->patrol_variation, sizeof(monster->patrol_variation));
    chunks->m_hFile->write(&monster->patrol_index, sizeof(monster->patrol_index));
    const int point_count = static_cast<int>(monster->vector.size());
    chunks->m_hFile->write(&point_count, sizeof(point_count));
    for (int i = 0; i < point_count; ++i) {
        chunks->m_hFile->write(&monster->vector[i], sizeof(srVector3T<float>));
    }
    srVector3T<float> point;
    point.Set(monster->direction_x, monster->direction_y, monster->direction_z);
    chunks->m_hFile->write(&point, sizeof(point));
    chunks->m_hFile->write(&monster->face_party, sizeof(monster->face_party));
    chunks->m_hFile->write(&monster->stay_home, sizeof(monster->stay_home));
    return true;
}
catch (const std::exception&) { return false; }

/* Open a per-level status file and hand it to the section reader. A file that
   cannot be opened reports failure without touching the live status. */
// FUNCTION: WIZ8 0x005135d0
bool LoadLevelStatus(const char* path, int level)
try
{
    W8Chunk chunk;
    bool result = false;

    if (chunk.OpenRead(const_cast<char*>(path))) {
        result = LoadItemStatus(&chunk, level);
        chunk.Close();
    }
    return result;
}
catch (const std::exception&) { return false; }

/* Walk one status file's top-level chunks and apply the saved section for the
   requested level. A section at the file's end carries no payload and is skipped; a non-matching
   section is released without walking its children. On a save load the level's
   shipped status is folded in first, so baseline state exists under the saved
   overrides. The chunk ids dispatch as a flat chain; LOCK and LCKS share the
   trigger-state loader. */
// FUNCTION: WIZ8 0x00513650
bool LoadItemStatus(W8Chunk* chunk, int level)
try
{
    unsigned int file_level;
    bool result = false;
    int outer_count = chunk->ChunkCount();
    unsigned int index;

    for (int outer = 0; outer < outer_count; ++outer) {
        if (result) {
            return result;
        }
        chunk->OpenChunk(0, 0);
        if (chunk->CurrentChunkId() == 0x534c564c) { /* LVLS */
            if (chunk->CurrentChunkAtEnd() != 0) {
                chunk->OpenGroup();
                chunk->m_hFile->read_exact(&file_level, 4);
                chunk->SkipCurrentChunk();
            } else {
                chunk->OpenGroup();
                chunk->m_hFile->read_exact(&file_level, 4);
                if (level == static_cast<int>(file_level)) {
                    if (!g_level_status_loading) {
                        LoadDefaultLevelStatus(level);
                    }
                    result = true;
                    for (int inner = chunk->ChunkCount(); inner > 0; --inner) {
                        chunk->OpenChunk(0, 0);
                        if (chunk->CurrentChunkAtEnd() == 0) {
                            w8_ulong chunk_id = chunk->CurrentChunkId();

                            if (chunk_id == 0x54415453) { /* STAT */
                                if (!LoadStatusHeader(chunk)) {
                                    return false;
                                }
                            } else if (chunk_id == 0x534e4f4d) { /* MONS */
                                unsigned int group_count;
                                unsigned int monster_count;

                                chunk->m_hFile->read_exact(&group_count, 4);
                                chunk->m_hFile->read_exact(&monster_count, 4);
                                const std::int64_t remaining =
                                    chunk->CurrentChunkEnd() - chunk->m_hFile->tell();
                                if (remaining < 0 ||
                                    std::uint64_t{group_count} * 4 + std::uint64_t{monster_count} * 9 >
                                        static_cast<std::uint64_t>(remaining)) {
                                    return false;
                                }
                                for (index = 0; index < group_count; ++index) {
                                    if (!LoadMonsterGroup(chunk)) {
                                        return false;
                                    }
                                }
                                for (index = 0; index < monster_count; ++index) {
                                    if (!LoadMonster(chunk)) {
                                        return false;
                                    }
                                }
                                ReapplyMonsterGroupFormations();
                                RepairMonsterGroupLeaderLinks();
                                ApplyDefaultMonsterGroupSounds();
                            } else if (chunk_id == 0x4d455449) { /* ITEM */
                                unsigned int item_count;

                                chunk->m_hFile->read_exact(&item_count, 4);
                                const std::int64_t remaining =
                                    chunk->CurrentChunkEnd() - chunk->m_hFile->tell();
                                if (remaining < 0 || item_count > static_cast<std::uint64_t>(remaining) /
                                                                     sizeof(W8WorldItem)) {
                                    return false;
                                }
                                for (index = 0; index < item_count; ++index) {
                                    if (LoadItem(chunk->m_hFile.get(), true) == 0) {
                                        return false;
                                    }
                                }
                            } else if (chunk_id == 0x45425543) { /* CUBE */
                                if (!g_level_status_loading) {
                                    ReleaseWorldCursorNodes();
                                }
                                LoadWorldCursorNodes(chunk->m_hFile.get());
                                if (g_level_status_loading) {
                                    LoadWorldCursorNodeStates(chunk->m_hFile.get());
                                }
                            } else if (chunk_id == 0x474e4f4d) { /* MONG */
                                if (!g_level_status_loading) {
                                    DestroyMonsterGenerators();
                                }
                                MonGen::LoadAll(chunk->m_hFile.get());
                            } else if (chunk_id == 0x4b434f4c || /* LOCK */
                                       chunk_id == 0x534b434c) { /* LCKS */
                                LoadTriggerRuntimeStates(chunk->m_hFile.get());
                            } else if (chunk_id == 0x53455254) { /* TRES */
                                LoadTriggerActionData(chunk->m_hFile.get());
                            } else if (chunk_id == 0x4f545541) { /* AUTO */
                                LoadAutomapNotes(chunk->m_hFile.get());
                            } else if (chunk_id == 0x47495254) { /* TRIG */
                                LoadWorldTriggers(g_world, chunk->m_hFile.get());
                            } else if (chunk_id == 0x54535041) { /* APST */
                                LoadWorldProps(g_world, chunk->m_hFile.get());
                            } else if (chunk_id == 0x53425543) { /* CUBS */
                                LoadWorldCursorNodeStates(chunk->m_hFile.get());
                            } else if (chunk_id == 0x534e474d) { /* MGNS */
                                LoadMonsterGenerators(chunk->m_hFile.get());
                            } else if (chunk_id == 0x53424d41) { /* AMBS */
                                LoadAmbientSoundList(chunk->m_hFile.get());
                            } else if (chunk_id == 0x54524150) { /* PART */
                                LoadParticleStates(chunk->m_hFile.get());
                            } else if (chunk_id == 0x5448474c) { /* LGHT */
                                LoadLightStates(chunk->m_hFile.get());
                            }
                        }
                        chunk->SkipCurrentChunk();
                        chunk->ReleaseCurrentChunk();
                    }
                }
            }
            chunk->ReleaseGroup();
        }
        chunk->SkipCurrentChunk();
        chunk->ReleaseCurrentChunk();
    }
    return result;
}
catch (const std::exception&) { return false; }

/* Fold the shipped per-level status file into the live state - the baseline a
   save's section is layered over. The path build is the same table walk
   BuildLevelStatusPath spells out, but the basename search starts four
   characters into the file name and the file itself is opened and walked here.
   Only the persistent-state chunks are taken: cursor nodes, generators and
   both trigger-state records. The level number the LVLS group carries is read
   and discarded; the file is already level-specific. */
// FUNCTION: WIZ8 0x005139c0
bool LoadDefaultLevelStatus(unsigned int level)
try
{
    W8Chunk chunk;
    W8LevelInfo info;
    std::string path;
    int file_level;
    int count;

    if (!LevelBuildInfoByID(level, &info)) {
        srAssertFail("LevelFilesExist(ulLevel, &LevelName)",
                     "C:\\Projects\\Wizardry 8\\Local Code\\LoadSaveGame.cpp", 0x366, 0);
    }
    *strchr(info.level_file_name + 4, '.') = '\0';
    if (level < 0x39) {
        if (level == 0x38) {
            path = fmt::format("{}\\Test\\DefaultLevel.{}", "Levels", "STS");
        } else {
            path = fmt::format("{}\\{}\\{}.{}", "Levels", g_level_folders[level].folder_name,
                               g_level_folders[level].level_name, "STS");
        }
    } else {
        path = fmt::format("{}\\Test\\Level{:c}.{}", "Levels", level - 0x38, "STS");
    }
    if (chunk.OpenRead(path)) {
        chunk.OpenChunk(0, 0);
        chunk.OpenGroup();
        chunk.m_hFile->read_exact(&file_level, 4);
        for (count = chunk.ChunkCount(); count > 0; --count) {
            chunk.OpenChunk(0, 0);
            if (chunk.CurrentChunkAtEnd() == 0) {
                w8_ulong chunk_id = chunk.CurrentChunkId();

                switch (chunk_id) {
                case 0x4b434f4c: /* LOCK */
                    LoadTriggerRuntimeStates(chunk.m_hFile.get());
                    break;
                case 0x45425543: /* CUBE */
                    LoadWorldCursorNodes(chunk.m_hFile.get());
                    break;
                case 0x474e4f4d: /* MONG */
                    MonGen::LoadAll(chunk.m_hFile.get());
                    break;
                case 0x53455254: /* TRES */
                    LoadTriggerActionData(chunk.m_hFile.get());
                    break;
                }
            }
            chunk.SkipCurrentChunk();
            chunk.ReleaseCurrentChunk();
        }
        chunk.ReleaseGroup();
        chunk.SkipCurrentChunk();
        chunk.ReleaseCurrentChunk();
        chunk.Close();
        return true;
    }
    return false;
}
catch (const std::exception&) { return false; }

/* Reads one saved monster group and files it under the species or the encounter
   list. The record's own size leads it, and the assertion that bounds it names
   the record: uiSize <= sizeof(*pMonsterGroup), at line 1517 of this unit.
   A record whose database entry is marked deleted is read and then dropped: it
   is neither listed nor given a monster list, and the function still reports
   success. */
// FUNCTION: WIZ8 0x00513c20
bool LoadMonsterGroup(W8Chunk* chunk)
try
{
    unsigned int record_size;
    unsigned char is_encounter = 0;
    std::unique_ptr<W8MonsterGroup, decltype(&free)> group(
        static_cast<W8MonsterGroup*>(calloc(1, sizeof(W8MonsterGroup))), &free);
    if (!group) {
        return false;
    }
    chunk->m_hFile->read_exact(&record_size, sizeof(record_size));
    if (record_size > sizeof(W8MonsterGroup)) {
        return false;
    }
    chunk->m_hFile->read_exact(group.get(), record_size);
    if (group->version >= 2) {
        chunk->m_hFile->read_exact(&is_encounter, sizeof(is_encounter));
    }
    if (group->version < 3) {
        group->forced_neutral = false;
    }
    const std::int64_t chunk_end =
        chunk->CurrentChunkEnd();
    if (chunk->m_hFile->tell() > chunk_end) {
        return false;
    }
    if (static_cast<unsigned int>(group->monster_id) >= std::size(gXStatus.monster_record_cache)) {
        return false;
    }
    const auto* record = MonsterDBFromSpecies(group->monster_id);
    if (!record) {
        return false;
    }
    if (record->deleted) {
        return true;
    }
    auto members = std::make_unique<std::vector<int>>();
    group->monsters = members.get();
    group->member_count = 0;
    group->active_member_count = 0;
    group->members_active = false;
    group->fInCombat = false;
    auto& list = is_encounter ? gXStatus.encounter_groups : gXStatus.monster_groups;
    list.push_back(group.get());
    (void)members.release();
    auto* loaded = group.release();
    ActivateGroupMembers(loaded, W8_MONSTER_LOAD_ALL_CYCLES);
    if (loaded->encounter_registered && loaded->leader_group_id == 0) {
        RegisterActiveEncounterGroup(loaded);
    }
    return true;
}
catch (const std::exception&) { return false; }

/* One saved monster entry: a version dword, the uiSize-prefixed
   W8MonsterInfo record, then the script block, the unborn-list flag, the
   navigator movement state and - for newer records - the order, patrol and
   facing fields. The monster is adopted into the live or unborn list,
   rejoined to its group, activated, given back its condition and effect
   visuals and its script, then dropped again if its database record was
   deleted and started dying when the record says it is dead. */
// FUNCTION: WIZ8 0x00513d80
bool LoadMonster(W8Chunk* chunk)
try
{
    std::unique_ptr<W8MonsterInfo, decltype(&free)> owner(
        static_cast<W8MonsterInfo*>(calloc(1, sizeof(W8MonsterInfo))), &free);
    if (!owner) {
        return false;
    }
    auto* monster_info = owner.get();
    const std::int64_t chunk_end =
        chunk->CurrentChunkEnd();
    unsigned int record_version;
    unsigned int record_size;
    unsigned char has_script;
    unsigned char unborn = 0;
    char script_name[0x40]{};
    W8MonsterScriptCommand script_wait{};
    int script_line = 0;
    std::vector<unsigned char> script_conditions;

    chunk->m_hFile->read_exact(&record_version, sizeof(record_version));
    chunk->m_hFile->read_exact(&record_size, sizeof(record_size));
    if (record_size > sizeof(W8MonsterInfo)) {
        return false;
    }
    chunk->m_hFile->read_exact(monster_info, record_size);
    chunk->m_hFile->read_exact(&has_script, sizeof(has_script));
    if (has_script) {
        chunk->m_hFile->read_exact(script_name, sizeof(script_name));
        if (!memchr(script_name, '\0', sizeof(script_name))) {
            return false;
        }
        chunk->m_hFile->read_exact(&script_wait, sizeof(script_wait));
        chunk->m_hFile->read_exact(&script_line, sizeof(script_line));
        int queue_count;
        chunk->m_hFile->read_exact(&queue_count, sizeof(queue_count));
        if (queue_count < 0 || queue_count > chunk_end - chunk->m_hFile->tell()) {
            return false;
        }
        script_conditions.resize(queue_count);
        chunk->m_hFile->read_exact(script_conditions.data(), script_conditions.size());
    }
    monster_info->fActive = false;
    monster_info->p3D = nullptr;
    monster_info->fInCombat = false;
    monster_info->pCombat = nullptr;
    if (record_version >= 5) {
        chunk->m_hFile->read_exact(&unborn, sizeof(unborn));
    }

    unsigned char has_movement = 0;
    float minimum_height = 0;
    float maximum_height = 0;
    srVector3T<float> patrol_home{};
    srVector3T<float> movement_target{};
    if (record_version >= 2) {
        chunk->m_hFile->read_exact(&has_movement, sizeof(has_movement));
        if (has_movement) {
            chunk->m_hFile->read_exact(&minimum_height, sizeof(minimum_height));
            chunk->m_hFile->read_exact(&maximum_height, sizeof(maximum_height));
            chunk->m_hFile->read_exact(&patrol_home, sizeof(patrol_home));
            chunk->m_hFile->read_exact(&movement_target, sizeof(movement_target));
        }
    }
    unsigned char defining_orders = 0;
    unsigned char order_mode = 0;
    unsigned char orders_finished = 0;
    unsigned char deaf = 0;
    float patrol_distance = 0;
    float patrol_variation = 0;
    unsigned char patrol_index = 0;
    std::vector<srVector3T<float>> points;
    srVector3T<float> direction{};
    unsigned char face_party = 0;
    unsigned char stay_home = 0;
    if (record_version >= 3) {
        chunk->m_hFile->read_exact(&defining_orders, sizeof(defining_orders));
        chunk->m_hFile->read_exact(&order_mode, sizeof(order_mode));
        chunk->m_hFile->read_exact(&orders_finished, sizeof(orders_finished));
        chunk->m_hFile->read_exact(&deaf, sizeof(deaf));
        chunk->m_hFile->read_exact(&patrol_distance, sizeof(patrol_distance));
        chunk->m_hFile->read_exact(&patrol_variation, sizeof(patrol_variation));
        chunk->m_hFile->read_exact(&patrol_index, sizeof(patrol_index));
        int point_count;
        chunk->m_hFile->read_exact(&point_count, sizeof(point_count));
        if (point_count < 0 ||
            point_count > (chunk_end - chunk->m_hFile->tell()) /
                              static_cast<std::int64_t>(sizeof(srVector3T<float>))) {
            return false;
        }
        points.resize(point_count);
        chunk->m_hFile->read_exact(points.data(), points.size() * sizeof(points[0]));
        if (record_version >= 4) {
            chunk->m_hFile->read_exact(&direction, sizeof(direction));
        }
        if (record_version >= 6) {
            chunk->m_hFile->read_exact(&face_party, sizeof(face_party));
        }
        if (record_version >= 7) {
            chunk->m_hFile->read_exact(&stay_home, sizeof(stay_home));
        }
    }
    if (chunk->m_hFile->tell() > chunk_end) {
        return false;
    }
    for (const auto& effect : monster_info->effect_slots) {
        if (effect.duration && static_cast<unsigned int>(effect.effect_id) >=
                std::size(g_effect_visual_table)) {
            return false;
        }
    }
    if (monster_info->monster_species >= std::size(gXStatus.monster_record_cache)) {
        return false;
    }
    auto* record = MonsterDBFromSpecies(monster_info->monster_species);
    if (!record) {
        return false;
    }
    if (record->deleted) {
        return true;
    }
    auto* monster_group = GetMonsterGroupByListIndex(GetMonsterGroupIndexByID(
        0x698, LOADSAVEGAME_CPP, monster_info->monster_group_id, true));
    if (!monster_group) {
        return false;
    }
    auto& monsters = unborn ? gXStatus.unborn_monsters : gXStatus.monsters;
    monsters.reserve(monsters.size() + 1);
    monster_group->monsters->push_back(monster_info->location_id);
    monsters.push_back(monster_info);
    (void)owner.release();
    if (static_cast<unsigned int>(monster_group->leader_location_id) ==
            WIZ8_DEBUG_UNINITIALIZED_HEAP_PATTERN ||
        static_cast<unsigned int>(monster_group->leader_location_id) <
            static_cast<unsigned int>(monster_info->location_id)) {
        monster_group->leader_location_id = monster_info->location_id;
    }
    ++monster_group->member_count;
    RequestRedrawParty();
    if (monster_info->highest_condition < W8_CONDITION_TURNCOAT) {
        ++monster_group->active_member_count;
    }
    W8Monster* monster;
    int index;
    ActivateMonster(monster_info, W8_MONSTER_LOAD_ALL_CYCLES);
    ActivateMonsterInWorld(monster_info);
    monster = monster_info->p3D;
    if (monster_info->highest_condition != W8_CONDITION_NONE) {
        for (index = 0; index < W8_CONDITION_COUNT; ++index) {
            if (monster_info->uiCondition[index] != 0) {
                SetMonsterSpellIcon(monster, static_cast<W8MonsterSpellIconId>(index - 1), true);
            }
        }
    }
    for (index = 0; index < 8; ++index) {
        if (monster_info->enchantments[index].turns != 0) {
            SetMonsterSpellIcon(monster, static_cast<W8MonsterSpellIconId>(index + 0x10), true);
        }
    }
    for (index = 0; index < 12; ++index) {
        if (monster_info->effect_slots[index].duration != 0) {
            SetMonsterSpellIcon(
                monster,
                g_effect_visual_table[monster_info->effect_slots[index].effect_id].monster_icon,
                true);
        }
    }
    if (monster_info->charm_strength > 0) {
        SetMonsterSpellIcon(monster, SPELL_ICON_CHARMED, true);
    }
    if (monster_info->summoned != W8_MONSTER_SUMMON_NONE) {
        SetMonsterSpellIcon(monster, SPELL_ICON_SUMMONED, true);
    }
    if (has_movement) {
        monster->minimum_height = minimum_height;
        monster->maximum_height = maximum_height;
        monster->patrol_home = patrol_home;
        monster->movement.attachment->InitializeSegment(&monster->movement.position, &movement_target);
        monster->movement_target = movement_target;
        monster->flags |= 0x20000000;
        monster->movement.attachment->flags |= W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
        monster->movement.target_position = patrol_home;
        if (monster->SetMovementTarget(&monster->movement_target, true)) {
            monster->flags |= 0x6;
            monster->ClearMovementStopped();
            monster->halted = false;
            monster->movement_target = monster->movement.attachment->path_destination;
        }
    }
    if (record_version >= 3) {
        monster->defining_orders = defining_orders != 0;
        monster->order_mode = order_mode;
        monster->orders_finished = orders_finished != 0;
        monster->deaf = deaf != 0;
        monster->patrol_distance = patrol_distance;
        monster->patrol_variation = patrol_variation;
        monster->patrol_index = patrol_index;
        monster->vector.reserve(points.size());
        for (const auto& point : points) {
            monster->vector.push_back(point);
        }
        if (record_version >= 4) {
            monster->direction_x = direction.x;
            monster->direction_y = direction.y;
            monster->direction_z = direction.z;
        }
        if (record_version >= 6) {
            monster->face_party = face_party != 0;
        }
        if (record_version >= 7) {
            monster->stay_home = stay_home != 0;
        }
        monster_info->ai_mode |= W8_MONSTER_AI_REAPPLY_MODE;
    }
    if (script_name[0] != '\0') {
        monster_info->ai_mode |= W8_MONSTER_AI_RESTORE_SCRIPT;
        monster->SetScript(script_name, false);
        monster->script_wait = script_wait;
        monster->script_line = script_line;
        monster->script_conditions.reserve(script_conditions.size());
        for (const auto condition : script_conditions) {
            monster->script_conditions.push_back(condition != 0);
        }
    }
    if (monster_info->hp_current == 0) {
        MonsterStartsDying(monster_info, true);
    }
    return true;
}
catch (const std::exception&) { return false; }

/* Write the MONS section: the group and monster counts, then every group
   record followed by the flag that says whether the group is also on the
   encounter list, then every monster record through SaveMonster. Live list
   indices come first; the encounter groups and the unborn monsters are the
   same records under indices biased by 10000. A null group or a failed
   monster write abandons the section by releasing the current chunk. */

/* Write one monster's record inside the open MONS section: the version and
   record-size words, the W8MonsterInfo image, then the script state, the
   unborn flag, the navigator movement state and the version-3 order/patrol
   fields, mirroring the layout LoadMonster reads back. The script name is
   seeded from the shared empty-name word before the copy the way the ambient
   and spell code seed theirs. */

/* Tear down the live session before a new-game or save load replaces it:
   unload the current level, empty queued character events and spell effects,
   and reset the main-game screen and gameplay status blocks. */
// FUNCTION: WIZ8 0x00512c40
void ResetLiveSessionForLoad(void)
{
    if (g_status.current_level != -1) {
        UnloadLevel("");
        SoundEmptyCache();
    }
    if (gXStatus.character_event_queue != 0) {
        gXStatus.character_event_queue->DestroyAllEvents();
    }
    ResetMainGameScreenState();
    ClearNpcMessageQueue();
    ResetMainScreenStateBlock();
    wiz8::remove_all_and_delete(g_spell_effects);
    ReleaseAllTriggers();
    ResetGameplayStatusBlock();
}

/* Materialize save directories in the mutable overlay, never chmod assets. */
// FUNCTION: WIZ8 0x00512d00
bool VerifyDataSubdirs(void)
{
    try {
        for (const char* directory : {"Saves", "Saves\\Characters", "Saves\\NPCs"}) {
            wiz8::create_directory(directory);
            const auto entry = wiz8::file_status(directory);
            if (!entry || entry->info.type != SDL_PATHTYPE_DIRECTORY || !entry->writable) {
                return false;
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

/* Write each sibling record with its own live position and representation flags. */
// FUNCTION: WIZ8 0x00514be0
bool SaveItemFile(wiz8::File* handle, W8WorldItem* item_info)
try
{
    for (auto* item = item_info; item; item = item->next) {
        item->saved_marker = 1;
        if (item->fActive) {
            item->p3D->m_pRep->GetLocation(&item->position);
            item->entity_flags = static_cast<W8ItemRep*>(item->p3D->m_pRep)->flags;
        }
        if (g_level_status_loading) {
            item->entity_flags &= ~W8_ITEM_ENTITY_RADAR_SEEN;
        }
        handle->write(item, sizeof(W8WorldItem));
    }
    return true;
}
catch (const std::exception&) { return false; }

/* Saved next pointers only mark the presence of another record. Stage the
   complete chain before publishing it to the item list or search registry. */
// FUNCTION: WIZ8 0x00514c80
W8WorldItem* LoadItem(wiz8::File* handle, bool add_to_list)
try
{
    std::vector<std::unique_ptr<W8WorldItem, decltype(&free)>> items;
    std::vector<std::unique_ptr<W8Searchable>> searchables;
    bool has_next;
    do {
        std::unique_ptr<W8WorldItem, decltype(&free)> item(
            static_cast<W8WorldItem*>(malloc(sizeof(W8WorldItem))), &free);
        if (!item) {
            return nullptr;
        }
        handle->read_exact(item.get(), sizeof(W8WorldItem));
        has_next = W8SerializedPointerPresent(item->next);
        item->next = nullptr;
        item->sector_id = -2;
        item->fActive = false;
        item->p3D = nullptr;
        if (g_level_status_loading) {
            item->entity_flags &= ~W8_ITEM_ENTITY_RADAR_SEEN;
        }
        if (ItemHasFlags(item.get(), W8_WORLD_ITEM_HIDDEN)) {
            auto searchable = std::make_unique<W8Searchable>();
            searchable->world_item = item.get();
            searchables.push_back(std::move(searchable));
        }
        if (!items.empty()) {
            items.back()->next = item.get();
        }
        items.push_back(std::move(item));
    } while (has_next);

    g_searchables.reserve(g_searchables.size() + searchables.size());
    auto* first = items.front().get();
    if (add_to_list) gXStatus.world_items.push_back(first);
    for (auto& searchable : searchables) {
        g_searchables.push_back(searchable.release());
    }
    for (auto& item : items) {
        (void)item.release();
    }
    return first;
}
catch (const std::exception&) { return nullptr; }

/* Reports whether any save exists other than the autosave. The main menu stores
   this and greys its second item out when it is clear, which is what makes the
   continue entry unavailable on a fresh install. */
// FUNCTION: WIZ8 0x00512fb0
bool SaveGameExists(void)
{
    for (const auto& entry : wiz8::list_directory("Saves", std::string("*.") + g_save_extension)) {
        if (SDL_strcasecmp(entry.c_str(), "CurrentGame.SAV") != 0) return true;
    }
    return false;
}

/* Writes one character record back to Saves\\Characters or Saves\\NPCs. The
   file name is the character's UTF-8 name with a CHR extension, and the
   record is written as a four-byte length followed by that many bytes, which is
   the pair LoadCharacter reads back.

   An existing read-only file has its attribute cleared first, and a failure to
   clear it is treated exactly like a failure to open.

   Failure reporting has two shapes. With report_failure set the caller gets the
   save-failed notice and the continuation is dropped; without it the
   continuation runs instead. Either way the answer is failure. */
// FUNCTION: WIZ8 0x00515090
bool SaveCharacter(W8Character* character, int slot, bool report_failure,
                   void (*continuation)(void))
try
{
    std::string path;
    bool saved = false;

    character->record_version = 1;
    path = BuildCharacterPath(character->name, slot);

    try {
        if (!g_status.game_started) {
            const auto status = wiz8::file_status(path);
            if (status && status->read_only) {
                wiz8::clear_read_only(path);
            }
            wiz8::StagedFile staged(path);
            auto handle = staged.file();
            const unsigned int size = W8_CHARACTER_SERIALIZED_SIZE;
            handle->write(&size, sizeof(size));
            wiz8::retail::write(*handle, *character);
            staged.commit();
            saved = true;
        } else {
            saved = SaveCharacterToCurrentGame(path, slot, character);
        }
    } catch (const std::exception&) {
        saved = false;
    }
    if (saved) {
        return true;
    }
    if (report_failure) {
        CreateMessageBox(
            fmt::sprintf(g_localized_strings[W8_NOTICE_CHARACTER_SAVE_FAILED], character->name),
            g_small_font, 1, true, false, continuation);
        return false;
    }
    if (continuation != 0) {
        continuation();
    }
    return false;
}
catch (const std::exception&) { return false; }

/* The two chunk tags the walk recognises, as the four-character codes the
   comparison spells them. */
enum { W8_SAVE_TAG_CHAR = 0x52414843, W8_SAVE_TAG_LVLS = 0x534c564c };

/* Replace all historical records with this name in one scan. Publication of
   the finalized RIFF is atomic; a failed replacement leaves the old save intact.
   Malformed CHAR payloads are skipped by loading and can be replaced normally. */
// FUNCTION: WIZ8 0x005155b0
bool SaveCharacterToCurrentGame(std::string_view path, int /*slot*/, W8Character* character)
try
{
    if (path.empty() || !character || path.size() >= 64) return false;
    W8Chunk source;
    if (!source.OpenRead("Saves\\CurrentGame.SAV")) return false;
    SaveTransaction transaction("Saves\\CurrentGame.SAV");
    for (int index = 0; index < source.ChunkCount(); ++index) {
        source.OpenChunk(0, 0);
        bool replace = false;
        if (source.CurrentChunkId() == W8_SAVE_TAG_CHAR && source.CurrentChunkExtent() >= 64) {
            char name[64];
            source.m_hFile->read_exact(name, sizeof(name));
            replace = memchr(name, '\0', sizeof(name)) && CompareTextIgnoreAsciiCase(name, path) == 0;
            source.RewindCurrentChunk();
        }
        if (!replace && !source.CurrentChunkAtEnd() && !transaction.chunks.CopyCurrentChunkFrom(&source))
            throw std::runtime_error("copy character save chunk");
        source.SkipCurrentChunk();
        source.ReleaseCurrentChunk();
    }
    source.Close();
    char name[64]{};
    std::copy(path.begin(), path.end(), name);
    const unsigned size = W8_CHARACTER_SERIALIZED_SIZE;
    transaction.chunks.OpenChunk(W8_SAVE_TAG_CHAR, 0);
    transaction.chunks.m_hFile->write(name, sizeof(name));
    transaction.chunks.m_hFile->write(&size, sizeof(size));
    wiz8::retail::write(*transaction.chunks.m_hFile, *character);
    transaction.chunks.ReleaseCurrentChunk();
    transaction.commit();
    return true;
}
catch (const std::exception&) { return false; }

/* Load one character record from a char chunk in Saves\\CurrentGame.SAV. */
// FUNCTION: WIZ8 0x005156c0
bool LoadCharacterFromCurrentGame(std::string_view path, W8Character* character)
try
{
    if (path.empty() || !character) return false;
    W8Chunk chunk;
    if (!chunk.OpenRead("Saves\\CurrentGame.SAV")) return false;
    W8Character saved{};
    bool found = false;
    for (int index = 0; index < chunk.ChunkCount(); ++index) {
        chunk.OpenChunk(0, 0);
        if (!chunk.CurrentChunkAtEnd() && chunk.CurrentChunkId() == W8_SAVE_TAG_CHAR &&
            chunk.CurrentChunkExtent() >= 68) {
            char name[64];
            unsigned size;
            chunk.m_hFile->read_exact(name, sizeof(name));
            chunk.m_hFile->read_exact(&size, sizeof(size));
            if (memchr(name, '\0', sizeof(name)) && CompareTextIgnoreAsciiCase(name, path) == 0 &&
                size <= W8_CHARACTER_SERIALIZED_SIZE && size <= unsigned(chunk.CurrentChunkExtent() - 68)) {
                saved = {};
                wiz8::retail::read_exact(*chunk.m_hFile, saved, size);
                found = true;
            }
        }
        chunk.SkipCurrentChunk();
        chunk.ReleaseCurrentChunk();
    }
    chunk.Close();
    if (found) *character = saved;
    return found;
}
catch (const std::exception&) { return false; }

/* Render the world into the slot's embedded 80x60 ARGB1555 pixel buffer.
   Option 4 is suppressed so the HUD does not bleed into the thumbnail, the
   full frame is re-rendered afterwards to restore the screen. */
// FUNCTION: WIZ8 0x00515840
void CaptureSaveScreenshot(W8SaveScreenshot* screenshot)
{
    W8ScreenRect rect;
    std::unique_ptr<srColorSurface> surface;

    screenshot->version = 1.0f;
    rect.top = 0;
    rect.left = 0;
    rect.right = 640;
    rect.bottom = 480;
    surface =
        std::make_unique<srColorSurface>(srPixelConvert::SURFACE_ARGB1555, screenshot->pixels, 0x50, 0x3c, 0xa0);
    SetRendererAutoFlipEnabled(false);
    screenshot->capture_result = RenderWorldToSurface(surface.get(), &rect, true);
    RenderFrame();
    SetRendererAutoFlipEnabled(true);
}

/* Save-slot bookkeeping from the same established
   Local Code\LoadSaveGame.cpp translation unit. */

// GLOBAL: WIZ8 0x00689f98
bool g_save_pending;

/* 0x0061A144, the save-file extension. It sits in writable .data with 16
   reference sites across 10 functions rather than in .rdata with the format
   literals, so it is a mutable character array rather than a string literal;
   this build initialises it to "SAV". */
/* 0x0061A134/0x0061A138: the mask pair SaveGame XORs the iron-man save file's
   creation file time with before storing it in the status block. */

// GLOBAL: WIZ8 0x0061A144
char g_save_extension[] = "SAV";

/* Delete the mutable slot and CurrentGame files; installed assets stay intact. */
// FUNCTION: WIZ8 0x00515920
void DeleteCurrentSaveFiles(void)
{
    std::string path;

    path = fmt::format("{}\\{}.{}", "Saves", GetLastSaveName(), g_save_extension);
    wiz8::remove_file(path);
    wiz8::remove_file("Saves\\CurrentGame.SAV");
}

/* Two gates with no established meaning beyond their position in the chain, so
   both keep positional names. Both are zero in the shipped image. */

/* gXStatus.fCombatMode and gXStatus.fCampMode reach this unit through
   xstatus.h. */
/* Byte-sized, not int: the refusal below returns through `mov al,1` and the
   save arm returns this result unchanged, so both share one byte register. */

/* Declining an autosave is reported as success. Iron Man saves overwrite the
   current slot rather than the fixed AutoSave slot. */
// FUNCTION: WIZ8 0x005159e0
bool AutoSaveIfAllowed(bool forced)
{

    gXStatus.save_notice_shown = false;
    if (g_status.world_cursor_gate == 0 && !AnyMonsterDying() &&
        ((g_settings.auto_save != 0 && !forced) || g_status.iron_man) && !gXStatus.fCombatMode &&
        !IsSightRangeOverridden() && CanInterruptLevelMovement() && !gXStatus.fNpcDialogueMode &&
        !gXStatus.fCampMode) {
        return SaveGame(g_status.iron_man ? GetLastSaveName() : "AutoSave", nullptr);
    }
    return true;
}

/* Take the pending-save flag and clear it in one go, so the caller that reads
   it is the only one that sees it. */
// FUNCTION: WIZ8 0x00515910
bool TakePendingSaveFlag(void)
{
    bool pending = g_save_pending;

    g_save_pending = false;
    return pending;
}

/* Whether one save slot's file is on disk. The path is built into a MAX_PATH
   buffer from the saves directory, the slot name and the extension. */
// FUNCTION: WIZ8 0x00512f70
unsigned char SaveSlotFileExists(std::string_view slot_name)
{
    std::string path;

    path = fmt::format("Saves\\{}.{}", slot_name, g_save_extension);
    const auto status = wiz8::file_status(path);
    return status && status->info.type == SDL_PATHTYPE_FILE;
}

/* Note that the save could not be written. The notice is only shown on the
   screen that owns saving, but the flag is raised either way. */
// FUNCTION: WIZ8 0x00515ac0
void ReportSaveFailed(bool quiet)
{
    if (!quiet || g_status.iron_man) {
        gXStatus.save_notice_shown = true;
        if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x783]);
        }
    }
}

/* Deferred main-game autosave. The first eligible frame after the gameplay
   timer elapses raises the notice and restarts the timer; the next eligible
   frame clears that flag and calls SaveGame. Iron Man overwrites the current
   slot name the same way AutoSaveIfAllowed does. Declining the second-pass
   gates is reported as success so the success notice still posts. */
// FUNCTION: WIZ8 0x00515b00
void ProcessMainGameAutoSave(void)
{
    std::string_view name;
    bool saved;

    if (g_status.world_cursor_gate != 0) {
        return;
    }
    if (AnyMonsterDying()) {
        return;
    }
    if (g_settings.auto_save == 0 && !g_status.iron_man) {
        return;
    }
    if (gXStatus.fCombatMode) {
        return;
    }
    if (IsSightRangeOverridden()) {
        return;
    }
    if (!CanInterruptLevelMovement()) {
        return;
    }
    if (gXStatus.fNpcDialogueMode) {
        return;
    }
    if (gXStatus.fCampMode) {
        return;
    }
    if (!gXStatus.save_notice_shown) {
        if (gXStatus.gameplay_timer->GetProgress() <= g_float_one) {
            return;
        }
        gXStatus.save_notice_shown = true;
        if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x783]);
        }
        gXStatus.gameplay_timer->Restart();
        return;
    }
    gXStatus.save_notice_shown = false;
    if (g_status.world_cursor_gate == 0 && !AnyMonsterDying() &&
        (g_settings.auto_save != 0 || g_status.iron_man) && !gXStatus.fCombatMode &&
        !IsSightRangeOverridden() && CanInterruptLevelMovement() && !gXStatus.fNpcDialogueMode &&
        !gXStatus.fCampMode) {
        if (g_status.iron_man) {
            name = GetLastSaveName();
        } else {
            name = "AutoSave";
        }
        saved = SaveGame(name, 0);
    } else {
        saved = true;
    }
    if (g_current_screen_state.id != W8_SCREEN_MAIN_GAME) {
        return;
    }
    if (!saved) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x785]);
        return;
    }
    ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x784]);
}

/* Serialize the live monster-control effect into the open HYPN chunk. The
   pointer-list vectors and the trailing result are deliberately skipped; the
   loader rebuilds them. The assertion names the local pLure and belongs to
   this unit at source line 3507. */
// FUNCTION: WIZ8 0x00516580
void SaveMonsterControlSpellEffect(W8Chunk* chunks)
{
    W8SpellEffectEntry* lure = FindMonsterControlSpellEffect();

    if (lure == 0) {
        srAssertFail("pLure", LOADSAVEGAME_CPP, 0xdb3, 0);
    }
    chunks->m_hFile->write(&lure->kind, 4);
    chunks->m_hFile->write(&lure->turns_remaining, 4);
    chunks->m_hFile->write(&lure->Source, sizeof(lure->Source));
    chunks->m_hFile->write(&lure->target, sizeof(lure->target));
    chunks->m_hFile->write(&lure->OrigSource, sizeof(lure->OrigSource));
    chunks->m_hFile->write(&lure->OrigTarget, sizeof(lure->OrigTarget));
    chunks->m_hFile->write(&lure->recast, 1);
    chunks->m_hFile->write(&lure->sustained, 1);
    chunks->m_hFile->write(&lure->missiles_pending, 1);
    chunks->m_hFile->write(&lure->targets_resolved, 1);
    chunks->m_hFile->write(&lure->definition, sizeof(lure->definition));
}

/* Choose the numbered quick-save slot that the next quick save should write.
   A missing slot wins immediately; when all three exist, replace the one with
   the oldest modification time. */
// FUNCTION: WIZ8 0x00516670
std::optional<std::string> SelectQuickSaveSlotForWrite()
try
{
    wiz8::DiskFileTime oldest_write_time{};
    int write_slot = 1;

    for (int slot = 1; slot <= 3; ++slot) {
        const std::string path = "Saves\\Quick " + std::to_string(slot) + "." + g_save_extension;
        if (!wiz8::file_status(path)) {
            write_slot = slot;
            break;
        }
        const auto handle = wiz8::open_file(path);
        const auto write_time = handle->times().modified;
        if (slot == 1 || write_time.ticks() < oldest_write_time.ticks()) {
            oldest_write_time = write_time;
            write_slot = slot;
        }
    }
    return fmt::format("Quick {}", write_slot);
}
catch (const std::exception&) { return {}; }

/* Select the newest numbered quick save for command-line startup. The three
   candidates are real save files named Quick 1 through Quick 3; the unnumbered
   Quick slot is accepted only when none of those files exists. */
// FUNCTION: WIZ8 0x00516740
std::optional<std::string> FindStartupQuickSave()
try
{
    std::string slot_name;
    int newest_slot = 0;
    wiz8::DiskFileTime newest_write_time{};

    for (int slot = 1; slot <= 3; ++slot) {
        const std::string path = "Saves\\Quick " + std::to_string(slot) + "." + g_save_extension;
        const auto status = wiz8::file_status(path);
        if (!status || status->info.type != SDL_PATHTYPE_FILE) {
            continue;
        }
        const auto handle = wiz8::open_file(path);
        const auto write_time = handle->times().modified;
        if (newest_slot == 0 || write_time.ticks() > newest_write_time.ticks()) {
            newest_write_time = write_time;
            newest_slot = slot;
        }
    }
    if (newest_slot > 0) {
        return fmt::format("Quick {}", newest_slot);
    }
    const auto status = wiz8::file_status(std::string("Saves\\Quick.") + g_save_extension);
    if (status && status->info.type == SDL_PATHTYPE_FILE) {
        return "Quick";
    }
    return {};
}
catch (const std::exception&) { return {}; }

/* Walk every chunk of a saved game. Character chunks are read straight in; a
   level chunk is read only for the level the party is actually on, and one for
   any other level is rewound and read as a character chunk instead. */
// FUNCTION: WIZ8 0x00514d50
void ReadSaveChunks(W8Chunk* source, W8Chunk* destination)
{
    int remaining = source->ChunkCount();

    if (remaining > 0) {
        int level;
        unsigned int tag;

        do {
            source->OpenChunk(0, 0);
            if (!source->CurrentChunkAtEnd()) {
                tag = source->CurrentChunkId();
                if (tag == W8_SAVE_TAG_CHAR) {
                    destination->CopyCurrentChunkFrom(source);
                } else if (tag == W8_SAVE_TAG_LVLS) {
                    source->OpenGroup();
                    source->m_hFile->read_exact(&level, 4);
                    if (level != g_status.current_level) {
                        source->RewindCurrentChunk();
                        destination->CopyCurrentChunkFrom(source);
                    }
                    source->ReleaseGroup();
                }
            }
            source->SkipCurrentChunk();
            source->ReleaseCurrentChunk();
            --remaining;
        } while (remaining != 0);
    }
}

/* Read the complete GSTA payload and its two eight-record collections. The
   pointers at the head of the fixed block are process ownership, so they are
   preserved across the serialized read. Old status blocks get the one retail
   compatibility migration retained by this build. Character records repair
   the pre-v2 profession field, while live global party rows rebuild or clear
   every transient pointer rather than trusting saved addresses. */
// FUNCTION: WIZ8 0x00515cf0
void LoadGameStatus(W8Chunk* chunks, W8GlobalStatus* status)
{
    W8Character* characters = status->buffers.Char;
    W8PartySlotRow* party_rows = status->buffers.XChar;
    if (!characters || !party_rows) {
        throw std::runtime_error("missing status buffers");
    }
    W8GlobalStatus saved{};
    W8Character saved_characters[8]{};
    W8PartySlotRow saved_rows[8]{};
    unsigned int size;

    chunks->m_hFile->read_exact(&size, sizeof(size));
    if (size > wiz8::retail::kGlobalStatusBytes) {
        throw std::runtime_error("oversized global status record");
    }
    wiz8::retail::read_exact(*chunks->m_hFile, saved, size);
    if (saved.buffers.save_version < 1.1f) {
        for (int box = 0; box < 3; ++box) {
            saved.text_box_lines_used[box] = saved.legacy_text_box_lines[0][box];
            saved.text_box_lines_shown[box] = saved.legacy_text_box_lines[1][box];
        }
        saved.text_box_lines_used[3] = 0;
        saved.text_box_lines_shown[3] = 0;
    }
    for (int box = 0; box < 4; ++box) {
        if (saved.text_box_lines_used[box] > std::size(g_message_storage[box]) ||
            saved.text_box_lines_shown[box] > std::size(g_message_storage[box])) {
            throw std::runtime_error("invalid message storage count");
        }
    }
    for (auto& character : saved_characters) {
        chunks->m_hFile->read_exact(&size, sizeof(size));
        if (size > wiz8::retail::kCharacterBytes) {
            throw std::runtime_error("oversized character status record");
        }
        wiz8::retail::read_exact(*chunks->m_hFile, character, size);
        if (character.record_version < 2 &&
            character.original_profession == W8_PROFESSION_FIGHTER &&
            character.profession_levels[W8_PROFESSION_FIGHTER] == 0) {
            character.original_profession = character.iProfession;
        }
    }
    for (auto& party_row : saved_rows) {
        chunks->m_hFile->read_exact(&size, sizeof(size));
        if (size > wiz8::retail::kPartySlotBytes) {
            throw std::runtime_error("oversized party status record");
        }
        wiz8::retail::read_exact(*chunks->m_hFile, party_row, size);
    }
    const std::int64_t chunk_end =
        chunks->CurrentChunkEnd();
    if (chunks->m_hFile->tell() > chunk_end) {
        throw std::runtime_error("status record exceeds chunk extent");
    }
    saved.buffers.Char = characters;
    saved.buffers.XChar = party_rows;
    std::move(std::begin(saved_characters), std::end(saved_characters), characters);
    std::copy(std::begin(saved_rows), std::end(saved_rows), party_rows);
    *status = std::move(saved);
    RebuildPartyStatus(&status->formation, party_rows);

    if (status == &g_status) {
        for (int slot = 0; slot < 8; ++slot) {
            auto& party_row = party_rows[slot];
            W8ItemInstance* item = nullptr;
            const auto origin = static_cast<signed char>(party_row.item_origin);
            const auto item_slot = static_cast<short>(party_row.item_slot);
            if (party_row.fOccupied && party_row.pending_action == W8_ACTION_USE_ITEM &&
                origin != -1 && item_slot != -1) {
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
}

/* Write the global status as one GSTA chunk. The two pointed-to collections
   follow the fixed status object in record-sized pieces so each record remains
   an independently sized save field. */
// FUNCTION: WIZ8 0x00515fa0
void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status)
{
    unsigned int size;
    unsigned int slot;

    chunks->OpenChunk(0x41545347, 0);
    size = wiz8::retail::kGlobalStatusBytes;
    chunks->m_hFile->write(&size, sizeof(size));
    wiz8::retail::write(*chunks->m_hFile, *status);
    for (slot = 0; slot != 8; ++slot) {
        size = wiz8::retail::kCharacterBytes;
        chunks->m_hFile->write(&size, sizeof(size));
        wiz8::retail::write(*chunks->m_hFile, status->buffers.Char[slot]);
    }
    for (slot = 0; slot != 8; ++slot) {
        size = wiz8::retail::kPartySlotBytes;
        chunks->m_hFile->write(&size, sizeof(size));
        wiz8::retail::write(*chunks->m_hFile, status->buffers.XChar[slot]);
    }
    chunks->ReleaseCurrentChunk();
}

/* Collect one level's saved world items out of the fixed current-save file.
   The LVLS groups are walked for a matching level number; every ITEM record
   inside is loaded and appended to the caller's vector. The scan stops once
   the level's group has been processed. */
// FUNCTION: WIZ8 0x00516070
bool LoadSavedLevelItems(int level, std::vector<W8WorldItem*>& items)
try
{
    W8Chunk chunk;
    unsigned int file_level;
    unsigned int item_count;
    unsigned int index;
    int inner;
    int outer_count;
    int outer;
    bool found = false;

    if (chunk.OpenRead(const_cast<char*>("Saves\\CurrentGame.SAV")) == 0) {
        return false;
    }
    outer_count = chunk.ChunkCount();
    for (outer = 0; outer < outer_count; ++outer) {
        if (found) {
            break;
        }
        chunk.OpenChunk(0, 0);
        if (chunk.CurrentChunkId() == 0x534c564c) { /* LVLS */
            if (chunk.CurrentChunkAtEnd() != 0) {
                chunk.OpenGroup();
                chunk.m_hFile->read_exact(&file_level, 4);
                chunk.SkipCurrentChunk();
            } else {
                chunk.OpenGroup();
                chunk.m_hFile->read_exact(&file_level, 4);
                if (level == static_cast<int>(file_level)) {
                    found = true;
                    for (inner = chunk.ChunkCount(); inner > 0; --inner) {
                        chunk.OpenChunk(0, 0);
                        if (chunk.CurrentChunkAtEnd() == 0 &&
                            chunk.CurrentChunkId() == 0x4d455449) { /* ITEM */
                            chunk.m_hFile->read_exact(&item_count, 4);
                            const std::int64_t remaining =
                                chunk.CurrentChunkEnd() - chunk.m_hFile->tell();
                            if (remaining < 0 || item_count > static_cast<std::uint64_t>(remaining) /
                                                                 sizeof(W8WorldItem) ||
                                item_count > static_cast<unsigned int>(
                                    std::numeric_limits<int>::max() - static_cast<int>(items.size()))) {
                                return false;
                            }
                            items.reserve(items.size() + item_count);
                            for (index = 0; index < item_count; ++index) {
                                W8WorldItem* item = LoadItem(chunk.m_hFile.get(), false);

                                if (!item) {
                                    return false;
                                }
                                items.push_back(item);
                            }
                        }
                        chunk.SkipCurrentChunk();
                        chunk.ReleaseCurrentChunk();
                    }
                }
            }
            chunk.ReleaseGroup();
        }
        chunk.SkipCurrentChunk();
        chunk.ReleaseCurrentChunk();
    }
    chunk.Close();
    return found;
}
catch (const std::exception&) { return false; }

/* Pick a free autosave slot for the ending sequence: "Ending", then
   "Ending1" through "Ending20" until Saves\<name>.<ext> does not exist.
   Writes the chosen bare name into `name`; returns 0 when all twenty-one
   slots are taken. */
// FUNCTION: WIZ8 0x00516890
std::optional<std::string> FindFreeEndingSaveName()
try
{
    for (int index = 0; index <= 20; ++index) {
        auto name = index == 0 ? std::string("Ending") : fmt::format("Ending{}", index);
        const auto status = wiz8::file_status(fmt::format("Saves\\{}.{}", name, g_save_extension));
        if (!status || status->info.type != SDL_PATHTYPE_FILE) return name;
    }
    return {};
}
catch (const std::exception&) { return {}; }

/* Write a full save slot: repair the target's read-only bit, fold the running
   CurrentGame sections forward unless this save is CurrentGame itself, refresh
   the camera anchor and per-box countdown snapshots, then emit the GVER
   version triple, the SHOT screenshot, TEXT message storage, the optional TVAR
   location variables, and the NPCI/NPCT/NPCF/FATA/JRNL chain before
   SaveStatusHeader appends the level sections. The transcript writer declining
   NPCI ends the chain early. */

/* Load a save slot: tear down the live session the way
   ResetLiveSessionForLoad does, copy the slot over Saves\CurrentGame.SAV, then
   dispatch each chunk to its section loader. Afterward the four
   message-storage countdown clocks are re-armed from the snapshots the save
   recorded, the gameplay timer restarts, the held-item cursor is restored,
   and the loaded items are normalized. */
// FUNCTION: WIZ8 0x00512920
bool LoadGame(std::string_view slot_name)
try
{
    W8Chunk chunks;
    std::string path;
    int count;
    int index;
    int box;

    ResetLiveSessionForLoad();
    path = fmt::format("Saves\\{}.{}", slot_name, g_save_extension);
    wiz8::copy_file(path, "Saves\\CurrentGame.SAV", wiz8::CopyMode::replace);
    if (chunks.OpenRead(const_cast<char*>("Saves\\CurrentGame.SAV")) == 0) {
        return false;
    }
    count = chunks.ChunkCount();
    for (index = 0; index < count; ++index) {
        chunks.OpenChunk(0, 0);
        if (!chunks.CurrentChunkAtEnd()) {
            switch (chunks.CurrentChunkId()) {
            case 0x41545347: /* GSTA */
                LoadGameStatus(&chunks, &g_status);
                break;
            case 0x54584554: /* TEXT */
                LoadMessageStorage(chunks.m_hFile.get());
                break;
            case 0x52415654: /* TVAR */
                LoadLocationVariables(chunks.m_hFile.get());
                break;
            case 0x4943504e: /* NPCI */
                LoadNpcDialogueTranscript(chunks.m_hFile.get());
                break;
            case 0x5443504e: /* NPCT */
                LoadNpcStates(&chunks);
                break;
            case 0x4643504e: /* NPCF */
                LoadFactState(chunks.m_hFile.get());
                break;
            case 0x41544146: /* FATA */
                LoadFactionState(chunks.m_hFile.get());
                break;
            case 0x4c4e524a: /* JRNL */
                LoadJournalEntries(chunks.m_hFile.get());
                break;
            case 0x4e505948: /* HYPN */
                LoadMonsterControlSpellEffect(&chunks);
                break;
            }
        }
        chunks.SkipCurrentChunk();
        chunks.ReleaseCurrentChunk();
    }
    chunks.Close();
    for (box = 0; box < 4; ++box) {
        /* 0x00512BA6 tests the member unsigned, so this loop has its own
           unsigned counter rather than the function's signed `index`. */
        for (unsigned int line = 0; line < g_status.text_box_lines_shown[box]; ++line) {
            g_message_storage[box][line].clock =
                w8_get_ticks() + (g_message_storage[box][line].saved_remaining_ms);
        }
    }
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

/* Render the world onto an 80x60 ARGB1555 surface backed by the record's
   pixel store. Renderer option 4 is suppressed while RenderWorldToSurface captures
   the frame, then RenderFrame repaints the real front buffer before the
   option is restored. */

/* Read the HYPN record: one live spell-effect entry for the monster-control
   effect, rebuilt through the entry constructor and pushed onto
   g_spell_effects. */
// FUNCTION: WIZ8 0x00516310
void LoadMonsterControlSpellEffect(W8Chunk* chunks)
{
    auto effect = std::make_unique<W8SpellEffectEntry>();

    chunks->m_hFile->read_exact(&effect->kind, 4);
    chunks->m_hFile->read_exact(&effect->turns_remaining, 4);
    chunks->m_hFile->read_exact(&effect->Source, 0x34);
    chunks->m_hFile->read_exact(&effect->target, 0x20);
    chunks->m_hFile->read_exact(&effect->OrigSource, 0x34);
    chunks->m_hFile->read_exact(&effect->OrigTarget, 0x20);
    chunks->m_hFile->read_exact(&effect->recast, 1);
    chunks->m_hFile->read_exact(&effect->sustained, 1);
    chunks->m_hFile->read_exact(&effect->missiles_pending, 1);
    chunks->m_hFile->read_exact(&effect->targets_resolved, 1);
    chunks->m_hFile->read_exact(&effect->definition, sizeof(effect->definition));
    AddSpellEffect(effect.get());
    (void)effect.release();
}

/* Write the monster-control spell effect as the HYPN record: the same fields
   the loader reads, none of the runtime vectors or result state. */
