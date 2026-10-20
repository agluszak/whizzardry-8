#include "wiz8/local_code/Factions.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <fmt/printf.h>

#include "wiz8/chunk.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/platform/assertion.h"

/* Local Code\Factions.cpp. The unit is named by the two assertions this body
   embeds, and the 0..20 domain they bound is the same one the contiguous
   21-name faction table establishes. */

/* The starting relation cells: each faction that sides with another. */
W8FactionRelations StartingFactionRelations()
{
    W8FactionRelations relations{};
    relations[W8_FACTION_BROTHERHOOD][W8_FACTION_RAPAX_COMMON] = 1;
    relations[W8_FACTION_BROTHERHOOD][W8_FACTION_RAPAX_TEMPLAR] = 1;
    relations[W8_FACTION_BROTHERHOOD][W8_FACTION_RAPAX_ARMY] = 1;
    relations[W8_FACTION_BROTHERHOOD][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_BROTHERHOOD][W8_FACTION_KINGS_ASSASINS] = 1;
    relations[W8_FACTION_HIGARDI_BANK][W8_FACTION_RAPAX_COMMON] = 1;
    relations[W8_FACTION_HIGARDI_BANK][W8_FACTION_RAPAX_TEMPLAR] = 1;
    relations[W8_FACTION_HIGARDI_BANK][W8_FACTION_RAPAX_ARMY] = 1;
    relations[W8_FACTION_HIGARDI_BANK][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_HIGARDI_BANK][W8_FACTION_KINGS_ASSASINS] = 1;
    relations[W8_FACTION_HIGARDI_HLL][W8_FACTION_RAPAX_COMMON] = 1;
    relations[W8_FACTION_HIGARDI_HLL][W8_FACTION_RAPAX_TEMPLAR] = 1;
    relations[W8_FACTION_HIGARDI_HLL][W8_FACTION_RAPAX_ARMY] = 1;
    relations[W8_FACTION_HIGARDI_HLL][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_HIGARDI_HLL][W8_FACTION_KINGS_ASSASINS] = 1;
    relations[W8_FACTION_HIGARDI_COMMON][W8_FACTION_RAPAX_COMMON] = 1;
    relations[W8_FACTION_HIGARDI_COMMON][W8_FACTION_RAPAX_TEMPLAR] = 1;
    relations[W8_FACTION_HIGARDI_COMMON][W8_FACTION_RAPAX_ARMY] = 1;
    relations[W8_FACTION_HIGARDI_COMMON][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_HIGARDI_COMMON][W8_FACTION_KINGS_ASSASINS] = 1;
    relations[W8_FACTION_TRYNNIE][W8_FACTION_RAPAX_COMMON] = 1;
    relations[W8_FACTION_TRYNNIE][W8_FACTION_RATTKIN_COMMON] = 1;
    relations[W8_FACTION_TRYNNIE][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_UMPANI][W8_FACTION_TRANG] = 1;
    relations[W8_FACTION_UMPANI][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_TRANG][W8_FACTION_UMPANI] = 1;
    relations[W8_FACTION_TRANG][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_RATTKIN_COMMON][W8_FACTION_TRYNNIE] = 1;
    relations[W8_FACTION_RATTKIN_MAFIA][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_RAPAX_COMMON][W8_FACTION_BROTHERHOOD] = 1;
    relations[W8_FACTION_RAPAX_COMMON][W8_FACTION_HIGARDI_BANK] = 1;
    relations[W8_FACTION_RAPAX_COMMON][W8_FACTION_HIGARDI_HLL] = 1;
    relations[W8_FACTION_RAPAX_COMMON][W8_FACTION_HIGARDI_COMMON] = 1;
    relations[W8_FACTION_RAPAX_COMMON][W8_FACTION_TRYNNIE] = 1;
    relations[W8_FACTION_RAPAX_TEMPLAR][W8_FACTION_BROTHERHOOD] = 1;
    relations[W8_FACTION_RAPAX_TEMPLAR][W8_FACTION_HIGARDI_BANK] = 1;
    relations[W8_FACTION_RAPAX_TEMPLAR][W8_FACTION_HIGARDI_HLL] = 1;
    relations[W8_FACTION_RAPAX_TEMPLAR][W8_FACTION_HIGARDI_COMMON] = 1;
    relations[W8_FACTION_RAPAX_TEMPLAR][W8_FACTION_TRYNNIE] = 1;
    relations[W8_FACTION_RAPAX_ARMY][W8_FACTION_BROTHERHOOD] = 1;
    relations[W8_FACTION_RAPAX_ARMY][W8_FACTION_HIGARDI_BANK] = 1;
    relations[W8_FACTION_RAPAX_ARMY][W8_FACTION_HIGARDI_HLL] = 1;
    relations[W8_FACTION_RAPAX_ARMY][W8_FACTION_HIGARDI_COMMON] = 1;
    relations[W8_FACTION_RAPAX_ARMY][W8_FACTION_TRYNNIE] = 1;
    relations[W8_FACTION_MAD_MARTEN][W8_FACTION_DARK_SAVANT] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_BROTHERHOOD] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_HIGARDI_BANK] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_HIGARDI_HLL] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_HIGARDI_COMMON] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_TRYNNIE] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_UMPANI] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_TRANG] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_RATTKIN_COMMON] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_RATTKIN_MAFIA] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_MOOK] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_MAD_MARTEN] = 1;
    relations[W8_FACTION_DARK_SAVANT][W8_FACTION_COSMIC_LORDS] = 1;
    return relations;
}

/* The per-faction starting disposition scores. The low scores are the hostile
   bands the faction accessors cut at 34 and 67. */
W8FactionStates StartingFactions()
{
    W8FactionStates factions{};
    factions[W8_FACTION_PARTY].disposition_score = 100;
    factions[W8_FACTION_COSMIC_LORDS].disposition_score = 100;
    factions[W8_FACTION_DARK_SAVANT].disposition_score = 0;
    factions[W8_FACTION_UMPANI].disposition_score = 0x50;
    factions[W8_FACTION_TRANG].disposition_score = 0x50;
    factions[W8_FACTION_MOOK].disposition_score = 0x50;
    factions[W8_FACTION_RATTKIN_COMMON].disposition_score = 0x32;
    factions[W8_FACTION_RATTKIN_MAFIA].disposition_score = 0x50;
    factions[W8_FACTION_BROTHERHOOD].disposition_score = 0x50;
    factions[W8_FACTION_HIGARDI_BANK].disposition_score = 0x50;
    factions[W8_FACTION_HIGARDI_HLL].disposition_score = 0x50;
    factions[W8_FACTION_HIGARDI_COMMON].disposition_score = 0x50;
    factions[W8_FACTION_TRYNNIE].disposition_score = 0x50;
    factions[W8_FACTION_MAD_MARTEN].disposition_score = 0x46;
    factions[W8_FACTION_RAPAX_COMMON].disposition_score = 0x19;
    factions[W8_FACTION_RAPAX_TEMPLAR].disposition_score = 0x32;
    factions[W8_FACTION_RAPAX_ARMY].disposition_score = 0x32;
    factions[W8_FACTION_KINGS_ASSASINS].disposition_score = 0x50;
    return factions;
}

W8FactionDisposition GetFactionDisposition(signed char faction)
{
    signed char disposition_score;

    if (faction < 0) {
        Fail("bFaction >= 0");
    }
    if (faction >= W8_FACTION_COUNT) {
        Fail("bFaction < FACTION_COUNT");
    }

    disposition_score = CurrentGame().story.factions[faction].disposition_score;
    if (disposition_score < 34) {
        return W8_FACTION_HOSTILE;
    }
    if (disposition_score < 67) {
        return W8_FACTION_NEUTRAL;
    }
    return W8_FACTION_FRIENDLY;
}

/* The Data().strings name id for each faction row; the two filler
   factions reuse the unaligned name. */
static unsigned short g_faction_name_ids[W8_FACTION_COUNT] = {
    0x5a1, 0x5a2, 0x5a3, 0x5a4, 0x5a5, 0x5a6, 0x5a7, 0x5a8, 0x5a9, 0x5aa, 0x5ab,
    0x5ac, 0x5ad, 0x5ae, 0x5af, 0x5b0, 0x5b1, 0x5b2, 0x5b3, 0x5a1, 0x5a1,
};

/* Snap a faction's score onto the requested band's anchor (25/55/75). When the
   band actually moved, stamp the world clock and post the worsened/improved
   notice naming the faction. */
void SetFactionDispositionBand(signed char faction, W8FactionDisposition band)
{
    std::string notice;
    W8FactionDisposition old_band;
    char palette;

    old_band = GetFactionDisposition(faction);
    switch (band) {
    case W8_FACTION_HOSTILE:
        CurrentGame().story.factions[faction].disposition_score = 0x19;
        break;
    case W8_FACTION_NEUTRAL:
        CurrentGame().story.factions[faction].disposition_score = 0x37;
        break;
    case W8_FACTION_FRIENDLY:
        CurrentGame().story.factions[faction].disposition_score = 0x4b;
        break;
    }
    if (band != old_band) {
        CurrentGame().story.factions[faction].band_changed_clock = CurrentGame().status.world_clock;
        if (band < old_band) {
            notice = Data().strings[0x245];
            palette = 0;
        } else {
            notice = Data().strings[0x246];
            palette = 5;
        }
        ShowNotice(palette, fmt::sprintf(Data().strings[0x247],
                                         Data().strings[g_faction_name_ids[faction]], notice));
    }
}

/* The disposition `faction` holds toward `other`: the party target reads the
   live score band while any other target is answered by the static relation
   matrix, reporting friendly unless the stored relation is neutral. */
W8FactionDisposition GetFactionDispositionToward(signed char faction, signed char other)
{
    if (other == W8_FACTION_PARTY) {
        return GetFactionDisposition(faction);
    }
    return CurrentGame().story.faction_relations[faction][other] != W8_FACTION_NEUTRAL
               ? W8_FACTION_FRIENDLY
               : W8_FACTION_HOSTILE;
}

/* Direct disposition changes skip the unaligned/party factions. Witnessed
   offenses use RecordFactionOffense with the victim itself. */
void ApplyFactionChange(char mode, char op, signed char faction, int value)
{
    if (op == 1 && faction != 0 && faction != 1) {
        if (mode != 0) {
            if (mode > 0 && mode <= 3) {
                AdjustFactionDisposition(faction, value);
            }
        }
    }
}

/* A witnessed offense against the faction: bump the running count, then weigh
   the victim. Factions that flip hostile outright do so once they were not
   already hostile; everyone else loses a record-scaled amount that grows as
   the offense count climbs. */
void RecordFactionOffense(signed char faction, W8MonsterInfo* monster_info)
{
    W8MonsterRecord* record;

    if (CurrentGame().story.factions[faction].offense_count < 0xfa) {
        CurrentGame().story.factions[faction].offense_count++;
    }
    record = GetMonsterDataForInfo(monster_info);
    switch (faction) {
    case W8_FACTION_MOOK:
    case W8_FACTION_RATTKIN_MAFIA:
    case W8_FACTION_HIGARDI_BANK:
    case W8_FACTION_HIGARDI_HLL:
    case W8_FACTION_HIGARDI_COMMON:
        if (faction < 0) {
            Fail("bFaction >= 0");
        }
        if (faction >= W8_FACTION_COUNT) {
            Fail("bFaction < FACTION_COUNT");
        }
        if (CurrentGame().story.factions[faction].disposition_score >= 34) {
            SetFactionDispositionBand(faction, W8_FACTION_HOSTILE);
            return;
        }
    }
    if (record->record_id == 0x146) {
        AdjustFactionDisposition(faction, -0x28);
        return;
    }
    if ((record->flags & W8_MONSTER_FLAG_NPC) != 0) {
        AdjustFactionDisposition(faction, -0x14);
        return;
    }
    if (CurrentGame().story.factions[faction].offense_count < 0x15) {
        AdjustFactionDisposition(faction, -2);
        return;
    }
    AdjustFactionDisposition(faction,
                             CurrentGame().story.factions[faction].offense_count > 0x28 ? -10 : -6);
}

/* Shift a faction's disposition by delta, clamped to 0..99. A band crossing
   stamps the world clock; any score move posts the worsened/improved notice
   naming the faction. The bounds asserts genuinely run twice - before the
   read and again before the band check. */
void AdjustFactionDisposition(signed char faction, char delta)
{
    std::string notice;
    signed char score;
    signed char old_score;
    W8FactionDisposition old_band;
    W8FactionDisposition new_band;
    char palette;

    old_band = GetFactionDisposition(faction);
    score = CurrentGame().story.factions[faction].disposition_score;
    old_score = CurrentGame().story.factions[faction].disposition_score;
    if (score + delta > 99) {
        score = 99;
    } else if (score + delta < 0) {
        score = 0;
    } else {
        score += delta;
    }
    CurrentGame().story.factions[faction].disposition_score = score;
    new_band = GetFactionDisposition(faction);
    if (new_band != old_band) {
        CurrentGame().story.factions[faction].band_changed_clock = CurrentGame().status.world_clock;
    }
    if (CurrentGame().story.factions[faction].disposition_score < old_score) {
        notice = Data().strings[0x245];
        palette = 0;
    } else if (CurrentGame().story.factions[faction].disposition_score > old_score) {
        notice = Data().strings[0x246];
        palette = 5;
    } else {
        return;
    }
    ShowNotice(palette, fmt::sprintf(Data().strings[0x247],
                                     Data().strings[g_faction_name_ids[faction]], notice));
}

namespace {
// The FATA section's disposition record.
#pragma pack(push, 1)
struct FactionRecord {
    signed char disposition_score;
    std::int32_t offense_count;
    std::uint8_t unknown_05;
    std::int32_t band_changed_clock;
    std::uint8_t encountered;
    std::uint8_t unknown_0b[3];
};
#pragma pack(pop)
static_assert(sizeof(FactionRecord) == 14);
} // namespace

/* Write both faction tables into the open FATA chunk: the 21x21 relation
   matrix, then the 21 disposition records. */
void SaveFactionState(wiz8::File& file)
{
    const auto& story = CurrentGame().story;
    for (const auto& row : story.faction_relations) file.write(row.data(), row.size());
    for (const auto& faction : story.factions) {
        FactionRecord record{faction.disposition_score,  faction.offense_count, faction.unknown_05,
                             faction.band_changed_clock, faction.encountered,   {}};
        std::ranges::copy(faction.unknown_0b, record.unknown_0b);
        file.write(&record, sizeof(record));
    }
}

/* Read the relation matrix and disposition records back from the open FATA
   chunk, then re-arm the party's fixed score. */
void LoadFactionState(W8Chunk& file)
{
    W8FactionRelations relations;
    for (auto& row : relations) file.ReadBytes(row.data(), row.size());
    W8FactionStates factions;
    for (auto& faction : factions) {
        FactionRecord record;
        file.ReadBytes(&record, sizeof(record));
        faction = {record.disposition_score, record.offense_count, record.band_changed_clock,
                   record.encountered != 0,  record.unknown_05,    {}};
        std::ranges::copy(record.unknown_0b, faction.unknown_0b.begin());
    }
    auto& story = CurrentGame().story;
    story.faction_relations = relations;
    story.factions = factions;
    story.factions[W8_FACTION_PARTY].disposition_score = 100;
}
