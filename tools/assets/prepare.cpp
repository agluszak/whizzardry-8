#include "prepare.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "slf.h"
#include "wiz8/asset_paths.h"
#include "wiz8/file_time.h"
#include "wiz8/filesystem.h"

namespace fs = std::filesystem;
namespace wiz8::preparation {
namespace {
static_assert(std::endian::native == std::endian::little, "SLF records are little endian");
template<std::size_t N>
std::string_view disk_text(const char (&text)[N])
{
    const auto* end = static_cast<const char*>(std::memchr(text, 0, N));
    if (!end) throw std::runtime_error("unterminated SLF text field");
    return {text, static_cast<std::size_t>(end - text)};
}
struct Entry {
    fs::path name;
    std::uint32_t offset, length;
    DiskFileTime modified;
};
struct Archive {
    fs::path path, prefix;
    std::vector<Entry> entries;
};
Archive read_archive(const fs::path& path)
{
    auto file = open_file(path);
    const auto size = file->size();
    SlfHeader header{};
    file->read_exact(&header, sizeof(header));
    disk_text(header.sLibName);
    const auto prefix = disk_text(header.sPathToLibrary);
    Archive archive{path, prefix.empty() ? fs::path{} : normalize_asset_path(prefix), {}};
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
    for (std::uint32_t remaining = header.iEntries; remaining;) {
        const auto count = std::min<std::size_t>(remaining, buffer.size());
        file->read_exact(buffer.data(), count * sizeof(SlfEntry));
        remaining -= count;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& entry = buffer[i];
            if (entry.ubState != 0) continue;
            const auto name = normalize_asset_path(disk_text(entry.sFileName));
            if (name == "." || name.filename().empty() || entry.uiOffset < sizeof(header) ||
                entry.uiOffset > table_start || entry.uiLength > table_start - entry.uiOffset)
                throw std::runtime_error("invalid SLF entry extent or filename");
            archive.entries.push_back(
                {archive.prefix / name, entry.uiOffset, entry.uiLength, entry.sFileTime});
        }
    }
    if (archive.entries.size() != static_cast<std::size_t>(header.iUsed))
        throw std::runtime_error("inconsistent SLF live entry count");
    std::sort(archive.entries.begin(), archive.entries.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    for (std::size_t i = 1; i < archive.entries.size(); ++i)
        if (archive.entries[i - 1].name == archive.entries[i].name)
            throw std::runtime_error("duplicate normalized SLF entry name");
    return archive;
}
void extract(const Archive& archive, const fs::path& output)
{
    auto input = open_file(archive.path);
    std::array<std::byte, 65536> buffer;
    const auto root = fs::weakly_canonical(output);
    for (const auto& entry : archive.entries) {
        const auto target = output / entry.name;
        const auto relative = fs::weakly_canonical(target).lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            throw std::runtime_error("SLF destination escapes output directory");
        fs::create_directories(target.parent_path());
        auto file = open_file(target, OpenMode::replace);
        input->seek(entry.offset, SeekOrigin::begin);
        for (std::uint32_t remaining = entry.length; remaining;) {
            const auto count = std::min<std::size_t>(remaining, buffer.size());
            input->read_exact(buffer.data(), count);
            file->write(buffer.data(), count);
            remaining -= count;
        }
        file->close();
        const auto time = SDL_TimeFromWindows(entry.modified.low, entry.modified.high);
        fs::last_write_time(target, fs::file_time_type::clock::from_sys(
                                        std::chrono::sys_time<std::chrono::nanoseconds>(
                                            std::chrono::nanoseconds(time))));
    }
}
using Tree = std::map<std::string, fs::path>;
Tree index_tree(const fs::path& source)
{
    Tree tree;
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_symlink())
            throw std::runtime_error("symlink in asset source: " + path_to_utf8(entry.path()));
        if (!entry.is_regular_file()) continue;
        const auto generic =
            normalize_asset_path(path_to_utf8(entry.path().lexically_relative(source)))
                .generic_u8string();
        const std::string name(generic.begin(), generic.end());
        if (!tree.emplace(name, entry.path()).second)
            throw std::runtime_error("case collision in asset source: " + name);
    }
    return tree;
}
}
void extract_slf(const fs::path& archive, const fs::path& output)
{
    extract(read_archive(archive), output);
}
void prepare_assets(std::span<const fs::path> sources, const fs::path& output)
{
    if (sources.empty() || fs::exists(output))
        throw std::invalid_argument("provide source directories and a new output directory");
    const auto destination = fs::weakly_canonical(fs::absolute(output));
    std::vector<Tree> trees;
    for (const auto& source : sources) {
        const auto relative = destination.lexically_relative(fs::canonical(source));
        if (!relative.empty() && *relative.begin() != "..")
            throw std::invalid_argument("output must be outside source directories");
        trees.push_back(index_tree(source));
    }
    const auto find = [&](const std::string& name) -> fs::path {
        for (const auto& tree : trees)
            if (const auto it = tree.find(name); it != tree.end()) return it->second;
        return {};
    };
    std::vector<Archive> bases, patches;
    for (const auto* name :
         {"data/data.slf", "data/sound/sound.slf", "data/sound/monsters/monstersound.slf",
          "data/music/music.slf", "data/monsters/monsters.slf", "levels/levels.slf"})
        if (const auto path = find(name); !path.empty()) bases.push_back(read_archive(path));
    // More specific library prefixes override less specific bases; later ties win.
    std::stable_sort(bases.begin(), bases.end(), [](const auto& a, const auto& b) {
        return path_to_utf8(a.prefix).size() < path_to_utf8(b.prefix).size();
    });
    for (int n = 0; n < 50; ++n) {
        auto name = std::string("patches/patch.") + char('0' + n / 100) + char('0' + n / 10 % 10) +
                    char('0' + n % 10);
        if (const auto path = find(name); !path.empty()) patches.push_back(read_archive(path));
    }
    fs::create_directories(output);
    try {
        for (const auto& archive : bases) extract(archive, output);
        for (const auto& archive : patches) extract(archive, output);
        // Installed loose files win over CDs, and all loose files win over archives.
        for (auto tree = trees.rbegin(); tree != trees.rend(); ++tree)
            for (const auto& [name, path] : *tree) {
                const auto normalized = path_from_utf8(name);
                if (normalized.extension() == ".slf" || name.starts_with("patches/") ||
                    name == "cd.rom" || normalized.extension() == ".sav" || name == "wiz8.cfg" ||
                    name == "3dvideo.cfg" || name.starts_with("config/"))
                    continue;
                const auto target = output / normalized;
                fs::create_directories(target.parent_path());
                fs::copy_file(path, target, fs::copy_options::overwrite_existing);
                fs::permissions(target, fs::perms::owner_write, fs::perm_options::add);
                fs::last_write_time(target, fs::last_write_time(path));
            }
    } catch (...) {
        fs::remove_all(output);
        throw;
    }
}
}
