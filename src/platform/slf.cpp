#include "slf.h"
#include "wiz8/slf.h"
#include "path_resolver.h"
#include "wiz8/asset_paths.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <SDL3/SDL_log.h>

namespace wiz8
{
namespace
{
struct Entry { std::string name; AssetEntry file; };
struct Archive
{
    std::string prefix;
    bool patch;
    std::vector<Entry> entries;
};
std::mutex mutex;
std::vector<Archive> archives;

template<std::size_t N>
std::string_view disk_text(const char (&text)[N])
{
    const auto* end = static_cast<const char*>(std::memchr(text, 0, N));
    if (!end) throw std::runtime_error("unterminated SLF text field");
    return {text, static_cast<std::size_t>(end - text)};
}
std::string key(std::string_view text)
{
    std::string result(text);
    for (auto& value : result)
        if (value == '/') value = '\\';
        else if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
    return result;
}
}

void mount_slf(std::string_view game_path, bool patch)
{
    const auto resolved = w8_native::resolve_path(game_path);
    auto file = open_host_file(resolved.readable);
    const auto size = file->size();
    SlfHeader header{};
    file->read_exact(&header, sizeof(header));
    disk_text(header.sLibName);
    Archive archive{key(disk_text(header.sPathToLibrary)), patch, {}};
    if (!archive.prefix.empty() && archive.prefix.back() != '\\') archive.prefix += '\\';
    if (header.iEntries < 0 || header.iUsed < 0 || header.iUsed > header.iEntries ||
        header.iUsed > std::numeric_limits<std::uint16_t>::max())
        throw std::runtime_error("invalid SLF entry count");
    const auto table_size = std::uint64_t(header.iEntries) * sizeof(SlfEntry);
    if (size < static_cast<std::int64_t>(sizeof(header)) ||
        table_size > std::uint64_t(size) - sizeof(header))
        throw std::runtime_error("SLF directory exceeds archive");
    const auto table_start = std::uint64_t(size) - table_size;
    file->seek(table_start, SeekOrigin::begin);
    std::array<SlfEntry, 64> buffer;
    for (std::uint32_t remaining = header.iEntries; remaining;)
    {
        const auto count = std::min<std::size_t>(remaining, buffer.size());
        file->read_exact(buffer.data(), count * sizeof(SlfEntry));
        remaining -= count;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& entry = buffer[i];
            if (entry.ubState != 0) continue;
            const auto name = disk_text(entry.sFileName);
            if (name.empty() || entry.uiOffset < sizeof(header) || entry.uiOffset > table_start ||
                entry.uiLength > table_start - entry.uiOffset)
                throw std::runtime_error("invalid SLF entry extent");
            archive.entries.push_back({key(name), {file->physical_path(), entry.uiOffset,
                                                  entry.uiLength, entry.sFileTime}});
        }
    }
    if (archive.entries.size() != static_cast<std::size_t>(header.iUsed))
        throw std::runtime_error("inconsistent SLF live entry count");
    std::sort(archive.entries.begin(), archive.entries.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    for (std::size_t i = 1; i < archive.entries.size(); ++i)
        if (archive.entries[i - 1].name == archive.entries[i].name)
            throw std::runtime_error("duplicate SLF entry name");
    std::lock_guard lock(mutex);
    archives.push_back(std::move(archive));
}
void clear_asset_archives()
{
    std::lock_guard lock(mutex);
    archives.clear();
}
void refresh_asset_archives()
{
    clear_asset_archives();
    // GLOBAL: WIZ8 0x006000c8
    constexpr const char* gGameLibaries[] = {"Data\\Data.slf", "Data\\Sound\\Sound.slf",
        "Data\\Sound\\Monsters\\MonsterSound.slf", "Data\\Music\\Music.slf",
        "Data\\Monsters\\Monsters.slf", "Levels\\Levels.slf"};
    for (const auto* archive : gGameLibaries)
    {
        for (const auto* drive : {"C:\\", "D:\\", "E:\\", "F:\\"})
        {
            const std::string path = std::string(drive) + archive;
            try
            {
                const auto status = file_status(path);
                if (status && !status->archived && status->info.type == SDL_PATHTYPE_FILE)
                {
                    mount_slf(path);
                    break;
                }
            }
            catch (const std::exception& error)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", path.c_str(), error.what());
            }
        }
    }
    for (int patch = 0; patch < 50; ++patch)
    {
        char path[32];
        SDL_snprintf(path, sizeof(path), "C:\\Patches\\Patch.%03d", patch);
        try
        {
            const auto status = file_status(path);
            if (status && !status->archived && status->info.type == SDL_PATHTYPE_FILE)
                mount_slf(path, true);
        }
        catch (const std::exception& error)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", path, error.what());
        }
    }
}
std::optional<AssetEntry> archive_entry(std::string_view game_path)
{
    // Resolve first so traversal, virtual cwd, discs and invalid names follow
    // the same policy as loose assets. Explicit host paths never enter here.
    w8_native::resolve_path(game_path);
    const auto name = key(w8_native::full_path(std::string(game_path).c_str()));
    auto lookup = std::string_view(name);
    if (lookup.size() >= 2 && lookup[1] == ':') lookup.remove_prefix(2);
    while (lookup.starts_with('\\')) lookup.remove_prefix(1);
    std::lock_guard lock(mutex);
    const Archive* best = nullptr;
    const Entry* found = nullptr;
    for (const auto& archive : archives)
    {
        if (!lookup.starts_with(archive.prefix)) continue;
        const auto relative = lookup.substr(archive.prefix.size());
        const auto entry = std::lower_bound(archive.entries.begin(), archive.entries.end(), relative,
            [](const Entry& a, std::string_view b) { return a.name < b; });
        if (entry == archive.entries.end() || entry->name != relative) continue;
        if (!best || archive.patch || (!best->patch && archive.prefix.size() > best->prefix.size()))
        {
            best = &archive;
            found = &*entry;
        }
    }
    return found ? std::optional(found->file) : std::nullopt;
}
} // namespace wiz8
