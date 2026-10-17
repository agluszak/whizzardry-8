#include "wiz8/local_code/Traps.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "glm/ext/vector_float3.hpp"
#include "input.h"
#include "layouts/gameplay_databases.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "random.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/startup_world.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"

#define TRAPS_CPP "C:\\Projects\\Wizardry 8\\Local Code\\Traps.cpp"

// GLOBAL: WIZ8 0x0069ca68
static char g_record_mode_line[0x1000];
// GLOBAL: WIZ8 0x0069da6c
static bool g_record_mode_active;
// GLOBAL: WIZ8 0x0069da70
static int g_record_mode_length;

// GLOBAL: WIZ8 0x00650384
static char s_record_mode_prompt[] = "Type in your text, then ENTER or ESC.";
// GLOBAL: WIZ8 0x006503ac
static char s_record_mode_default_location[] = "tst";
// GLOBAL: WIZ8 0x006503d0
static char s_data_notes_txt[] = "logs\\notes.txt";
// GLOBAL: WIZ8 0x006503e0
static char s_exiting_record_mode[] = "Exiting record mode.";
// GLOBAL: WIZ8 0x006503f8
static char s_error_deleting_log_file[] = "Error deleting log file.";
// GLOBAL: WIZ8 0x00650414
static char s_log_file_deleted[] = "Log file deleted.";
// GLOBAL: WIZ8 0x00650428
static char s_delete_log[] = "DELETE LOG";
/* Per-device spell/notice table; TriggerTrapDevice reads
   the effect spell id at index device + 0xb. */
// GLOBAL: WIZ8 0x006504E8
int g_table2[] = {10,  25, 35, 40, 50, 60, 70, 80,  90,  100, 110, 121, 122,
                  123, 24, 47, 36, 37, 60, 70, 124, 125, 126, 86,  127, 91};
// GLOBAL: WIZ8 0x00650434
static unsigned char g_table1[15][8] = {
    {0, 1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 1, 0, 0},
    {0, 0, 0, 0, 1, 1, 0, 0}, {0, 0, 1, 1, 0, 0, 0, 1}, {1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 0, 1, 0, 1, 0}, {1, 0, 0, 1, 0, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 0, 0, 1, 0, 1}, {0, 1, 1, 0, 0, 0, 0, 1}, {0, 0, 1, 1, 0, 0, 1, 0},
    {0, 1, 0, 0, 1, 0, 1, 0}, {0, 1, 0, 0, 0, 1, 1, 0}, {1, 0, 0, 0, 0, 1, 1, 0},
};

/* Record mode appends the current camera position, orientation, level
   location code and the typed line to data\notes.txt. */
// FUNCTION: WIZ8 0x005E3280
void WriteRecordModeEntry(void)
{
    W8WorldCameraState state;
    char location_code[32];
    std::string line;
    std::unique_ptr<wiz8::File> file;
    unsigned int length;

    file = [&]() {
        try {
            return wiz8::open_file(wiz8::user_path(s_data_notes_txt), wiz8::OpenMode::replace);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (file != 0) {
        file->seek(-static_cast<std::int64_t>(0), wiz8::SeekOrigin::end);
        state = cameraState(*GetWorld());
        // GetCameraOrientation zeroes every angle-record slot after the first.
        line =
            fmt::format("{:f} {:f} {:f}\n", state.position.x, state.position.y, state.position.z);
        file->write(line.data(), line.size());
        line = fmt::format("{:f} {:f} {:f} {:f} {:f} {:d}\n", state.pitch[0], state.pitch[1],
                           state.pitch[2], state.pitch[3], state.pitch[4], 0);
        file->write(line.data(), line.size());
        line = fmt::format("{:f} {:f} {:f} {:f} {:f} {:d}\n", state.yaw[0], state.yaw[1],
                           state.yaw[2], state.yaw[3], state.yaw[4], 0);
        file->write(line.data(), line.size());
        if (GetLevelLocationCode(g_status.current_level, location_code) == 0) {
            strcpy(location_code, s_record_mode_default_location);
        }
        length = strlen(location_code);
        location_code[length] = '\n';
        file->write(location_code, length + 1);
        length = strlen(g_record_mode_line);
        g_record_mode_line[length] = '\n';
        file->write(g_record_mode_line, length + 1);
        if (file) file->close();
        file.reset();
    }
    ResetEditorStatusLine(-1);
}

/* The ENTER-key apply callback: an ordinary line is appended to the log, while
   "DELETE LOG" removes the file and leaves record mode. */
// FUNCTION: WIZ8 0x005E34B0
void ApplyRecordModeLine(void)
{
    char message[1024];

    if (SDL_strcasecmp(g_record_mode_line, s_delete_log) != 0) {
        WriteRecordModeEntry();
        return;
    }
    if (wiz8::remove_file(wiz8::user_path(s_data_notes_txt)) == 0) {
        ResetEditorStatusLine(-1);
        strcpy(message, s_error_deleting_log_file);
    } else {
        ResetEditorStatusLine(-1);
        strcpy(message, s_log_file_deleted);
    }
    ShowNotice(W8_FONT_PALETTE_PINK, message);
    g_record_mode_line[g_record_mode_length] = 0;
    g_record_mode_length = 0;
    g_record_mode_active = false;
    strcpy(message, s_exiting_record_mode);
    ShowNotice(W8_FONT_PALETTE_PINK, message);
}

/* The per-key prompt callback: clears the status line and shows the record
   mode prompt while each character is being composed. */
// FUNCTION: WIZ8 0x005E35A0
void PromptRecordModeEntry(void)
{
    char message[100];

    ResetEditorStatusLine(-1);
    strcpy(message, s_record_mode_prompt);
    ShowNotice(W8_FONT_PALETTE_PINK, message);
}

/* Local Code\Traps.cpp. The bodies at 0x5E3600-0x5E3730 sit in the
   attribution gap before the asserted Traps.cpp body at 0x5E3800 (line 148);
   their placement here is provisional, not proven ownership. */

// FUNCTION: WIZ8 0x005E3600
bool IsRecordModeActive(void)
{
    return g_record_mode_active;
}
/* Record-mode key handler: collects a printable line into
   g_record_mode_line. Returns 1 on ENTER (the caller then runs the
   apply callback), -1 on ESC, 0 otherwise. */
// FUNCTION: WIZ8 0x005E3610
char HandleRecordModeKey(const InputEvent* input, void (*prompt)(void))
{
    char character;
    std::string text;

    if (((GetInputState().modifiers & CTRL_DOWN) != 0)) {
        return 0;
    }
    if (input->key > 0x7f) return 0;
    character = static_cast<char>(toupper(static_cast<unsigned char>(input->key)));
    if (input->type != InputEventType::KeyUp) {
        return 0;
    }
    if (character == 8) {
        if (g_record_mode_length == 0) {
            return 0;
        }
        --g_record_mode_length;
        g_record_mode_line[g_record_mode_length] = 0;
    } else if (character == 0xd) {
        g_record_mode_line[g_record_mode_length] = 0;
        g_record_mode_length = 0;
        g_record_mode_active = false;
        return 1;
    } else if (character == 0x1b) {
        g_record_mode_line[g_record_mode_length] = 0;
        g_record_mode_length = 0;
        g_record_mode_active = false;
        ResetEditorStatusLine(-1);
        return -1;
    } else {
        text = &character;
        if (((GetInputState().modifiers & SHIFT_DOWN) == 0) && !IsKeyHeld(SDLK_CAPSLOCK) &&
            text[0] >= 'A' && text[0] <= 'Z') {
            text[0] += 0x20;
        }
        g_record_mode_line[g_record_mode_length] = text[0];
        g_record_mode_line[g_record_mode_length + 1] = 0;
        ++g_record_mode_length;
    }
    if (prompt != 0) {
        prompt();
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, g_record_mode_line);
    return 0;
}

// FUNCTION: WIZ8 0x005E3730
unsigned char GetTable650434Entry(int row, int column)
{
    return g_table1[row][column];
}

/* Per-type device floor for the sprung-trap discharge: the type's entry is
   subtracted from the trigger's device count before extra targets and power
   are rolled. */
// GLOBAL: WIZ8 0x006504AC
static int g_trap_difficulty[W8_TRAP_TYPE_COUNT] = {1, 1, 2, 2, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7};

/* Roll the trigger's trap type (device_id) on first interaction: rejection-
   sample the fifteen-row trap table until a type whose per-type difficulty
   lands within four of the trigger's grade (difficulty, floored at one). */
// FUNCTION: WIZ8 0x005E3740
void SelectTrapType(Trigger* trigger)
{
    W8LockState* lock_state;
    int budget;
    int type;

    lock_state = &trigger->lock_state;
    if (lock_state == 0) {
        return;
    }
    budget = lock_state->difficulty;
    if (budget < 1) {
        budget = 1;
    }
    do {
        type = Random(0xf);
        lock_state->device_id = type;
    } while (g_trap_difficulty[type] > budget || g_trap_difficulty[type] + 4 < budget);
}

// FUNCTION: WIZ8 0x005E3780
void CompleteTrapDisarm(Trigger* trigger)
{
    int type;
    std::string text;

    trigger->CompleteItemInteraction();
    type = trigger->lock_state.device_id;
    if (Random(100) < 40) {
        ApplyItemEffectToRandomCharacter(g_learn_sound, -1, 0, kCharacterEventNoFlags);
    }
    text = fmt::format("{} {}", g_localized_strings[g_trap_name_string_ids[type]],
                       g_localized_strings[0x7b2]);
    ShowString(text);
    trigger->Run(-1);
}

// FUNCTION: WIZ8 0x005E3800
static void DischargeTrapSpell(glm::vec3 point, int spell_id, unsigned int power_level,
                               int num_targets)
{
    int index;
    int eligible;
    std::vector<int> targets;
    W8CombatSlot target;
    W8TargetSource source;

    if (num_targets < 1) {
        srAssertFail("iNumTargets > 0", "C:\\Projects\\Wizardry 8\\Local Code\\Traps.cpp", 0x94, 0);
    }
    ResetTargetSource(&source);
    source.iType = W8_TARGET_SOURCE_INDIRECT;
    source.point = point;
    if (GetSpellTargetType(spell_id, false) == W8_TARGET_TYPE_POINT) {
        target.iType = W8_TARGET_KIND_PLACE;
        target.point = g_startup_world->GetPosition();
        CastSpellFromSource(spell_id, &source, &target, power_level, 0, 0, false, 0, 0, 0, 0);
    } else {
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_PARTY;
        target.point = g_startup_world->GetPosition();
        eligible = 0;
        for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (CanPartySlotParticipate(index)) {
                targets.push_back(index);
                ++eligible;
            }
        }
        if (num_targets >= eligible) {
            num_targets = eligible;
        }
        while (static_cast<int>(targets.size()) > num_targets) {
            index = Random(static_cast<int>(targets.size()));
            if (targets[index] != g_status.selected_character) {
                targets.erase(targets.begin() + index);
            }
        }
        CastSpellFromSource(spell_id, &source, &target, power_level, 0, 0, false, 0, 0, &targets,
                            0);
    }
}

// FUNCTION: WIZ8 0x005E3AB0
void ResolveSprungTrap(Trigger* trigger)
{
    int devices;
    int type;
    int count;
    int power;
    std::string result;
    std::string text;
    glm::vec3 point;
    glm::vec3 camera;

    devices = trigger->lock_state.difficulty;
    devices = std::clamp(devices, 1, 7);
    type = trigger->lock_state.device_id;
    if (Random(2) == 0) {
        trigger->CompleteItemInteraction();
        result = g_localized_strings[0x7b3];
    } else {
        result = g_localized_strings[0x7b4];
    }
    text = fmt::format("{} {}", g_localized_strings[g_trap_name_string_ids[type]], result);
    ShowString(text);
    count = devices - static_cast<int>(Random(devices / 2));
    power = 4;
    if (devices > g_trap_difficulty[type]) {
        devices -= g_trap_difficulty[type];
        power = devices + 4;
        if (power > 7) {
            count += static_cast<int>(Random(devices - 3));
            power = 7;
        }
    }
    W8Prop* prop = trigger->GetProp();
    if (prop == 0) {
        GetCameraForwardPoint(1000.0f, &point);
    } else {
        const auto center = prop->GetCenterPosition();
        if (!center) return;
        point = *center;
    }
    camera = cameraLocation();
    g_octree->TraceLineOfSight(&camera, &point, true, -3, -3, true, 0);
    DischargeTrapSpell(point, g_table2[type + 11], power, count);
}
