#include "wiz8/story.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "wiz8/layouts/npc_state.h"
#include "wiz8/unicode.h"

void Story::restoreFacts(const decltype(facts)& saved, int level)
{
    facts = saved;
    if (!facts[W8_FACT_IMPORT_TRANG] && !facts[W8_FACT_IMPORT_UMPANI] && !facts[W8_FACT_VIRGIN]) {
        // This derived fact has no gameplay side effects. A missing saved journal
        // retains its change entry; an explicit journal snapshot replaces it later.
        if (facts[W8_FACT_IMPORT_NOALIGN] != 1)
            journal.push_back(
                {.level = level, .fact = W8_FACT_IMPORT_NOALIGN, .alternate_text = true});
        facts[W8_FACT_IMPORT_NOALIGN] = 1;
    }
}

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

W8NpcState* Story::adoptNpc(std::shared_ptr<W8NpcState> npc)
{
    const auto released = std::ranges::find_if(
        npcs, [](const auto& state) { return state && state->binding_unavailable; });
    const auto index = released - npcs.begin();
    if (index > 255) throw std::runtime_error("too many NPC bindings");
    npc->partner_index = static_cast<unsigned char>(index);
    auto* result = npc.get();
    if (released == npcs.end())
        npcs.push_back(std::move(npc));
    else
        *released = std::move(npc);
    return result;
}
