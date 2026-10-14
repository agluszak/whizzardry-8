#pragma once

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
        auto found = entries.find(std::string(name));
        return found == entries.end() ? nullptr : found->second.lock();
    }

    void insert(std::string name, const std::shared_ptr<T>& resource)
    {
        entries.insert_or_assign(std::move(name), resource);
    }

    void erase(std::string_view name) { entries.erase(std::string(name)); }

    void clear() { entries.clear(); }

private:
    std::unordered_map<std::string, std::weak_ptr<T>> entries;
};
