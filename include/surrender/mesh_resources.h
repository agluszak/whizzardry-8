#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// Mesh assignments share each distinct resource once. Rendering borrows this
// table object, so growing its resource pool does not invalidate the view.
template<class T> struct MeshResources {
    std::vector<std::shared_ptr<T>> resources;
    std::vector<std::uint32_t> indices;

    bool empty() const { return indices.empty(); }
    std::size_t size() const { return indices.size(); }
    const std::shared_ptr<T>& operator[](std::size_t element) const
    {
        return resources[indices[element]];
    }
    void resize(std::size_t count)
    {
        if (count == 0) {
            *this = {};
            return;
        }
        if (count && resources.empty()) resources.emplace_back();
        const bool shrinking = count < indices.size();
        indices.resize(count);
        if (shrinking) {
            for (std::size_t i = 1; i < resources.size(); ++i)
                if (std::find(indices.begin(), indices.end(), i) == indices.end())
                    resources[i].reset();
        }
    }
    void set(std::size_t element, std::shared_ptr<T> resource)
    {
        const auto old = indices[element];
        auto found = std::find(resources.begin(), resources.end(), resource);
        if (found == resources.end()) {
            // Entry zero represents unassigned elements and is never reused.
            found = std::find(resources.begin() + 1, resources.end(), nullptr);
            if (found == resources.end()) {
                resources.push_back(std::move(resource));
                found = resources.end() - 1;
            } else {
                *found = std::move(resource);
            }
        }
        const auto selected = static_cast<std::uint32_t>(found - resources.begin());
        indices[element] = selected;
        if (old != selected && resources[old] &&
            std::find(indices.begin(), indices.end(), old) == indices.end()) {
            resources[old].reset();
        }
    }
};
