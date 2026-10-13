#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <wiz8/asset_paths.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "wiz8/filesystem.h"

namespace fs = std::filesystem;
using namespace wiz8;
struct TemporaryDirectory
{
    fs::path path;
    TemporaryDirectory()
    {
        std::random_device random;
        for (unsigned i = 0; i < 100; ++i)
        {
            path = fs::temp_directory_path() / ("wiz8-api-" + std::to_string(random()));
            if (fs::create_directory(path))
                return;
        }
        throw std::runtime_error("create test directory");
    }
    ~TemporaryDirectory() { fs::remove_all(path); }
    TemporaryDirectory(const TemporaryDirectory&) = default;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = default;
};
void fixture(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    REQUIRE(output.good());
}
std::string contents(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
std::string read_all(File& file)
{
    std::string result(std::size_t(file.size()) + 1, '\0');
    const auto read = file.read(result.data(), result.size());
    REQUIRE(read.eof);
    result.resize(read.bytes);
    return result;
}
void set_environment(const char* name, const std::string& value)
{
    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true));
}
static void exercise_filesystem(bool default_roots)
{
    static_assert(std::is_move_constructible_v<File> && !std::is_copy_constructible_v<File>);
    REQUIRE(SDL_Init(0));
    TemporaryDirectory temporary;
    const auto assets = temporary.path / "assets";
    const auto user = temporary.path / path_from_utf8("utilisateur-\xc3\xa9");
    const auto disc = temporary.path / "disc";
    const auto outside = temporary.path / "outside";
    fixture(assets / "Data" / "Mixed.BIN", "retail");
    fixture(assets / "Data" / "NoExtension", "plain");
    fixture(assets / "Data" / "ReadOnly.bin", "locked");
    fs::permissions(assets / "Data" / "ReadOnly.bin", fs::perms::owner_read |
                     fs::perms::group_read | fs::perms::others_read);
    fixture(assets / "Data" / path_from_utf8("caf\xc3\xa9.bin"), "unicode");
    fixture(disc / "Levels" / "LEVELS.SLF", "disc");
    fixture(outside / path_from_utf8("h\xc3\xb4te.bin"), "import");
    if (default_roots)
    {
        set_environment("WIZ8_ASSET_ROOT", path_to_utf8(assets));
        set_environment("WIZ8_USER_ROOT", "");
        set_environment("XDG_DATA_HOME", path_to_utf8(temporary.path / "preferences"));
        // SDL derives its pref root from process environment on Unix. Its
        // environment object does not mutate the process; unsafe is used ONLY
        // in this isolated test before any filesystem calls or other threads.
        REQUIRE(SDL_setenv_unsafe("XDG_DATA_HOME", path_to_utf8(temporary.path / "preferences").c_str(), 1) == 0);
        char* preferred = SDL_GetPrefPath("Whizzardry", "whizzardry8");
        REQUIRE(preferred);
        const auto pref = path_from_utf8(preferred).lexically_normal();
        SDL_free(preferred);
        auto app = pref.filename().empty() ? pref.parent_path() : pref;
        const auto legacy = app.parent_path().parent_path() / "whizzardry8";
        const auto relative = fs::relative(app, temporary.path);
        const bool isolated_pref = !relative.empty() && *relative.begin() != "..";
        if (isolated_pref)
        {
            REQUIRE(w8_native::existing_legacy_user_root(path_to_utf8(pref)).empty());
            REQUIRE(!fs::exists(legacy));
            fixture(legacy / "Saves" / "old.sav", "old-save");
        }
        const auto old = w8_native::existing_legacy_user_root(path_to_utf8(pref));
        const auto roots = w8_native::path_roots();
        REQUIRE(fs::equivalent(path_from_utf8(roots.user), old.empty() ? pref : path_from_utf8(old)));
        if (isolated_pref)
            REQUIRE(read_all(*open_file("C:/Saves/old.sav")) == "old-save");
        SDL_Quit();
        return;
    }
    fs::create_directories(user);
    w8_native::configure_paths({path_to_utf8(assets), path_to_utf8(user), {path_to_utf8(disc), "", ""}});
    auto file = open_file("c:/data/mixed.bin");
    auto independent = open_file("C:\\DATA\\MIXED.BIN");
    REQUIRE((file->size() == 6 && file->tell() == 0));
    REQUIRE(!file->opened_writer_create_time());
    char buffer[32]{};
    auto first = file->read(buffer, 2);
    REQUIRE((first.bytes == 2 && !first.eof && file->tell() == 2));
    REQUIRE(independent->tell() == 0);
    REQUIRE(file->seek(-2, SeekOrigin::end) == 4);
    auto tail = file->read(buffer, sizeof(buffer));
    REQUIRE((tail.bytes == 2 && tail.eof));
    REQUIRE(file->read(buffer, 1).eof);
    REQUIRE(file->seek(0, SeekOrigin::begin) == 0);
    REQUIRE(!file->read(nullptr, 0).eof);
    REQUIRE_THROWS_AS(([&] { file->write("x", 1); })(), std::exception);
    REQUIRE_THROWS_AS(([&] { file->seek(-1, SeekOrigin::begin); })(), std::exception);
    REQUIRE(file->tell() == 0);
    REQUIRE_THROWS_AS(([&] { file->seek(std::numeric_limits<std::int64_t>::min(), SeekOrigin::current); })(), std::exception);
    REQUIRE(file->tell() == 0);
    REQUIRE(file->seek(1, SeekOrigin::begin) == 1);
    REQUIRE_THROWS_AS(([&] { file->seek(std::numeric_limits<std::int64_t>::max(), SeekOrigin::current); })(), std::exception);
    REQUIRE_THROWS_AS(([&] { file->seek(std::numeric_limits<std::int64_t>::max(), SeekOrigin::end); })(), std::exception);
    REQUIRE(file->tell() == 1);
    REQUIRE(file->seek(0, SeekOrigin::begin) == 0);
    File moved(std::move(*file));
    REQUIRE((!file->is_open() && moved.is_open()));
    REQUIRE(read_all(moved) == "retail");
    moved.close();
    moved.close();
    REQUIRE_THROWS_AS(([&] { moved.read(buffer, 1); })(), std::exception);
    independent.reset();
    file.reset();

    auto writer = open_file("Data/mixed.bin", OpenMode::update);
    REQUIRE(writer->opened_writer_create_time());
    const auto snapshot = writer->opened_writer_create_time();
    writer->write("game", 4);
    writer->flush();
    REQUIRE(writer->opened_writer_create_time() == snapshot);
    REQUIRE(writer->status().info.type == SDL_PATHTYPE_FILE);
    const auto overlay = writer->physical_path();
    writer.reset();
    REQUIRE(contents(assets / "Data" / "Mixed.BIN") == "retail");
    REQUIRE(contents(overlay) == "gameil");
    REQUIRE(read_all(*open_file("Data/MIXED.bin")) == "gameil");
    REQUIRE(file_status("Data/mixed.bin")->writable);
    REQUIRE(!file_status("D:/Levels/LEVELS.SLF")->writable);
    REQUIRE(!file_status("Data/missing"));
    REQUIRE_THROWS_AS(([] { open_file("Data/missing", OpenMode::update); })(), std::exception);
    REQUIRE(!fs::exists(user / "Data" / "missing"));
    REQUIRE_THROWS_AS(([] { open_file("Data/missing-parent/new.bin", OpenMode::replace); })(), std::exception);
    REQUIRE(!fs::exists(user / "Data" / "missing-parent"));
    auto copied_read_only = open_file("Data/ReadOnly.bin", OpenMode::update);
    copied_read_only->write("game", 4);
    copied_read_only.reset();
    REQUIRE(contents(assets / "Data" / "ReadOnly.bin") == "locked");
    REQUIRE(read_all(*open_file("Data/ReadOnly.bin")) == "gameed");
    auto append = open_file("Data/mixed.bin", OpenMode::append);
    append->seek(0, SeekOrigin::begin);
    append->write("!", 1);
    append.reset();
    REQUIRE(read_all(*open_file("Data/MIXED.bin")) == "gameil!");
    auto replace = open_file("Data/mixed.bin", OpenMode::replace);
    replace->write("new", 3);
    replace.reset();
    REQUIRE(remove_file("Data/MIXED.bin"));
    REQUIRE(!remove_file("Data/MIXED.bin"));
    REQUIRE(read_all(*open_file("Data/MIXED.bin")) == "retail");
    create_directory("C:/Saves/Profiles");
    auto save = open_file("Saves/Profiles/game.sav", OpenMode::replace);
    save->write("save", 4);
    save.reset();
    REQUIRE(fs::exists(user / "Saves" / "Profiles" / "game.sav"));
    REQUIRE(read_all(*open_file("D:/levels/levels.slf")) == "disc");
    REQUIRE_THROWS_AS(([] { open_file("D:/Levels/LEVELS.SLF", OpenMode::replace); })(), std::exception);
    REQUIRE_THROWS_AS(([] { create_directory("E:/Saves"); })(), std::exception);
    REQUIRE_THROWS_AS(([] { open_file("C:/../../outside/pwn", OpenMode::replace); })(), std::exception);
    REQUIRE_THROWS_AS(([] { create_directory("C:/Saves/C:/Injected"); })(), std::exception);
    REQUIRE_THROWS_AS(([] { open_file("C:/Saves/game.sav:extra", OpenMode::replace); })(), std::exception);
    REQUIRE_THROWS_AS(([] { open_file(std::string("Data\0Mixed.BIN", 14)); })(), std::exception);

    const auto listing = list_directory("Data", "*.*");
    REQUIRE(std::find(listing.begin(), listing.end(), "NoExtension") != listing.end());
    REQUIRE(list_directory("Data", "m?xed.BIN").size() == 1);
    REQUIRE_THROWS_AS(([] { list_directory("not-a-directory"); })(), std::exception);
    REQUIRE(read_all(*open_file("Data/caf\xc3\xa9.bin")) == "unicode");
    auto host = open_host_file(outside / path_from_utf8("h\xc3\xb4te.bin"));
    REQUIRE(read_all(*host) == "import");
    host.reset();
    fixture(outside / "CaseSensitive.bin", "exact host");
    std::error_code case_error;
    const bool case_insensitive_host = fs::exists(outside / "casesensitive.BIN", case_error);
    if (case_insensitive_host)
        REQUIRE(read_all(*open_host_file(outside / "casesensitive.BIN")) == "exact host");
    else
        REQUIRE_THROWS_AS(([&] { open_host_file(outside / "casesensitive.BIN"); })(), std::exception);
    REQUIRE_THROWS_AS(([&] { open_file(path_to_utf8(outside / "new"), OpenMode::replace); })(), std::exception);
    REQUIRE_THROWS_AS(([&] { open_file(path_to_utf8(assets / "Data" / "Mixed.BIN")); })(), std::exception);
    REQUIRE_THROWS_AS(([&] { open_host_file(assets / "Data" / "Mixed.BIN", OpenMode::replace); })(), std::exception);
    REQUIRE_THROWS_AS(([&] { open_host_file(disc / "Levels" / "LEVELS.SLF", OpenMode::update); })(), std::exception);
    REQUIRE(!host_file_status(assets / "Data" / "Mixed.BIN")->writable);

    copy_file("Data/Mixed.BIN", "Data/Copy.bin");
    REQUIRE(read_all(*open_file("Data/copy.bin")) == "retail");
    REQUIRE_THROWS_AS(([] { copy_file("Data/Mixed.BIN", "Data/Copy.bin"); })(), std::exception);
    REQUIRE_THROWS_AS(([] { copy_file("Data/missing", "Data/Copy.bin", CopyMode::replace); })(), std::exception);
    REQUIRE(read_all(*open_file("Data/copy.bin")) == "retail");
    auto live_destination = open_file("Data/Copy.bin", OpenMode::update);
    replace_file(*live_destination, "Data/NoExtension");
    REQUIRE(!live_destination->is_open());
    REQUIRE(read_all(*open_file("Data/Copy.bin")) == "plain");
    copy_file("Data/Mixed.BIN", "Data/Copy.bin", CopyMode::replace);
    REQUIRE(read_all(*open_file("Data/Copy.bin")) == "retail");
    REQUIRE_THROWS_AS(([] { copy_file("Data/Copy.bin", "Data/Copy.bin", CopyMode::replace); })(), std::exception);
    for (const auto& entry : fs::directory_iterator(user / "Data"))
        REQUIRE(path_to_utf8(entry.path().filename()).rfind(".wiz8-copy-", 0) != 0);

    // Symlink creation may need host privileges (notably on Windows); no runtime
    // OS branch. If supported, both virtual and host write aliases are blocked.
    std::error_code error;
    fs::create_directory_symlink(assets / "Data", user / "alias", error);
    if (!error)
    {
        REQUIRE_THROWS_AS(([] { open_file("alias/Mixed.BIN", OpenMode::replace); })(), std::exception);
        REQUIRE_THROWS_AS(([] { create_directory("alias/new"); })(), std::exception);
        REQUIRE_THROWS_AS(([&] { open_host_file(user / "alias" / "Mixed.BIN", OpenMode::replace); })(), std::exception);
        REQUIRE_THROWS_AS(([] { remove_file("alias/Mixed.BIN"); })(), std::exception);
        REQUIRE(contents(assets / "Data" / "Mixed.BIN") == "retail");
    }
    else
        std::fprintf(stderr, "Symlink checks unavailable: %s\n", error.message().c_str());
    error.clear();
    fs::create_directory_symlink(outside, user / "escape", error);
    if (!error)
    {
        REQUIRE_THROWS_AS(([] { open_file("escape/new", OpenMode::replace); })(), std::exception);
        REQUIRE_THROWS_AS(([&] { open_host_file(user / "escape" / "new", OpenMode::replace); })(), std::exception);
    }
    error.clear();
    fs::create_directory_symlink(outside, assets / "escape", error);
    if (!error)
        REQUIRE_THROWS_AS(([&] { open_host_file(assets / "escape" / "new", OpenMode::replace); })(), std::exception);
    const auto synthetic_pref = temporary.path / "preferences" / "Whizzardry" / "whizzardry8";
    const auto synthetic_legacy = temporary.path / "preferences" / "whizzardry8";
    REQUIRE(w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref)).empty());
    REQUIRE((!fs::exists(synthetic_pref) && !fs::exists(synthetic_legacy)));
    fixture(synthetic_legacy / "Saves" / "old.sav", "old");
    REQUIRE(fs::equivalent(path_from_utf8(w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref))), synthetic_legacy));
    REQUIRE(!fs::exists(synthetic_pref));
    fs::create_directories(synthetic_pref);
    REQUIRE(!w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref)).empty());
    fixture(synthetic_pref / "Saves" / "new.sav", "new");
    REQUIRE(w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref)).empty());
    REQUIRE(contents(synthetic_legacy / "Saves" / "old.sav") == "old");
    REQUIRE(contents(synthetic_pref / "Saves" / "new.sav") == "new");
    fs::permissions(assets / "Data" / "ReadOnly.bin", fs::perms::owner_write, fs::perm_options::add);
    w8_native::configure_paths({path_to_utf8(assets), path_to_utf8(assets / "nested-user"), {}});
    REQUIRE(w8_native::path_roots().user.empty());
    REQUIRE_THROWS_AS(([] { open_file("Data/new", OpenMode::replace); })(), std::exception);
    SDL_Quit();

}

TEST_CASE("filesystem")
{
    exercise_filesystem(false);
}

TEST_CASE("default-roots")
{
    exercise_filesystem(true);
}
