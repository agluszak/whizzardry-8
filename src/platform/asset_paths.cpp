#include <wiz8/asset_paths.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <cerrno>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

namespace fs = std::filesystem;
namespace w8_native
{
namespace
{
fs::path native_path(const std::string& text)
{
    return fs::u8path(text);
}
std::string utf8(const fs::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}
std::recursive_mutex lock;
Roots roots;
bool initialized = false;
std::string cwd = "C:\\";

std::string folded(std::string text)
{
    for (char& c : text)
    {
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    }
    return text;
}
std::string environment(const char* name)
{
    const char* value = SDL_getenv(name);
    return value != nullptr ? value : "";
}
std::string absolute(const std::string& path)
{
    std::error_code error;
    fs::path full = fs::absolute(native_path(path), error);
    if (error)
    {
        errno = error.value();
        return "";
    }
    const fs::path canonical = fs::weakly_canonical(full, error);
    if (error)
    {
        errno = error.value();
        return "";
    }
    return utf8(canonical);
}
bool ancestry(const std::string& path, const std::string& base, bool canonicalize)
{
    if (path.empty() || base.empty())
        return false;
    std::error_code error;
    const fs::path root = fs::weakly_canonical(native_path(base), error);
    if (error)
        return false;
    fs::path cursor = canonicalize ? fs::weakly_canonical(native_path(path), error)
                                  : fs::absolute(native_path(path), error).lexically_normal();
    if (error)
        return false;
    for (;;)
    {
        if (cursor == root || fs::equivalent(cursor, root, error))
            return true;
        const fs::path parent = cursor.parent_path();
        if (parent.empty() || parent == cursor)
            return false;
        cursor = parent;
        error.clear();
    }
}
bool within(const std::string& path, const std::string& base)
{
    return ancestry(path, base, true);
}
bool immutable(const std::string& path)
{
    for (const auto& root : {roots.assets, roots.discs[0], roots.discs[1], roots.discs[2]})
        if (within(path, root) || ancestry(path, root, false))
            return true;
    return false;
}
void validate_roots()
{
    for (const auto& base : {roots.assets, roots.discs[0], roots.discs[1], roots.discs[2]})
    {
        if (within(roots.user, base) || within(base, roots.user))
            roots.user.clear();
    }
}
void initialize()
{
    if (initialized)
        return;
    roots.assets = environment("WIZ8_ASSET_ROOT");
    if (roots.assets.empty())
    {
        const char* base = SDL_GetBasePath();
        roots.assets = base ? base : ".";
    }
    roots.assets = absolute(roots.assets);
    roots.user = environment("WIZ8_USER_ROOT");
    if (roots.user.empty())
    {
        char* pref = SDL_GetPrefPath("Whizzardry", "whizzardry8");
        if (pref)
        {
            roots.user = pref;
            SDL_free(pref);
            const std::string legacy = existing_legacy_user_root(roots.user);
            if (!legacy.empty())
                roots.user = legacy;
        }
    }
    if (!roots.user.empty())
        roots.user = absolute(roots.user);
    for (int i = 0; i < 3; ++i)
    {
        const std::string key = "WIZ8_CD" + std::to_string(i + 1) + "_ROOT";
        roots.discs[i] = environment(key.c_str());
        if (!roots.discs[i].empty())
            roots.discs[i] = absolute(roots.discs[i]);
    }
    validate_roots();
    initialized = true;
}
struct Path
{
    int drive = 0;
    std::string relative;
    std::string physical;
};
Path parse(const char* input)
{
    initialize();
    Path path;
    if (input == nullptr || *input == 0)
    {
        errno = ENOENT;
        path.drive = -1;
        return path;
    }
    std::string text(input);
    /* C: through F: are always virtual, including forward-slash spellings.
       Native drive paths use the explicit host entry points instead. */
    const bool drive_prefix = text.size() >= 2 && text[1] == ':';
    if (!drive_prefix && native_path(text).is_absolute())
    {
        text = absolute(text);
        if (text.empty())
        {
            path.drive = -1;
            return path;
        }
        const std::string bases[] = {roots.assets, roots.user, roots.discs[0], roots.discs[1],
                                     roots.discs[2]};
        for (int i = 0; i < 5; ++i)
        {
            if (!within(text, bases[i]))
                continue;
            path.drive = i < 2 ? 0 : i - 1;
            std::error_code error;
            path.relative = utf8(fs::relative(native_path(text), native_path(bases[i]), error));
            if (error)
                path.drive = -1;
            return path;
        }
        path.physical = text;
        return path;
    }
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.rfind("//", 0) == 0)
    {
        errno = ENOTSUP;
        path.drive = -1;
        return path;
    }
    if (text.size() >= 2 && text[1] == ':')
    {
        char drive = text[0];
        if (drive >= 'a' && drive <= 'z')
            drive -= 'a' - 'A';
        path.drive = drive - 'C';
        if (path.drive < 0 || path.drive > 3)
        {
            errno = ENOENT;
            path.drive = -1;
            return path;
        }
        text.erase(0, 2);
        /* Only C: has a process current directory; disc paths are rooted. */
        if (text.empty() || text[0] != '/')
        {
            if (path.drive == 0 && cwd[0] == 'C')
                text = cwd.substr(3) + "/" + text;
        }
    }
    else
    {
        path.drive = cwd[0] - 'C';
        if (text[0] != '/')
            text = cwd.substr(3) + "/" + text;
    }
    std::replace(text.begin(), text.end(), '\\', '/');
    /* A traversal outside a virtual root is never a writable alias. */
    std::vector<std::string> components;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('/', start);
        if (end == std::string::npos)
            end = text.size();
        std::string part = text.substr(start, end - start);
        // Embedded drive names and alternate streams are not game components.
        if (part.find(':') != std::string::npos)
        {
            errno = EINVAL;
            path.drive = -1;
            return path;
        }
        if (part == "..")
        {
            if (components.empty())
            {
                errno = EACCES;
                path.drive = -1;
                return path;
            }
            components.pop_back();
        }
        else if (!part.empty() && part != ".")
            components.push_back(part);
        start = end + 1;
    }
    for (const auto& part : components)
    {
        if (!path.relative.empty())
            path.relative += '/';
        path.relative += part;
    }
    return path;
}
/* Prefer exact spelling, then the lexically first ASCII case-insensitive match.
   Sorting also makes case collisions reproducible on case-sensitive hosts. */
std::string locate(const std::string& root, const std::string& relative)
{
    fs::path cursor = native_path(root);
    for (const auto& component : native_path(relative))
    {
        fs::path exact = cursor / component;
        std::error_code error;
        if (fs::exists(exact, error))
        {
            cursor = exact;
            continue;
        }
        std::vector<std::string> matches;
        for (fs::directory_iterator it(cursor, error), end; !error && it != end;
             it.increment(error))
        {
            const std::string name = utf8(it->path().filename());
            if (folded(name) == folded(utf8(component)))
                matches.push_back(name);
        }
        std::sort(matches.begin(), matches.end());
        cursor /= matches.empty() ? utf8(component) : matches.front();
    }
    return utf8(cursor);
}
std::string locate_physical(const std::string& path)
{
    const fs::path physical = native_path(path);
    return locate(utf8(physical.root_path()), utf8(physical.relative_path()));
}
bool exists(const std::string& path)
{
    std::error_code error;
    return !path.empty() && fs::exists(native_path(path), error);
}
std::string read(const Path& path)
{
    if (path.drive < 0)
        return "";
    if (!path.physical.empty())
        return locate_physical(path.physical);
    if (path.drive != 0)
    {
        const auto& root = roots.discs[path.drive - 1];
        if (root.empty())
        {
            errno = ENOENT;
            return "";
        }
        return locate(root, path.relative);
    }
    std::string user = roots.user.empty() ? "" : locate(roots.user, path.relative);
    if (exists(user))
        return user;
    if (roots.assets.empty())
    {
        errno = ENOENT;
        return "";
    }
    return locate(roots.assets, path.relative);
}
} // namespace

void configure_paths(const Roots& configuration)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    roots = configuration;
    roots.assets = absolute(roots.assets.empty() ? "." : roots.assets);
    if (!roots.user.empty())
        roots.user = absolute(roots.user);
    for (auto& disc : roots.discs)
        if (!disc.empty())
            disc = absolute(disc);
    /* Reject overlapping roots: an overlay must never write into assets. */
    validate_roots();
    initialized = true;
    cwd = "C:\\";
}
Roots path_roots()
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    initialize();
    return roots;
}
std::string full_path(const char* input)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    Path path = parse(input);
    if (path.drive < 0)
        return "";
    if (!path.physical.empty())
        return path.physical;
    std::string result(1, 'C' + path.drive);
    result += ":\\" + path.relative;
    std::replace(result.begin() + 3, result.end(), '/', '\\');
    return result;
}
std::string read_path(const char* input)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    return read(parse(input));
}
std::string existing_legacy_user_root(const std::string& preferred)
{
    fs::path app = native_path(preferred).lexically_normal();
    if (app.filename().empty())
        app = app.parent_path();
    const fs::path candidate = app.parent_path().parent_path() / "whizzardry8";
    std::error_code error;
    // Prefer the current root when both contain data; never strand newer saves.
    const bool preferred_has_data = fs::is_directory(app, error) && !fs::is_empty(app, error);
    if (!preferred_has_data && app.parent_path().filename() == "Whizzardry" &&
        fs::is_directory(candidate, error) && !fs::is_empty(candidate, error))
        return absolute(utf8(candidate));
    return "";
}
std::string host_read_path(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    initialize();
    if (path.empty())
    {
        errno = ENOENT;
        return "";
    }
    const std::string full = absolute(path);
    return full;
}
std::string host_write_path(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    initialize();
    const std::string destination = host_read_path(path);
    if (destination.empty() || immutable(path) || immutable(destination) ||
        (ancestry(path, roots.user, false) && !within(destination, roots.user)))
    {
        errno = EACCES;
        return "";
    }
    return destination;
}
std::string mutation_path(const char* input)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    Path path = parse(input);
    if (path.drive < 0)
        return "";
    if (!path.physical.empty())
        return host_write_path(path.physical);
    if (path.drive != 0 || roots.user.empty())
    {
        errno = EACCES;
        return "";
    }
    const std::string destination = locate(roots.user, path.relative);
    if (!within(destination, roots.user) || immutable(destination))
    {
        errno = EACCES;
        return "";
    }
    return destination;
}
std::string write_path(const char* input, bool preserve)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    Path path = parse(input);
    if (path.drive < 0)
        return "";
    if (!path.physical.empty())
        return host_write_path(path.physical);
    if (path.drive != 0 || roots.user.empty())
    {
        errno = EACCES;
        return "";
    }
    std::string destination = locate(roots.user, path.relative);
    const std::string source = read(path);
    if (!within(absolute(destination), roots.user) || immutable(destination))
    {
        errno = EACCES;
        return "";
    }
    std::error_code error;
    /* Materialize root and only parents that already exist in the virtual tree.
       This keeps CreateFile from silently creating arbitrary directories. */
    const fs::path parent = native_path(path.relative).parent_path();
    Path parent_path = path;
    parent_path.relative = utf8(parent);
    if (!fs::is_directory(native_path(read(parent_path)), error))
    {
        errno = ENOTDIR;
        return "";
    }
    fs::create_directories(native_path(destination).parent_path(), error);
    if (error)
    {
        errno = error.value();
        return "";
    }
    if (preserve && source != destination && exists(source))
    {
        fs::copy_file(native_path(source), native_path(destination), fs::copy_options::none, error);
        if (error)
        {
            errno = error.value();
            return "";
        }
        /* A copied read-only asset remains read-only until explicitly cleared. */
    }
    return destination;
}
std::vector<std::string> directory_entries(const char* input)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    Path path = parse(input);
    std::vector<std::string> names;
    std::vector<std::string> bases;
    if (path.drive < 0)
        return names;
    if (!path.physical.empty() || path.drive != 0)
        bases.push_back(read(path));
    else
    {
        if (!roots.user.empty())
            bases.push_back(locate(roots.user, path.relative));
        bases.push_back(locate(roots.assets, path.relative));
    }
    for (const auto& base : bases)
    {
        std::error_code error;
        std::vector<std::string> next;
        for (fs::directory_iterator it(native_path(base), error), end; !error && it != end; it.increment(error))
            next.push_back(utf8(it->path().filename()));
        std::sort(next.begin(), next.end());
        for (const auto& name : next)
        {
            bool duplicate = false;
            for (const auto& previous : names)
                if (folded(name) == folded(previous))
                    duplicate = true;
            if (!duplicate)
                names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}
int change_directory(const char* input)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    const std::string physical = read_path(input);
    std::error_code error;
    if (!fs::is_directory(native_path(physical), error))
    {
        errno = ENOENT;
        return -1;
    }
    const std::string full = full_path(input);
    /* Explicit external directories use the host CWD for host relative I/O. */
    if (full.empty() || !parse(input).physical.empty())
    {
        errno = ENOTSUP;
        return -1;
    }
    cwd = full;
    return 0;
}
std::string current_directory()
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    initialize();
    return cwd;
}
bool is_read_only_path(const char* input)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    Path path = parse(input);
    if (path.drive < 0 || path.drive != 0)
        return true;
    if (!path.physical.empty())
        return immutable(path.physical);
    if (roots.user.empty())
        return true;
    const std::string source = read(path);
    return source != locate(roots.user, path.relative) || !within(absolute(source), roots.user);
}
} // namespace w8_native
