#pragma once

extern char g_default_level[];
#include <vector>

#include "wiz8/fact_state.h"

namespace wiz8 {
class File;
}

struct W8JournalEntry {
    int level;
    W8FactId fact;
    int alternate_text;
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
unsigned char JournalScreenInitialize(void);
unsigned char JournalScreenEnter(void);
void JournalScreenFrame(void);
unsigned char JournalScreenLeave(int leaving);
unsigned char JournalScreenFinalize(void);
/* Load the fact journal vector from the JRNL section. */
void LoadJournalEntries(wiz8::File& file);
