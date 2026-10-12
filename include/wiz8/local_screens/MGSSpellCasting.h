#pragma once

#include <cstdint>
#include <memory>

#include "input.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "compat/compiler.h"

class W8TextControl;
class ScreenRegionSet;
struct Controls;
struct W8Character;

enum {
    W8_SPELL_POWER_PIP_COUNT = 9,
    W8_SPELL_NAME_CONTROL = 9,
    W8_SPELL_CANCEL_CONTROL = 10,
    W8_SPELL_POWER_CONTROL_COUNT = 11
};

/* The spell-casting view state gpSCSV, malloc'd when the view opens. */
struct W8SpellCastingView {
    std::uint8_t unknown_000[0xf8];
    W8Character* caster;
    W8SpellRealm iSpellRealm; /* selected realm, -1 when none */
    std::int32_t uiSpellToCast;
    std::int32_t override_spell;                 /* detail/commit override spell */
    std::int32_t iSpellPower;                    /* chosen power index, -1 when unset */
    W8SpellPowerClass iSpellPowerClass; /* the spell record's power class */
    std::uint32_t uiPowerLevels;         /* affordable power-level count */
    std::uint64_t realm_anim_timer;
    std::uint32_t realm_anim_frame;
    W8LearnedSpellState learned;
    std::int32_t uiSpellIndex; /* clicked list row */
    std::int32_t selected_spell_index;
    std::int32_t field_500;
    std::unique_ptr<Controls> panels[3];
    W8TextControl* realm_buttons[6];
    W8TextControl* realm_icons[6];
    /* Indexed by region callback id. */
    W8TextControl* power_controls[W8_SPELL_POWER_CONTROL_COUNT];
    W8MainUiMode saved_game_mode;
    bool input_blocked;
    std::uint8_t pad_571[3];
    std::int32_t field_574;
    W8NpcDialogueLayout interact_id;
    std::int32_t location_id;
    std::uint32_t uiSpellsInList;
    std::int32_t uiSpells[0x15e];
    std::int8_t alt_colors[0x15e];
    bool closing; /* close already in progress */
    bool dialog_confirmed;
};

std::uint8_t OpenSpellCastingView(std::int32_t party_slot);
void CloseSpellCastingView(void);
extern ScreenRegionSet g_spell_casting_regions;
enum SpellCastingRegionOffset {
    kSpellPowerPipRegionOffset = 11,
};
void RestoreSpellCastingRegions(void);
void SelectSpellCastingCharacter(std::int32_t party_slot);
void BeginSpellCast(std::int32_t spell_id, std::int32_t location_id, W8NpcDialogueLayout interact_id);
void SetSpellCastingPanelsActive(bool active);
void InvalidateSpellCastingDescription(void);
void SelectSpellPowerLevel(std::int32_t power_level);
void ResetSpellCastingSelection(void);
void CommitSpellCastingSelection(void);
void SetSpellCastingMode(W8MainUiMode value);
std::int32_t GetSpellCastingSelection(void);

struct W8Region;
class W8DialogBase;

/* Destroy callback for the spell/use-item reason notices. */
void SpellCastingNoticeClosed(W8DialogBase* dialog);
std::uint8_t SpellRealmButtonRegionEvent(const InputEvent* event, W8Region* region);
std::uint8_t SpellPowerPipRegionEvent(const InputEvent* event, W8Region* region);
std::uint8_t IgnoreSpellCastingInput(const InputEvent* event, W8Region* region);
/* Text-box body callback while spell casting is active. */
std::uint8_t SpellCastTextBoxRegionEvent(const InputEvent* event, W8Region* region);

/* Live spell-casting view, or null when the panel is closed. */
extern std::unique_ptr<W8SpellCastingView> gpSCSV;
/* gpSCSV->iSpellPower, or -1 when the view is closed. */
std::int32_t GetSpellCastingPowerIndex(void);
