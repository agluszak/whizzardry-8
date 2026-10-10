/* Exercises the native policy with isolated roots, then the recovered SGP SLF
   and save-file callers. This is I/O integration, not a native game launch. */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <ctime>
#include "temporary_directory.h"

#include "compat/platform.h"
#include "platform_paths.h"
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "surrender/srSystem.h"
#include "surrender/srBinFStream.h"
#include "surrender/srStringTable.h"

namespace fs = std::filesystem;
void reset_timezone()
{
#ifdef _WIN32
    _tzset();
#else
    tzset();
#endif
}
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "line %d: %s (Win32 error %u, errno %d)\n", __LINE__, #expression,     \
                    W8GetLastError(), errno);                                                      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

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
static HANDLE open_file(const char* path, DWORD access, DWORD disposition = OPEN_EXISTING,
                        DWORD share = FILE_SHARE_READ)
{
    return W8CreateFile(path, access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
}
static void write_bytes(HANDLE file, const std::string& text)
{
    DWORD written;
    CHECK(W8WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr));
    CHECK(written == text.size());
}
static std::string read_bytes(HANDLE file, DWORD size)
{
    std::string result(size, '\0');
    DWORD count;
    CHECK(W8ReadFile(file, result.data(), size, &count, nullptr));
    result.resize(count);
    return result;
}
static void make_slf(const fs::path& path)
{
    /* These are the on-disk sizes/offsets used by InitializeLibrary. */
    static_assert(sizeof(LIBHEADER) == 532);
    static_assert(sizeof(DIRENTRY) == 280);
    LIBHEADER header{};
    strcpy(header.sLibName, "Data.slf");
    strcpy(header.sPathToLibrary, "Data\\");
    header.iEntries = header.iUsed = 2;
    header.iVersion = 0x200;
    DIRENTRY entries[2]{};
    strcpy(entries[0].sFileName, "ArchiveOnly.bin");
    entries[0].uiOffset = sizeof(header);
    entries[0].uiLength = 8;
    strcpy(entries[1].sFileName, "Override.bin");
    entries[1].uiOffset = sizeof(header) + 8;
    entries[1].uiLength = 7;
    std::string bytes(reinterpret_cast<char*>(&header), sizeof(header));
    bytes += "archive!";
    bytes += "packed!";
    bytes.append(reinterpret_cast<char*>(entries), sizeof(entries));
    fixture(path, bytes);
}
int main()
{
    const auto temporary = make_temporary_directory("wiz8-files");
    fs::path root(temporary), assets = root / "assets", user = root / "user", disc = root / "disc";
    fixture(assets / "data" / "MixedCase.BIN", "retail bytes");
    fixture(assets / "data" / "Override.bin", "loose!");
    fixture(assets / "data" / "NoExtension", "plain");
    fixture(assets / "data" / "ReadOnly.bin", "locked");
    fs::permissions(assets / "data" / "ReadOnly.bin", fs::perms::owner_read |
                    fs::perms::group_read | fs::perms::others_read);
    fixture(disc / "Levels" / "LEVELS.SLF", "disc archive");
    make_slf(assets / "data" / "DATA.SLF");
    CHECK(W8SetEnvironmentVariable("WIZ8_ASSET_ROOT", assets.string().c_str()));
    CHECK(W8SetEnvironmentVariable("WIZ8_USER_ROOT", user.string().c_str()));
    CHECK(W8SetEnvironmentVariable("WIZ8_CD1_ROOT", disc.string().c_str()));
    const auto configured = w8_native::path_roots();
    CHECK(configured.assets == fs::weakly_canonical(assets).generic_string());
    CHECK(configured.user == fs::weakly_canonical(user).generic_string());
    CHECK(configured.discs[0] == fs::weakly_canonical(disc).generic_string());
    w8_native::configure_paths({assets.string(), user.string(), {disc.string(), "", ""}});

    HANDLE file = open_file("DATA\\mixedcase.bin", GENERIC_READ);
    CHECK(file != INVALID_HANDLE_VALUE);
    CHECK(W8GetFileSize(file, nullptr) == 12);
    CHECK(read_bytes(file, 64) == "retail bytes");
    CHECK(read_bytes(file, 64).empty());
    CHECK(W8SetFilePointer(file, -5, nullptr, FILE_END) == 7);
    CHECK(read_bytes(file, 5) == "bytes");
    CHECK(W8SetFilePointer(file, -99, nullptr, FILE_CURRENT) == INVALID_SET_FILE_POINTER);
    CHECK(W8GetLastError() == ERROR_NEGATIVE_SEEK);
    CHECK(W8SetFilePointer(file, 0, nullptr, FILE_CURRENT) == 12);
    CHECK(open_file("data\\MIXEDCASE.bin", GENERIC_WRITE) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_SHARING_VIOLATION);
    DWORD count = 99;
    CHECK(!W8WriteFile(file, "x", 1, &count, nullptr) && count == 0);
    CHECK(W8GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(W8CloseHandle(file));
    CHECK(!W8CloseHandle(file) && W8GetLastError() == ERROR_INVALID_HANDLE);

    /* Update a loose asset without changing the installed file. */
    file = open_file("data\\MixedCase.BIN", GENERIC_READ | GENERIC_WRITE);
    CHECK(file != INVALID_HANDLE_VALUE);
    write_bytes(file, "native");
    CHECK(W8SetFilePointer(file, 0, nullptr, FILE_BEGIN) == 0);
    CHECK(read_bytes(file, 12) == "native bytes");
    CHECK(W8CloseHandle(file));
    CHECK(contents(assets / "data" / "MixedCase.BIN") == "retail bytes");
    CHECK(open_file("Data\\ReadOnly.bin", GENERIC_WRITE) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(w8_chmod("Data\\ReadOnly.bin", _S_IREAD | _S_IWRITE) == 0);
    file = open_file("Data\\ReadOnly.bin", GENERIC_WRITE);
    CHECK(file != INVALID_HANDLE_VALUE);
    write_bytes(file, "unlocked");
    CHECK(W8CloseHandle(file));
    CHECK(contents(assets / "data" / "ReadOnly.bin") == "locked");
    CHECK(!W8DeleteFile("Data\\Override.bin") && W8GetLastError() == ERROR_ACCESS_DENIED);

    CHECK(W8CreateDirectory("Saves", nullptr));
    CHECK(!W8CreateDirectory("sAVES", nullptr) && W8GetLastError() == ERROR_ALREADY_EXISTS);
    file = open_file("Saves\\CurrentGame.SAV", GENERIC_READ | GENERIC_WRITE, CREATE_NEW, 0);
    CHECK(file != INVALID_HANDLE_VALUE && W8GetLastError() == ERROR_SUCCESS);
    write_bytes(file, "save payload");
    CHECK(!W8DeleteFile("saves\\currentgame.sav") && W8GetLastError() == ERROR_SHARING_VIOLATION);
    CHECK(W8CloseHandle(file));
    CHECK(open_file("SAVES\\currentgame.sav", GENERIC_WRITE, CREATE_NEW) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_FILE_EXISTS);
    file = open_file("Saves\\CurrentGame.SAV", GENERIC_READ, OPEN_ALWAYS);
    CHECK(file != INVALID_HANDLE_VALUE && W8GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(W8CloseHandle(file));
    CHECK(W8CopyFile("Saves\\CurrentGame.SAV", "Saves\\Backup.SAV", 1));
    CHECK(!W8CopyFile("Saves\\CurrentGame.SAV", "Saves\\backup.sav", 1));
    CHECK(W8GetLastError() == ERROR_FILE_EXISTS);
    CHECK(!W8MoveFile("Saves\\CurrentGame.SAV", "Saves\\Backup.SAV") &&
          W8GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(W8MoveFile("Saves\\Backup.SAV", "Saves\\CleanUp.SAV"));
    CHECK(w8_rename("Saves\\CleanUp.SAV", "Saves\\CurrentGame.SAV") == -1 && errno == EACCES);
    CHECK(w8_remove("Saves\\CurrentGame.SAV") == 0);
    CHECK(w8_rename("Saves\\CleanUp.SAV", "Saves\\CurrentGame.SAV") == 0);
    CHECK(contents(user / "Saves" / "CurrentGame.SAV") == "save payload");

    CHECK(open_file("missing\\save.sav", GENERIC_WRITE, CREATE_NEW) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_PATH_NOT_FOUND);
    CHECK(!W8CopyFile("Saves\\CurrentGame.SAV", "Saves\\CurrentGame.SAV", 0));
    CHECK(contents(user / "Saves" / "CurrentGame.SAV") == "save payload");
    fs::create_directory_symlink(assets, user / "AssetLink");
    CHECK(open_file("AssetLink\\data\\MixedCase.BIN", GENERIC_WRITE) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_ACCESS_DENIED);

    CHECK(!W8DeleteFile("AssetLink\\data\\MixedCase.BIN"));
    CHECK(W8GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(!W8MoveFile("AssetLink\\data\\MixedCase.BIN", "Saves\\MovedAsset.SAV"));
    CHECK(W8GetLastError() == ERROR_ACCESS_DENIED);

    /* Sparse offsets: 0xffffffff is also a valid successful return value. */
    file = open_file("Saves\\Sparse.SAV", GENERIC_READ | GENERIC_WRITE, CREATE_NEW);
    CHECK(file != INVALID_HANDLE_VALUE);
    LONG high = 0;
    CHECK(W8SetFilePointer(file, LONG(0xffffffffu), &high, FILE_BEGIN) == 0xffffffffu);
    CHECK(W8GetLastError() == ERROR_SUCCESS && high == 0);
    write_bytes(file, "x");
    DWORD upper;
    CHECK(W8GetFileSize(file, &upper) == 0 && upper == 1);
    CHECK(W8SetFilePointer(file, 0, nullptr, FILE_CURRENT) == INVALID_SET_FILE_POINTER);
    CHECK(W8GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(W8CloseHandle(file));
    CHECK(W8DeleteFile("Saves\\Sparse.SAV"));

    file = open_file("data\\DATA.SLF", GENERIC_READ);
    CHECK(file != INVALID_HANDLE_VALUE);
    HANDLE mapping = W8CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CHECK(mapping);
    CHECK(!W8MapViewOfFile(mapping, FILE_MAP_READ, 0, 1, 0));
    const char* view = static_cast<const char*>(W8MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    CHECK(view);
    CHECK(W8CloseHandle(mapping) && W8CloseHandle(file));
    CHECK(memcmp(view + sizeof(LIBHEADER), "archive!", 8) == 0);
    CHECK(W8UnmapViewOfFile(view));
    CHECK(!W8UnmapViewOfFile(view));

    WIN32_FIND_DATAA entry;
    HANDLE search = W8FindFirstFile("dAtA\\*.*", &entry);
    CHECK(search != INVALID_HANDLE_VALUE);
    std::set<std::string> names;
    do
    {
        CHECK(!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
        names.insert(entry.cFileName);
    } while (W8FindNextFile(search, &entry));
    CHECK(W8GetLastError() == ERROR_NO_MORE_FILES);
    CHECK(names.size() == 5 && names.count("NoExtension") == 1);
    CHECK(W8FindClose(search));
    CHECK(!W8FindNextFile(search, &entry) && W8GetLastError() == ERROR_INVALID_HANDLE);
    CHECK(W8FindFirstFile("Data\\*.absent", &entry) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_FILE_NOT_FOUND);

    /* Delete-on-close waits for the final sharing handle. */
    file = W8CreateFile("Saves\\Temporary.SAV", GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                        FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    HANDLE shared = open_file("Saves\\Temporary.SAV", GENERIC_READ, OPEN_EXISTING,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    CHECK(shared != INVALID_HANDLE_VALUE);
    CHECK(W8CloseHandle(file));
    CHECK(W8GetFileAttributes("Saves\\Temporary.SAV") != INVALID_FILE_ATTRIBUTES);
    CHECK(W8CloseHandle(shared));
    CHECK(W8GetFileAttributes("Saves\\Temporary.SAV") == INVALID_FILE_ATTRIBUTES);

    CHECK(w8_chdir("DATA") == 0);
    char path[512];
    CHECK(w8_getcwd(path, sizeof(path)) && !strcmp(path, "C:\\DATA"));
    FILE* stream = fopen("mixedcase.bin", "rb");
    CHECK(stream && fread(path, 1, 12, stream) == 12 && !memcmp(path, "native bytes", 12));
    CHECK(fclose(stream) == 0);
    CHECK(w8_chdir("..\\Saves") == 0);
    stream = fopen("LOG.TXT", "a+t");
    CHECK(stream && fputs("native log\n", stream) >= 0 && fclose(stream) == 0);
    CHECK(w8_chdir("C:\\") == 0);
    char drive[3], directory[256], name[256], extension[256];
    w8_splitpath("C:\\Data\\DATA.SLF", drive, directory, name, extension);
    CHECK(!strcmp(drive, "C:") && !strcmp(directory, "\\Data\\") && !strcmp(name, "DATA") &&
          !strcmp(extension, ".SLF"));

    CHECK(W8GetDriveType("C:\\") == DRIVE_FIXED);
    CHECK(W8GetDriveType("D:\\") == DRIVE_CDROM && W8GetDriveType("E:\\") == DRIVE_NO_ROOT_DIR);
    CHECK(W8GetLogicalDriveStrings(0, nullptr) == 9);
    CHECK(W8GetLogicalDriveStrings(sizeof(path), path) == 8);
    CHECK(!memcmp(path, "C:\\\0D:\\\0\0", 9));
    CHECK(
        W8GetVolumeInformation("D:\\", path, sizeof(path), nullptr, nullptr, nullptr, nullptr, 0));
    CHECK(!strcmp(path, "WIZ8_1"));
    file = open_file("D:\\levels\\levels.slf", GENERIC_READ);
    CHECK(file != INVALID_HANDLE_VALUE && read_bytes(file, 64) == "disc archive");
    CHECK(W8CloseHandle(file));
    CHECK(open_file("D:\\Levels\\LEVELS.SLF", GENERIC_WRITE) == INVALID_HANDLE_VALUE);
    CHECK(W8GetLastError() == ERROR_ACCESS_DENIED);
    DWORD sectors, bytes, free_count, total;
    CHECK(W8GetDiskFreeSpace("C:\\", &sectors, &bytes, &free_count, &total) && sectors && bytes &&
          total);
    CHECK(W8GetModuleFileName(nullptr, path, sizeof(path)) && !strncmp(path, "C:\\", 3));
    CHECK(W8SetEnvironmentVariable("WIZ8_TEST_VARIABLE", "abc"));
    CHECK(W8GetEnvironmentVariable("WIZ8_TEST_VARIABLE", nullptr, 0) == 4);
    CHECK(W8GetEnvironmentVariable("WIZ8_TEST_VARIABLE", path, sizeof(path)) == 3 &&
          !strcmp(path, "abc"));
    CHECK(W8SetEnvironmentVariable("WIZ8_TEST_VARIABLE", nullptr));
    CHECK(!W8GetEnvironmentVariable("WIZ8_TEST_VARIABLE", path, sizeof(path)) &&
          W8GetLastError() == ERROR_ENVVAR_NOT_FOUND);
    CHECK(W8SetEnvironmentVariable("TZ", "UTC0"));
    reset_timezone();
    FILETIME epoch{0xd53e8000u, 0x019db1deu}, local;
    SYSTEMTIME date;
    CHECK(W8FileTimeToSystemTime(&epoch, &date));
    CHECK(date.wYear == 1970 && date.wMonth == 1 && date.wDay == 1 && date.wDayOfWeek == 4);
    FILETIME origin{};
    CHECK(W8FileTimeToSystemTime(&origin, &date));
    CHECK(date.wYear == 1601 && date.wMonth == 1 && date.wDay == 1 && date.wDayOfWeek == 1);
    CHECK(W8FileTimeToSystemTime(&epoch, &date));
    CHECK(W8FileTimeToLocalFileTime(&epoch, &local) && !W8CompareFileTime(&epoch, &local));
    CHECK(W8GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, &date, "dddd',' MMMM dd',' yyyy", path,
                          sizeof(path)));
    CHECK(!strcmp(path, "Thursday, January 01, 1970"));
    CHECK(W8SetEnvironmentVariable("TZ", "EST5"));
    reset_timezone();
    CHECK(W8FileTimeToLocalFileTime(&epoch, &local));
    CHECK(W8FileTimeToSystemTime(&local, &date) && date.wYear == 1969 && date.wHour == 19);
    CHECK(W8SetEnvironmentVariable("TZ", "EST5EDT,M3.2.0,M11.1.0"));
    reset_timezone();
    CHECK(W8FileTimeToLocalFileTime(&epoch, &local));
    const time_t now = time(nullptr);
    tm current;
#ifdef _WIN32
    CHECK(localtime_s(&current, &now) == 0);
#else
    CHECK(localtime_r(&now, &current));
#endif
    uint64_t expected = (uint64_t(epoch.dwHighDateTime) << 32) | epoch.dwLowDateTime;
#ifdef _WIN32
    long timezone, dst_bias;
    CHECK(_get_timezone(&timezone) == 0 && _get_dstbias(&dst_bias) == 0);
    expected -= int64_t(timezone + (current.tm_isdst > 0 ? dst_bias : 0)) * 10000000;
#else
    expected += int64_t(current.tm_gmtoff) * 10000000;
#endif
    CHECK(local.dwLowDateTime == DWORD(expected) && local.dwHighDateTime == DWORD(expected >> 32));
    DWORD root_error = W8GetLastError();
    std::thread other(
        []
        {
            CHECK(!W8CloseHandle(INVALID_HANDLE_VALUE));
            CHECK(W8GetLastError() == ERROR_INVALID_HANDLE);
        });
    other.join();
    CHECK(W8GetLastError() == root_error);

    /* Renderer file opens and searches use the same virtual namespace. */
    {
        srBinIFStream renderer_input("DATA\\mixedcase.bin");
        CHECK(renderer_input.isOpen());
        renderer_input.read(path, 12);
        CHECK(!memcmp(path, "native bytes", 12));
        renderer_input.close();
        srBinOFStream renderer_output("Saves\\Renderer.SAV");
        CHECK(renderer_output.isOpen());
        renderer_output.write("renderer save", 13);
        renderer_output.close();
        CHECK(contents(user / "Saves" / "Renderer.SAV") == "renderer save");
    }
    srStringTable table;
    CHECK(srSystem::scanFiles(table, "Data\\*.SLF") == 1);
    CHECK(srSystem::fullPath(path, "Data\\..\\Saves\\CurrentGame.SAV", sizeof(path)));
    CHECK(!strcmp(path, "C:\\Saves\\CurrentGame.SAV"));

    /* Run the actual recovered reader, first mapped then via seek/read. */
    for (bool mapped : {true, false})
    {
        gGameLibaries[0].fMapFile = mapped;
        CHECK(InitializeFileDatabase());
        CHECK(gFileDataBase.pLibraries[0].fLibraryOpen);
        CHECK(bool(gFileDataBase.pLibraries[0].pFileMapping) == mapped);
        char archived[] = "Data\\archiveonly.bin";
        HWFILE archive = FileOpen(archived, FILE_ACCESS_READ, 0);
        CHECK(archive && FileGetSize(archive) == 8);
        UINT32 read;
        CHECK(FileRead(archive, path, 8, &read) && read == 8 && !memcmp(path, "archive!", 8));
        CHECK(FileSeek(archive, 3, FILE_SEEK_FROM_START));
        CHECK(FileRead(archive, path, 5, &read) && !memcmp(path, "hive!", 5));
        SGP_FILETIME creation, accessed, modified;
        /* The recovered mapped path rejects this query; preserve that behavior. */
        CHECK(bool(GetFileManFileTime(archive, &creation, &accessed, &modified)) == !mapped);
        FileClose(archive);
        char override_name[] = "Data\\Override.bin";
        HWFILE loose = FileOpen(override_name, FILE_ACCESS_READ, 0);
        CHECK(loose && FileGetSize(loose) == 6);
        CHECK(FileRead(loose, path, 6, &read) && !memcmp(path, "loose!", 6));
        FileClose(loose);
        char save_name[] = "Saves\\SGP.SAV";
        HWFILE save = FileOpen(save_name, FILE_ACCESS_WRITE | FILE_CREATE_ALWAYS, 0);
        CHECK(save && FileWrite(save, const_cast<char*>("sgp save"), 8, &read));
        FileClose(save);
        save = FileOpen(save_name, FILE_ACCESS_READ, 0);
        CHECK(save && FileRead(save, path, 8, &read) && !memcmp(path, "sgp save", 8));
        FileClose(save);
        CHECK(ShutDownFileDatabase());
    }
    CHECK(contents(assets / "data" / "MixedCase.BIN") == "retail bytes");
    CHECK(contents(assets / "data" / "ReadOnly.bin") == "locked");
    fs::permissions(assets / "data" / "ReadOnly.bin", fs::perms::owner_write,
                    fs::perm_options::add);
    fs::remove_all(root);
    puts("ok: native paths, file handles, mapped/streamed SLF, loose overrides and SGP saves");
}
