#include "wiz8/local_screens/MGSPortraits.h"

#include <array>
#include <bitset>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "layouts/main_game_screen.h"
#include "layouts/npc_dialogue.h"
#include "local_code/ControlsRect.h"
#include "local_code/MonsterManager.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

static void DrawDamageSplatOverlay(unsigned int party_slot);
void DrawPortraitEffectIcon(unsigned int party_slot);

/* Dead-character portrait catalog ids, two per race - the small
   party-strip image at [race][0] and the large header portrait at [race][1]. */
int g_dead_portrait_catalog_ids[W8_RACE_COUNT][2] = {
    {20, 31}, {20, 31}, {20, 31}, {20, 31}, {20, 31}, {20, 31}, {23, 34}, {21, 32},
    {22, 33}, {25, 36}, {24, 35}, {27, 38}, {26, 37}, {28, 39}, {29, 40}, {30, 41},
};

/* Empty-hand catalog ids when a primary hand slot is bare. Each race
   stores the right-hand id at +0 and the left-hand id at +4 (retail also reaches
   the left id through 0x00649DD8). */
int g_empty_hand_catalog_ids[32] = {
    103, 102, 103, 102, 103, 102, 103, 102, 103, 102, 105, 104, 109, 108, 109, 108,
    107, 106, 107, 106, 107, 106, 107, 106, 109, 108, 103, 102, 109, 108, 111, 110,
};

namespace {
struct PortraitButtons {
    UiLayer input{UiLayerOrder::PortraitButtons};
    std::array<std::unique_ptr<W8ButtonWidget>, 8> buttons;
};
std::unique_ptr<PortraitButtons> portrait_buttons;
struct ConditionIcons {
    UiLayer input{UiLayerOrder::PortraitStatus};
    struct Icon {
        HitTarget* target;
        SDL_Point position;
        int catalog = 0;
        bool dirty = true;
    };
    std::array<Icon, 8> icons;
    bool active = false;
};
std::unique_ptr<ConditionIcons> condition_icons;
}
char s_general_magic_sound[] = "Data\\Spells\\Sounds\\GeneralMagic.wav";

/* Refresh cached HP/stamina/spell portrait bar widths; dirty + redraw when
   any slot's displayed fraction (or numeric HP) changes. */
void SyncPartyPortraitVitalsBars(void)
{
    int slot;

    for (slot = 0; slot < 8; ++slot) {
        W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[slot];
        unsigned int hp_bar = 0;
        unsigned int stamina_bar = 0;
        unsigned int spell_bar = 0;
        W8Character* character;

        if (!CurrentGame().party.members[slot].has_value()) {
            continue;
        }

        character = &CurrentGame().party.characters[slot];
        if (character->hp_current == 0) {
            hp_bar = 0;
            stamina_bar = 0;
        } else {
            unsigned int stamina;
            int left;
            int total;

            hp_bar = (character->hp_current * 0x2d) / static_cast<unsigned int>(character->uiHPMax);
            if (hp_bar == 0) {
                hp_bar = 1;
            }

            stamina = character->stamina;
            stamina_bar = ((static_cast<int>(stamina) < 0 ? 0 : stamina) * 0x2d) /
                          static_cast<unsigned int>(character->uiStaminaMax);
            if (stamina_bar == 0 && stamina != 0) {
                stamina_bar = 1;
            }

            if (SumCharacterSpellPoints(character) == 0) {
                spell_bar = 0;
            } else {
                left = SumCharacterSpellPointsLeft(character);
                if (left < 0) {
                    left = 0;
                } else {
                    left = SumCharacterSpellPointsLeft(character);
                }
                total = SumCharacterSpellPoints(character);
                spell_bar =
                    (static_cast<unsigned int>(left) * 0x2d) / static_cast<unsigned int>(total);
                if (spell_bar == 0 && SumCharacterSpellPointsLeft(character) != 0) {
                    spell_bar = 1;
                }
            }

            if (hp_bar != static_cast<unsigned int>(entry->cached_hp_bar) ||
                stamina_bar != static_cast<unsigned int>(entry->cached_stamina_bar) ||
                spell_bar != static_cast<unsigned int>(entry->cached_spell_bar) ||
                (g_settings.options.numeric_hit_points != 0 &&
                 static_cast<int>(character->hp_current) != entry->cached_hp)) {
                if (auto* hud = MainGameHud()) hud->repaint.portrait_bars.set(slot);
            }
        }

        entry->cached_hp_bar = static_cast<int>(hp_bar);
        entry->cached_stamina_bar = static_cast<int>(stamina_bar);
        entry->cached_spell_bar = static_cast<int>(spell_bar);
        entry->cached_hp = static_cast<int>(character->hp_current);
    }
}

/* Open the slot's floating damage-number splat on a fresh hit, or accumulate
   into it while one is already up: the death variant runs the longer
   0x92-catalog animation when the character's hit points are gone, a forced
   portrait refresh defers the frame to -1, and a still-playing effect icon
   defers it likewise. */
void RecordCharacterDamage(int party_slot, unsigned int amount)
{
    bool splat_started = false;

    if (CurrentGame().surprise_possible) {
        return;
    }
    W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[party_slot];
    if (!entry->damage_splat_active) {
        entry->damage_splat_amount = amount;
        entry->damage_splat_active = true;
        if (PresentationRng().below(2) == 0) {
            entry->damage_splat_catalog = 0x90;
        } else {
            entry->damage_splat_catalog = 0x91;
        }
        if (CurrentGame().party.characters[party_slot].hp_current == 0) {
            entry->damage_splat_death_variant = true;
            entry->damage_splat_end_frame = 0x1e;
        } else {
            entry->damage_splat_death_variant = false;
            entry->damage_splat_end_frame = 8;
        }
        entry->damage_splat_frame = 0;
        splat_started = true;
        if (GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_PORTRAITS &&
            MainGameHud()->portrait_refresh_pending[party_slot] == 0) {
            RefreshSelectedPartyPortrait(party_slot);
            entry->auto_portrait_refresh = true;
            entry->damage_splat_frame = -1;
        }
    } else {
        entry->damage_splat_amount += amount;
        entry->damage_splat_frame = 0;
        if (CurrentGame().party.characters[party_slot].hp_current == 0 &&
            !entry->damage_splat_death_variant) {
            entry->damage_splat_death_variant = true;
            entry->damage_splat_end_frame = 0x1e;
        }
    }
    if (!entry->keyboard_menu_open) {
        if (auto* hud = MainGameHud()) hud->repaint.portrait(party_slot);
    }
    if (entry->effect_icon_active && splat_started) {
        entry->damage_splat_frame = -1;
        return;
    }
    entry->portrait_fx_clock = MonotonicMilliseconds() + (100);
}

/* Draw the slot's floating damage-number splat over the portrait: the picked
   (or death-variant) catalog image at the current frame plus the running
   damage total in bold text for the first six frames. Frame -1 means the
   splat is deferred behind a portrait refresh or a still-playing effect
   icon. */
static void DrawDamageSplatOverlay(unsigned int party_slot)
{
    W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[party_slot];
    int frame = entry->damage_splat_frame;
    int top = 0;

    if (frame != -1) {
        int left = ((party_slot & 1) != 0 ? 0x1ff : 0) + 0x16;
        switch (party_slot >> 1) {
        case 0:
            top = 0x12;
            break;
        case 1:
            top = 0x67;
            break;
        case 2:
            top = 0xbc;
            break;
        case 3:
            top = 0x111;
            break;
        }
        unsigned int object =
            !entry->damage_splat_death_variant ? entry->damage_splat_catalog : 0x92;
        CatalogArtwork::Load(object, 0).draw(GetFrameBufferSurface(), static_cast<short>(frame),
                                             left, top);
        if (entry->damage_splat_frame < 6) {
            W8ControlsRect bounds;
            bounds.left = left;
            bounds.top = top + 4;
            bounds.right = left + 0x52;
            bounds.bottom = top + 0x4c;
            W8TextBuffer splat_text(&bounds, {}, g_wiz_text_bold_font, 0, 4);
            splat_text.RenderString(fmt::format("{:d}", entry->damage_splat_amount),
                                    g_wiz_text_bold_font, false, GetFrameBufferSurface());
        }
        if (CurrentGame().combat.has_value()) {
            entry->combat_portrait_dirty = true;
        }
    }
}

/* Draw the in-flight spell/condition effect icon over the slot's portrait.
   frame -1 is the deferred-start state and draws nothing; the icon is skipped
   while a pending portrait refresh sits under another overlay (the settings
   mode parked off PORTRAITS with the slot's refresh flag set). */
void DrawPortraitEffectIcon(unsigned int party_slot)
{
    int image = CurrentGame().monster_manager_entries[party_slot].effect_icon_frame;
    int top = 0;
    if (image != -1) {
        switch (party_slot >> 1) {
        case 0:
            top = 0x12;
            break;
        case 1:
            top = 0x67;
            break;
        case 2:
            top = 0xbc;
            break;
        case 3:
            top = 0x111;
        }
        if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
            MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
            CatalogArtwork::Load(
                CurrentGame().monster_manager_entries[party_slot].effect_icon_catalog, 0)
                .drawAndInvalidate(GetFrameBufferSurface(), image, (party_slot & 1) << 9 | 0x17,
                                   top);
        }
    }
}

/* Advance the per-slot portrait FX counters on a 100ms clock and request a
   redraw when a visible slot's animation frame advances. */
void TickPartyPortraitFx(void)
{
    unsigned char slot;

    for (slot = 0; slot < 8; ++slot) {
        W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[slot];
        bool clock_expired;
        bool dirty = false;

        if (!CurrentGame().party.members[slot].has_value()) {
            continue;
        }

        clock_expired = TimeUntilDeadline(entry->portrait_fx_clock) == 0;
        if (entry->damage_splat_active) {
            if (clock_expired) {
                ++entry->damage_splat_frame;
                dirty = true;
            }
            if (entry->damage_splat_frame == entry->damage_splat_end_frame) {
                entry->damage_splat_active = false;
                entry->damage_splat_death_variant = false;
            } else if (entry->damage_splat_death_variant && entry->damage_splat_frame == 0xd &&
                       !entry->dead_portrait_revealed) {
                entry->dead_portrait_revealed = true;
            }
        }
        if (entry->effect_icon_active) {
            if (clock_expired) {
                ++entry->effect_icon_frame;
                dirty = true;
            }
            if (entry->effect_icon_frame == entry->effect_icon_end_frame) {
                entry->effect_icon_active = false;
            }
        }
        if (clock_expired) {
            entry->portrait_fx_clock = MonotonicMilliseconds() + (100);
        }
        if (dirty && !entry->keyboard_menu_open) {
            if (auto* hud = MainGameHud()) hud->repaint.portrait(slot);
        }
    }
}

/* Clear each occupied slot's damage-splat and effect-icon portrait overlays
   and rearm the shared FX clock; the main-game screen leave runs it so a
   pending animation does not survive the screen transition. */
void ResetPartyPortraitFx(void)
{
    unsigned char slot;

    for (slot = 0; slot < 8; ++slot) {
        W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[slot];

        if (!CurrentGame().party.members[slot].has_value()) {
            continue;
        }
        entry->effect_icon_active = false;
        entry->effect_icon_frame = -1;
        entry->effect_icon_catalog = -1;
        entry->damage_splat_active = false;
        entry->damage_splat_death_variant = false;
        entry->dead_portrait_revealed = false;
        entry->damage_splat_frame = -1;
        entry->portrait_fx_clock = MonotonicMilliseconds() + (0);
    }
}

/* Draw each occupied, living and eligible party slot's combat portrait in the
   side strip once the slot's dirty flag is raised: the normal frame, or the
   alternate while the slot is the hovered combat slot, then the can't-act
   badge or the party-member target marker. The slot currently acting is drawn
   by the action panel instead. */
void RedrawCombatPortraits(void)
{
    int portrait_x;
    int badge_x;
    int row_y;
    int portrait_image;
    int slot;

    for (slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* party_row = &CurrentGame().party.rows[slot];
        W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[slot];
        W8CombatCharacterRow* combat_row = &CurrentGame().combat->characters[slot];

        if (!CanPartySlotParticipate(slot) || combat_row->portrait_image == -1 ||
            !entry->combat_portrait_dirty || slot == MainGameHud()->combat_slot) {
            continue;
        }
        if ((slot & 1) == 0) {
            portrait_x = 0x19;
            badge_x = 0x19;
        } else {
            portrait_x = 0x253;
            badge_x = 0x25b;
        }
        row_y = (slot >> 1) * 0x55;
        if (GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_PORTRAITS &&
            MainGameHud()->portrait_refresh_pending[slot] == 0) {
            EraseRegion(portrait_x, row_y + 0x37, portrait_x + 0x14, row_y + 0x58);
        }
        if (MainGameHud()->combat_action_hover_party_slot == -1 ||
            MainGameHud()->combat_action_hover_party_slot != slot) {
            portrait_image = combat_row->portrait_image;
        } else {
            portrait_image = combat_row->portrait_image_alternate;
        }
        CatalogArtwork::Load(0x8a, 0).drawAndInvalidate(GetFrameBufferSurface(), portrait_image,
                                                        portrait_x, row_y + 0x44);
        if (!CharacterCanSwitchTo(slot, SelectCombatAction(slot, W8_TARGETING_CONTEXT_IN_COMBAT),
                                  false, false)) {
            CatalogArtwork::Load(0x8b, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, badge_x,
                                                            row_y + 0x37);
        } else if (party_row->committed.target.iType == W8_TARGET_KIND_CHARACTER) {
            CatalogArtwork::Load(0x8c, 0).drawAndInvalidate(
                GetFrameBufferSurface(),
                CurrentGame().party.orderIndexOf(party_row->committed.target.iChar), badge_x,
                row_y + 0x38);
        }
        entry->combat_portrait_dirty = false;
    }
}

/* Toggle numeric hit-point display on the party portraits and invalidate all
   eight slot masks so the new mode repaints everywhere. */
void ToggleNumericHitPoints(void)
{
    g_settings.options.numeric_hit_points = g_settings.options.numeric_hit_points == 0;
    for (unsigned int slot = 0; slot < 8; slot++) {
        if (auto* hud = MainGameHud()) hud->repaint.portrait(slot);
    }
}

// Offset follows the mirrored hover-slide direction.
DesignRect PartyPortraitBounds(unsigned int slot, int offset)
{
    const int left = (slot & 1) ? 512 - offset : offset;
    const int top = 18 + static_cast<int>(slot / 2) * 85;
    return {left, top, left + 128, top + 86};
}

/* The slot's anchor positions for the keyboard menu and portrait band: the
   panel corner, the band's two x edges (their order swaps with the column),
   the grid row and the column pixel. 'adjust' applies the compact-display
   shift used when the party display is a single column. */
void GetPartySlotMenuAnchor(int party_slot, int* menu_x, int* menu_y, int* band_menu_edge,
                            int* band_portrait_edge, int* grid_row, int* column_x, int adjust)
{
    if (party_slot < 0 || party_slot >= 8)
        throw std::logic_error("GetPartySlotMenuAnchor: party slot out of range");
    const auto rect = PartyPortraitBounds(party_slot);
    const bool right = (party_slot & 1) != 0;
    *menu_x = *column_x = rect.left;
    *menu_y = rect.top;
    *grid_row = 1 + (party_slot / 2) * 4 + right;
    *band_menu_edge = rect.left + (right ? 0x6b : 2);
    *band_portrait_edge = rect.left + (right ? 0 : 0x69);
    if (adjust != 0 && GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_PORTRAITS &&
        static_cast<unsigned int>(GetEffectiveMainUiMode()) <=
            static_cast<unsigned int>(W8_MAIN_UI_MODE_RADAR)) {
        if ((party_slot & 1) == 0) {
            *menu_x -= 0x69;
            *band_menu_edge = -1;
            *band_portrait_edge -= 0x69;
            --*grid_row;
            *column_x = 0;
        } else {
            *menu_x = 0x269;
            *band_menu_edge = -1;
            *band_portrait_edge = 0x269;
            ++*grid_row;
            *column_x = 0x269;
        }
    }
}

/* Repaint one party slot's HP, stamina and optional spell-point bars beside
   the portrait, including numeric HP text when that option is enabled. */
void RedrawPartyPortraitBars(unsigned int party_slot, bool slot_enabled)
{
    W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[party_slot];
    W8Character* character = &CurrentGame().party.characters[party_slot];
    int menu_x;
    int menu_y;
    int band_menu_edge;
    int band_portrait_edge;
    int grid_row;
    int column_x;
    int bar_y;
    int hp_bar_x;
    unsigned int stamina_bar_x;
    unsigned int spell_bar_x;
    int hp_catalog;
    int stamina_catalog;
    int spell_catalog;
    unsigned int numeric_hp_mode;

    if (character->hp_current != 0) {
        GetPartySlotMenuAnchor(party_slot, &menu_x, &menu_y, &band_menu_edge, &band_portrait_edge,
                               &grid_row, &column_x, slot_enabled);
        if (g_settings.options.numeric_hit_points != 0) {
            bar_y = 0x11;
            hp_bar_x = 3;
            stamina_bar_x = 9;
            spell_bar_x = 0xf;
            hp_catalog = 0x52;
            stamina_catalog = 0x53;
            spell_catalog = 0x54;
        } else {
            bar_y = 0x17;
            hp_bar_x = 6;
            stamina_bar_x = 10;
            spell_bar_x = 0xe;
            hp_catalog = 0x4f;
            stamina_catalog = 0x50;
            spell_catalog = 0x51;
        }
        numeric_hp_mode = g_settings.options.numeric_hit_points != 0;

        CatalogArtwork::Load(hp_catalog, 0)
            .draw(GetFrameBufferSurface(), 0, band_portrait_edge + hp_bar_x, bar_y + menu_y);
        ShadeStatusBarGap(0x2d - entry->cached_hp_bar, hp_bar_x + band_portrait_edge,
                          bar_y - numeric_hp_mode + menu_y);

        CatalogArtwork::Load(stamina_catalog, 0)
            .draw(GetFrameBufferSurface(), 0, band_portrait_edge + stamina_bar_x, bar_y + menu_y);
        ShadeStatusBarGap(0x2d - entry->cached_stamina_bar, stamina_bar_x + band_portrait_edge,
                          bar_y - numeric_hp_mode + menu_y);

        if (SumCharacterSpellPoints(character) != 0) {
            CatalogArtwork::Load(spell_catalog, 0)
                .draw(GetFrameBufferSurface(), 0, band_portrait_edge + spell_bar_x, bar_y + menu_y);
            ShadeStatusBarGap(0x2d - entry->cached_spell_bar, spell_bar_x + band_portrait_edge,
                              bar_y - numeric_hp_mode + menu_y);
        }

        if (g_settings.options.numeric_hit_points != 0) {
            W8TextBuffer text;
            W8ControlsRect bounds;

            bounds.left = band_portrait_edge + 2;
            bounds.right = band_portrait_edge + 0x14;
            bounds.top = menu_y + 0x3e;
            bounds.bottom = menu_y + 0x46;
            text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
            text.SetClipBounds(bounds);
            text.SetLayoutBounds(&bounds, true);
            g_smfnt_font.resetPalette();
            text.SetText(fmt::format("{:d}", character->hp_current), g_smfnt_font);
            const SDL_Rect background{bounds.left, bounds.top + 1, bounds.right - bounds.left,
                                      bounds.bottom - bounds.top - 1};
            SDL_FillSurfaceRect(&GetFrameBufferSurface(), &background, 0x8000);
            text.Paint(GetFrameBufferSurface());
        }
        InvalidateRegion(band_portrait_edge, bar_y - numeric_hp_mode + menu_y,
                         band_portrait_edge + 0x18,
                         menu_y + (numeric_hp_mode ? 0x46 : bar_y + 0x2e));
    }
}

void StageMonsterCastIcon(unsigned int party_slot, W8SpellRealm realm, bool alternate, int spell_id)
{
    if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
        return;
    }
    W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[party_slot];
    entry->effect_icon_active = true;
    entry->effect_icon_frame = 0;
    int catalog;
    switch (realm) {
    case 0:
        catalog = 0xac - (alternate);
        break;
    case 1:
        catalog = 0xae - (alternate);
        break;
    case 2:
        catalog = 0xb0 - (alternate);
        break;
    case 3:
        catalog = 0xb2 - (alternate);
        break;
    case 4:
        catalog = 0xb4 - (alternate);
        break;
    case 5:
        catalog = 0xb6 - (alternate);
        break;
    default:
        catalog = 0xaa;
    }
    entry->effect_icon_catalog = catalog;
    entry->effect_icon_end_frame =
        static_cast<unsigned short>(CatalogArtwork::Load(catalog).spriteSheet()->frames.size());
    std::string sound =
        spell_id != W8_SPELL_NONE && Data().spells[spell_id].sound_name[0] != 0
            ? fmt::format("Data\\Spells\\Sounds\\{}.wav", Data().spells[spell_id].sound_name)
            : s_general_magic_sound;
    wiz8::sound::play(sound);
    if (GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_PORTRAITS &&
        MainGameHud()->portrait_refresh_pending[party_slot] == 0) {
        RefreshSelectedPartyPortrait(party_slot);
        entry->auto_portrait_refresh = true;
        entry->effect_icon_frame = -1;
    }
    if (!entry->keyboard_menu_open) {
        if (auto* hud = MainGameHud()) hud->repaint.portrait(party_slot);
    }
    if (!entry->damage_splat_active) {
        entry->portrait_fx_clock = MonotonicMilliseconds() + (100);
        return;
    }
    entry->effect_icon_frame = -1;
}

bool PreparePartyPortraitOverlay(unsigned int party_slot, unsigned int left, unsigned int top)
{
    if (CurrentGame().activity.conversation && (party_slot & 1) != 0 &&
        IsPortraitObscuredByNpcDialogue(party_slot)) {
        return false;
    }
    if ((MainGameHud() == nullptr ||
         (GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_FORMATION &&
          GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_RADAR) ||
         MainGameHud()->portrait_refresh_pending[party_slot] != 0) &&
        CurrentGame().party.members[party_slot].has_value()) {
        W8Character* character = &CurrentGame().party.characters[party_slot];
        if (character->hp_current != 0) {
            int portrait = character->portrait_index;
            SpriteDraw draw{};
            if ((g_portrait_descriptors[portrait].mirror_side == W8PortraitMirrorSide::left &&
                 (party_slot & 1) == 0) ||
                (g_portrait_descriptors[portrait].mirror_side == W8PortraitMirrorSide::right &&
                 (party_slot & 1) != 0)) {
                draw.mirror = true;
            }
            if (BlitPartyPortraitAnimation(GetFrameBufferSurface(), portrait, left, top, draw,
                                           party_slot, false) &&
                ((CurrentGame().combat.has_value() &&
                  CurrentGame().combat->characters[party_slot].dead) ||
                 CurrentGame().surprise_possible ||
                 character->highest_condition == W8_CONDITION_MISSING)) {
                return true;
            }
        }
    }
    return false;
}

/* Repaint one party-slot portrait band: frame, live or dead portrait, item
   hands, HP/stamina chrome, labels, condition/enchantment icons, and any
   active overlay callees for that slot. */
void RedrawPartyPortraitOverlay(unsigned int party_slot, bool highlighted, bool overlay_ready,
                                bool slot_enabled)
{
    int menu_x;
    int menu_y;
    int band_menu_edge;
    int band_portrait_edge;
    int grid_row;
    int column_x;
    W8Character* character;
    W8MonsterManagerEntry* entry;
    SpriteDraw portrait_draw{};
    int portrait_catalog;
    int left_condition_x;
    int right_condition_x;
    int condition_frame;
    int enchantment_frame;
    std::string text;
    short text_width;
    unsigned int text_shade;
    int main_hand_item_id;
    int off_hand_item_id;
    bool show_off_hand_row;
    unsigned short hp_bar_frame;

    if (CurrentGame().activity.conversation && (party_slot & 1) != 0) {
        IsPortraitObscuredByNpcDialogue(party_slot);
    }

    GetPartySlotMenuAnchor(party_slot, &menu_x, &menu_y, &band_menu_edge, &band_portrait_edge,
                           &grid_row, &column_x, slot_enabled);

    character = &CurrentGame().party.characters[party_slot];
    entry = &CurrentGame().monster_manager_entries[party_slot];

    if (!CurrentGame().party.members[party_slot].has_value()) {
        CatalogArtwork::Load(0x31, 0).draw(GetFrameBufferSurface(), 0, menu_x + 0x14, menu_y);
    }

    if (overlay_ready) {
        if (!CurrentGame().party.members[party_slot].has_value()) {
            portrait_catalog = 0x34;
            portrait_draw = {};
            CatalogArtwork::Load(portrait_catalog, 0)
                .draw(GetFrameBufferSurface(), 0, menu_x + 0x14, menu_y, portrait_draw);
        } else if (character->hp_current == 0 &&
                   (!entry->damage_splat_death_variant || entry->dead_portrait_revealed)) {
            portrait_catalog = g_dead_portrait_catalog_ids[character->iRace][1];
            portrait_draw = {};
            portrait_draw.mirror = (party_slot & 1) != 0;
            CatalogArtwork::Load(portrait_catalog, 0)
                .draw(GetFrameBufferSurface(), 0, menu_x + 0x14, menu_y, portrait_draw);
        } else {
            portrait_catalog = character->portrait_index;
            portrait_draw = {};
            if ((g_portrait_descriptors[portrait_catalog].mirror_side ==
                     W8PortraitMirrorSide::left &&
                 (party_slot & 1) == 0) ||
                (g_portrait_descriptors[portrait_catalog].mirror_side ==
                     W8PortraitMirrorSide::right &&
                 (party_slot & 1) != 0)) {
                portrait_draw.mirror = true;
            }
            RenderPartyPortrait(GetFrameBufferSurface(), portrait_catalog, menu_x + 0x14, menu_y,
                                portrait_draw, 1, party_slot);
        }
        CatalogArtwork::Load(0x82, 0).draw(GetFrameBufferSurface(), static_cast<short>(grid_row),
                                           column_x, menu_y);
    }

    CatalogArtwork::Load(0x82, 0).draw(GetFrameBufferSurface(), 0x10, menu_x + 0x17, menu_y);

    if (CurrentGame().party.members[party_slot].has_value()) {
        if (overlay_ready) {
            if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
                MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
                main_hand_item_id = character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
                if (main_hand_item_id == -1 ||
                    (Data().items[main_hand_item_id].flags & W8_ITEM_FLAG_TWO_HANDED) == 0) {
                    show_off_hand_row = false;
                    hp_bar_frame = 0;
                } else {
                    show_off_hand_row = true;
                    hp_bar_frame = 2;
                }
                CatalogArtwork::Load(0x80, 0).draw(GetFrameBufferSurface(),
                                                   static_cast<short>(hp_bar_frame), band_menu_edge,
                                                   menu_y);

                if (main_hand_item_id == -1) {
                    CatalogArtwork::Load(g_empty_hand_catalog_ids[character->iRace * W8_RACE_DWARF],
                                         0)
                        .draw(GetFrameBufferSurface(), 0, band_menu_edge + 4, menu_y + 0x17);
                } else {
                    DrawSprite(GetFrameBufferSurface(), *ItemSprites(main_hand_item_id), 2,
                               band_menu_edge + 3, menu_y + 0x17);
                    if (character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].stack_count != 0) {
                        text = fmt::format(
                            "{:d}",
                            static_cast<int>(
                                character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].stack_count));
                        g_smfnt_font.resetPalette();
                        text_width = g_smfnt_font.textWidth(text);
                        DrawText(GetFrameBufferSurface(), g_smfnt_font,
                                 {(band_menu_edge - (text_width + 1) / 2) + 8, menu_y + 0x26},
                                 text);
                    }
                }

                if (!show_off_hand_row) {
                    off_hand_item_id =
                        character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo;
                    if (off_hand_item_id == -1) {
                        CatalogArtwork::Load(
                            g_empty_hand_catalog_ids[character->iRace * W8_RACE_DWARF + 1], 0)
                            .draw(GetFrameBufferSurface(), 0, band_menu_edge + 4, menu_y + 0x2f);
                    } else {
                        DrawSprite(GetFrameBufferSurface(), *ItemSprites(off_hand_item_id), 2,
                                   band_menu_edge + 3, menu_y + 0x2f);
                        if (character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].stack_count !=
                            0) {
                            text = fmt::format(
                                "{:d}", static_cast<int>(
                                            character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON]
                                                .stack_count));
                            g_smfnt_font.resetPalette();
                            text_width = g_smfnt_font.textWidth(text);
                            DrawText(GetFrameBufferSurface(), g_smfnt_font,
                                     {(band_menu_edge - (text_width + 1) / 2) + 0xb, menu_y + 0x3e},
                                     text);
                        }
                    }
                }
            }

            hp_bar_frame = g_settings.options.numeric_hit_points == 0 ? 1 : 3;
            CatalogArtwork::Load(0x80, 0).draw(GetFrameBufferSurface(),
                                               static_cast<short>(hp_bar_frame), band_portrait_edge,
                                               menu_y);
            RedrawPartyPortraitBars(party_slot, slot_enabled);

            if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
                MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
                text = fmt::format("{:d}", character->armor_class_average);
                if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
                    Palette16 palette = g_smfnt_font.glyphs->colors;
                    if (character->load_category != 0) {
                        palette = g_font_state_palettes
                            [g_load_category_palettes[character->load_category]];
                    }
                    g_smfnt_font.setPalette(palette);
                }
                text_width = g_smfnt_font.textWidth(text);
                DrawText(GetFrameBufferSurface(), g_smfnt_font,
                         {(0xd - text_width) / 2 + 3 + band_menu_edge, menu_y + 3}, text);
            }

            text = Data().strings[g_profession_name_message_ids[character->iProfession + 16]];
            if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
                g_smfnt_font.setPalette(
                    g_font_state_palettes[CurrentGame().party.orderIndexOf(party_slot)]);
            }
            text_width = g_smfnt_font.textWidth(text);
            DrawText(GetFrameBufferSurface(), g_smfnt_font,
                     {(0x12 - text_width) / 2 + 2 + band_portrait_edge, menu_y + 3}, text);

            if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME ||
                (g_wiz_text_font_secondary.resetPalette(),
                 CurrentScreenState().id != W8_SCREEN_MAIN_GAME) ||
                (text_shade = 1, MainGameHud()->highlight.SlotFor(W8_HIGHLIGHT_NAME) !=
                                     static_cast<int>(party_slot))) {
                text_shade = 4;
            }
            g_wiz_text_font_secondary.setShade(text_shade);
            text_width = g_wiz_text_font_secondary.textWidth(character->name);
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                     {(0x54 - text_width) / 2 + 0x15 + menu_x, menu_y + 0x49}, character->name);
            g_wiz_text_font_secondary.setShade(Font::kNormalShade);

            if (CurrentGame().combat.has_value()) {
                entry->combat_portrait_dirty = true;
            }
            condition_icons->icons[party_slot].dirty = true;
        }

        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
            (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_FORMATION ||
             GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_RADAR) &&
            MainGameHud()->portrait_refresh_pending[party_slot] == 0) {
            int highlight_x;
            int highlight_y;
            int highlight_catalog;

            if (!highlighted || IsLeftButtonHeld() || MainGameHud()->portrait_flash != 0) {
                if (CurrentGame().status.selected_character != static_cast<int>(party_slot)) {
                    goto draw_condition_icons;
                }
                highlight_x = band_portrait_edge + 1;
                highlight_catalog = 0x5f;
                highlight_y = menu_y + 2;
            } else {
                highlight_x = band_portrait_edge + 1;
                highlight_catalog = 0x5e;
                highlight_y = menu_y + 2;
            }
            CatalogArtwork::Load(highlight_catalog, 0)
                .draw(GetFrameBufferSurface(), 0, highlight_x, highlight_y);
        } else {
            int highlight_x;
            int highlight_y;
            int highlight_catalog;

            highlight_y = menu_y;
            if (!highlighted || (IsLeftButtonHeld() && !FormationPanelOpen()) ||
                (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
                 MainGameHud()->portrait_flash != 0)) {
                if (CurrentGame().status.selected_character != static_cast<int>(party_slot)) {
                    goto draw_condition_icons;
                }
                highlight_x = menu_x + 0x17;
                highlight_catalog = 0x33;
            } else {
                highlight_x = menu_x + 0x17;
                highlight_catalog = 0x32;
            }
            CatalogArtwork::Load(highlight_catalog, 0)
                .draw(GetFrameBufferSurface(), 0, highlight_x, highlight_y);
        }
    }

draw_condition_icons:
    if ((party_slot & 1) == 0) {
        left_condition_x = menu_x + 0x19;
        right_condition_x = menu_x + 0x58;
        condition_frame = 0x2f;
        enchantment_frame = 0x30;
    } else {
        right_condition_x = menu_x + 0x19;
        left_condition_x = menu_x + 0x58;
        enchantment_frame = 0x2f;
        condition_frame = 0x30;
    }

    if (character->highest_condition != W8_CONDITION_NONE) {
        condition_frame = static_cast<int>(character->highest_condition) + 0xb6;
    }
    CatalogArtwork::Load(condition_frame, 0)
        .draw(GetFrameBufferSurface(), 0, left_condition_x, menu_y + 3);

    if (character->enchantment_top != W8_ENCHANTMENT_NONE) {
        enchantment_frame = character->enchantment_top + 0xc9;
    }
    CatalogArtwork::Load(enchantment_frame, 0)
        .draw(GetFrameBufferSurface(), 0, right_condition_x, menu_y + 3);

    if (CurrentGame().party.members[party_slot].has_value() &&
        CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        if (MainGameHud()->highlight.SlotFor(W8_HIGHLIGHT_CONDITION) ==
            static_cast<int>(party_slot)) {
            CatalogArtwork::Load(0x60, 0).draw(GetFrameBufferSurface(), 0, left_condition_x - 1,
                                               menu_y + 2);
        } else if (character->highest_condition != W8_CONDITION_NONE) {
            CatalogArtwork::Load(0x61, 0).draw(GetFrameBufferSurface(), 0, left_condition_x - 1,
                                               menu_y + 2);
        }

        if (MainGameHud()->highlight.SlotFor(W8_HIGHLIGHT_ENCHANTMENT) ==
            static_cast<int>(party_slot)) {
            CatalogArtwork::Load(0x60, 0).draw(GetFrameBufferSurface(), 0, right_condition_x - 1,
                                               menu_y + 2);
        } else if (character->highest_condition != W8_CONDITION_NONE) {
            CatalogArtwork::Load(0x61, 0).draw(GetFrameBufferSurface(), 0, right_condition_x - 1,
                                               menu_y + 2);
        }

        if (MainGameHud()->highlight.SlotFor(W8_HIGHLIGHT_NAME) == static_cast<int>(party_slot) &&
            (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
             MainGameHud()->portrait_refresh_pending[party_slot] != 0)) {
            CatalogArtwork::Load(0x62, 0).draw(GetFrameBufferSurface(), 0, menu_x + 0x13,
                                               menu_y + 0x48);
        }

        if ((GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
             MainGameHud()->portrait_refresh_pending[party_slot] != 0) &&
            MainGameHud()->highlight.SlotFor(W8_HIGHLIGHT_ASSAY) == static_cast<int>(party_slot)) {
            int assay_y;
            int assay_catalog;

            if (MainGameHud()->portrait_assay_hover_mode ==
                W8_PORTRAIT_ASSAY_HOVER_PRIMARY_WEAPON) {
                assay_y = menu_y + 0x15;
                assay_catalog = 99;
            } else if (MainGameHud()->portrait_assay_hover_mode ==
                       W8_PORTRAIT_ASSAY_HOVER_SECONDARY_WEAPON) {
                assay_y = menu_y + 0x2d;
                assay_catalog = 99;
            } else if (MainGameHud()->portrait_assay_hover_mode ==
                       W8_PORTRAIT_ASSAY_HOVER_TWO_HANDED_WEAPON) {
                assay_y = menu_y + 0x15;
                assay_catalog = 0x73;
            } else {
                goto portrait_fx;
            }
            CatalogArtwork::Load(assay_catalog, 0)
                .draw(GetFrameBufferSurface(), 0, band_menu_edge + 1, assay_y);
        }
    }

portrait_fx:
    if (entry->damage_splat_active) {
        DrawDamageSplatOverlay(party_slot);
    }
    if (entry->effect_icon_active) {
        DrawPortraitEffectIcon(party_slot);
    }

    if (MainGameHud()->highlight.SlotFor(W8_HIGHLIGHT_VITALS) == static_cast<int>(party_slot)) {
        int hp_overlay_y;
        int hp_overlay_x;
        int hp_overlay_catalog;

        if (g_settings.options.numeric_hit_points == 0) {
            hp_overlay_y = menu_y + 0x15;
            hp_overlay_x = band_portrait_edge + 3;
            hp_overlay_catalog = 100;
        } else {
            hp_overlay_y = menu_y + 0xe;
            hp_overlay_catalog = 0x65;
            hp_overlay_x = band_portrait_edge;
        }
        CatalogArtwork::Load(hp_overlay_catalog, 0)
            .draw(GetFrameBufferSurface(), 0, hp_overlay_x, hp_overlay_y);
    }

    if (!CurrentGame().combat.has_value() && party_slot < 8 &&
        CurrentGame().party.members[party_slot].has_value()) {
        portrait_buttons->buttons[party_slot]->m_dirty = true;
    }

    if (MainGameHud()->portrait_overlay_party_slot == static_cast<int>(party_slot)) {
        DrawPortraitConditionOverlay(MainGameHud()->portrait_overlay_party_slot);
    }
    if (MainGameHud()->condition_orb_party_slot == static_cast<int>(party_slot)) {
        DrawPortraitEnchantmentOverlay(MainGameHud()->condition_orb_party_slot);
    }
    if (MainGameHud()->enchantment_orb_party_slot == static_cast<int>(party_slot)) {
        DrawPortraitVitalsOverlay(MainGameHud()->enchantment_orb_party_slot);
    }
    if (MainGameHud()->condition_highlight_party_slot == static_cast<int>(party_slot)) {
        DrawPortraitStatusOverlay(MainGameHud()->condition_highlight_party_slot);
    }

    if (GetEffectiveMainUiMode() != W8_MAIN_UI_MODE_PORTRAITS &&
        MainGameHud()->portrait_refresh_pending[party_slot] != 0 &&
        (MainGameHud()->condition_highlight_party_slot != -1 ||
         MainGameHud()->portrait_overlay_party_slot != -1)) {
        RefreshTrackedPortraitOverlay();
    }

    if (overlay_ready && CurrentGame().activity.conversation &&
        CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT &&
        !CurrentGame().npc_interaction.scripted_dialogue &&
        !CurrentGame().npc_interaction.dialogue_panel_hidden &&
        CurrentGame().npc_dialogue.reply() == W8NpcReplyState::none &&
        CurrentGame().npc_interaction.dialogue_hidden == 0 &&
        !CurrentGame().activity.scripted_scene) {
        SetNpcDialoguePanelVisible(1);
    }
}

void ShadeStatusBarGap(int length, int left, int top)
{
    if (length != 0) {
        int rows = (g_settings.options.numeric_hit_points != 0 ? 2 : 0) + 3;
        auto& framebuffer = GetFrameBufferSurface();
        while (rows != 0) {
            short color = Get16BPPColor(SDL_Color{1, 1, 1, SDL_ALPHA_OPAQUE});
            const SDL_Rect line{left, top, 1, length + 1};
            SDL_FillSurfaceRect(&framebuffer, &line, static_cast<Uint16>(color));
            ++left;
            --rows;
        }
    }
}

void ReleasePortraitControls()
{
    portrait_buttons.reset();
}

bool PortraitControlsExist(void)
{
    return portrait_buttons != nullptr;
}
void ReleaseConditionButtons()
{
    condition_icons.reset();
}

static void ClearConditionHighlight()
{
    MainGameHud()->condition_highlight_party_slot = -1;
    DismissHighlightOverlay();
    if (auto* hud = MainGameHud()) {
        hud->repaint.portrait_panel = true;
        hud->repaint.portraits.set();
    }
}

void DisablePortraitControls()
{
    portrait_buttons->input.setEnabled(false);
    for (auto& button : portrait_buttons->buttons) button->SetVisible(false);
}

void DisableConditionButtons()
{
    condition_icons->active = false;
    condition_icons->input.setEnabled(false);
    if (MainGameHud()->condition_highlight_party_slot != -1) ClearConditionHighlight();
}

void EnableConditionButtons()
{
    condition_icons->active = true;
    condition_icons->input.setEnabled(true);
    for (auto& icon : condition_icons->icons) icon.dirty = true;
}

void EnablePortraitAdvanceRegions()
{
    portrait_buttons->input.setEnabled(true);
    for (int slot = 0; slot < 8; ++slot)
        portrait_buttons->buttons[slot]->SetEnabled(
            IsCharacterReadyToAdvance(slot) &&
            CurrentGame().party.members[slot]->level_up_announced);
}

void InvalidatePortraitControl(unsigned int party_slot)
{
    if (party_slot < 8 && CurrentGame().party.members[party_slot].has_value())
        portrait_buttons->buttons[party_slot]->m_dirty = true;
}

void InvalidatePortraitPanel()
{
    for (auto& button : portrait_buttons->buttons) button->m_dirty = true;
}

void UpdatePortraitAdvanceButtons()
{
    for (int slot = 0; slot < 8; ++slot) {
        auto& button = *portrait_buttons->buttons[slot];
        button.SetVisible(IsCharacterReadyToAdvance(slot) && !CurrentGame().activity.conversation &&
                          CurrentGame().party.members[slot]->level_up_announced);
        button.Refresh();
    }
}

void CreateLevelButtons()
{
    portrait_buttons = std::make_unique<PortraitButtons>();
    for (int slot = 0; slot < 8; ++slot) {
        auto& button = portrait_buttons->buttons[slot] =
            std::make_unique<W8ButtonWidget>(portrait_buttons->input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0xa7, 0).spriteSheet(), 3, 0, 1, 2, 4),
            {.on_click =
                 [slot](auto*) {
                     AfterUiDispatch([slot] {
                         RequestScreen({.id = W8_SCREEN_CHARACTER,
                                        .mode = W8_CHARACTER_LEVEL_UP,
                                        .character = &CurrentGame().party.characters[slot]});
                     });
                 },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = 1984,
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(((slot & 1) ? 0x23b : 0) + 0x19, (slot / 2) * 0x55 + 0x46);
        button->Resize(0x12, 0x12);
        button->SetVisible(false);
    }
}

void CreateConditionButtons()
{
    condition_icons = std::make_unique<ConditionIcons>();
    for (int slot = 0; slot < 8; ++slot) {
        auto& icon = condition_icons->icons[slot];
        icon.position = {((slot & 1) ? 0x23f : 0) + 0x17, (slot / 2) * 0x55 + 0x13};
        icon.target = &condition_icons->input.add(
            SDL_Rect{icon.position.x, icon.position.y, 20, 20},
            [slot](const UiPointerEvent& event) {
                if (event.action != UiPointerAction::Up || event.captured_buttons)
                    PlayButtonSound(event.action);
                condition_icons->icons[slot].dirty = true;
                if (event.action == UiPointerAction::Leave ||
                    (event.action == UiPointerAction::Up &&
                     event.button == UiPointerButton::Left)) {
                    if (MainGameHud()->condition_highlight_party_slot != -1)
                        ClearConditionHighlight();
                } else if (event.action == UiPointerAction::Down &&
                           event.button == UiPointerButton::Left) {
                    MainGameHud()->condition_highlight_party_slot = slot;
                    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
                }
                return UiResult::Handled;
            });
        icon.target->setTooltip([] { return Data().strings[1985]; });
    }
    DisableConditionButtons();
}

void UpdateConditionButtons()
{
    for (int slot = 0; slot < 8; ++slot) {
        auto& icon = condition_icons->icons[slot];
        const auto& entry = CurrentGame().monster_manager_entries[slot];
        int catalog = 0;
        if (CurrentGame().party.members[slot].has_value() &&
            MainGameHud()->portrait_refresh_pending[slot] == 0 && !entry.keyboard_menu_open) {
            const auto& character = CurrentGame().party.characters[slot];
            if (character.highest_condition)
                catalog = character.highest_condition + 0xb6;
            else if (character.enchantment_top)
                catalog = character.enchantment_top + 0xc9;
        }
        if (catalog != icon.catalog) {
            icon.dirty = true;
            if (!catalog) {
                if (!MainGameHud()->portrait_refresh_pending[slot] && !entry.keyboard_menu_open)
                    ClearSurfaceRect(icon.position.x, icon.position.y, icon.position.x + 19,
                                     icon.position.y + 19);
                if (MainGameHud()->condition_highlight_party_slot == slot)
                    ClearConditionHighlight();
            }
            icon.catalog = catalog;
        }
        icon.target->setEnabled(catalog != 0);
        if (condition_icons->active && catalog && icon.dirty) {
            auto& target = GetFrameBufferSurface();
            CatalogArtwork::Load(0xa8, 0).drawAndInvalidate(target, icon.target->hovered() ? 1 : 0,
                                                            icon.position.x, icon.position.y);
            CatalogArtwork::Load(catalog, 0)
                .drawAndInvalidate(target, 0, icon.position.x + 2, icon.position.y + 2);
            icon.dirty = false;
        }
    }
}
