#include "wiz8/local_screens/MGSKeyboard.h"
#include "input.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/sr_api.h"
#include "wiz8/utility.h"
#include "wiz8/virtual_file.h"

#include "wiz8/filesystem.h"
#include "input.h"


/* Local Code\InputMapper.cpp. MGSKeyboard::LoadDefaults at 0x0055D800
   asserts this unit (line 478); it is the only body placed here. */

#pragma pack(push, 1)
struct MGSKeyName {
    const char* name;
    unsigned short key;
};

// GLOBAL: WIZ8 0x00647770
static MGSKeyName g_mgs_key_names[] = {
    {"F1", 112},        {"F2", 113},        {"F3", 114},        {"F4", 115},
    {"F5", 116},        {"F6", 117},        {"F7", 118},        {"F8", 119},
    {"F9", 120},        {"F10", 121},       {"F11", 122},       {"F12", 123},
    {"ESC", 27},        {"TAB", 9},         {"PAUSE", 19},      {"NUM_LOCK", 144},
    {"BACKSPACE", 8},   {"INSERT", 45},     {"DEL", 46},        {"KEY_END", 35},
    {"PGDN", 34},       {"PGUP", 33},       {"HOME", 36},       {"ENTER", 13},
    {"SPACE", 32},      {"DNARROW", 40},    {"LEFTARROW", 37},  {"RIGHTARROW", 39},
    {"UPARROW", 38},    {"NUM_0", 96},      {"NUM_1", 97},      {"NUM_2", 98},
    {"NUM_3", 99},      {"NUM_4", 100},     {"NUM_5", 101},     {"NUM_6", 102},
    {"NUM_7", 103},     {"NUM_8", 104},     {"NUM_9", 105},     {"NUM_TIMES", 106},
    {"NUM_PLUS", 107},  {"NUM_ENTER", 108}, {"NUM_MINUS", 109}, {"NUM_PERIOD", 110},
    {"NUM_SLASH", 111}, {"SCRL_LOCK", 145}, {"/", 191},         {"-", 189},
    {".", 190}};
#pragma pack(pop)

// FUNCTION: WIZ8 0x0055D800
unsigned char MGSKeyboard::LoadDefaults(const char* path)
{
    bool loaded_binding = false;
    std::unique_ptr<wiz8::File> handle = [&]() { try { return wiz8::open_file((char*)path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (handle == 0) {
        srAssertFail("hFile", "C:\\Projects\\Wizardry 8\\Local Code\\InputMapper.cpp", 478,
                     FormatString("Couldn't open keyboard init file %s", path));
    }

    char line[3 * (200) + 1];
    unsigned char more;
    while (!(handle->tell() >= handle->size())) {
        if (!ReadRetailTextLine(handle.get(), line, 200, &more) || line[0] == '*') {
            continue;
        }

        char* token = strtok(line, " \t\r\n");
        if (token == 0 || strlen(token) == 0 || !isdigit(*token)) {
            continue;
        }
        W8MGSCommand command = static_cast<W8MGSCommand>(atoi(token));
        token = strtok(0, " \t\r\n");
        if (token == 0) {
            continue;
        }

        unsigned short key = *token;
        int index;
        for (index = 0;
             index < static_cast<int>(sizeof(g_mgs_key_names) / sizeof(g_mgs_key_names[0]));
             ++index) {
            if (strcmp(token, g_mgs_key_names[index].name) == 0) {
                key = g_mgs_key_names[index].key;
                break;
            }
        }
        if (index == static_cast<int>(sizeof(g_mgs_key_names) / sizeof(g_mgs_key_names[0]))) {
            if (strlen(token) != 1 || (key = TranslateCharacterToKey(key)) == 0) {
                continue;
            }
        }

        unsigned short modifiers = 0;
        while ((token = strtok(0, " \t\r\n")) != 0) {
            if (strcmp(token, "SHIFT_DOWN") == 0) {
                modifiers |= SHIFT_DOWN;
            } else if (strcmp(token, "CTRL_DOWN") == 0) {
                modifiers |= CTRL_DOWN;
            } else if (strcmp(token, "ALT_DOWN") == 0) {
                modifiers |= ALT_DOWN;
            }
        }

        MGSKeyBinding* binding = new MGSKeyBinding;
        binding->key = key;
        binding->modifiers = modifiers;
        binding->active = 1;
        binding->command = command;

        int old_index = FindBinding(command);
        unsigned int command_key = command;
        if (old_index != -1) {
            m_bindings.RemoveAtAndDelete(old_index);
            m_command_index.Remove(&command_key);
        }
        if (m_bindings.Add(binding) != -1) {
            m_command_index.Insert(&command_key, &binding);
        }
        loaded_binding = true;
    }
    if (handle) handle->close();
    handle.reset();
    return loaded_binding;
}
