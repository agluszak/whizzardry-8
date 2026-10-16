#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "input.h"
#include "mgs_commands.h"
#include "surrender/srDebug.h"
#include "wiz8/filesystem.h"
#include "wiz8/key_bindings.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"

/* Local Code\InputMapper.cpp. MGSKeyboard::LoadDefaults at 0x0055D800
   asserts this unit (line 478); it is the only body placed here. */

// FUNCTION: WIZ8 0x0055D800
unsigned char MGSKeyboard::LoadDefaults(const char* path)
{
    bool loaded_binding = false;
    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file(wiz8::asset_path((char*)path), wiz8::OpenMode::read);
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

        SetBinding({key, modifiers, 1, command});
        loaded_binding = true;
    }
    return loaded_binding;
}
