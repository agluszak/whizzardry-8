#pragma once

#include <algorithm>
#include <functional>
#include <vector>

namespace wiz8 {

// Some game APIs use integer indices with -1 for a missing entry.
template <class T, class U, class Projection = std::identity>
int index_of(const std::vector<T>& values, const U& value, Projection projection = {})
{
    const auto found = std::ranges::find(values, value, projection);
    return found == values.end() ? -1 : static_cast<int>(found - values.begin());
}

// Unlike std::erase, remove only the first occurrence. Lists may contain duplicates.
template <class T, class U>
bool erase_first(std::vector<T>& values, const U& value)
{
    const auto found = std::ranges::find(values, value);
    if (found == values.end())
        return false;
    values.erase(found);
    return true;
}

// Unlink before deletion: destructors can remove and destroy other list entries.
template <class T>
void remove_all_and_delete(std::vector<T*>& values)
{
    while (!values.empty()) {
        T* value = values.back();
        values.pop_back();
        delete value;
    }
}

// Drain in order, unlinking before calling code that may mutate the collection.
template <class T, class Dispose>
void clear_and_dispose(std::vector<T>& values, Dispose dispose)
{
    while (!values.empty()) {
        T value = values.front();
        values.erase(values.begin());
        dispose(value);
    }
}

// Use where destruction order is observable (for example, queued events).
template <class T>
void remove_all_and_delete_in_order(std::vector<T*>& values)
{
    clear_and_dispose(values, [](T* value) { delete value; });
}

} // namespace wiz8
