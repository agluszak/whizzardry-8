/* Asset, SLF and save contracts through the game file manager, not OS handles. */
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "platform_paths.h"
#include <wiz8/filesystem.h>
#include <wiz8/file_time.h>
#include <SDL3/SDL_stdinc.h>
#include "surrender/srSystem.h"
#include "surrender/srBinFStream.h"
#include "surrender/srStringTable.h"
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
static HWFILE open(const char* path, UINT32 options = FILE_ACCESS_READ, bool temporary = false)
{
    std::string name(path);
    return FileOpen(name.data(), options, temporary);
}
static void write_bytes(HWFILE file, const std::string& text)
{
    UINT32 count = 99;
    CHECK(FileWrite(file, const_cast<char*>(text.data()), UINT32(text.size()), &count));
    CHECK(count == text.size());
}
static std::string read_bytes(HWFILE file, UINT32 bytes, bool complete = true)
{
    std::string result(bytes, '\0');
    UINT32 count = 99;
    CHECK(bool(FileRead(file, result.data(), bytes, &count)) == complete);
    CHECK(count <= bytes);
    result.resize(count);
    return result;
}
static std::string slf_bytes()
{
    static_assert(sizeof(LIBHEADER) == 532);
    static_assert(sizeof(DIRENTRY) == 280);
    static_assert(sizeof(SGP_FILETIME) == 8);
    LIBHEADER header{};
    strcpy(header.sLibName, "Data.slf");
    strcpy(header.sPathToLibrary, "Data\\");
    header.iEntries = header.iUsed = 2;
    header.iVersion = 0x200;
    DIRENTRY entries[2]{};
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
static std::set<std::string> scan(char* pattern)
{
    GETFILESTRUCT entry{};
    std::set<std::string> names;
    if (GetFileFirst(pattern, &entry))
    {
        do { names.insert(entry.zFileName); } while (GetFileNext(&entry));
        GetFileClose(&entry);
        CHECK(!GetFileNext(&entry));
        GetFileClose(&entry);
    }
    return names;
}

int main()
{
    const auto temporary = make_temporary_directory("wiz8-files");
    const fs::path root = wiz8::path_from_utf8(temporary);
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
    CHECK(configured.assets == wiz8::path_to_utf8(fs::weakly_canonical(assets)));
    CHECK(configured.user == wiz8::path_to_utf8(fs::weakly_canonical(user)));
    w8_native::configure_paths({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user),
                                {wiz8::path_to_utf8(disc), "", ""}});
    CHECK(InitializeFileManager(nullptr));
    CHECK(InitializeFileDatabase());

    HWFILE file = open("DATA\\mixedcase.bin");
    CHECK(file && FileGetSize(file) == 12);
    HWFILE independent = open("C:/data/MixedCase.BIN");
    CHECK(independent && independent != file);
    CHECK(read_bytes(file, 64, false) == "retail bytes");
    CHECK(FileCheckEndOfFile(file) && FileGetPos(file) == 12);
    CHECK(read_bytes(file, 1, false).empty());
    CHECK(FileGetPos(independent) == 0 && read_bytes(independent, 6) == "retail");
    UINT32 count = 99;
    CHECK(!FileWrite(file, const_cast<char*>("x"), 1, &count) && count == 0);
    CHECK(FileSeek(file, 5, FILE_SEEK_FROM_END) && FileGetPos(file) == 7);
    CHECK(read_bytes(file, 5) == "bytes");
    CHECK(!FileSeek(file, UINT32(-99), FILE_SEEK_FROM_CURRENT));
    CHECK(FileGetPos(file) == 12 && !FileSeek(file, 0, 99));
    CHECK(FileSeek(file, 0, FILE_SEEK_FROM_START) && !FileCheckEndOfFile(file));
    CHECK(FileRead(file, nullptr, 0, &count) && count == 0);
    CHECK(!FileRead(file, nullptr, 1, &count) && count == 0);
    FileClose(file);
    FileClose(file);
    FileClose(independent);
    CHECK(!FileRead(file, nullptr, 1, &count) && count == 0);
    CHECK(!FileRead(UINT32(-1), nullptr, 0, &count) && count == 0);
    CHECK(!FileSeek(0, 0, FILE_SEEK_FROM_START) && FileGetPos(0) == -1);
    CHECK(!open("Data", FILE_ACCESS_READ) && !FileExistsNoDB(const_cast<char*>("Data")));
    CHECK(!open("missing\\new.sav", FILE_ACCESS_WRITE | FILE_CREATE_NEW));

    file = open("Data\\MixedCase.BIN", FILE_ACCESS_READWRITE | FILE_OPEN_EXISTING);
    CHECK(file);
    write_bytes(file, "native");
    CHECK(FileSeek(file, 0, FILE_SEEK_FROM_START));
    CHECK(read_bytes(file, 12) == "native bytes");
    FileClose(file);
    CHECK(contents(assets / "data" / "MixedCase.BIN") == "retail bytes");
    CHECK(FileDelete(const_cast<char*>("Data\\mixedcase.bin")));
    file = open("Data\\MixedCase.BIN");
    CHECK(read_bytes(file, 12) == "retail bytes");
    FileClose(file);
    CHECK(!FileDelete(const_cast<char*>("Data\\MixedCase.BIN")));

    // Read-only installed assets can be copied up, but never altered in place.
    CHECK(FileGetAttributes(const_cast<char*>("Data\\ReadOnly.bin")) & FILE_ATTRIBUTES_READONLY);
    CHECK(FileClearAttributes(const_cast<char*>("Data\\ReadOnly.bin")));
    CHECK(!(FileGetAttributes(const_cast<char*>("Data\\ReadOnly.bin")) & FILE_ATTRIBUTES_READONLY));
    file = open("Data\\ReadOnly.bin", FILE_ACCESS_WRITE | FILE_OPEN_EXISTING);
    CHECK(file);
    write_bytes(file, "unlocked");
    FileClose(file);
    CHECK(contents(assets / "data" / "ReadOnly.bin") == "locked");
    CHECK(!FileOpenHost(assets / "data" / "ReadOnly.bin", FILE_ACCESS_WRITE));
    file = open("D:/levels/levels.slf");
    CHECK(file && read_bytes(file, 10) == "disc bytes");
    FileClose(file);
    for (UINT32 mode : {FILE_OPEN_EXISTING, FILE_CREATE_ALWAYS, FILE_OPEN_ALWAYS, FILE_ACCESS_APPEND})
        CHECK(!open("D:\\Levels\\LEVELS.SLF", FILE_ACCESS_WRITE | mode));
    CHECK(!FileClearAttributes(const_cast<char*>("D:\\Levels\\LEVELS.SLF")));
    CHECK(!FileDelete(const_cast<char*>("D:\\Levels\\LEVELS.SLF")));

    char saves[] = "sAVES", characters[] = "Saves\\Characters";
    CHECK(DirectoryExists(saves) && MakeFileManDirectory(saves));
    CHECK(MakeFileManDirectory(characters) && DirectoryExists(characters));
    CHECK(FileGetAttributes(saves) & FILE_ATTRIBUTES_DIRECTORY);
    file = open("Saves\\CurrentGame.SAV");
    CHECK(file && read_bytes(file, 9) == "user save");
    FileClose(file);
    CHECK(!open("Saves\\currentgame.sav", FILE_ACCESS_WRITE | FILE_CREATE_NEW));
    fs::permissions(user / "Saves" / "CurrentGame.SAV", fs::perms::owner_read,
                    fs::perm_options::replace);
    CHECK(FileClearAttributes(const_cast<char*>("Saves\\CurrentGame.SAV")));
    CHECK((fs::status(user / "Saves" / "CurrentGame.SAV").permissions() & fs::perms::owner_write) !=
          fs::perms::none);
    CHECK(contents(user / "Saves" / "CurrentGame.SAV") == "user save");
    CHECK(!open("Saves\\missing.sav", FILE_ACCESS_WRITE | FILE_TRUNCATE_EXISTING));
    file = open("Saves\\SGP.SAV", FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS | FILE_TRUNCATE_EXISTING);
    CHECK(file);
    write_bytes(file, "sgp save");
    CHECK(!FileRead(file, &count, sizeof(count), nullptr));
    FileClose(file);
    file = open("Saves\\SGP.SAV", FILE_ACCESS_READWRITE | FILE_ACCESS_APPEND);
    CHECK(file && FileSeek(file, 0, FILE_SEEK_FROM_START));
    write_bytes(file, " appended");
    FileClose(file);
    CHECK(contents(user / "Saves" / "SGP.SAV") == "sgp save appended");
    file = open("Saves\\Installed.SAV", FILE_ACCESS_READWRITE | FILE_ACCESS_APPEND);
    CHECK(file);
    write_bytes(file, "+user");
    FileClose(file);
    CHECK(contents(assets / "Saves" / "Installed.SAV") == "installed save");
    CHECK(contents(user / "Saves" / "Installed.SAV") == "installed save+user");
    file = open("Saves\\SGP.SAV", FILE_ACCESS_WRITE | FILE_TRUNCATE_EXISTING);
    CHECK(file && FileGetSize(file) == 0);
    write_bytes(file, "replacement");
    FileClose(file);
    file = open("Saves\\SGP.SAV", FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS);
    CHECK(file && FileGetSize(file) == 0);
    FileClose(file);
    CHECK(contents(user / "Saves" / "CurrentGame.SAV") == "user save");

    CHECK(FileCopy(const_cast<char*>("Saves\\CurrentGame.SAV"),
                   const_cast<char*>("Saves\\Backup.SAV"), TRUE));
    CHECK(!FileCopy(const_cast<char*>("Saves\\CurrentGame.SAV"),
                    const_cast<char*>("Saves\\backup.sav"), TRUE));
    CHECK(!FileCopy(const_cast<char*>("Saves\\CurrentGame.SAV"),
                    const_cast<char*>("Saves\\currentgame.sav"), FALSE));
    CHECK(!FileCopy(const_cast<char*>("missing.sav"),
                    const_cast<char*>("Saves\\Backup.SAV"), FALSE));
    CHECK(contents(user / "Saves" / "Backup.SAV") == "user save");
    CHECK(FileCopy(const_cast<char*>("Data\\MixedCase.BIN"),
                   const_cast<char*>("Saves\\Backup.SAV"), FALSE));
    CHECK(contents(user / "Saves" / "Backup.SAV") == "retail bytes");
    file = open("Saves\\Temporary.SAV", FILE_ACCESS_WRITE | FILE_CREATE_NEW, true);
    CHECK(file);
    write_bytes(file, "temporary");
    CHECK(w8_native::change_directory("Data") == 0);
    FileClose(file);
    CHECK(w8_native::change_directory("C:\\") == 0);
    CHECK(!FileExists(const_cast<char*>("Saves\\Temporary.SAV")));
    CHECK(!open("Saves\\SGP.SAV", FILE_ACCESS_WRITE | FILE_ACCESS_APPEND | FILE_TRUNCATE_EXISTING));
    CHECK(!open("Saves\\SGP.SAV", FILE_ACCESS_READ | FILE_ACCESS_APPEND));
    file = open("Saves\\NewAppend.SAV", FILE_ACCESS_WRITE | FILE_ACCESS_APPEND | FILE_CREATE_NEW);
    CHECK(file);
    write_bytes(file, "first");
    CHECK(FileSeek(file, 0, FILE_SEEK_FROM_START));
    write_bytes(file, "+second");
    FileClose(file);
    CHECK(contents(user / "Saves" / "NewAppend.SAV") == "first+second");
    file = open("Saves\\Sparse.SAV", FILE_ACCESS_READWRITE | FILE_CREATE_NEW);
    CHECK(file && FileSeek(file, UINT32_MAX, FILE_SEEK_FROM_START));
    CHECK(FileGetPos(file) == -1); // SGP exposes signed 32-bit positions.
    write_bytes(file, "x");
    CHECK(FileGetSize(file) == 0); // No silent narrowing of a >32-bit size.
    CHECK(FileSeek(file, UINT32_MAX, FILE_SEEK_FROM_END) && FileGetPos(file) == 1);
    FileClose(file);
    CHECK(FileDelete(const_cast<char*>("Saves\\Sparse.SAV")));

    const auto unicode = root / wiz8::path_from_utf8("zażółć-雪.bin");
    fixture(unicode, "unicode import");
    CHECK(wiz8::path_from_utf8(wiz8::path_to_utf8(unicode)) == unicode);
    file = FileOpenHost(unicode, FILE_ACCESS_READ);
    CHECK(file && read_bytes(file, 14) == "unicode import");
    FileClose(file);
    const auto host_text = wiz8::path_to_utf8(unicode);
    CHECK(!open(host_text.c_str()));
    CHECK(!open("C:\\..\\outside.sav", FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS));
    CHECK(!open("Saves\\..\\..\\outside.sav", FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS));
    CHECK(!open("Saves\\part:stream", FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS));
    const auto outside = root / "outside";
    fixture(outside / "untouched.sav", "outside");
    fs::create_directory_symlink(outside, user / "Escape");
    fs::create_directory_symlink(assets, user / "AssetLink");
    CHECK(!open("Escape\\untouched.sav", FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS));
    CHECK(!open("AssetLink\\data\\MixedCase.BIN", FILE_ACCESS_WRITE));
    CHECK(!FileDelete(const_cast<char*>("AssetLink\\data\\MixedCase.BIN")));
    CHECK(!FileCopy(const_cast<char*>("Saves\\CurrentGame.SAV"),
                    const_cast<char*>("Escape\\untouched.sav"), FALSE));
    CHECK(contents(outside / "untouched.sav") == "outside");

    char pattern[] = "dAtA\\*.*", no_match[] = "Data\\*.absent";
    const auto names = scan(pattern);
    CHECK(names.size() == 5 && names.count("NoExtension") && names.count("MixedCase.BIN"));
    CHECK(scan(no_match).empty());
    GETFILESTRUCT bad{};
    bad.iFindHandle = 999;
    CHECK(!GetFileNext(&bad));
    GetFileClose(&bad);
    std::vector<GETFILESTRUCT> active(20);
    for (auto& search : active)
        CHECK(GetFileFirst(pattern, &search));
    CHECK(!GetFileFirst(pattern, &bad));
    for (auto& search : active)
        GetFileClose(&search);
    CHECK(GetFileFirst(pattern, &bad));
    CHECK(w8_native::change_directory("Saves") == 0);
    CHECK(GetFileNext(&bad));
    CHECK(w8_native::change_directory("C:\\") == 0);
    GetFileClose(&bad);
    char directory[512];
    CHECK(GetExecutableDirectory(directory) && directory == configured.assets);

    // Renderer callers share the same namespace, without inspecting their adapters.
    {
        srBinIFStream input("DATA\\mixedcase.bin");
        CHECK(input.isOpen());
        input.read(directory, 12);
        CHECK(!memcmp(directory, "retail bytes", 12));
        input.close();
        srBinOFStream output("Saves\\Renderer.SAV");
        CHECK(output.isOpen());
        output.write("renderer save", 13);
        output.close();
        CHECK(contents(user / "Saves" / "Renderer.SAV") == "renderer save");
    }
    srStringTable table;
    CHECK(srSystem::scanFiles(table, "Data\\*.SLF") == 1);

    CHECK(FileExists(const_cast<char*>("Data\\archiveonly.bin")));
    CHECK(!FileExistsNoDB(const_cast<char*>("Data\\archiveonly.bin")));
    HWFILE archived = open("Data\\archiveonly.bin");
    CHECK(archived && FileGetSize(archived) == 8);
    // Library reads are entry-bounded: an oversized request is rejected whole.
    CHECK(read_bytes(archived, 9, false).empty() && FileGetPos(archived) == 0);
    CHECK(read_bytes(archived, 8) == "archive!");
    CHECK(FileCheckEndOfFile(archived));
    CHECK(FileSeek(archived, 3, FILE_SEEK_FROM_START));
    CHECK(read_bytes(archived, 5) == "hive!");
    CHECK(!FileWrite(archived, const_cast<char*>("x"), 1, &count) && count == 0);
    SGP_FILETIME creation{}, accessed{}, modified{};
    CHECK(GetFileManFileTime(archived, &creation, &accessed, &modified));
    CHECK(creation.dwLowDateTime == 0 && creation.dwHighDateTime == 0);
    CHECK(modified.dwLowDateTime == 0xd53e8001u && modified.dwHighDateTime == 0x019db1deu);
    FileClose(archived);
    file = open("Data\\Override.bin");
    CHECK(file && read_bytes(file, 6) == "loose!");
    FileClose(file);

    // Keep archive corruption negatives at the real SLF initializer.
    for (std::size_t length : {std::size_t(20), archive_bytes.size() - 1})
    {
        fixture(assets / "data" / "Truncated.slf", archive_bytes.substr(0, length));
        LibraryHeaderStruct library{};
        char path[] = "Data\\Truncated.slf";
        CHECK(!InitializeLibrary(path, &library, FALSE));
    }
    auto invalid_entry = archive_bytes;
    DIRENTRY invalid{};
    memcpy(&invalid, invalid_entry.data() + sizeof(LIBHEADER) + 15, sizeof(invalid));
    invalid.uiLength = UINT32(-1);
    memcpy(invalid_entry.data() + sizeof(LIBHEADER) + 15, &invalid, sizeof(invalid));
    fixture(assets / "data" / "InvalidEntry.slf", invalid_entry);
    LibraryHeaderStruct library{};
    char invalid_path[] = "Data\\InvalidEntry.slf";
    CHECK(!InitializeLibrary(invalid_path, &library, FALSE));
    archived = open("Data\\archiveonly.bin");
    CHECK(archived);
    fs::resize_file(assets / "data" / "DATA.SLF", sizeof(LIBHEADER) + 3);
    CHECK(read_bytes(archived, 4, false).empty() && FileGetPos(archived) == 0);
    FileClose(archived);
    fixture(assets / "data" / "DATA.SLF", archive_bytes);

    CHECK(!GetFileManFileTime(0, &creation, &accessed, &modified));
    CHECK(creation.dwLowDateTime == 0 && modified.dwHighDateTime == 0);
    // Grow slots past the initial allocation and close a sparse set safely.
    std::vector<HWFILE> readers;
    for (unsigned i = 0; i < 45; ++i)
    {
        file = open("Saves\\CurrentGame.SAV");
        CHECK(file);
        readers.push_back(file);
    }
    for (std::size_t i = 0; i < readers.size(); i += 2)
        FileClose(readers[i]);
    ShutdownFileManager();
    CHECK(gFileDataBase.RealFiles.iNumFilesOpen == 0);
    CHECK(ShutDownFileDatabase());
    CHECK(InitializeFileDatabase());
    file = open("Saves\\DatabaseTemporary.SAV", FILE_ACCESS_WRITE | FILE_CREATE_NEW, true);
    CHECK(file);
    write_bytes(file, "delete on database shutdown");
    CHECK(ShutDownFileDatabase());
    CHECK(!FileExistsNoDB(const_cast<char*>("Saves\\DatabaseTemporary.SAV")));
    ShutdownFileManager();
    CHECK(contents(user / "Saves" / "CurrentGame.SAV") == "user save");
    CHECK(contents(assets / "data" / "MixedCase.BIN") == "retail bytes");
    CHECK(contents(assets / "data" / "ReadOnly.bin") == "locked");
    fs::permissions(assets / "data" / "ReadOnly.bin", fs::perms::owner_write, fs::perm_options::add);
    fs::remove_all(root);
    puts("ok: game streams, immutable assets, user overlays, UTF-8 imports and bounded SLF records");
}
