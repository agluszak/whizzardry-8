#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"

/* One fact change as the journal lists it. */
struct W8JournalEntry {
    int level = 0;
    W8FactId fact{};
    /* The fact was set when it changed, so the journal shows its alternate text. */
    bool alternate_text = false;
    /* Retail leaves the three bytes after the flag uninitialized; saves keep them. */
    std::array<std::uint8_t, 3> unknown_09{};
};

/* A faction's standing with the party. */
struct W8FactionState {
    signed char disposition_score = 0;
    /* Witnessed offenses against the faction; RecordFactionOffense increments it
       (cap 0xfa) and reads it for the penalty tiers. */
    int offense_count = 0;
    /* The world clock at the last band change. */
    int band_changed_clock = 0;
    /* Raised by the sight pass the first time the party sees one of this faction's
       monsters; the journal lists encountered factions. */
    bool encountered = false;
    /* Saved bytes with no known meaning. */
    std::uint8_t unknown_05 = 0;
    std::array<std::uint8_t, 3> unknown_0b{};
};

using W8FactionStates = std::array<W8FactionState, W8_FACTION_COUNT>;
/* One row per faction: nonzero where the row's faction sides with the column's. */
using W8FactionRelations =
    std::array<std::array<unsigned char, W8_FACTION_COUNT>, W8_FACTION_COUNT>;

/* The dispositions and relations a new game begins with. */
W8FactionStates StartingFactions();
W8FactionRelations StartingFactionRelations();

/* A level script's named variable, scoped to the level that created it. */
struct W8LocationVariable {
    std::string name;
    int value = 0;
    int level = 0;
};

struct World;

/* What one play-through's story has come to: facts, factions, the journal, the
   learned keywords, the NPCs and the level scripts' variables. A new game starts from these defaults, and NPC
   states are created when the opening sequence runs; a load replaces each part from
   its save section. */
struct Story {
    std::array<unsigned char, W8_FACT_SLOT_COUNT> facts{};
    W8FactionStates factions = StartingFactions();
    W8FactionRelations faction_relations = StartingFactionRelations();
    std::vector<W8JournalEntry> journal;
    /* The learned keyword notebook. Every addition and removal changes it at once;
       the dialogue transcript only shows it. */
    std::vector<W8DialogueTranscriptRecord> keywords;
    // Whether the transcript lists the notebook sorted; saves keep it with the keywords.
    bool keywords_sorted = false;
    /* Indexed by the slot that party rows and monster bindings name. A released NPC's
       slot can go to a different NPC; anything still working with the released one
       shares it, so it never starts referring to the slot's new occupant. */
    std::vector<std::shared_ptr<W8NpcState>> npcs;
    std::vector<W8LocationVariable> location_variables;

    /* Give each variable a trigger of `world` initializes the trigger's initial value,
       unless the level already has the variable. */
    void addTriggerVariableDefaults(const World& world);
};
