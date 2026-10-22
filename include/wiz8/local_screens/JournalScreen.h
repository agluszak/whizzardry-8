#pragma once

extern char g_default_level[];
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "wiz8/fact_state.h"
#include "wiz8/screen.h"
#include "wiz8/story.h"

/* Append one changed fact to the journal and, unless notices are
   suppressed, post the fact's own journal entry. */
void RecordFactChangeForJournal(W8FactId fact_id);
/* The fact journal, two pages of entries filtered by difficulty. */
std::unique_ptr<Screen> CreateJournalScreen();
