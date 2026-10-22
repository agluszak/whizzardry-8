#pragma once

#include <filesystem>
#include <functional>
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
class Game;
// Decoded game sections, before the current session or its presentation is released.
struct PreparedGame {
    std::vector<std::move_only_function<void(Game&)>> sections;
    void install(GameSession& games) &&;
};
std::optional<PreparedGame> ReadSavedGame(std::string_view slot_name);
struct World;
// What entering a built level still installs (LevelState::build, InstallLevelSessionState).
struct W8LoadedLevelStatus {
    struct Data;
    std::unique_ptr<Data> data;
    W8LoadedLevelStatus();
    ~W8LoadedLevelStatus();
};
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
