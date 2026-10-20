#pragma once
struct W8Chunk;

extern char g_default_level[];
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "wiz8/fact_state.h"
#include "wiz8/screen.h"
#include "wiz8/story.h"

namespace wiz8 {
class File;
}

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
