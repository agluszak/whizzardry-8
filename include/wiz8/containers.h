#pragma once

#include <algorithm>
#include <vector>

namespace wiz8 {

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
