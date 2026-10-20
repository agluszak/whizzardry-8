#pragma once
struct W8Chunk;

extern char g_default_level[];
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "wiz8/fact_state.h"
#include "wiz8/screen.h"

namespace wiz8 {
class File;
}

/* One fact change as the journal lists it. */
struct W8JournalEntry {
    int level = 0;
    W8FactId fact{};
    /* The fact was set when it changed, so the journal shows its alternate text. */
    bool alternate_text = false;
    /* Retail leaves the three bytes after the flag uninitialized; saves keep them. */
    std::array<std::uint8_t, 3> unknown_09{};
};

/* The fact journal, created lazily by the initializer below and
   appended to whenever a fact changes. */
extern std::vector<W8JournalEntry> g_fact_journal_entries;
/* The "%d / %d" current-over-max format shared by journal pages
   and debug stat readouts. */
void InitializeFactJournal(void);
/* Append one changed fact to the journal and, unless notices are
   suppressed, post the fact's own journal entry. */
void RecordFactChangeForJournal(W8FactId fact_id);
/* Write the entry count, a format dword and each journal entry
   into the open JRNL chunk. */
void SaveFactJournal(wiz8::File& file);
/* The fact journal, two pages of entries filtered by difficulty. */
std::unique_ptr<Screen> CreateJournalScreen();
/* Load the fact journal vector from the JRNL section. */
void LoadJournalEntries(W8Chunk& file);
