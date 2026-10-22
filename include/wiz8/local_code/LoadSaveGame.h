#pragma once
class Rng;

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
struct World;
// Store the current level's group among the levels the game has left (Game::left_levels).
bool StoreLeftLevel(Game& game);
// Decoded level data. Its implementation stays with the existing retail codecs.
struct W8LoadedLevelStatus {
    struct Data;
    std::unique_ptr<Data> data;
    W8LoadedLevelStatus();
    ~W8LoadedLevelStatus();
};
std::unique_ptr<W8LoadedLevelStatus> ReadLevelStatus(World& world,
                                                     const std::filesystem::path& path, int level,
                                                     bool from_assets, Rng& rng);
// The level's state as the party left it, or null when it never left the level.
std::unique_ptr<W8LoadedLevelStatus> ReadLeftLevelStatus(World& world, const Game& game, int level,
                                                         Rng& rng);
void PrepareLevelPopulation(World& world, W8LoadedLevelStatus& status, Rng& rng);
void InstallLevelSessionState(World& world, W8LoadedLevelStatus* status, int next_item_id);
void BeginRestoredLevelActivity(World& world, W8LoadedLevelStatus* status);
std::string BuildLevelStatusPath(unsigned int level);
bool SaveStatusHeader(W8Chunk* chunks);

extern bool g_save_pending;

/* Deferred main-game autosave: notice first, then SaveGame on the next tick. */
void ProcessMainGameAutoSave(void);
bool SaveMonsterStatus(W8Chunk* chunks);

std::optional<std::string> SelectQuickSaveSlotForWrite();
std::optional<std::string> FindFreeEndingSaveName();

extern char g_save_extension[]; /* Initialized "SAV" */

void DeleteCurrentSaveFiles(void);

void ReportSaveFailed(bool quiet);
