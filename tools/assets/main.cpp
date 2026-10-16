#include <cstdio>
#include <exception>
#include <filesystem>
#include <vector>

#include "prepare.h"
#include "wiz8/filesystem.h"

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "Usage: wiz8_prepare_assets OUTPUT INSTALL [CD_DIRECTORY ...]\n");
        return 1;
    }
    try {
        std::vector<std::filesystem::path> sources;
        for (int i = 2; i < argc; ++i) sources.push_back(wiz8::path_from_utf8(argv[i]));
        wiz8::preparation::prepare_assets(sources, wiz8::path_from_utf8(argv[1]));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Asset preparation failed: %s\n", error.what());
        return 1;
    }
}
