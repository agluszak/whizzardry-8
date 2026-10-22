#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_time.h>

#include "wiz8/layouts/save_screenshot.h"

bool VerifyDataSubdirs(void);
std::optional<std::string> FindStartupQuickSave();
int GetSaveGameLevel(std::string_view slot_name);

/* Save-list entries are allocated as 0x2640 bytes by both the enumerator and
   the Options controller. The SHOT chunk occupies the embedded screenshot. */
struct W8SaveSlot {
    std::string name;
    SDL_Time write_time;
    SDL_DateTime timestamp;
    int game_time_days;
    unsigned int game_time_ms;
    int level_id;
    bool iron_man;
    W8SaveScreenshot screenshot;
    int version_major;
    unsigned int version_minor;
    unsigned int version_patch;
};

void CaptureSaveScreenshot(W8SaveScreenshot& screenshot);
void FillCurrentSaveSlot(W8SaveSlot& slot);
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots);

bool SaveGame(std::string_view name, const W8SaveScreenshot* screenshot);

class GameSession;
struct W8Character;

bool SaveCharacter(const W8Character* character, int slot);

bool AutoSaveIfAllowed(bool forced);

bool TakePendingSaveFlag(void);

struct W8Chunk;

bool SaveSlotFileExists(std::string_view slot_name);
bool LoadCharacter(std::string_view name, W8Character* character, int slot);
std::string BuildCharacterFilePath(std::string_view filename, int slot);
std::string BuildCharacterPath(std::string_view name, int slot);
bool SaveGameExists(void);
/* Load a save slot by name into a new game of `games`, replacing any in progress; the
   Please Wait screen drives it. */
bool LoadGame(GameSession& games, std::string_view slot_name);

class Game;
struct GameStatus;
struct World;
// Store the current level's group among the levels the game has left (Game::left_levels).
bool StoreLeftLevel(Game& game);
/* A level's saved state as its LVLS group holds it, decoded without the level's world,
   graphics or scripts (defined with the retail codecs). */
struct LevelState {
    struct Data;
    std::unique_ptr<Data> data;
    LevelState();
    LevelState(LevelState&&) noexcept;
    LevelState& operator=(LevelState&&) noexcept;
    ~LevelState();
};
/* Decode level `level`'s group from a status file, or nothing when the file is missing or
   has none. Throws on malformed input. */
std::optional<LevelState> DecodeLevelState(const std::filesystem::path& path, int level,
                                           bool from_assets = false);
// The level's state as the party left it, or nothing when it never left the level.
std::optional<LevelState> DecodeLeftLevelState(const Game& game, int level);
// The state a save keeps of level `level`, from its world and the game's status.
LevelState CaptureLevelState(World& world, int level, const GameStatus& status);
/* Write a state decoded from a save or captured from a world as the LVLS group it came
   from; the asset status's cursor nodes and encounters are not written. */
void EncodeLevelState(W8Chunk& chunks, const LevelState& state);
// What entering a built level still installs (InstallLevelSessionState).
struct W8LoadedLevelStatus {
    struct Data;
    std::unique_ptr<Data> data;
    W8LoadedLevelStatus();
    ~W8LoadedLevelStatus();
};
/* Build the unpublished `world`'s monsters, items and saved state from `state`, loading
   their representations and scripts. Throws when the state names what the game data lacks;
   the world is then discarded. */
std::unique_ptr<W8LoadedLevelStatus> BuildLevel(World& world, LevelState state);
void InstallLevelSessionState(World& world, W8LoadedLevelStatus* status);
void BeginRestoredLevelActivity(World& world, W8LoadedLevelStatus* status);
std::string BuildLevelStatusPath(unsigned int level);

extern bool g_save_pending;

/* Deferred main-game autosave: notice first, then SaveGame on the next tick. */
void ProcessMainGameAutoSave(void);

std::optional<std::string> SelectQuickSaveSlotForWrite();
std::optional<std::string> FindFreeEndingSaveName();

extern char g_save_extension[]; /* Initialized "SAV" */

void DeleteCurrentSaveFiles(void);

void ReportSaveFailed(bool quiet);
