#include "platform_paths.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <cerrno>
#include <SDL3/SDL_filesystem.h>

namespace fs = std::filesystem;
namespace w8_native
{
namespace
{
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
    const char* value = getenv(name);
    return value != nullptr ? value : "";
}
std::string absolute(const std::string& path)
{
    std::error_code error;
    fs::path full = fs::absolute(path, error);
    if (error)
        return "";
    const fs::path canonical = fs::weakly_canonical(full, error);
    if (error)
    {
        errno = error.value();
        return "";
    }
    return canonical.generic_string();
}
bool within(const std::string& path, const std::string& base)
{
    if (base.empty())
        return false;
#ifdef _WIN32
    const std::string name = folded(path), root = folded(base);
#else
    const std::string& name = path;
    const std::string& root = base;
#endif
    return name == root || name.rfind(root.back() == '/' ? root : root + "/", 0) == 0;
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
#ifdef _WIN32
    {
        const char* base = SDL_GetBasePath();
        roots.assets = base ? base : ".";
    }
#else
        roots.assets = ".";
#endif
    roots.assets = absolute(roots.assets);
    roots.user = environment("WIZ8_USER_ROOT");
    if (roots.user.empty())
    {
#ifdef _WIN32
        char* pref = SDL_GetPrefPath("Whizzardry", "whizzardry8");
        if (pref)
        {
            roots.user = pref;
            SDL_free(pref);
        }
#else
        roots.user = environment("XDG_DATA_HOME");
        if (roots.user.empty())
        {
            const std::string home = environment("HOME");
            if (home.empty())
            {
                /* No writable root is guessed when neither variable exists. */
                roots.user.clear();
            }
            else
            {
#ifdef __APPLE__
                roots.user = home + "/Library/Application Support";
#else
                roots.user = home + "/.local/share";
#endif
            }
        }
        if (!roots.user.empty())
            roots.user += "/whizzardry8";
#endif
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
#ifdef _WIN32
    /* Keep game C:\ paths virtual. C:/host paths in a configured root use
       that root's overlay policy; other existing host paths are imports. */
    if (text.size() > 2 && text[1] == ':' && text[2] == '/')
    {
        const std::string host = absolute(text);
        if (host.empty())
        {
            path.drive = -1;
            return path;
        }
        const std::string bases[] = {roots.assets, roots.user, roots.discs[0], roots.discs[1],
                                     roots.discs[2]};
        for (int i = 0; i < 5; ++i)
        {
            if (!within(host, bases[i]))
                continue;
            path.drive = i < 2 ? 0 : i - 1;
            const size_t prefix = bases[i].size() + (bases[i].back() == '/' ? 0 : 1);
            path.relative = folded(host) == folded(bases[i]) ? "" : host.substr(prefix);
            return path;
        }
        std::error_code error;
        const fs::path parent = fs::path(host).parent_path();
        if (fs::exists(host, error) ||
            (parent != parent.root_path() && fs::exists(parent, error)))
        {
            path.physical = host;
            return path;
        }
    }
#endif
    /* Explicit POSIX paths are allowed for imports and renderer search paths.
       Paths within a configured root retain that root's write policy. */
    if (text[0] == '/')
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
            if (bases[i].empty())
                continue;
            if (text == bases[i] || text.compare(0, bases[i].size() + 1, bases[i] + "/") == 0)
            {
                path.drive = i < 2 ? 0 : i - 1;
                path.relative = text == bases[i] ? "" : text.substr(bases[i].size() + 1);
                return path;
            }
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
    /* Clamp '..' at a virtual drive root, as Win32 does. */
    std::vector<std::string> components;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('/', start);
        if (end == std::string::npos)
            end = text.size();
        std::string part = text.substr(start, end - start);
        if (part == "..")
        {
            if (!components.empty())
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
    fs::path cursor(root);
    for (const auto& component : fs::path(relative))
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
            const std::string name = it->path().filename().string();
            if (folded(name) == folded(component.string()))
                matches.push_back(name);
        }
        std::sort(matches.begin(), matches.end());
        cursor /= matches.empty() ? component.string() : matches.front();
    }
    return cursor.string();
}
std::string locate_physical(const std::string& path)
{
    const fs::path physical(path);
    return locate(physical.root_path().string(), physical.relative_path().generic_string());
}
bool exists(const std::string& path)
{
    std::error_code error;
    return !path.empty() && fs::exists(path, error);
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
std::string write_path(const char* input, bool preserve)
{
    std::lock_guard<std::recursive_mutex> guard(lock);
    Path path = parse(input);
    if (path.drive < 0)
        return "";
    if (!path.physical.empty())
        return locate_physical(path.physical);
    if (path.drive != 0 || roots.user.empty())
    {
        errno = EACCES;
        return "";
    }
    std::string destination = locate(roots.user, path.relative);
    const std::string source = read(path);
    if (!within(absolute(destination), roots.user))
    {
        errno = EACCES;
        return "";
    }
    std::error_code error;
    /* Materialize root and only parents that already exist in the virtual tree.
       This keeps CreateFile from silently creating arbitrary directories. */
    const fs::path parent = fs::path(path.relative).parent_path();
    Path parent_path = path;
    parent_path.relative = parent.string();
    if (!fs::is_directory(read(parent_path), error))
    {
        errno = ENOTDIR;
        return "";
    }
    fs::create_directories(fs::path(destination).parent_path(), error);
    if (error)
    {
        errno = error.value();
        return "";
    }
    if (preserve && source != destination && exists(source))
    {
        fs::copy_file(source, destination, fs::copy_options::none, error);
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
        for (fs::directory_iterator it(base, error), end; !error && it != end; it.increment(error))
            next.push_back(it->path().filename().string());
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
    if (!fs::is_directory(physical, error))
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
        return false;
    if (roots.user.empty())
        return true;
    const std::string source = read(path);
    return source != locate(roots.user, path.relative) || !within(absolute(source), roots.user);
}
} // namespace w8_native
