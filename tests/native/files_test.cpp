/* Asset, SLF and save contracts through native production streams. */
#include "wiz8/slf.h"
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <wiz8/file_time.h>
#include <SDL3/SDL_stdinc.h>
#include "surrender/srSystem.h"
#include "surrender/srBinFStream.h"
#include "wiz8/virtual_file_stream.h"
#include "wiz8/chunk.h"
#include "temporary_directory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); std::exit(1); } } while (0)

static void fixture(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(content.data(), content.size());
    CHECK(file.good());
}
static std::string contents(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
template<class F> static bool fails(F&& action)
{
    try { action(); return false; } catch (const std::exception&) { return true; }
}
static std::string read_bytes(wiz8::File& file, std::size_t size)
{
    std::string bytes(size, '\0');
    const auto result = file.read(bytes.data(), bytes.size());
    CHECK(result.bytes <= size);
    bytes.resize(result.bytes);
    return bytes;
}
static std::string slf_bytes()
{
    static_assert(sizeof(wiz8::SlfHeader) == 532);
    static_assert(sizeof(wiz8::SlfEntry) == 280);
    static_assert(sizeof(wiz8::DiskFileTime) == 8);
    wiz8::SlfHeader header{};
    strcpy(header.sLibName, "Data.slf");
    strcpy(header.sPathToLibrary, "Data\\");
    header.iEntries = header.iUsed = 2;
    header.iVersion = 0x200;
    wiz8::SlfEntry entries[2]{};
    strcpy(entries[0].sFileName, "ArchiveOnly.bin");
    entries[0].uiOffset = sizeof(header);
    entries[0].uiLength = 8;
    entries[0].sFileTime = {0xd53e8001u, 0x019db1deu};
    strcpy(entries[1].sFileName, "Override.bin");
    entries[1].uiOffset = sizeof(header) + 8;
    entries[1].uiLength = 7;
    std::string bytes(reinterpret_cast<char*>(&header), sizeof(header));
    bytes += "archive!packed!";
    bytes.append(reinterpret_cast<char*>(entries), sizeof(entries));
    // Serialized FILETIME stays a low/high pair at the fixed directory offset.
    CHECK(static_cast<unsigned char>(bytes[sizeof(header) + 15 + 268]) == 1);
    return bytes;
}
int main() try
{
    const auto root = wiz8::path_from_utf8(make_temporary_directory("wiz8-files"));
    const auto assets = root / "assets", user = root / "user", disc = root / "disc";
    fixture(assets / "data" / "MixedCase.BIN", "retail bytes");
    fixture(assets / "data" / "Override.bin", "loose!");
    fixture(assets / "data" / "NoExtension", "plain");
    fixture(assets / "data" / "ReadOnly.bin", "locked");
    fs::permissions(assets / "data" / "ReadOnly.bin", fs::perms::owner_read |
                    fs::perms::group_read | fs::perms::others_read);
    fixture(assets / "Saves" / "Installed.SAV", "installed save");
    fixture(user / "Saves" / "CurrentGame.SAV", "user save");
    fixture(disc / "Levels" / "LEVELS.SLF", "disc bytes");
    const auto archive_bytes = slf_bytes();
    fixture(assets / "data" / "DATA.SLF", archive_bytes);
    CHECK(SDL_setenv_unsafe("WIZ8_ASSET_ROOT", wiz8::path_to_utf8(assets).c_str(), 1) == 0);
    CHECK(SDL_setenv_unsafe("WIZ8_USER_ROOT", wiz8::path_to_utf8(user).c_str(), 1) == 0);
    CHECK(SDL_setenv_unsafe("WIZ8_CD1_ROOT", wiz8::path_to_utf8(disc).c_str(), 1) == 0);
    const auto configured = w8_native::path_roots();
    CHECK(fs::equivalent(wiz8::path_from_utf8(configured.assets), assets));
    CHECK(fs::equivalent(wiz8::path_from_utf8(configured.user), user));
    w8_native::configure_paths({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user),
                                {wiz8::path_to_utf8(disc), wiz8::path_to_utf8(disc), wiz8::path_to_utf8(disc)}});
    wiz8::mount_slf("Data/DATA.SLF");
    auto file = wiz8::open_file("DATA/mixedcase.bin");
    auto independent = wiz8::open_file("C:/data/MixedCase.BIN");
    CHECK(file->size() == 12 && independent->tell() == 0);
    CHECK(read_bytes(*file, 64) == "retail bytes");
    CHECK(file->tell() == 12 && read_bytes(*file, 1).empty());
    CHECK(fails([&] { char byte; file->read_exact(&byte, 1); }));
    CHECK(independent->tell() == 0 && read_bytes(*independent, 6) == "retail");
    CHECK(fails([&] { file->write("x", 1); }));
    CHECK(file->seek(-5, wiz8::SeekOrigin::end) == 7);
    CHECK(read_bytes(*file, 5) == "bytes");
    CHECK(fails([&] { file->seek(-99, wiz8::SeekOrigin::current); }));
    file->seek(0, wiz8::SeekOrigin::begin);
    CHECK(file->read(nullptr, 0).bytes == 0);
    CHECK(fails([&] { file->read(nullptr, 1); }));
    file->close(); file->close(); independent.reset();
    CHECK(fails([&] { file->read(nullptr, 1); }));
    CHECK(fails([&] { wiz8::open_file("Data"); }));
    CHECK(fails([&] { wiz8::open_file("missing/new.sav", wiz8::OpenMode::replace); }));

    file = wiz8::open_file("Data/MixedCase.BIN", wiz8::OpenMode::update);
    file->write("native", 6);
    file->seek(0, wiz8::SeekOrigin::begin);
    CHECK(read_bytes(*file, 12) == "native bytes"); file.reset();
    CHECK(contents(assets / "data" / "MixedCase.BIN") == "retail bytes");
    CHECK(wiz8::remove_file("Data/mixedcase.bin"));
    CHECK(!wiz8::remove_file("Data/MixedCase.BIN"));
    CHECK(wiz8::file_status("Data/ReadOnly.bin")->read_only);
    wiz8::clear_read_only("Data/ReadOnly.bin");
    CHECK(!wiz8::file_status("Data/ReadOnly.bin")->read_only);
    file = wiz8::open_file("Data/ReadOnly.bin", wiz8::OpenMode::update);
    file->write("unlocked", 8); file.reset();
    CHECK(contents(assets / "data" / "ReadOnly.bin") == "locked");
    CHECK(fails([&] { wiz8::open_host_file(assets / "data" / "ReadOnly.bin", wiz8::OpenMode::update); }));
    for (const char* drive : {"D", "E", "F"}) {
        file = wiz8::open_file(std::string(drive) + ":/Levels/Levels.slf");
        CHECK(read_bytes(*file, 10) == "disc bytes"); file.reset();
    }
    for (auto mode : {wiz8::OpenMode::update, wiz8::OpenMode::replace, wiz8::OpenMode::append})
        CHECK(fails([&] { wiz8::open_file("D:/Levels/Levels.slf", mode); }));
    CHECK(fails([&] { wiz8::clear_read_only("D:/Levels/Levels.slf"); }));
    CHECK(fails([&] { wiz8::remove_file("D:/Levels/Levels.slf"); }));
    wiz8::create_directory("sAVES"); wiz8::create_directory("Saves/Characters");
    CHECK(wiz8::file_status("Saves/Characters")->info.type == SDL_PATHTYPE_DIRECTORY);
    file = wiz8::open_file("Saves/Installed.SAV", wiz8::OpenMode::append);
    file->write("+user", 5); file.reset();
    CHECK(contents(user / "Saves" / "Installed.SAV") == "installed save+user");
    CHECK(contents(assets / "Saves" / "Installed.SAV") == "installed save");
    file = wiz8::open_file("Saves/Native.SAV", wiz8::OpenMode::replace);
    file->write("save", 4); file.reset();
    file = wiz8::open_file("Saves/Native.SAV", wiz8::OpenMode::append);
    file->seek(0, wiz8::SeekOrigin::begin); file->write(" appended", 9); file.reset();
    CHECK(contents(user / "Saves" / "Native.SAV") == "save appended");
    file = wiz8::open_file("Saves/Native.SAV", wiz8::OpenMode::replace);
    CHECK(file->size() == 0); file.reset();
    {
        char path[] = "Saves/Native.SAV";
        fixture(user / "Saves" / "Native.SAV", "RIFF");
        W8Chunk chunk;
        CHECK(!chunk.OpenRead(path) && !chunk.m_hFile && chunk.m_heads.GetCount() == 0);
        CHECK(chunk.OpenWrite(path));
        const unsigned word = 0x12345678;
        CHECK(chunk.OpenChunk(0x54534554, false));
        CHECK(chunk.Write(&word, sizeof(word), nullptr));
        CHECK(chunk.ReleaseCurrentChunk());
        chunk.Close();
        CHECK(chunk.OpenRead(path) && chunk.ChunkCount() == 1);
        CHECK(chunk.OpenChunk(0, false) && chunk.CurrentChunkId() == 0x54534554);
        unsigned loaded = 0;
        CHECK(chunk.Read(&loaded, sizeof(loaded), nullptr) && loaded == word);
        CHECK(chunk.ReleaseCurrentChunk());
        chunk.Close();
        // Destruction owns pending headers as well as the stream, even without Close.
        CHECK(chunk.OpenRead(path));
    }
    if (fs::exists("/dev/full")) {
        auto full = wiz8::open_host_file("/dev/full", wiz8::OpenMode::update);
        CHECK(fails([&] { full->write("checked", 7); full->flush(); }));
    }
    wiz8::copy_file("Saves/CurrentGame.SAV", "Saves/Backup.SAV");
    CHECK(fails([&] { wiz8::copy_file("Saves/CurrentGame.SAV", "Saves/backup.sav"); }));
    CHECK(fails([&] { wiz8::copy_file("Saves/CurrentGame.SAV", "Saves/currentgame.sav", wiz8::CopyMode::replace); }));
    CHECK(fails([&] { wiz8::copy_file("missing.sav", "Saves/Backup.SAV", wiz8::CopyMode::replace); }));
    CHECK(contents(user / "Saves" / "Backup.SAV") == "user save");
    wiz8::copy_file("Data/MixedCase.BIN", "Saves/Backup.SAV", wiz8::CopyMode::replace);
    CHECK(contents(user / "Saves" / "Backup.SAV") == "retail bytes");
    const auto unicode = root / wiz8::path_from_utf8("zażółć-雪.bin");
    fixture(unicode, "unicode import");
    CHECK(wiz8::path_from_utf8(wiz8::path_to_utf8(unicode)) == unicode);
    file = wiz8::open_host_file(unicode);
    CHECK(read_bytes(*file, 14) == "unicode import"); file.reset();
    std::vector<std::string> host_files;
    const auto host_text = wiz8::path_to_utf8(unicode);
    CHECK(srSystem::scanFiles(host_files, host_text.c_str()) == 1);
    CHECK(host_files == std::vector<std::string>{host_text});
    CHECK(fails([&] { wiz8::open_file(host_text); }));
    for (const char* path : {"C:/../outside.sav", "Saves/../../outside.sav", "Saves/part:stream"})
        CHECK(fails([&] { wiz8::open_file(path, wiz8::OpenMode::replace); }));
    const auto outside = root / "outside";
    fixture(outside / "untouched.sav", "outside");
    std::error_code symlink_error;
    fs::create_directory_symlink(outside, user / "Escape", symlink_error);
    if (!symlink_error) {
        CHECK(fails([&] { wiz8::open_file("Escape/untouched.sav", wiz8::OpenMode::replace); }));
        CHECK(fails([&] { wiz8::copy_file("Saves/CurrentGame.SAV", "Escape/untouched.sav", wiz8::CopyMode::replace); }));
        CHECK(contents(outside / "untouched.sav") == "outside");
    } else fprintf(stderr, "Symlink checks unavailable: %s\n", symlink_error.message().c_str());
    symlink_error.clear();
    fs::create_directory_symlink(assets, user / "AssetLink", symlink_error);
    if (!symlink_error) {
        CHECK(fails([&] { wiz8::open_file("AssetLink/data/MixedCase.BIN", wiz8::OpenMode::update); }));
        CHECK(fails([&] { wiz8::remove_file("AssetLink/data/MixedCase.BIN"); }));
    }
    const auto names = wiz8::list_directory("dAtA", "*.*");
    CHECK(names.size() == 5 && std::find(names.begin(), names.end(), "NoExtension") != names.end());
    CHECK(wiz8::list_directory("Data", "*.absent").empty());
    char directory[512];
    // Renderer callers share the same namespace, without inspecting their adapters.
    {
        srBinIFStream input("DATA\\mixedcase.bin");
        CHECK(input.isOpen());
        CHECK(std::string(input.getPath()) == "DATA\\mixedcase.bin");
        input.read(directory, 12);
        CHECK(!memcmp(directory, "retail bytes", 12));
        input.close();
        CHECK(!input.isOpen() && std::string(input.getPath()).empty());
        input.close();
        input.open("Data/MixedCase.BIN");
        CHECK(input.isOpen() && std::string(input.getPath()) == "Data/MixedCase.BIN");
        input.close();
        input.open(nullptr);
        CHECK(!input.isOpen() && std::string(input.getPath()).empty());
        srBinOFStream output("Saves\\Renderer.SAV");
        CHECK(output.isOpen());
        output.write("renderer save", 13);
        output.close();
        CHECK(contents(user / "Saves" / "Renderer.SAV") == "renderer save");
    }
    std::vector<std::string> table{"existing"};
    CHECK(srSystem::scanFiles(table, "Data\\*.SLF") == 1);
    CHECK(table.size() == 2 && table.front() == "existing");
    CHECK(table.back() == "C:\\Data\\DATA.SLF");
    CHECK(srSystem::scanFiles(table, "Data/", "*.SLF") == 1);
    CHECK(table.size() == 3 && table[1] == table[2]);
    CHECK(srSystem::scanFiles(table, nullptr) == 0);
    CHECK(srSystem::scanFiles(table, "") == 0);
    CHECK(srSystem::scanFiles(table, "Data/") == 0);
    CHECK(srSystem::scanFiles(table, "Data", nullptr) == 0);
    CHECK(srSystem::scanFiles(table, "Data/*.absent") == 0 && table.size() == 3);

    for (const char* path : {"Data/archiveonly.bin", "Data\\archiveonly.bin",
                             "C:/Data\\archiveonly.bin"}) {
        W8VirtualFileBinIStream input(path);
        CHECK(input.good() && input.getSize() == 8);
        char bytes[8];
        input.read(bytes, sizeof(bytes));
        CHECK(input.good() && input.tell() == sizeof(bytes));
        CHECK(!memcmp(bytes, "archive!", sizeof(bytes)));
    }
    CHECK(!W8VirtualFileBinIStream(nullptr).good());
    CHECK(!W8VirtualFileBinIStream("").good());


    CHECK(wiz8::file_status("Data/archiveonly.bin")->archived);
    auto archived = wiz8::open_file("Data/archiveonly.bin");
    CHECK(archived->size() == 8);
    CHECK(fails([&] { read_bytes(*archived, 9); }) && archived->tell() == 0);
    CHECK(read_bytes(*archived, 8) == "archive!");
    CHECK(archived->read(nullptr, 0).bytes == 0);
    CHECK(fails([&] { read_bytes(*archived, 1); }));
    CHECK(archived->seek(3, wiz8::SeekOrigin::begin) == 3);
    CHECK(read_bytes(*archived, 5) == "hive!");
    CHECK(fails([&] { archived->write("x", 1); }));
    const auto times = archived->times();
    CHECK(times.created.ticks() == 0 && times.accessed.ticks() == 0);
    CHECK(times.modified.low == 0xd53e8001u && times.modified.high == 0x019db1deu);
    auto second = wiz8::open_file("Data/archiveonly.bin");
    CHECK(second->tell() == 0 && read_bytes(*second, 4) == "arch");
    CHECK(archived->seek(-1, wiz8::SeekOrigin::current) == 7);
    CHECK(fails([&] { archived->seek(2, wiz8::SeekOrigin::current); }));
    CHECK(archived->tell() == 7);
    CHECK(fails([&] { second->seek(-9, wiz8::SeekOrigin::end); }));
    CHECK(second->tell() == 4);
    wiz8::clear_asset_archives();
    CHECK(read_bytes(*archived, 1) == "!");
    CHECK(read_bytes(*second, 4) == "ive!");
    archived.reset(); second.reset();
    wiz8::mount_slf("Data/DATA.SLF");
    file = wiz8::open_file("Data/Override.bin");
    CHECK(read_bytes(*file, 6) == "loose!"); file.reset();
    auto patch = archive_bytes;
    patch.replace(sizeof(wiz8::SlfHeader), 8, "patched!");
    fixture(assets / "Patches" / "Patch.000", patch);
    wiz8::mount_slf("Patches/Patch.000", true);
    file = wiz8::open_file("C:/DATA/ARCHIVEONLY.BIN");
    CHECK(read_bytes(*file, 8) == "patched!"); file.reset();
    auto latest_patch = patch;
    latest_patch.replace(sizeof(wiz8::SlfHeader), 8, "latest!!");
    fixture(assets / "Patches" / "Patch.049", latest_patch);
    wiz8::mount_slf("Patches/Patch.049", true);
    file = wiz8::open_file("Data/archiveonly.bin");
    CHECK(read_bytes(*file, 8) == "latest!!"); file.reset();
    fixture(assets / "data" / "ArchiveOnly.bin", "loose archive");
    file = wiz8::open_file("Data/archiveonly.bin");
    CHECK(read_bytes(*file, 32) == "loose archive"); file.reset();
    fs::remove(assets / "data" / "ArchiveOnly.bin");
    for (std::size_t length : {std::size_t(20), archive_bytes.size() - 1}) {
        fixture(assets / "data" / "Truncated.slf", archive_bytes.substr(0, length));
        CHECK(fails([&] { wiz8::mount_slf("Data/Truncated.slf"); }));
    }
    auto invalid = archive_bytes;
    wiz8::SlfEntry entry{};
    memcpy(&entry, invalid.data() + sizeof(wiz8::SlfHeader) + 15, sizeof(entry));
    entry.uiLength = UINT32_MAX;
    memcpy(invalid.data() + sizeof(wiz8::SlfHeader) + 15, &entry, sizeof(entry));
    fixture(assets / "data" / "InvalidEntry.slf", invalid);
    CHECK(fails([&] { wiz8::mount_slf("Data/InvalidEntry.slf"); }));
    for (int corruption = 0; corruption < 4; ++corruption) {
        auto bytes = archive_bytes;
        wiz8::SlfHeader header{};
        memcpy(&header, bytes.data(), sizeof(header));
        if (corruption == 0) header.iEntries = -1;
        if (corruption == 1) header.iUsed = 3;
        if (corruption == 2) memset(header.sPathToLibrary, 'x', sizeof(header.sPathToLibrary));
        if (corruption == 3) {
            const auto offset = sizeof(header) + 15;
            memcpy(bytes.data() + offset + sizeof(wiz8::SlfEntry), bytes.data() + offset,
                   sizeof(wiz8::SlfEntry));
        }
        memcpy(bytes.data(), &header, sizeof(header));
        fixture(assets / "data" / "Malformed.slf", bytes);
        CHECK(fails([&] { wiz8::mount_slf("Data/Malformed.slf", true); }));
        file = wiz8::open_file("Data/archiveonly.bin");
        CHECK(read_bytes(*file, 8) == "latest!!"); file.reset();
    }
    wiz8::clear_asset_archives();
    wiz8::mount_slf("Data/DATA.SLF");
    auto stale_archive = wiz8::open_file("Data/ArchiveOnly.bin");
    fs::resize_file(assets / "data" / "DATA.SLF", sizeof(wiz8::SlfHeader) + 3);
    CHECK(fails([&] { wiz8::open_file("Data/archiveonly.bin"); }));
    CHECK(fails([&] { char byte; stale_archive->read(&byte, 1); }));
    stale_archive.reset();
    fixture(assets / "data" / "DATA.SLF", archive_bytes);
    std::vector<std::unique_ptr<wiz8::File>> readers;
    for (unsigned i = 0; i < 45; ++i) {
        auto reader = wiz8::open_file(i % 2 ? "Data/archiveonly.bin" : "Saves/CurrentGame.SAV");
        CHECK(read_bytes(*reader, 4) == (i % 2 ? "arch" : "user"));
        readers.push_back(std::move(reader));
    }
    for (std::size_t i = 0; i < readers.size(); i += 2) readers[i].reset();
    readers.clear();
    auto level_archive = archive_bytes;
    wiz8::SlfHeader level_header{};
    memcpy(&level_header, level_archive.data(), sizeof(level_header));
    strcpy(level_header.sPathToLibrary, "Levels\\");
    memcpy(level_archive.data(), &level_header, sizeof(level_header));
    fixture(disc / "Levels" / "LEVELS.SLF", level_archive);
    wiz8::refresh_asset_archives();
    file = wiz8::open_file("Levels/ArchiveOnly.bin");
    CHECK(read_bytes(*file, 8) == "archive!"); file.reset();
    file = wiz8::open_file("Data/ArchiveOnly.bin");
    CHECK(read_bytes(*file, 8) == "latest!!");
    // Refresh replaced disc metadata, but never invalidates an existing cursor.
    level_archive.replace(sizeof(level_header), 8, "newdisc!");
    fixture(disc / "Levels" / "LEVELS.SLF", level_archive);
    wiz8::refresh_asset_archives();
    CHECK(file->tell() == 8); file.reset();
    file = wiz8::open_file("Levels/ArchiveOnly.bin");
    CHECK(read_bytes(*file, 8) == "newdisc!"); file.reset();
    wiz8::clear_asset_archives();
    CHECK(contents(user / "Saves" / "CurrentGame.SAV") == "user save");
    CHECK(contents(assets / "data" / "MixedCase.BIN") == "retail bytes");
    CHECK(contents(assets / "data" / "ReadOnly.bin") == "locked");
    fs::permissions(assets / "data" / "ReadOnly.bin", fs::perms::owner_write, fs::perm_options::add);
    fs::remove_all(root);
    puts("ok: native game streams, immutable assets, user overlays, UTF-8 imports and bounded SLF records");
}
catch (const std::exception& error)
{
    fprintf(stderr, "native file fixture: %s\n", error.what());
    return 1;
}
