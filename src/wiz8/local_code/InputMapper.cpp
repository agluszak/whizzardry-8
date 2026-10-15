#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "input.h"
#include "mgs_commands.h"
#include "wiz8/filesystem.h"
#include "wiz8/key_bindings.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/sr_api.h"

/* Local Code\InputMapper.cpp. MGSKeyboard::LoadDefaults at 0x0055D800
   asserts this unit (line 478); it is the only body placed here. */

// FUNCTION: WIZ8 0x0055D800
unsigned char MGSKeyboard::LoadDefaults(const char* path)
{
    bool loaded_binding = false;
    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file((char*)path, wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (handle == 0) {
        srAssertFail("hFile", "C:\\Projects\\Wizardry 8\\Local Code\\InputMapper.cpp", 478,
                     fmt::format("Couldn't open keyboard init file {}", path));
    }

    std::string line;
    while (!(handle->tell() >= handle->size())) {
        auto line_result = ReadRetailTextLine(handle.get());
        if (!line_result) break;
        line = std::move(*line_result);
        if (line.empty() || line[0] == '*') {
            continue;
        }

        char* token = strtok(line.data(), " \t\r\n");
        if (token == 0 || strlen(token) == 0 || !isdigit(*token)) {
            continue;
        }
        W8MGSCommand command = static_cast<W8MGSCommand>(atoi(token));
        token = strtok(0, " \t\r\n");
        if (token == 0) {
            continue;
        }

        const SDL_Keycode key = KeyFromBindingName(token);
        if (key == SDLK_UNKNOWN) continue;

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

        auto binding = std::make_unique<MGSKeyBinding>();
        binding->key = key;
        binding->modifiers = modifiers;
        binding->active = 1;
        binding->command = command;

        int old_index = FindBinding(command);
        if (old_index != -1) {
            m_command_index.erase(command);
            m_bindings.erase(m_bindings.begin() + old_index);
        }
        auto* borrowed = binding.get();
        m_bindings.push_back(std::move(binding));
        m_command_index.emplace(command, borrowed);
        loaded_binding = true;
    }
    if (handle) handle->close();
    handle.reset();
    return loaded_binding;
}
