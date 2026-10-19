#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_time.h>

#include "compat/compiler.h"
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
    bool dev_flagged; /* saved copy of status dev_flagged */
};

void CaptureSaveScreenshot(W8SaveScreenshot& screenshot);
void FillCurrentSaveSlot(W8SaveSlot& slot);
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots);

bool SaveGame(std::string_view name, const W8SaveScreenshot* screenshot);

struct W8Character;

bool SaveCharacter(const W8Character* character, int slot);

bool AutoSaveIfAllowed(bool forced);

bool TakePendingSaveFlag(void);

struct W8Chunk;
struct W8GlobalStatus;

unsigned char SaveSlotFileExists(std::string_view slot_name);
bool LoadCharacter(std::string_view name, W8Character* character, int slot);
std::string BuildCharacterFilePath(std::string_view filename, int slot);
std::string BuildCharacterPath(std::string_view name, int slot);
bool SaveGameExists(void);
void LoadGameStatus(W8Chunk* chunks, W8GlobalStatus* status);
void SaveGlobalStatus(W8Chunk* chunks, const W8GlobalStatus* status);
/* Load a save slot by name; the Please Wait screen drives it. */
bool LoadGame(std::string_view slot_name);

bool SaveLevelStatus(const char* path);
struct World;
// Decoded level data. Its implementation stays with the existing retail codecs.
struct W8LoadedLevelStatus {
    struct Data;
    std::unique_ptr<Data> data;
    W8LoadedLevelStatus();
    ~W8LoadedLevelStatus();
};
std::unique_ptr<W8LoadedLevelStatus> ReadLevelStatus(World& world,
                                                     const std::filesystem::path& path, int level,
                                                     bool from_assets = false);
void PrepareLevelPopulation(World& world, W8LoadedLevelStatus& status);
void InstallLevelSessionState(World& world, W8LoadedLevelStatus* status);
void BeginRestoredLevelActivity(World& world, W8LoadedLevelStatus* status);
std::string BuildLevelStatusPath(unsigned int level);
bool SaveStatusHeader(W8Chunk* chunks);

extern bool g_save_pending;

/* Replace one character record in Saves\\CurrentGame.SAV. `slot` is unused. */
bool SaveCharacterToCurrentGame(std::string_view path, int slot, const W8Character* character);
bool LoadCharacterFromCurrentGame(std::string_view path, W8Character* character);
/* Deferred main-game autosave: notice first, then SaveGame on the next tick. */
void ProcessMainGameAutoSave(void);
bool SaveMonsterStatus(W8Chunk* chunks);
void SaveMonsterControlSpellEffect(W8Chunk* chunks);
void LoadMonsterControlSpellEffect(W8Chunk* chunks);

void ResetLiveSessionForLoad(void);

std::optional<std::string> SelectQuickSaveSlotForWrite();
std::optional<std::string> FindFreeEndingSaveName();

extern char g_save_extension[]; /* Initialized "SAV" */

void DeleteCurrentSaveFiles(void);

void ReportSaveFailed(bool quiet);
