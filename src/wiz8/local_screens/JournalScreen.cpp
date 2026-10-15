#include "wiz8/local_screens/JournalScreen.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "Font.h"
#include "difficulty.h"
#include "himage.h"
#include "image.h"
#include "input.h"
#include "message_box.h"
#include "soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

namespace {
enum JournalAction { Previous, Next, Mode, Close };

class JournalScreen {
public:
    JournalScreen();
    void Refresh();
    void Draw(SDL_Surface& target);

private:
    void Activate(JournalAction action);
    void DrawContent(SDL_Surface& target);
    UiLayer m_input;
    CatalogArtwork m_bar = CatalogArtwork::Load(0xf3, 0);
    std::array<std::unique_ptr<W8ButtonWidget>, 4> m_buttons;
    std::unique_ptr<W8TextBuffer> m_page_text;
    bool m_alternate_mode = false;
    bool m_dirty = true;
};
} // namespace

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
static std::unique_ptr<JournalScreen> g_journal_panel;
// GLOBAL: WIZ8 0x0069C4E4
std::vector<W8JournalEntry> g_journal_entries;
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
        g_npc_dialogue.Queue(W8NpcCommand::journal_quote);
        return;
    }
    int range = GetTextBoxScrollRange();
    ShowNotice(W8_FONT_PALETTE_BLUE, g_localized_strings[0x74a], 2, range);
    wiz8::sound::play("Data\\Sound\\Misc\\Journal Entry.wav");
}

/* Write the JRNL format dword, entry count, then each 0x0c-byte entry. */
// FUNCTION: WIZ8 0x00558A90
void SaveFactJournal(wiz8::File& file)
{
    int format = 1;
    int count;

    count = static_cast<int>(g_fact_journal_entries.size());
    file.write(&format, 4);
    file.write(&count, 4);
    for (const auto& entry : g_fact_journal_entries) {
        file.write(&entry, sizeof(entry));
    }
}

/* Resize the live journal entries before reading the JRNL records. */
// FUNCTION: WIZ8 0x00558B20
void LoadJournalEntries(wiz8::File& file)
{
    int format;
    int count;

    InitializeFactJournal();
    file.read_exact(&format, 4);
    file.read_exact(&count, 4);
    g_fact_journal_entries.resize(count);
    for (auto& entry : g_fact_journal_entries) {
        file.read_exact(&entry, sizeof(entry));
    }
}

// FUNCTION: WIZ8 0x005bdd00
void DrawJournalLine(SDL_Surface& target, std::string_view text, int column, int y, int palette,
                     bool centered)
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
    if (palette == 0) {
        g_journal_font.resetPalette();
    } else if (palette == 1) {
        g_journal_font.setPalette(g_journal_font_palette);
    }
    SDL_Rect clip{left, y, right, 0x1e};
    ScopedClipRect clipping(target, clip);
    DrawText(target, g_journal_font, {left + x, y}, text);
}

// FUNCTION: WIZ8 0x005bd860
void JournalScreen::Refresh()
{
    if (!m_alternate_mode) {
        const int last_page = (static_cast<int>(g_journal_entries.size()) - 1) / 12;
        const int page_count = last_page + 1;
        if (page_count != g_journal_page_count || g_journal_page < 0 ||
            g_journal_page >= page_count) {
            g_journal_page_count = page_count;
            g_journal_page = last_page;
        }
        m_buttons[Next]->SetEnabled(g_journal_page < last_page);
        m_buttons[Previous]->SetEnabled(g_journal_page > 0);
        m_page_text->SetText(fmt::format("{:d} / {:d}", g_journal_page + 1, page_count),
                             g_options_detail_font);
    } else {
        m_buttons[Next]->SetEnabled(false);
        m_buttons[Previous]->SetEnabled(false);
        m_page_text->SetText(g_journal_alternate_page, g_options_detail_font);
    }
    m_dirty = true;
}

void JournalScreen::DrawContent(SDL_Surface& target)
{
    if (!m_alternate_mode) {
        CatalogArtwork::Load(0x1b8, 0).drawAndInvalidate(target, 0, 0, 0);
        DrawJournalLine(target, g_localized_strings[0x6db], 0, 0x19, 0, true);
        DrawJournalLine(target, g_localized_strings[0x6dc], 1, 0x19, 0, true);

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
                DrawJournalLine(target, level_name, 0, y, 0, true);
            }
            DrawJournalLine(target, description, 1, y, active, false);
            previous_level = entry->level;
        }
    } else {
        CatalogArtwork::Load(0x1b8, 0).drawAndInvalidate(target, 0, 0, 0);
        DrawJournalLine(target, g_localized_strings[0x6de], 0, 0x19, 0, true);
        DrawJournalLine(target, g_localized_strings[0x6df], 1, 0x19, 0, true);

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
                    DrawJournalLine(target, disposition_name, 0, y, 0, true);
                }
                DrawJournalLine(target, g_localized_strings[g_journal_faction_name_indices[index]],
                                1, y, 0, false);
                y += 0x1e;
            }
        }
    }
    InvalidateRegion(0, 0, 640, 480);
}

JournalScreen::JournalScreen()
{
    auto add = [&](JournalAction action, int artwork_id, SDL_Point origin,
                   std::array<int, 5> frames, ButtonToggle toggle = ButtonToggle::None) {
        auto artwork = CatalogArtwork::Load(artwork_id, 0);
        const auto size = artwork.size(frames[0]);
        const SDL_Rect bounds{origin.x, origin.y, size.x, size.y};
        auto& button = m_buttons[action];
        button = std::make_unique<W8ButtonWidget>(m_input);
        const auto frame = [&](int index) {
            return index < 0 ? -1 : index + artwork.imageOffset();
        };
        button->Configure(ButtonGraphics::Load(artwork.spriteSheet(), frame(frames[4]),
                                               frame(frames[0]), frame(frames[2]), frame(frames[1]),
                                               frame(frames[3])),
                          {.on_click =
                               [this, action](W8ButtonWidget*) {
                                   AfterUiDispatch([this, action] { Activate(action); });
                               },
                           .policy = {.primary_toggle = toggle,
                                      .input_rule = ButtonInputRule::PrimaryPressedState},
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});
        button->SetPosition(bounds.x, bounds.y);
        button->Resize(bounds.w, bounds.h);
    };
    add(Previous, 0xf4, {0x69, 0x1be}, {0, 2, 1, -1, 3});
    add(Next, 0xf4, {0x185, 0x1be}, {4, 6, 5, -1, 7});
    add(Mode, 0x1bb, {0x216, 0x1b9}, {0, 2, 1, 2, 3}, ButtonToggle::Release);
    add(Close, 0x106, {0x250, 0x1b9}, {0x10, -1, 0x11, 0x12, 0x13});
    m_buttons[Mode]->SetTooltipIndex(0x6eb);
    m_buttons[Close]->SetTooltipIndex(0x6ed);
    const auto size = m_bar.size(0);
    const W8ControlsRect bounds{0x66, 0x1bb, 0x66 + size.x, 0x1bb + size.y};
    m_page_text =
        std::make_unique<W8TextBuffer>(&bounds, "", g_options_detail_font,
                                       g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
}

void JournalScreen::Draw(SDL_Surface& target)
{
    bool redraw_buttons = m_dirty;
    for (auto& button : m_buttons) redraw_buttons = button->m_dirty || redraw_buttons;
    if (m_dirty) DrawContent(target);
    if (redraw_buttons) {
        CatalogArtwork::Load(0x1b7, 0).drawAndInvalidate(target, 1, 0, 0x1b4);
        m_bar.drawAndInvalidate(target, 0, 0x66, 0x1bb);
        for (auto& button : m_buttons) {
            button->DrawAndInvalidate(target, true);
        }
        m_page_text->RenderToTarget(0, true, target);
    }
    m_dirty = false;
}

void JournalScreen::Activate(JournalAction action)
{
    switch (action) {
    case Previous:
        if (g_journal_page > 0) {
            --g_journal_page;
            Refresh();
        }
        break;
    case Next:
        if (g_journal_page < (static_cast<int>(g_journal_entries.size()) - 1) / 12) {
            ++g_journal_page;
            Refresh();
        }
        break;
    case Close:
        RequestScreenTransition();
        break;
    case Mode:
        m_alternate_mode = m_buttons[Mode]->IsPressed();
        m_buttons[Mode]->SetTooltipIndex(m_alternate_mode ? 0x6ec : 0x6eb);
        Refresh();
        break;
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
    if (!g_journal_font.load("Data\\Journal\\journal_font.sti")) return 0;
    g_journal_font_palette = CatalogArtwork::Load(0x1b9).spriteSheet()->colors;
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

    SetViewport({0, 0, 0x281, 0x1e1});
    SetPrimarySurfaceTextureHint2Enabled(false);
    UpdateHeldItemCursor();
    g_journal_panel = std::make_unique<JournalScreen>();

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
        if ((g_journal_show_all || fact->visibility <= maximum_visibility) &&
            !description.empty()) {
            g_journal_entries.push_back(entry);
        }
    }
    g_journal_panel->Refresh();
    return 1;
}

// FUNCTION: WIZ8 0x005be0b0
unsigned char JournalScreenLeave(int)
{
    g_journal_panel.reset();
    g_journal_entries.clear();
    if (gXStatus.fCampMode) {
        SyncDialogueNpcState();
    }
    return 1;
}

// FUNCTION: WIZ8 0x005be110
void JournalScreenFrame(void)
{

    PumpUiInput({.unhandled = [&](const InputEvent& input) {
        if (input.type == InputEventType::KeyDown) {
            if (input.key == VK_ESCAPE) {
                AfterUiDispatch([] { RequestScreenTransition(); });
            } else if (input.key == VK_LEFT && g_journal_page > 0) {
                --g_journal_page;
                g_journal_panel->Refresh();
            } else if (input.key == VK_RIGHT &&
                       g_journal_page < (static_cast<int>(g_journal_entries.size()) - 1) / 12) {
                ++g_journal_page;
                g_journal_panel->Refresh();
            }
        }
        return InputRoute::Handled;
    }});
    g_journal_panel->Draw(GetFrameBufferSurface());
    RenderFrame();
}
