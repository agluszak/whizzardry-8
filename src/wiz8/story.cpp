#include "wiz8/story.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "wiz8/unicode.h"

namespace {
auto findLocationVariable(auto& variables, int level, std::string_view name)
{
    return std::ranges::find_if(variables, [&](const auto& variable) {
        return variable.level == level && CompareTextIgnoreAsciiCase(variable.name, name) == 0;
    });
}
}

void Story::ensureLocationVariable(int level, std::string_view name, int initial_value)
{
    if (name.empty()) throw std::invalid_argument("empty location variable name");
    if (findLocationVariable(location_variables, level, name) != location_variables.end()) return;
    if (location_variables.size() >= std::numeric_limits<std::int32_t>::max())
        throw std::length_error("Too many location variables");
    location_variables.push_back({std::string(name), initial_value, level});
}

int Story::locationVariable(int level, std::string_view name) const
{
    const auto found = findLocationVariable(location_variables, level, name);
    if (found == location_variables.end())
        throw std::out_of_range("unknown location variable " + std::string(name));
    return found->value;
}

void Story::setLocationVariable(int level, std::string_view name, int value)
{
    const auto found = findLocationVariable(location_variables, level, name);
    if (found == location_variables.end())
        throw std::out_of_range("unknown location variable " + std::string(name));
    found->value = value;
}
