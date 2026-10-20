#include "wiz8/local_screens/JournalScreen.h"

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "difficulty.h"
#include "message_box.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/chunk.h"
#include "wiz8/cursor.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/world/level_session.h"
#include "wiz8/xstatus.h"

namespace {
enum JournalAction { Previous, Next, Mode, Close };

class JournalScreen final : public Screen {
public:
    JournalScreen();
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;
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

int g_journal_page = -1;
bool g_journal_show_all;
char g_default_level[] = "Default Level";
signed char g_journal_factions[12] = {4, 5, 6, 7, 8, 9, 12, 11, 13, 15, 16, 0};
int g_journal_faction_name_indices[11] = {
    0x6e0, 0x6e1, 0x6e2, 0x6e3, 0x6e4, 0x6e5, 0x6e6, 0x6e7, 0x6e8, 0x6e9, 0x6ea,
};
char g_journal_alternate_page[] = "1 / 1";
int g_journal_page_count = -1;
/* The highlight palette, frame 0x1b9 of the catalog, taken while the screen is open. */
Palette16 g_journal_font_palette;
std::vector<W8JournalEntry> g_journal_entries;
std::vector<W8JournalEntry> g_fact_journal_entries;

bool g_fact_notifications_suppressed;

void InitializeFactJournal(void)
{
    g_fact_journal_entries.clear();
}

/* Append one changed fact to the journal and, when notices are not suppressed
   and the fact is visible at the current notice setting, post the fact's own
   description through the notice pane with its sound. */
void RecordFactChangeForJournal(W8FactId fact_id)
{
    W8JournalEntry entry;
    entry.level = GetLoadedLevelID();
    entry.fact = fact_id;
    entry.alternate_text = GetFact(fact_id) != 0;
    g_fact_journal_entries.push_back(entry);

    if (g_fact_notifications_suppressed ||
        static_cast<unsigned>(fact_id) >= g_fact_records.size()) {
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
    int range = (g_level_block->text_box_right - g_level_block->text_box_left);
    ShowNotice(W8_FONT_PALETTE_BLUE, g_localized_strings[0x74a], 2, range);
    wiz8::sound::play("Data\\Sound\\Misc\\Journal Entry.wav");
}

namespace {
/* A JRNL entry on disk. Retail writes the alternate-text flag as one byte and
   leaves the three bytes after it uninitialized. */
struct JournalEntryRecord {
    std::int32_t level;
    std::int32_t fact;
    std::uint8_t alternate_text;
    std::array<std::uint8_t, 3> unknown_09;
};
static_assert(sizeof(JournalEntryRecord) == 12);
}

/* Write the JRNL format dword, entry count, then each 0x0c-byte entry. */
void SaveFactJournal(wiz8::File& file)
{
    int format = 1;
    int count;

    count = static_cast<int>(g_fact_journal_entries.size());
    file.write(&format, 4);
    file.write(&count, 4);
    for (const auto& entry : g_fact_journal_entries) {
        const JournalEntryRecord record{entry.level, static_cast<std::int32_t>(entry.fact),
                                        entry.alternate_text, entry.unknown_09};
        file.write(&record, sizeof(record));
    }
}

/* Resize the live journal entries before reading the JRNL records. */
void LoadJournalEntries(W8Chunk& file)
{
    int format;
    int count;

    InitializeFactJournal();
    file.ReadBytes(&format, 4);
    file.ReadBytes(&count, 4);
    if (count < 0 || count > file.RemainingBytes() / std::int64_t{sizeof(JournalEntryRecord)})
        throw std::runtime_error("invalid journal count");
    g_fact_journal_entries.resize(count);
    for (auto& entry : g_fact_journal_entries) {
        JournalEntryRecord record;
        file.ReadBytes(&record, sizeof(record));
        entry = {record.level, static_cast<W8FactId>(record.fact), record.alternate_text != 0,
                 record.unknown_09};
    }
}

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
            if (g_factions[faction].encountered) {
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
    for (auto& button : m_buttons) redraw_buttons |= button->TakeDirty();
    if (m_dirty) DrawContent(target);
    if (redraw_buttons) {
        CatalogArtwork::Load(0x1b7, 0).drawAndInvalidate(target, 1, 0, 0x1b4);
        m_bar.drawAndInvalidate(target, 0, 0x66, 0x1bb);
        for (auto& button : m_buttons) button->Draw(target);
        m_page_text->Paint(target);
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
        ReturnToPreviousScreen();
        break;
    case Mode:
        m_alternate_mode = m_buttons[Mode]->IsPressed();
        m_buttons[Mode]->SetTooltipIndex(m_alternate_mode ? 0x6ec : 0x6eb);
        Refresh();
        break;
    }
}

bool JournalScreen::enter(const ScreenRequest&)
{
    int maximum_visibility;

    SetViewport({0, 0, 0x281, 0x1e1});
    SetPrimarySurfaceTextureHint2Enabled(false);
    UpdateHeldItemCursor();
    g_journal_font_palette = CatalogArtwork::Load(0x1b9).spriteSheet()->colors;

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
        // Retail saves can name a fact past the end of FACT.DBS; keep it, show nothing.
        if (static_cast<unsigned>(entry.fact) >= g_fact_records.size()) continue;
        const W8FactDatabaseRecord* fact = &g_fact_records[entry.fact];
        std::string description =
            entry.alternate_text ? fact->alternate_description : fact->description;
        if ((g_journal_show_all || fact->visibility <= maximum_visibility) &&
            !description.empty()) {
            g_journal_entries.push_back(entry);
        }
    }
    Refresh();
    return true;
}

bool JournalScreen::leave(ScreenDeparture)
{
    g_journal_entries.clear();
    if (gXStatus.fCampMode) {
        SyncDialogueNpcState();
    }
    return true;
}

void JournalScreen::frame()
{

    PumpUiInput({.unhandled = [&](const InputEvent& input) {
        if (input.type == InputEventType::KeyDown) {
            if (input.key == SDLK_ESCAPE) {
                AfterUiDispatch([] { ReturnToPreviousScreen(); });
            } else if (input.key == SDLK_LEFT && g_journal_page > 0) {
                --g_journal_page;
                Refresh();
            } else if (input.key == SDLK_RIGHT &&
                       g_journal_page < (static_cast<int>(g_journal_entries.size()) - 1) / 12) {
                ++g_journal_page;
                Refresh();
            }
        }
        return InputRoute::Handled;
    }});
    Draw(GetFrameBufferSurface());
    RenderFrame();
}

std::unique_ptr<Screen> CreateJournalScreen()
{
    return std::make_unique<JournalScreen>();
}
