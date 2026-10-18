#pragma once

#include <algorithm>
#include <functional>
#include <vector>

namespace wiz8 {

// Some game APIs use integer indices with -1 for a missing entry.
template<class T, class U, class Projection = std::identity>
int index_of(const std::vector<T>& values, const U& value, Projection projection = {})
{
    const auto found = std::ranges::find(values, value, projection);
    return found == values.end() ? -1 : static_cast<int>(found - values.begin());
}

// Unlike std::erase, remove only the first occurrence. Lists may contain duplicates.
template<class T, class U>
bool erase_first(std::vector<T>& values, const U& value)
{
    const auto found = std::ranges::find(values, value);
    if (found == values.end()) return false;
    values.erase(found);
    return true;
}

} // namespace wiz8
