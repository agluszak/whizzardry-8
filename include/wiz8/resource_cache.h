#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

/* Name-keyed lookup of live shared resources. The cache never keeps a resource
   alive: it is released with its last owner. A newer resource replaces the
   entry for its name. Names come from retail data written for a Windows file
   system, so they match without regard to ASCII case. */
template<class T> class ResourceCache {
public:
    std::shared_ptr<T> find(std::string_view name) const
    {
        auto found = entries.find(name);
        return found == entries.end() ? nullptr : found->second.lock();
    }

    void insert(std::string name, const std::shared_ptr<T>& resource)
    {
        entries.insert_or_assign(std::move(name), resource);
    }

    void erase(std::string_view name) { entries.erase(name); }

private:
    static char fold(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const noexcept
        {
            std::size_t hash = 0;
            for (char c : name) hash = hash * 31 + static_cast<unsigned char>(fold(c));
            return hash;
        }
    };
    struct NameEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept
        {
            return std::ranges::equal(a, b, {}, fold, fold);
        }
    };
    std::unordered_map<std::string, std::weak_ptr<T>, NameHash, NameEqual> entries;
};
