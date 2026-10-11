#include <tuple>
#include "himage.h"
#include <fmt/format.h>
#include <SDL3/SDL_rect.h>
#include <string_view>
#include <string>
#include <vector>

#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/xstatus.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/fonts.h"
#include "wiz8/fact_state.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/regions.h"
#include "wiz8/video_object_catalog.h"
#include "Font.h"
#include "soundman.h"
#include "wiz8/filesystem.h"
#include "input.h"
#include "mousesystem.h"
#include "difficulty.h"
#include "local_code/Controls.h"
#include "message_box.h"
#include "vobject.h"
#include "vsurface.h"

// GLOBAL: WIZ8 0x0064df38
int g_journal_page = -1;
// GLOBAL: WIZ8 0x0069c4e0
bool g_journal_show_all;
// GLOBAL: WIZ8 0x0064d7b8
char g_default_level[] = "Default Level";
// GLOBAL: WIZ8 0x0064df40
signed char g_journal_factions[12] = {4, 5, 6, 7, 8, 9, 12, 11, 13, 15, 16, 0};
// GLOBAL: WIZ8 0x0064df4c
int g_journal_faction_name_indices[11] = {
    0x6e0, 0x6e1, 0x6e2, 0x6e3, 0x6e4, 0x6e5, 0x6e6, 0x6e7, 0x6e8, 0x6e9, 0x6ea,
};
// GLOBAL: WIZ8 0x0064df78
char g_journal_alternate_page[] = "1 / 1";
// GLOBAL: WIZ8 0x0064df3c
int g_journal_page_count = -1;
// GLOBAL: WIZ8 0x0069C4CC
Font g_journal_font;
// GLOBAL: WIZ8 0x0069C4D0
Palette16 g_journal_font_palette;
// GLOBAL: WIZ8 0x0069C4D4
W8JournalPanel* g_journal_panel;
// GLOBAL: WIZ8 0x0069C4E4
std::vector<W8JournalEntry> g_journal_entries;
// GLOBAL: WIZ8 0x0069C4DC
unsigned int g_journal_region_set;
// GLOBAL: WIZ8 0x0068de40
std::vector<W8JournalEntry> g_fact_journal_entries;

// GLOBAL: WIZ8 0x0068de44
bool g_fact_notifications_suppressed;

// FUNCTION: WIZ8 0x00558820
void InitializeFactJournal(void)
{
    g_fact_journal_entries.clear();
}

// FUNCTION: WIZ8 0x005588e0
void SetFactNotificationsSuppressed(bool suppressed)
{
    g_fact_notifications_suppressed = suppressed;
}

/* Append one changed fact to the journal and, when notices are not suppressed
   and the fact is visible at the current notice setting, post the fact's own
   description through the notice pane with its sound. */
// FUNCTION: WIZ8 0x005588f0
void RecordFactChangeForJournal(W8FactId fact_id)
{
    W8JournalEntry entry;
    entry.level = g_status.current_level;
    entry.fact = fact_id;
    entry.alternate_text = GetFact(fact_id);
    g_fact_journal_entries.push_back(entry);

    if (g_fact_notifications_suppressed) {
        return;
    }
    int visibility;
    if (g_settings.difficulty == W8_DIFFICULTY_NOVICE) {
        visibility = 2;
    } else if (g_settings.difficulty == W8_DIFFICULTY_NORMAL) {
        visibility = 1;
    } else {
        visibility = fact_id;
        if (g_settings.difficulty == W8_DIFFICULTY_EXPERT) {
            visibility = 0;
        }
    }
    const W8FactDatabaseRecord* record = &g_fact_records[fact_id];
    if (record->visibility > visibility) {
        return;
    }
    std::string description =
        GetFact(fact_id) ? record->alternate_description : record->description;
    if (description.empty() || g_level_block == 0) {
        return;
    }
    if (gXStatus.fNpcDialogueMode) {
        QueueNpcMessageLine(W8_NPC_MSG_JOURNAL_QUOTE, 0);
        return;
    }
    int range = GetTextBoxScrollRange();
    ShowNotice(W8_FONT_PALETTE_BLUE, g_localized_strings[0x74a], 2, range);
    SoundPlay("Data\\Sound\\Misc\\Journal Entry.wav", 0);
}

/* Write the JRNL format dword, entry count, then each 0x0c-byte entry. */
// FUNCTION: WIZ8 0x00558A90
void SaveFactJournal(wiz8::File* file)
{
    int format = 1;
    int count;

    count = static_cast<int>(g_fact_journal_entries.size());
    file->write(&format, 4);
    file->write(&count, 4);
    for (const auto& entry : g_fact_journal_entries) {
        file->write(&entry, sizeof(entry));
    }
}

/* Resize the live journal entries before reading the JRNL records. */
// FUNCTION: WIZ8 0x00558B20
void LoadJournalEntries(wiz8::File* file)
{
    int format;
    int count;

    InitializeFactJournal();
    file->read_exact(&format, 4);
    file->read_exact(&count, 4);
    g_fact_journal_entries.resize(count);
    for (auto& entry : g_fact_journal_entries) {
        file->read_exact(&entry, sizeof(entry));
    }
}

// FUNCTION: WIZ8 0x005bdd00
void DrawJournalLine(std::string_view text, int column, int y, int palette, bool centered)
{
    int left;
    int right;
    int x;

    if (column == 0) {
        left = 0x0b;
        right = 0xaf;
    } else {
        left = 0xc1;
        right = 0x1b3;
    }
    if (!centered) {
        x = 5;
    } else {
        x = (right - g_journal_font.textWidth(text)) / 2;
        if (x < 0) {
            x = 0;
        }
    }
    SetFont(g_journal_font);
    SetFontDestBuffer(GetFrameBufferSurface(), left, y, left + right, y + 0x1e, 0);
    if (palette == 0) {
        g_journal_font.resetPalette();
    } else if (palette == 1) {
        g_journal_font.setPalette(g_journal_font_palette);
    }
    DrawFontText(left + x, y, text);
}

// FUNCTION: WIZ8 0x005bd860
void W8JournalPanel::Refresh()
{
    if (!m_alternate_mode) {
        int last_page = (static_cast<int>(g_journal_entries.size()) - 1) / 12;
        int page_count = last_page + 1;
        if (page_count != g_journal_page_count || g_journal_page < 0 ||
            g_journal_page >= page_count) {
            g_journal_page_count = page_count;
            g_journal_page = last_page;
        }
        m_next->SetEnabled(g_journal_page < last_page);
        m_previous->SetEnabled(g_journal_page > 0);

        std::string page_text;
        page_text = fmt::format("{:d} / {:d}", g_journal_page + 1, page_count);
        m_page_text->SetText(page_text, g_options_detail_font);
        DrawCatalogImageAndInvalidate(GetFrameBufferSurface(), 0x1b8, 0, 0, 0, 0, VO_BLT_SRCTRANSPARENCY, 0);
        DrawJournalLine(g_localized_strings[0x6db], 0, 0x19, 0, true);
        DrawJournalLine(g_localized_strings[0x6dc], 1, 0x19, 0, true);

        int first = g_journal_page * 12;
        int last = first + 11;
        if (last >= static_cast<int>(g_journal_entries.size())) {
            last = static_cast<int>(g_journal_entries.size()) - 1;
        }
        int previous_level = -1;
        int y = 0x39;
        for (int index = first; index <= last; ++index, y += 0x1e) {
            const W8JournalEntry* entry = &g_journal_entries[index];
            const W8FactDatabaseRecord* fact = &g_fact_records[entry->fact];
            int active = fact->highlight_when_true && GetFact(entry->fact);
            std::string description =
                entry->alternate_text ? fact->alternate_description : fact->description;
            std::string level_name;
            if (entry->level == 0x38) {
                level_name = g_default_level;
            } else if (entry->level == -1) {
                level_name = g_localized_strings[0x6dd];
            } else {
                level_name = g_localized_strings[g_level_name_indices[entry->level]];
            }
            if (entry->level != previous_level) {
                DrawJournalLine(level_name, 0, y, 0, true);
            }
            DrawJournalLine(description, 1, y, active, false);
            previous_level = entry->level;
        }
    } else {
        m_next->SetEnabled(false);
        m_previous->SetEnabled(false);
        m_page_text->SetText(g_journal_alternate_page, g_options_detail_font);
        DrawCatalogImageAndInvalidate(GetFrameBufferSurface(), 0x1b8, 0, 0, 0, 0, VO_BLT_SRCTRANSPARENCY, 0);
        DrawJournalLine(g_localized_strings[0x6de], 0, 0x19, 0, true);
        DrawJournalLine(g_localized_strings[0x6df], 1, 0x19, 0, true);

        int y = 0x39;
        for (int index = 0; index < 11; ++index) {
            signed char faction = g_journal_factions[index];
            if (GetFactionFlag(faction)) {
                W8FactionDisposition disposition = GetFactionDisposition(faction);
                std::string disposition_name;
                switch (disposition) {
                case W8_FACTION_HOSTILE:
                    disposition_name = g_localized_strings[0x200];
                    break;
                case W8_FACTION_NEUTRAL:
                    disposition_name = g_localized_strings[0x1ff];
                    break;
                case W8_FACTION_FRIENDLY:
                    disposition_name = g_localized_strings[0x201];
                    break;
                }
                if (!disposition_name.empty()) {
                    DrawJournalLine(disposition_name, 0, y, 0, true);
                }
                DrawJournalLine(g_localized_strings[g_journal_faction_name_indices[index]], 1, y, 0,
                                false);
                y += 0x1e;
            }
        }
    }
    Invalidate(0);
}

// VTABLE: WIZ8 0x005ef340 Controls
// VTABLE: WIZ8 0x005ef338 W8TextControl::Listener
// class W8JournalPanel

// FUNCTION: WIZ8 0x005bd530
W8JournalPanel::W8JournalPanel(unsigned int* region_set)
    : Controls(0x66, 0x1bb, 0, 0, 0xf3, 0, 0), m_next(0), m_previous(0), m_close(0), m_mode(0),
      m_page_text(0), m_alternate_mode(0)
{
    short width;
    short height;

    AcquireRegionSet(region_set);
    std::tie(width, height) = GetCatalogImageSize(0xf3, 0, 0);
    m_bounds.right = m_bounds.left + static_cast<unsigned short>(width);
    m_bounds.bottom = m_bounds.top + static_cast<unsigned short>(height);

    m_previous = new W8TextControl(this, 0xffffffff, 3, 3, 0, 0, 0xf4, 0, 0, 2, 1, -1, 3);
    m_previous->m_listener = this;

    m_next = new W8TextControl(this, 0xffffffff, 0x11f, 3, 0, 0, 0xf4, 0, 4, 6, 5, -1, 7);
    m_next->m_listener = this;

    W8ControlsRect bounds = {m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.bottom};
    m_page_text = new W8TextBuffer(&bounds, "", g_options_detail_font,
                                   g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);

    m_mode = new W8TextControl(this, 0xffffffff, 0x1b0, -2, 0, 0, 0x1bb, 0, 0, 2, 1, 2, 3);
    m_mode->AddLayoutFlags(g_W8TextControlLayoutToggle);
    m_mode->m_listener = this;

    m_close =
        new W8TextControl(this, 0xffffffff, 0x1ea, -2, 0, 0, 0x106, 0, 0x10, -1, 0x11, 0x12, 0x13);
    m_close->m_listener = this;
    m_close->EnableRegionHelp(0x6ed);
    SetEnabled(true);
    EnableRegionSet(true);
    m_alternate_mode = 0;
    m_mode->EnableRegionHelp(0x6eb);
    if ((m_mode->m_stateFlags & g_W8TextControlStateSecondary) != 0) {
        m_mode->DisableSecondaryState(true);
    }
}

// FUNCTION: WIZ8 0x005bd7f0
W8JournalPanel::~W8JournalPanel()
{
    DestroyAllControls();
    delete m_page_text;
}

// FUNCTION: WIZ8 0x005bdbf0
void W8JournalPanel::Redraw()
{
    if (m_fEnabled && (m_fDirty || m_fLayoutDirty)) {
        Controls::Redraw();
        m_page_text->RenderToTarget(0, true, GetFrameBufferSurface());
    }
}

// FUNCTION: WIZ8 0x005bdc20
void W8JournalPanel::OnPrimary(W8TextControl* control)
{
    if (control == m_previous) {
        if (g_journal_page > 0) {
            --g_journal_page;
            Refresh();
        }
    } else if (control == m_next) {
        if (g_journal_page < (static_cast<int>(g_journal_entries.size()) - 1) / 12) {
            ++g_journal_page;
            Refresh();
        }
    } else if (control == m_close) {
        RequestScreenTransition();
    } else {
        m_alternate_mode =
            static_cast<unsigned char>(m_mode->m_stateFlags & g_W8TextControlStateSecondary);
        if (m_alternate_mode) {
            m_mode->EnableRegionHelp(0x6ec);
            if ((m_mode->m_stateFlags & g_W8TextControlStateSecondary) == 0) {
                m_mode->EnableSecondaryState(true);
            }
        } else {
            m_mode->EnableRegionHelp(0x6eb);
            if ((m_mode->m_stateFlags & g_W8TextControlStateSecondary) != 0) {
                m_mode->DisableSecondaryState(true);
            }
        }
        Refresh();
    }
}

/* Lifecycle record 11's initializer and finalizer. The initializer loads the
   journal font, saves the palette the font arrived with, and takes a second
   palette from frame 0 of video object 0x1B9; the finalizer puts the original
   palette back and releases the one it took. The two together are the second
   proof that a record's fifth slot is its finalizer rather than a second
   initializer - record 10's allocate/free pair is the first. */
// FUNCTION: WIZ8 0x005bddd0
unsigned char JournalScreenInitialize(void)
{
    if (!g_journal_font.load("Data\\Journal\\journal_font.sti"))
        return 0;
    g_journal_font_palette = CopyCatalogImagePalette16BPP(0x1b9, 0);
    return 1;
}

// FUNCTION: WIZ8 0x005bde10
unsigned char JournalScreenFinalize(void)
{
    g_journal_font.clear();
    g_journal_font_palette = {};
    return 1;
}

// FUNCTION: WIZ8 0x005bde40
unsigned char JournalScreenEnter(void)
{
    int maximum_visibility;

    SetViewport(0, 0, 0x280, 0x1e0);
    SetPrimarySurfaceTextureHint2Enabled(false);
    MSYS_Init();
    ResetRegions();
    UpdateHeldItemCursor();
    g_journal_panel = new W8JournalPanel(&g_journal_region_set);

    switch (g_settings.difficulty) {
    case W8_DIFFICULTY_NOVICE:
        maximum_visibility = 2;
        break;
    case W8_DIFFICULTY_NORMAL:
        maximum_visibility = 1;
        break;
    case W8_DIFFICULTY_EXPERT:
        maximum_visibility = 0;
        break;
    }

    g_journal_entries.clear();
    for (const auto& entry : g_fact_journal_entries) {
        const W8FactDatabaseRecord* fact = &g_fact_records[entry.fact];
        std::string description =
            entry.alternate_text ? fact->alternate_description : fact->description;
        if ((g_journal_show_all || fact->visibility <= maximum_visibility) && !description.empty()) {
            g_journal_entries.push_back(entry);
        }
    }
    g_journal_panel->Refresh();
    DrawCatalogImageAndInvalidate(GetFrameBufferSurface(), 0x1b7, 0, 1, 0, 0x1b4, VO_BLT_SRCTRANSPARENCY, 0);
    return 1;
}

// FUNCTION: WIZ8 0x005be0b0
unsigned char JournalScreenLeave(int)
{
    MSYS_Shutdown();
    ResetRegions();
    delete g_journal_panel;
    g_journal_panel = 0;
    g_journal_entries.clear();
    if (gXStatus.fCampMode) {
        SyncDialogueNpcState();
    }
    return 1;
}

// FUNCTION: WIZ8 0x005be110
void JournalScreenFrame(void)
{
    SDL_Point point;
    InputAtom input;

    SGPMouseGetPos(&point);
    UpdateRegionMousePosition(point.x, point.y);
    while (DequeueEvent(&input) == 1) {
        if (!DispatchRegionInput(&input) && input.usEvent == KEY_DOWN) {
            if (input.usParam == VK_ESCAPE) {
                RequestScreenTransition();
            } else if (input.usParam == VK_LEFT && g_journal_page > 0) {
                --g_journal_page;
                g_journal_panel->Refresh();
            } else if (input.usParam == VK_RIGHT &&
                       g_journal_page < (static_cast<int>(g_journal_entries.size()) - 1) / 12) {
                ++g_journal_page;
                g_journal_panel->Refresh();
            }
        }
    }
    g_journal_panel->Redraw();
    RenderFrame();
}
