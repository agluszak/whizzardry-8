#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <wiz8/filesystem.h>

#include "file_operations.h"
#include "temporary_directory.h"
#include "wiz8/asset_paths.h"

namespace fs = std::filesystem;
TEST_CASE("Explicit roots isolate assets and atomic user saves")
{
    ScopedTemporaryDirectory temporary{"wiz8-paths"};
    const auto base = temporary.path;
    fs::create_directories(base / "assets/data");
    fs::create_directories(base / "user/saves");
    const auto previous = w8_native::path_roots();
    struct Restore {
        w8_native::Roots roots;
        ~Restore() { w8_native::configure_paths(roots); }
    } restore{previous};
    w8_native::configure_paths(
        {wiz8::path_to_utf8(base / "assets"), wiz8::path_to_utf8(base / "user")});
    std::ofstream(base / "assets/data/item.bin") << "asset";
    fs::create_directory(base / "user/data");
    std::ofstream(base / "user/data/item.bin") << "user";
    REQUIRE(wiz8::open_file(wiz8::asset_path("DATA\\Item.BIN"))->read_text() == "asset");
    REQUIRE(wiz8::open_file(wiz8::user_path("data/item.bin"))->read_text() == "user");
    const auto save = wiz8::user_path("saves/Current.SAV");
    {
        auto file = wiz8::open_file(save, wiz8::OpenMode::replace);
        file->write("old", 3);
    }
    {
        wiz8::StagedFile abandoned(save);
        abandoned.file()->write("partial", 7);
    }
    REQUIRE(wiz8::open_file(save)->read_text() == "old");
    std::shared_ptr<wiz8::File> borrowed;
    {
        wiz8::StagedFile staged(save);
        borrowed = staged.file();
        borrowed->write("new", 3);
        staged.commit();
        REQUIRE(!borrowed->is_open());
    }
    REQUIRE(wiz8::open_file(save)->read_text() == "new");
    REQUIRE(fs::exists(base / "user/saves/Current.SAV"));
    REQUIRE(!fs::exists(base / "assets/saves"));
    REQUIRE(wiz8::remove_file(save));
    REQUIRE(!wiz8::file_status(save));
    REQUIRE(wiz8::list_directory(base / "user/saves").empty());
    for (const auto* path : {"C:/data/item.bin", "D:\\item", "/etc/passwd", "../outside",
                             "data/../../x", "data/part:stream"})
        REQUIRE_THROWS(wiz8::asset_path(path));
    REQUIRE_THROWS(wiz8::user_path(std::string_view("x\0y", 3)));
    std::error_code error;
    fs::create_directory_symlink(base / "assets", base / "user/alias", error);
    if (!error) REQUIRE_THROWS(wiz8::user_path("alias/data/item.bin"));
    REQUIRE_THROWS(w8_native::configure_paths(
        {wiz8::path_to_utf8(base / "assets"), wiz8::path_to_utf8(base / "assets/nested")}));
}

TEST_CASE("Save copies publish writable files and preserve the old save on failure")
{
    ScopedTemporaryDirectory temporary{"wiz8-copy"};
    const auto source = temporary.path / "source.SAV";
    const auto target = temporary.path / "target.SAV";
    std::ofstream(source) << "replacement";
    std::ofstream(target) << "old";
    fs::permissions(source, fs::perms::owner_read, fs::perm_options::replace);
    REQUIRE_THROWS(wiz8::copy_file(temporary.path / "missing", target));
    REQUIRE(wiz8::open_file(target)->read_text() == "old");
    const auto directory = temporary.path / "directory";
    fs::create_directory(directory);
    REQUIRE_THROWS(wiz8::copy_file(directory, target));
    REQUIRE(wiz8::open_file(target)->read_text() == "old");
    {
        struct RestoreHook {
            wiz8::testing::FileOperationHook previous;
            ~RestoreHook() { wiz8::testing::set_file_operation_hook(previous); }
        } restore{wiz8::testing::set_file_operation_hook([](wiz8::testing::FileOperation op) {
            if (op == wiz8::testing::FileOperation::publish)
                throw std::runtime_error("publication failed");
        })};
        REQUIRE_THROWS(wiz8::copy_file(source, target));
        REQUIRE(wiz8::open_file(target)->read_text() == "old");
    }
    wiz8::copy_file(source, target);
    REQUIRE(wiz8::open_file(target)->read_text() == "replacement");
    REQUIRE_FALSE(wiz8::file_status(target)->read_only);
    REQUIRE_THROWS(wiz8::copy_file(target, target));
    REQUIRE(wiz8::list_directory(temporary.path, "*.sav") ==
            std::vector<std::string>{"source.SAV", "target.SAV"});
    auto file = wiz8::open_file(target);
    REQUIRE(file->seek(3, wiz8::SeekOrigin::begin) == 3);
    REQUIRE(file->seek(2, wiz8::SeekOrigin::current) == 5);
    REQUIRE(file->seek(-2, wiz8::SeekOrigin::end) == 9);
    REQUIRE_THROWS(file->seek(-20, wiz8::SeekOrigin::begin));
    REQUIRE(file->tell() == 9);
}
