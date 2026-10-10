#include <wiz8/asset_paths.h>
#include "wiz8/application.h"
#include <cstdio>
#include <exception>
#include <vector>

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--print-user-root") {
            const auto root = w8_native::path_roots().user;
            if (root.empty())
                return 1;
            printf("%s\n", root.c_str());
            return 0;
        }
        const std::vector<std::string_view> arguments(argv + 1, argv + argc);
        wiz8::Application application(arguments);
        application.run();
        return 0;
    } catch (const std::exception& failure) {
        fprintf(stderr, "Wizardry native platform: %s\n", failure.what());
        return 1;
    }
}
