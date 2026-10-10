#include "wiz8/filesystem.h"
#include <wiz8/asset_paths.h>

#include <SDL3/SDL_init.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace fs = std::filesystem;
using namespace wiz8;
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #test); std::exit(1); } } while (0)
template <typename Function> void check_rejection(Function function, int line)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    if (!rejected)
    {
        std::fprintf(stderr, "line %d: expected exception\n", line);
        std::exit(1);
    }
}
#define rejects(...) check_rejection((__VA_ARGS__), __LINE__)
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
};
void fixture(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    CHECK(output.good());
}
std::string contents(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
std::string read_all(File& file)
{
    std::string result(std::size_t(file.size()) + 1, '\0');
    const auto read = file.read(result.data(), result.size());
    CHECK(read.eof);
    result.resize(read.bytes);
    return result;
}
void set_environment(const char* name, const std::string& value)
{
    CHECK(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true));
}
int main(int argc, char**)
{
    static_assert(std::is_move_constructible_v<File> && !std::is_copy_constructible_v<File>);
    CHECK(SDL_Init(0));
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
    if (argc > 1)
    {
        set_environment("WIZ8_ASSET_ROOT", path_to_utf8(assets));
        set_environment("WIZ8_USER_ROOT", "");
        set_environment("XDG_DATA_HOME", path_to_utf8(temporary.path / "preferences"));
        // SDL derives its pref root from process environment on Unix. Its
        // environment object does not mutate the process; unsafe is used ONLY
        // in this isolated test before any filesystem calls or other threads.
        CHECK(SDL_setenv_unsafe("XDG_DATA_HOME", path_to_utf8(temporary.path / "preferences").c_str(), 1) == 0);
        char* preferred = SDL_GetPrefPath("Whizzardry", "whizzardry8");
        CHECK(preferred);
        const auto pref = path_from_utf8(preferred).lexically_normal();
        SDL_free(preferred);
        auto app = pref.filename().empty() ? pref.parent_path() : pref;
        const auto legacy = app.parent_path().parent_path() / "whizzardry8";
        const auto relative = fs::relative(app, temporary.path);
        const bool isolated_pref = !relative.empty() && *relative.begin() != "..";
        if (isolated_pref)
        {
            CHECK(w8_native::existing_legacy_user_root(path_to_utf8(pref)).empty());
            CHECK(!fs::exists(legacy));
            fixture(legacy / "Saves" / "old.sav", "old-save");
        }
        const auto old = w8_native::existing_legacy_user_root(path_to_utf8(pref));
        const auto roots = w8_native::path_roots();
        CHECK(fs::equivalent(path_from_utf8(roots.user), old.empty() ? pref : path_from_utf8(old)));
        if (isolated_pref)
            CHECK(read_all(*open_file("C:/Saves/old.sav")) == "old-save");
        SDL_Quit();
        return 0;
    }
    fs::create_directories(user);
    w8_native::configure_paths({path_to_utf8(assets), path_to_utf8(user), {path_to_utf8(disc), "", ""}});
    auto file = open_file("c:/data/mixed.bin");
    auto independent = open_file("C:\\DATA\\MIXED.BIN");
    CHECK(file->size() == 6 && file->tell() == 0);
    CHECK(!file->opened_writer_create_time());
    char buffer[32]{};
    auto first = file->read(buffer, 2);
    CHECK(first.bytes == 2 && !first.eof && file->tell() == 2);
    CHECK(independent->tell() == 0);
    CHECK(file->seek(-2, SeekOrigin::end) == 4);
    auto tail = file->read(buffer, sizeof(buffer));
    CHECK(tail.bytes == 2 && tail.eof);
    CHECK(file->read(buffer, 1).eof);
    CHECK(file->seek(0, SeekOrigin::begin) == 0);
    CHECK(!file->read(nullptr, 0).eof);
    rejects([&] { file->write("x", 1); });
    rejects([&] { file->seek(-1, SeekOrigin::begin); });
    CHECK(file->tell() == 0);
    rejects([&] { file->seek(std::numeric_limits<std::int64_t>::min(), SeekOrigin::current); });
    CHECK(file->tell() == 0);
    CHECK(file->seek(1, SeekOrigin::begin) == 1);
    rejects([&] { file->seek(std::numeric_limits<std::int64_t>::max(), SeekOrigin::current); });
    rejects([&] { file->seek(std::numeric_limits<std::int64_t>::max(), SeekOrigin::end); });
    CHECK(file->tell() == 1);
    CHECK(file->seek(0, SeekOrigin::begin) == 0);
    File moved(std::move(*file));
    CHECK(!file->is_open() && moved.is_open());
    CHECK(read_all(moved) == "retail");
    moved.close();
    moved.close();
    rejects([&] { moved.read(buffer, 1); });
    independent.reset();
    file.reset();

    auto writer = open_file("Data/mixed.bin", OpenMode::update);
    CHECK(writer->opened_writer_create_time());
    const auto snapshot = writer->opened_writer_create_time();
    writer->write("game", 4);
    writer->flush();
    CHECK(writer->opened_writer_create_time() == snapshot);
    CHECK(writer->status().info.type == SDL_PATHTYPE_FILE);
    const auto overlay = writer->physical_path();
    writer.reset();
    CHECK(contents(assets / "Data" / "Mixed.BIN") == "retail");
    CHECK(contents(overlay) == "gameil");
    CHECK(read_all(*open_file("Data/MIXED.bin")) == "gameil");
    CHECK(file_status("Data/mixed.bin")->writable);
    CHECK(!file_status("D:/Levels/LEVELS.SLF")->writable);
    CHECK(!file_status("Data/missing"));
    rejects([] { open_file("Data/missing", OpenMode::update); });
    CHECK(!fs::exists(user / "Data" / "missing"));
    rejects([] { open_file("Data/missing-parent/new.bin", OpenMode::replace); });
    CHECK(!fs::exists(user / "Data" / "missing-parent"));
    auto copied_read_only = open_file("Data/ReadOnly.bin", OpenMode::update);
    copied_read_only->write("game", 4);
    copied_read_only.reset();
    CHECK(contents(assets / "Data" / "ReadOnly.bin") == "locked");
    CHECK(read_all(*open_file("Data/ReadOnly.bin")) == "gameed");
    auto append = open_file("Data/mixed.bin", OpenMode::append);
    append->seek(0, SeekOrigin::begin);
    append->write("!", 1);
    append.reset();
    CHECK(read_all(*open_file("Data/MIXED.bin")) == "gameil!");
    auto replace = open_file("Data/mixed.bin", OpenMode::replace);
    replace->write("new", 3);
    replace.reset();
    CHECK(remove_file("Data/MIXED.bin"));
    CHECK(!remove_file("Data/MIXED.bin"));
    CHECK(read_all(*open_file("Data/MIXED.bin")) == "retail");
    create_directory("C:/Saves/Profiles");
    auto save = open_file("Saves/Profiles/game.sav", OpenMode::replace);
    save->write("save", 4);
    save.reset();
    CHECK(fs::exists(user / "Saves" / "Profiles" / "game.sav"));
    CHECK(read_all(*open_file("D:/levels/levels.slf")) == "disc");
    rejects([] { open_file("D:/Levels/LEVELS.SLF", OpenMode::replace); });
    rejects([] { create_directory("E:/Saves"); });
    rejects([] { open_file("C:/../../outside/pwn", OpenMode::replace); });
    rejects([] { create_directory("C:/Saves/C:/Injected"); });
    rejects([] { open_file("C:/Saves/game.sav:extra", OpenMode::replace); });
    rejects([] { open_file(std::string("Data\0Mixed.BIN", 14)); });

    const auto listing = list_directory("Data", "*.*");
    CHECK(std::find(listing.begin(), listing.end(), "NoExtension") != listing.end());
    CHECK(list_directory("Data", "m?xed.BIN").size() == 1);
    rejects([] { list_directory("not-a-directory"); });
    CHECK(read_all(*open_file("Data/caf\xc3\xa9.bin")) == "unicode");
    auto host = open_host_file(outside / path_from_utf8("h\xc3\xb4te.bin"));
    CHECK(read_all(*host) == "import");
    host.reset();
    fixture(outside / "CaseSensitive.bin", "exact host");
    std::error_code case_error;
    const bool case_insensitive_host = fs::exists(outside / "casesensitive.BIN", case_error);
    if (case_insensitive_host)
        CHECK(read_all(*open_host_file(outside / "casesensitive.BIN")) == "exact host");
    else
        rejects([&] { open_host_file(outside / "casesensitive.BIN"); });
    rejects([&] { open_file(path_to_utf8(outside / "new"), OpenMode::replace); });
    rejects([&] { open_file(path_to_utf8(assets / "Data" / "Mixed.BIN")); });
    rejects([&] { open_host_file(assets / "Data" / "Mixed.BIN", OpenMode::replace); });
    rejects([&] { open_host_file(disc / "Levels" / "LEVELS.SLF", OpenMode::update); });
    CHECK(!host_file_status(assets / "Data" / "Mixed.BIN")->writable);

    copy_file("Data/Mixed.BIN", "Data/Copy.bin");
    CHECK(read_all(*open_file("Data/copy.bin")) == "retail");
    rejects([] { copy_file("Data/Mixed.BIN", "Data/Copy.bin"); });
    rejects([] { copy_file("Data/missing", "Data/Copy.bin", CopyMode::replace); });
    CHECK(read_all(*open_file("Data/copy.bin")) == "retail");
    auto live_destination = open_file("Data/Copy.bin", OpenMode::update);
    replace_file(*live_destination, "Data/NoExtension");
    CHECK(!live_destination->is_open());
    CHECK(read_all(*open_file("Data/Copy.bin")) == "plain");
    copy_file("Data/Mixed.BIN", "Data/Copy.bin", CopyMode::replace);
    CHECK(read_all(*open_file("Data/Copy.bin")) == "retail");
    rejects([] { copy_file("Data/Copy.bin", "Data/Copy.bin", CopyMode::replace); });
    for (const auto& entry : fs::directory_iterator(user / "Data"))
        CHECK(path_to_utf8(entry.path().filename()).rfind(".wiz8-copy-", 0) != 0);

    // Symlink creation may need host privileges (notably on Windows); no runtime
    // OS branch. If supported, both virtual and host write aliases are blocked.
    std::error_code error;
    fs::create_directory_symlink(assets / "Data", user / "alias", error);
    if (!error)
    {
        rejects([] { open_file("alias/Mixed.BIN", OpenMode::replace); });
        rejects([] { create_directory("alias/new"); });
        rejects([&] { open_host_file(user / "alias" / "Mixed.BIN", OpenMode::replace); });
        rejects([] { remove_file("alias/Mixed.BIN"); });
        CHECK(contents(assets / "Data" / "Mixed.BIN") == "retail");
    }
    else
        std::fprintf(stderr, "Symlink checks unavailable: %s\n", error.message().c_str());
    error.clear();
    fs::create_directory_symlink(outside, user / "escape", error);
    if (!error)
    {
        rejects([] { open_file("escape/new", OpenMode::replace); });
        rejects([&] { open_host_file(user / "escape" / "new", OpenMode::replace); });
    }
    error.clear();
    fs::create_directory_symlink(outside, assets / "escape", error);
    if (!error)
        rejects([&] { open_host_file(assets / "escape" / "new", OpenMode::replace); });
    const auto synthetic_pref = temporary.path / "preferences" / "Whizzardry" / "whizzardry8";
    const auto synthetic_legacy = temporary.path / "preferences" / "whizzardry8";
    CHECK(w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref)).empty());
    CHECK(!fs::exists(synthetic_pref) && !fs::exists(synthetic_legacy));
    fixture(synthetic_legacy / "Saves" / "old.sav", "old");
    CHECK(fs::equivalent(path_from_utf8(w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref))), synthetic_legacy));
    CHECK(!fs::exists(synthetic_pref));
    fs::create_directories(synthetic_pref);
    CHECK(!w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref)).empty());
    fixture(synthetic_pref / "Saves" / "new.sav", "new");
    CHECK(w8_native::existing_legacy_user_root(path_to_utf8(synthetic_pref)).empty());
    CHECK(contents(synthetic_legacy / "Saves" / "old.sav") == "old");
    CHECK(contents(synthetic_pref / "Saves" / "new.sav") == "new");
    fs::permissions(assets / "Data" / "ReadOnly.bin", fs::perms::owner_write, fs::perm_options::add);
    w8_native::configure_paths({path_to_utf8(assets), path_to_utf8(assets / "nested-user"), {}});
    CHECK(w8_native::path_roots().user.empty());
    rejects([] { open_file("Data/new", OpenMode::replace); });
    SDL_Quit();
}
