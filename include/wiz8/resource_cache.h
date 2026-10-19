#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

/* Name-keyed lookup of live shared resources. The cache never keeps a resource
   alive: it is released with its last owner. A newer resource replaces the
   entry for its name. */
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
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const noexcept
        {
            return std::hash<std::string_view>{}(name);
        }
    };
    std::unordered_map<std::string, std::weak_ptr<T>, NameHash, std::equal_to<>> entries;
};
