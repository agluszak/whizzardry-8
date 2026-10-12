#include "wiz8/local_screens/MGSSpellIcons.h"

#include <memory>
#include <span>
#include <string>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <SDL3/SDL_rect.h>

#include "wiz8/catalog_artwork.h"
#include "wiz8/ui_layer.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/regions.h"
#include "wiz8/xstatus.h"

namespace {
std::unique_ptr<UiLayer> party_icons;
std::unique_ptr<UiLayer> left_icons;
std::unique_ptr<UiLayer> right_icons;

std::string EffectHelp(const W8EffectSlot& slot, bool show_duration)
{
    return fmt::format("{}: ", g_spell_records[slot.effect_id].display_name) +
        (show_duration ? fmt::sprintf(g_localized_strings[0x79c], slot.amount, slot.duration)
                       : fmt::sprintf(g_localized_strings[0x79d], slot.amount));
}

// Effects are already owned by the game. The HUD only owns their hit targets;
// icons have no independent state or lifetime and are painted from those effects.
void PaintEffectStrip(std::unique_ptr<UiLayer>& layer, std::span<const W8EffectSlot> slots,
                      SDL_Point origin, int step, int size, bool show_duration)
{
    layer = std::make_unique<UiLayer>(0x28);
    for (const auto& slot : slots) {
        if (!slot.active) continue;
        const int icon = g_effect_visual_table[slot.effect_id].hud_icon;
        if (icon < 0) continue;
        CatalogArtwork::Load(icon, 0).drawAndInvalidate(GetFrameBufferSurface(), 0,
                                                       origin.x, origin.y);
        auto& target = layer->add(SDL_Rect{origin.x, origin.y, size + 1, size + 1},
            [&slot, show_duration, duration = slot.duration](const UiPointerEvent& event) mutable {
                PushButtonSoundScheme(0, true);
                if (event.action == UiPointerAction::Enter) {
                    duration = slot.duration;
                    SetRegionHelpText(EffectHelp(slot, show_duration));
                    return UiResult::Handled;
                }
                if (event.action == UiPointerAction::Leave) return UiResult::Handled;
                if (event.action == UiPointerAction::Move && duration != slot.duration) {
                    duration = slot.duration;
                    SetRegionHelpText(EffectHelp(slot, show_duration));
                    ResetRegionHelp(false);
                }
                return UiResult::Pass;
            });
        target.setTooltip([&slot, show_duration] { return EffectHelp(slot, show_duration); });
        origin.x += step;
    }
}
}

void DestroySpellIconHudControls()
{
    party_icons.reset();
    left_icons.reset();
    right_icons.reset();
}

void RefreshSpellIconHudRows()
{
    PaintEffectStrip(party_icons, g_status.effect_slots, {0x26e, 0}, -0x13, 0x12, true);
}

void RefreshCombatEffectHud()
{
    left_icons.reset();
    right_icons.reset();
    if (!gXStatus.fCombatMode) return;
    PaintEffectStrip(left_icons, g_combat_state->effect_slots, {0x81, 0x14}, 0x15, 0x14, false);
    PaintEffectStrip(right_icons, g_combat_state->effect_slots0, {0x1eb, 0x14}, -0x15, 0x14, true);
    left_icons->setEnabled(!g_level_block->portrait_refresh_pending[0]);
    right_icons->setEnabled(!g_level_block->portrait_refresh_pending[1]);
}

void InvalidateMainGameEffectHud()
{
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        ClearSurfaceRect(0x7f, 0x14, 0x201, 0x28);
        InvalidateRegion(0x7f, 0x14, 0x201, 0x28);
    }
}
