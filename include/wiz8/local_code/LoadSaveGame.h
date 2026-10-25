#pragma once
struct GameData;

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_time.h>

#include "wiz8/layouts/save_screenshot.h"
#include "wiz8/level_state.h"
#include "wiz8/save_codec.h"

class Game;

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

void CaptureSaveScreenshot(Game& game, W8SaveScreenshot& screenshot);
void FillCurrentSaveSlot(W8SaveSlot& slot);
bool EnumerateSaveSlots(std::vector<std::unique_ptr<W8SaveSlot>>& slots);

bool SaveGame(std::string_view name, const W8SaveScreenshot* screenshot);

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
/* A save's game sections, decoded and checked. A repeated section keeps its last record
   (a character, the last under its name) or, for location variables and monster control,
   accumulates; a missing one leaves the new game's default. */
struct SavedGame {
    wiz8::save::GameState state;
    std::vector<wiz8::save::StoredCharacter> characters;
    // Each level's group as bytes, which a save copies and entering the level decodes.
    LeftLevels levels;
    std::optional<wiz8::save::MessageBoxes> messages;
    std::vector<W8LocationVariable> location_variables;
    std::optional<wiz8::save::DialogueTranscript> transcript;
    std::optional<std::vector<std::unique_ptr<W8NpcState>>> npcs;
    std::optional<wiz8::save::Facts> facts;
    std::optional<wiz8::save::Factions> factions;
    std::optional<std::vector<W8JournalEntry>> journal;
    std::vector<std::unique_ptr<W8SpellEffectEntry>> spell_effects;
    // Fill `game`, in the order a save writes its sections. The session owns replacing
    // any game in progress; installing a value cannot begin or end one.
    void install(Game& game) &&;
};
// Decode a save slot, or nothing (logged) when it is missing or malformed.
std::optional<SavedGame> ReadSavedGame(const GameData& data, std::string_view slot_name);
struct World;
// What entering a built level still installs (LevelState::build, InstallLevelSessionState).
struct W8LoadedLevelStatus {
    struct Data;
    std::unique_ptr<Data> data;
    W8LoadedLevelStatus();
    ~W8LoadedLevelStatus();
};
void InstallLevelSessionState(Game& game, World& world, W8LoadedLevelStatus* status);
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
