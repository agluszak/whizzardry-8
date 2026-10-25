#include "wiz8/story.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/unicode.h"

W8FactionDisposition Story::factionDisposition(signed char faction) const
{
    // A faction id outside the table has no standing with the party.
    if (faction < 0 || faction >= W8_FACTION_COUNT) return W8_FACTION_NEUTRAL;
    const auto score = factions[faction].disposition_score;
    if (score < 34) return W8_FACTION_HOSTILE;
    return score < 67 ? W8_FACTION_NEUTRAL : W8_FACTION_FRIENDLY;
}

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
auto FindLocationVariable(auto& variables, int level, std::string_view name)
{
    return std::ranges::find_if(variables, [&](const auto& variable) {
        return variable.level == level && CompareTextIgnoreAsciiCase(variable.name, name) == 0;
    });
}
}

void Story::ensureLocationVariable(int level, std::string_view name, int initial_value)
{
    if (name.empty()) throw std::invalid_argument("empty location variable name");
    if (FindLocationVariable(location_variables, level, name) != location_variables.end()) return;
    if (location_variables.size() >= std::numeric_limits<std::int32_t>::max())
        throw std::length_error("Too many location variables");
    location_variables.push_back({std::string(name), initial_value, level});
}

std::optional<int> Story::findLocationVariable(int level, std::string_view name) const
{
    const auto found = FindLocationVariable(location_variables, level, name);
    if (found == location_variables.end()) return std::nullopt;
    return found->value;
}

int Story::locationVariable(int level, std::string_view name) const
{
    const auto value = findLocationVariable(level, name);
    if (!value) throw std::out_of_range("unknown location variable " + std::string(name));
    return *value;
}

void Story::setLocationVariable(int level, std::string_view name, int value)
{
    const auto found = FindLocationVariable(location_variables, level, name);
    if (found == location_variables.end())
        throw std::out_of_range("unknown location variable " + std::string(name));
    found->value = value;
}

int Story::availableNpcSlot(std::span<const W8PreparedNpcBinding> pending) const
{
    for (int slot = 0; slot <= 255; ++slot) {
        if (std::ranges::find(pending, slot, &W8PreparedNpcBinding::slot) != pending.end())
            continue;
        if (!std::cmp_less(slot, npcs.size()) || (npcs[slot] && npcs[slot]->binding_unavailable))
            return slot;
    }
    throw std::runtime_error("too many NPC bindings");
}

W8NpcState* Story::adoptNpc(std::shared_ptr<W8NpcState> npc)
{
    const auto index = availableNpcSlot();
    npc->partner_index = static_cast<unsigned char>(index);
    auto* result = npc.get();
    if (std::cmp_equal(index, npcs.size()))
        npcs.push_back(std::move(npc));
    else
        npcs[index] = std::move(npc);
    return result;
}

W8NpcState* Story::npcByName(std::string_view name) const
{
    for (const auto& npc : npcs)
        if (npc && !npc->binding_unavailable &&
            CompareTextIgnoreAsciiCase(npcDisplayName(*npc), name) == 0)
            return npc.get();
    return nullptr;
}

int Story::adjustFactionDisposition(signed char faction, int delta, int clock)
{
    auto& state = factions[faction];
    const int previous = state.disposition_score;
    const auto band = factionDisposition(faction);
    state.disposition_score = static_cast<signed char>(std::clamp(previous + delta, 0, 99));
    if (factionDisposition(faction) != band) state.band_changed_clock = clock;
    return state.disposition_score - previous;
}

int Story::setFactionDisposition(signed char faction, W8FactionDisposition band, int clock)
{
    const auto previous = factionDisposition(faction);
    switch (band) {
    case W8_FACTION_HOSTILE:
        factions[faction].disposition_score = 25;
        break;
    case W8_FACTION_NEUTRAL:
        factions[faction].disposition_score = 55;
        break;
    case W8_FACTION_FRIENDLY:
        factions[faction].disposition_score = 75;
        break;
    }
    if (band != previous) factions[faction].band_changed_clock = clock;
    return band - previous;
}

int Story::recordFactionOffense(signed char faction, const W8MonsterRecord& victim, int clock)
{
    auto& offenses = factions[faction].offense_count;
    if (offenses < 250) ++offenses;
    switch (faction) {
    case W8_FACTION_MOOK:
    case W8_FACTION_RATTKIN_MAFIA:
    case W8_FACTION_HIGARDI_BANK:
    case W8_FACTION_HIGARDI_HLL:
    case W8_FACTION_HIGARDI_COMMON:
        if (factions[faction].disposition_score >= 34)
            return setFactionDisposition(faction, W8_FACTION_HOSTILE, clock);
    }
    const int penalty = victim.record_id == 0x146                   ? 40
                        : (victim.flags & W8_MONSTER_FLAG_NPC) != 0 ? 20
                        : offenses < 21                             ? 2
                        : offenses > 40                             ? 10
                                                                    : 6;
    return adjustFactionDisposition(faction, -penalty, clock);
}
