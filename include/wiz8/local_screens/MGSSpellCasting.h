#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "wiz8/data/records.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/platform/input.h"
#include "wiz8/ui_layer.h"

struct MainGameScreen;
struct W8Character;

inline constexpr int W8_SPELL_POWER_PIP_COUNT = 9;

/* The spell picker for one caster (`caster`, which follows the selected
   character). The main-game screen opens and owns it, with its input and
   controls, for exactly as long as it is open; whoever opened it supplies
   `on_return`, which the screen runs once a cast is chosen or the picker is
   cancelled. Abandoning the screen discards the picker without running it. */
struct W8SpellCastingView {
    W8SpellCastingView(MainGameScreen& screen, std::move_only_function<void()> on_return)
        : screen(screen), on_return(std::move(on_return))
    {
    }

    // The screen that owns the picker, and through it the game it casts in.
    MainGameScreen& screen;
    UiLayer input{UiLayerOrder::SpellCasting};
    HitTarget* name_target = nullptr;
    W8Character* caster = nullptr;
    W8SpellRealm iSpellRealm = W8_SPELL_REALM_NONE; /* selected realm, -1 when none */
    W8Action draft{W8_ACTION_CAST_SPELL, 0};
    uint64_t realm_anim_timer;
    unsigned int realm_anim_frame;
    W8LearnedSpellState learned;
    int uiSpellIndex = -1; /* clicked list row */
    int selected_spell_index = -1;
    std::array<std::unique_ptr<W8ButtonWidget>, 6> realm_buttons;
    std::array<W8TextBuffer, 6> realm_text;
    std::array<std::unique_ptr<W8ButtonWidget>, 9> power_controls;
    std::array<std::array<int, 5>, 9> power_frames;
    std::unique_ptr<W8ButtonWidget> cancel;
    W8TextBuffer spell_name;
    struct SpellRow {
        int spell;
        signed char color;
    };
    std::vector<SpellRow> spells;
    bool dialog_confirmed = false;
    std::move_only_function<void()> on_return;

    /* Build the three panels' controls. */
    void CreateControls();
    W8SpellPowerClass PowerClass() const;
    unsigned int AffordablePowerLevels() const;
    /* Point the picker at a new caster, closing it when the caster cannot cast. */
    void SelectCharacter(int party_slot);
    /* Select the spell's realm and list row. */
    void SelectSpell(int spell_id);
    void SelectPowerLevel(int power_level);
    /* Before the draw, add the panels whose widgets changed to `panels`; the draw
       then paints the requested ones. */
    void UpdatePanels(std::bitset<7>& panels);
    void PaintPanels(std::bitset<7> panels);
    /* Per frame: advance the realm icon and commit a ready cast. */
    void Update();
    /* Text-box body input while the picker is open. */
    UiResult HandleTextBox(const UiPointerEvent& event);
    /* The cancel button: close and return to the opener. */
    void Cancel();

private:
    // Request a panel's repaint.
    void Repaint(int panel);
    void UpdateRealmPointDisplays();
    void ClearChoice();
    void RefreshAvailablePowerPips();
    void UpdatePowerPips();
    void RefreshPowerPip(int pip);
    void RebuildList(int spell_id);
    void SetLineColor(int index, signed char color);
    void SelectRow(int index);
    void SelectRealm(W8SpellRealm realm);
    void SelectPip7();
    void SelectPip8();
    void PreviewPowerPipHover(int power_level);
    void DialogResult(bool accepted);
    void SelectListRow(int index);
    void TryCommit();
    void ShowError(int spell_id);
};

/* The main game's open spell picker, or null. */
W8SpellCastingView* SpellPicker();
void RestoreSpellCastingRegions(void);

/* Destroy callback for the spell/use-item reason notices. */
void SpellCastingNoticeClosed(bool);
