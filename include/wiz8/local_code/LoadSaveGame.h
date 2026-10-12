#pragma once

#include <cstdint>
#include <string_view>
#include <string>
#include <optional>

#include <memory>
#include <vector>

#include "wiz8/file_time.h"
#include "compat/compiler.h"

bool VerifyDataSubdirs(void);
std::optional<std::string> FindStartupQuickSave();
std::int32_t GetSaveGameLevel(std::string_view slot_name);

/* The SHOT record. SaveGame writes 0x2588 bytes; the save-screen producer
   copy-constructs one complete record, including its trailing padding.
   The 80 by 60 16-bit surface begins at offset 6. */
struct W8SaveScreenshot {
    float version;
    std::uint8_t capture_result;
    std::uint8_t padding_05;
    std::uint16_t pixels[60][80];
};

/* Save-list entries are allocated as 0x2640 bytes by both the enumerator and
   the Options controller. The SHOT chunk occupies the embedded screenshot. */
struct W8SaveSlot {
    std::string name;
    wiz8::DiskFileTime local_write_time;
    wiz8::CivilTime timestamp;
    std::int32_t game_time_days;
    std::uint32_t game_time_ms;
    std::int32_t level_id;
    bool iron_man;
    std::uint8_t padding_0a5[3];
    W8SaveScreenshot screenshot;
    std::int32_t version_major;
    std::uint32_t version_minor;
    std::uint32_t version_patch;
    bool dev_flagged; /* saved copy of status dev_flagged */
    std::uint8_t padding_263d[3];
};

void CaptureSaveScreenshot(W8SaveScreenshot& screenshot);
void FillCurrentSaveSlot(W8SaveSlot& slot);
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots);

bool SaveGame(std::string_view name, W8SaveScreenshot* screenshot);

struct W8Character;

bool SaveCharacter(W8Character* character, std::int32_t slot, bool report_failure,
                   void (*continuation)(void));

bool AutoSaveIfAllowed(bool forced);

bool TakePendingSaveFlag(void);

struct W8Chunk;
struct W8GlobalStatus;

std::uint8_t SaveSlotFileExists(std::string_view slot_name);
bool LoadCharacter(std::string_view name, W8Character* character, std::int32_t slot, bool report_failure);
std::string BuildCharacterFilePath(std::string_view filename, std::int32_t slot);
std::string BuildCharacterPath(std::string_view name, std::int32_t slot);
bool SaveGameExists(void);
void LoadGameStatus(W8Chunk* chunks, W8GlobalStatus* status);
void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status);
/* Load a save slot by name; the Please Wait screen drives it. */
bool LoadGame(std::string_view slot_name);

bool SaveLevelStatus(const char* path);
bool LoadLevelStatus(const char* path, std::int32_t level);
std::string BuildLevelStatusPath(std::uint32_t level);
bool LoadStatusHeader(W8Chunk* chunk);
bool SaveStatusHeader(W8Chunk* chunks);
/* The per-level item-section reader and the already-open level-status scan. */
bool LoadItemStatus(W8Chunk* chunk, std::int32_t level);

extern bool g_save_pending;

/* Replace one character record in Saves\\CurrentGame.SAV. `slot` is unused. */
bool SaveCharacterToCurrentGame(std::string_view path, std::int32_t slot, W8Character* character);
bool LoadCharacterFromCurrentGame(std::string_view path, W8Character* character);
/* Deferred main-game autosave: notice first, then SaveGame on the next tick. */
void ProcessMainGameAutoSave(void);
bool SaveMonsterStatus(W8Chunk* chunks);
void SaveMonsterControlSpellEffect(W8Chunk* chunks);
void LoadMonsterControlSpellEffect(W8Chunk* chunks);

bool LoadMonsterGroup(W8Chunk* chunk);
bool LoadMonster(W8Chunk* chunk);
/* Folds the shipped per-level status file in before a save's
   section is applied. */
bool LoadDefaultLevelStatus(std::uint32_t level);

void ResetLiveSessionForLoad(void);

std::optional<std::string> SelectQuickSaveSlotForWrite();
std::optional<std::string> FindFreeEndingSaveName();

extern char g_save_extension[]; /* Initialized "SAV" */

void DeleteCurrentSaveFiles(void);

void ReportSaveFailed(bool quiet);
