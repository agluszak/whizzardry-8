#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "prepare.h"
#include "slf.h"
#include "temporary_directory.h"
#include "wiz8/checked_integer.h"
#include "wiz8/filesystem.h"

namespace fs = std::filesystem;
namespace {
void loose(const fs::path& path, std::string_view data)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), data.size());
    REQUIRE(file.good());
}
void archive(const fs::path& path, const char* prefix, const char* name, std::string_view data)
{
    wiz8::SlfHeader header{};
    std::strcpy(header.sLibName, "fixture.slf");
    std::strcpy(header.sPathToLibrary, prefix);
    header.iEntries = header.iUsed = 1;
    wiz8::SlfEntry entry{};
    std::strcpy(entry.sFileName, name);
    entry.uiOffset = sizeof(header);
    entry.uiLength = wiz8::checked_integer<std::uint32_t>(data.size());
    std::string bytes(reinterpret_cast<const char*>(&header), sizeof(header));
    bytes += data;
    bytes.append(reinterpret_cast<const char*>(&entry), sizeof(entry));
    loose(path, bytes);
}
struct Fixture {
    ScopedTemporaryDirectory temporary{"wiz8-prepare"};
    fs::path root = temporary.path;
    fs::path install = root / "install", cd = root / "cd", output = root / "prepared";
    Fixture()
    {
        fs::create_directories(install);
        fs::create_directory(cd);
    }
    void prepare()
    {
        const std::array sources{install, cd};
        wiz8::preparation::prepare_assets(sources, output);
    }
    std::string read(const char* name) { return wiz8::open_file(output / name)->read_text(); }
};
}
TEST_CASE_METHOD(Fixture, "Preparation applies archive, patch and loose precedence")
{
    archive(install / "Data/Data.slf", "Data\\", "Sound\\Voice.WAV", "base");
    archive(cd / "Data/Sound/Sound.slf", "Data\\Sound\\", "Voice.WAV", "sound");
    prepare();
    REQUIRE(read("data/sound/voice.wav") == "sound");
    fs::remove_all(output);
    archive(install / "Patches/Patch.000", "Data\\", "Sound\\Voice.WAV", "patch zero");
    archive(install / "Patches/Patch.049", "Data\\", "Sound\\Voice.WAV", "patch last");
    prepare();
    REQUIRE(read("data/sound/voice.wav") == "patch last");
    fs::remove_all(output);
    loose(cd / "DATA/Sound/Voice.wav", "disc loose");
    loose(install / "DATA/Sound/Voice.wav", "installed loose");
    // Merge the new loose file with the installation's existing Data directory.
    // Case-colliding directories are allowed when their normalized files differ.
    prepare();
    REQUIRE(read("data/sound/voice.wav") == "installed loose");
    REQUIRE(!fs::exists(output / "data/data.slf"));
    REQUIRE(!fs::exists(output / "patches"));
    REQUIRE_THROWS(prepare()); // Never overwrite an existing prepared tree.
}
TEST_CASE_METHOD(Fixture, "Preparation rejects unsafe and corrupt SLF records")
{
    const auto slf = install / "Data/Data.slf";
    for (const auto* name : {"../escape", "D:\\file", "/absolute"}) {
        archive(slf, "Data\\", name, "payload");
        REQUIRE_THROWS(prepare());
        REQUIRE(!fs::exists(output));
        REQUIRE(!fs::exists(root / "escape"));
    }
    archive(slf, "Data\\", "good.bin", "payload");
    auto file = wiz8::open_file(slf, wiz8::OpenMode::update);
    file->seek(-std::int64_t(sizeof(wiz8::SlfEntry)) + offsetof(wiz8::SlfEntry, uiLength),
               wiz8::SeekOrigin::end);
    const std::uint32_t excessive = 0xffffffff;
    file->write(&excessive, sizeof(excessive));
    file->close();
    REQUIRE_THROWS(prepare());
    REQUIRE(!fs::exists(output));
    fs::remove(slf);
    loose(install / "Data/Duplicate.bin", "first");
    loose(install / "Data/duplicate.bin", "second");
    if (fs::directory_iterator(install / "Data") != fs::directory_iterator{}) {
        // On insensitive hosts these are the same file; collisions cannot occur.
        std::size_t count = 0;
        for (const auto& entry : fs::directory_iterator(install / "Data")) {
            (void)entry;
            ++count;
        }
        if (count == 2) REQUIRE_THROWS(prepare());
    }
}
