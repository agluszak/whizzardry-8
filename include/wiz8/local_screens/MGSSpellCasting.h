#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "compat/compiler.h"
#include "input.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/ui_layer.h"

struct W8Character;

inline constexpr int W8_SPELL_POWER_PIP_COUNT = 9;

/* The view owns its input and controls for exactly as long as it is open. */
struct W8SpellCastingView {
    UiLayer input{UiLayerOrder::SpellCasting};
    HitTarget* name_target = nullptr;
    W8Character* caster;
    W8SpellRealm iSpellRealm; /* selected realm, -1 when none */
    int uiSpellToCast;
    int override_spell;                 /* detail/commit override spell */
    int iSpellPower;                    /* chosen power index, -1 when unset */
    W8SpellPowerClass iSpellPowerClass; /* the spell record's power class */
    unsigned int uiPowerLevels;         /* affordable power-level count */
    uint64_t realm_anim_timer;
    unsigned int realm_anim_frame;
    W8LearnedSpellState learned;
    int uiSpellIndex; /* clicked list row */
    int selected_spell_index;
    std::array<std::unique_ptr<W8ButtonWidget>, 6> realm_buttons;
    std::array<W8TextBuffer, 6> realm_text;
    std::array<std::unique_ptr<W8ButtonWidget>, 9> power_controls;
    std::array<std::array<int, 5>, 9> power_frames;
    std::unique_ptr<W8ButtonWidget> cancel;
    W8TextBuffer spell_name;
    std::array<bool, 3> dirty{true, true, true};
    W8MainUiMode saved_game_mode;
    W8NpcDialogueLayout interact_id;
    int location_id;
    unsigned int uiSpellsInList;
    int uiSpells[0x15e];
    signed char alt_colors[0x15e];
    bool closing; /* close already in progress */
    bool dialog_confirmed;
};

unsigned char OpenSpellCastingView(int party_slot);
void CloseSpellCastingView(void);
void RestoreSpellCastingRegions(void);
void SelectSpellCastingCharacter(int party_slot);
void BeginSpellCast(int spell_id, int location_id, W8NpcDialogueLayout interact_id);
void SetSpellCastingPanelsActive(bool active);
void InvalidateSpellCastingDescription(void);
void SelectSpellPowerLevel(int power_level);
void ResetSpellCastingSelection(void);
void CommitSpellCastingSelection(void);
void SetSpellCastingMode(W8MainUiMode value);
int GetSpellCastingSelection(void);

class W8DialogBase;

/* Destroy callback for the spell/use-item reason notices. */
void SpellCastingNoticeClosed(bool);
/* Text-box body callback while spell casting is active. */
UiResult HandleSpellTextBox(const UiPointerEvent& event);

/* Live spell-casting view, or null when the panel is closed. */
extern std::unique_ptr<W8SpellCastingView> gpSCSV;
/* gpSCSV->iSpellPower, or -1 when the view is closed. */
int GetSpellCastingPowerIndex(void);
