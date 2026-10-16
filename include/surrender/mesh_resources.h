#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// Mesh assignments share each distinct resource once. Rendering borrows this
// table object, so growing its resource pool does not invalidate the view.
template<class T> struct MeshResources {
    std::vector<std::uint32_t> indices;

    bool empty() const { return indices.empty(); }
    std::size_t size() const { return indices.size(); }
    const std::shared_ptr<T>& operator[](std::size_t element) const
    {
        return resources[indices[element]].value;
    }
    void resize(std::size_t count)
    {
        if (count == 0) {
            *this = {};
            return;
        }
        if (resources.empty()) resources.emplace_back();
        for (std::size_t element = count; element < indices.size(); ++element)
            release(indices[element]);
        indices.resize(count);
    }
    void set(std::size_t element, std::shared_ptr<T> resource)
    {
        const auto old = indices[element];
        if (resources[old].value == resource) return;
        auto found = std::find_if(resources.begin(), resources.end(),
                                  [&](const auto& entry) { return entry.value == resource; });
        if (found == resources.end()) {
            // Entry zero represents unassigned elements and is never reused.
            found = std::find_if(resources.begin() + 1, resources.end(),
                                 [](const auto& entry) { return !entry.value; });
            if (found == resources.end()) {
                if (resources.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("Too many distinct mesh resources");
                resources.push_back({std::move(resource), 0});
                found = resources.end() - 1;
            } else {
                found->value = std::move(resource);
            }
        }
        const auto selected = static_cast<std::uint32_t>(found - resources.begin());
        indices[element] = selected;
        if (selected != 0) ++found->assignments;
        release(old);
    }

private:
    struct Resource {
        std::shared_ptr<T> value;
        std::size_t assignments = 0;
    };
    std::vector<Resource> resources;

    void release(std::uint32_t index)
    {
        if (index != 0 && --resources[index].assignments == 0) resources[index].value.reset();
    }
};
