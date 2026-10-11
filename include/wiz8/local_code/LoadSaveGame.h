#pragma once

#include <string_view>
#include <string>
#include <optional>

#include <memory>
#include <vector>

#include "wiz8/file_time.h"
#include "compat/compiler.h"

bool VerifyDataSubdirs(void);
std::optional<std::string> FindStartupQuickSave();
int GetSaveGameLevel(std::string_view slot_name);

/* The SHOT record. SaveGame writes 0x2588 bytes; the save-screen producer
   copy-constructs one complete record, including its trailing padding.
   The 80 by 60 16-bit surface begins at offset 6. */
struct W8SaveScreenshot {
    float version;
    unsigned char capture_result;
    unsigned char padding_05;
    unsigned short pixels[60][80];
};

/* Save-list entries are allocated as 0x2640 bytes by both the enumerator and
   the Options controller. The SHOT chunk occupies the embedded screenshot. */
struct W8SaveSlot {
    std::string name;
    wiz8::DiskFileTime local_write_time;
    wiz8::CivilTime timestamp;
    int game_time_days;
    unsigned int game_time_ms;
    int level_id;
    bool iron_man;
    unsigned char padding_0a5[3];
    W8SaveScreenshot screenshot;
    int version_major;
    unsigned int version_minor;
    unsigned int version_patch;
    bool dev_flagged; /* saved copy of status dev_flagged */
    unsigned char padding_263d[3];
};

void CaptureSaveScreenshot(W8SaveScreenshot& screenshot);
void FillCurrentSaveSlot(W8SaveSlot& slot);
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots);

bool SaveGame(std::string_view name, W8SaveScreenshot* screenshot);

struct W8Character;

bool SaveCharacter(W8Character* character, int slot, bool report_failure,
                   void (*continuation)(void));

bool AutoSaveIfAllowed(bool forced);

bool TakePendingSaveFlag(void);

struct W8Chunk;
struct W8GlobalStatus;

unsigned char SaveSlotFileExists(std::string_view slot_name);
bool LoadCharacter(std::string_view name, W8Character* character, int slot, bool report_failure);
std::string BuildCharacterFilePath(std::string_view filename, int slot);
std::string BuildCharacterPath(std::string_view name, int slot);
bool SaveGameExists(void);
void LoadGameStatus(W8Chunk* chunks, W8GlobalStatus* status);
void SaveGlobalStatus(W8Chunk* chunks, W8GlobalStatus* status);
/* Load a save slot by name; the Please Wait screen drives it. */
bool LoadGame(std::string_view slot_name);

bool SaveLevelStatus(const char* path);
bool LoadLevelStatus(const char* path, int level);
std::string BuildLevelStatusPath(unsigned int level);
bool LoadStatusHeader(W8Chunk* chunk);
bool SaveStatusHeader(W8Chunk* chunks);
/* The per-level item-section reader and the already-open level-status scan. */
bool LoadItemStatus(W8Chunk* chunk, int level);

extern bool g_save_pending;

/* Replace one character record in Saves\\CurrentGame.SAV. `slot` is unused. */
bool SaveCharacterToCurrentGame(std::string_view path, int slot, W8Character* character);
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
bool LoadDefaultLevelStatus(unsigned int level);

void ResetLiveSessionForLoad(void);

std::optional<std::string> SelectQuickSaveSlotForWrite();
std::optional<std::string> FindFreeEndingSaveName();

extern char g_save_extension[]; /* Initialized "SAV" */

void DeleteCurrentSaveFiles(void);

void ReportSaveFailed(bool quiet);
