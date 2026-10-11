#pragma once

extern char g_default_level[];
#include <vector>

#include "wiz8/fact_state.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/Controls.h"
#include "compat/compiler.h"

class W8TextBuffer;
namespace wiz8 {
class File;
}

class W8JournalPanel : public Controls, public W8TextControl::Listener {
public:
    W8JournalPanel();
    virtual ~W8JournalPanel();
    virtual void Redraw() override;
    virtual void OnPrimary(W8TextControl* control) override;
    void Refresh();

    W8TextControl* m_next;
    W8TextControl* m_previous;
    W8TextControl* m_close;
    W8TextControl* m_mode;
    W8TextBuffer* m_page_text;
    unsigned char m_alternate_mode;
    unsigned char m_pad_065[3];
};

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
