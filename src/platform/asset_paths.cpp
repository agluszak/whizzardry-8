#include "wiz8/asset_paths.h"

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include "wiz8/filesystem.h"

namespace fs = std::filesystem;
namespace {
std::mutex roots_mutex;
w8_native::Roots roots;
bool initialized = false;

w8_native::Roots defaults()
{
    const char* assets = SDL_getenv("WIZ8_ASSET_ROOT");
    const char* user = SDL_getenv("WIZ8_USER_ROOT");
    w8_native::Roots result{assets ? assets : "", user ? user : ""};
    if (result.user.empty()) {
        char* pref = SDL_GetPrefPath("Whizzardry", "whizzardry8");
        if (!pref) throw std::runtime_error("cannot locate user directory");
        result.user = pref;
        SDL_free(pref);
    }
    return result;
}
w8_native::Roots canonical_roots(w8_native::Roots result)
{
    if (!result.assets.empty())
        result.assets = wiz8::path_to_utf8(
            fs::weakly_canonical(fs::absolute(wiz8::path_from_utf8(result.assets))));
    if (result.user.empty()) throw std::invalid_argument("user root is required");
    result.user =
        wiz8::path_to_utf8(fs::weakly_canonical(fs::absolute(wiz8::path_from_utf8(result.user))));
    if (!result.assets.empty()) {
        const auto a = wiz8::path_from_utf8(result.assets);
        const auto u = wiz8::path_from_utf8(result.user);
        const auto below = [](const fs::path& child, const fs::path& parent) {
            const auto relative = child.lexically_relative(parent);
            return !relative.empty() && *relative.begin() != "..";
        };
        if (below(a, u) || below(u, a))
            throw std::invalid_argument("asset and user roots must be separate");
    }
    return result;
}
fs::path relative_path(std::string_view name, bool lowercase)
{
    if (name.empty() || name.find('\0') != name.npos)
        throw std::invalid_argument("empty path or embedded NUL");
    std::string text(name);
    for (auto& c : text) {
        if (c == '\\')
            c = '/';
        else if (lowercase && c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    }
    if (text.front() == '/' || text.find(':') != text.npos)
        throw std::invalid_argument("expected a relative path");
    auto path = wiz8::path_from_utf8(text);
    for (const auto& part : path)
        if (part == "..") throw std::invalid_argument("parent traversal in relative path");
    return path.lexically_normal();
}
fs::path rooted(std::string_view root, std::string_view name, bool lowercase)
{
    if (root.empty())
        throw std::runtime_error("WIZ8_ASSET_ROOT must name a prepared asset directory");
    const auto base = wiz8::path_from_utf8(root);
    const auto path = base / relative_path(name, lowercase);
    // Do not allow a symlink in either tree to escape its explicit root.
    const auto relative = fs::weakly_canonical(path).lexically_relative(base);
    if (relative.empty() || *relative.begin() == "..")
        throw std::invalid_argument("path escapes its root");
    return path;
}
}
namespace w8_native {
void configure_paths(const Roots& configuration)
{
    std::lock_guard guard(roots_mutex);
    roots = canonical_roots(configuration);
    initialized = true;
}
Roots path_roots()
{
    std::lock_guard guard(roots_mutex);
    if (!initialized) {
        roots = canonical_roots(defaults());
        initialized = true;
    }
    return roots;
}
}
namespace wiz8 {
fs::path normalize_asset_path(std::string_view name)
{
    return relative_path(name, true);
}
fs::path asset_path(std::string_view name)
{
    return rooted(w8_native::path_roots().assets, name, true);
}
fs::path user_path(std::string_view name)
{
    return rooted(w8_native::path_roots().user, name, false);
}
}
