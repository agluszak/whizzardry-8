#include "wiz8/local_screens/MGSPartyMovement.h"

#include <array>
#include <cassert>
#include <memory>
#include <string_view>
#include <vector>

#include <SDL3/SDL_rect.h>

#include "layouts/main_game_screen.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

PartyMovementPanel::PartyMovementPanel(MainGameScreen& screen) : screen_(screen)
{
    requestRepaint();
    const W8ControlsRect bounds{0xc3, 0x13f, 0x1b9, 0x153};
    caption_ =
        std::make_unique<W8TextBuffer>(&bounds, std::string_view{}, g_wiz_text_font_secondary,
                                       g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    const auto art = CatalogArtwork::Load(0x94, 0);
    for (int index = 0; index < 2; ++index) {
        auto& button = buttons_[index];
        button = std::make_unique<W8ButtonWidget>(input_);
        button->Configure(ButtonGraphics::Load(art.spriteSheet(), 7, 4, 5, 6, 6),
                          {.on_click =
                               [index](auto*) {
                                   AfterUiDispatch(index ? CancelPartyMovement
                                                         : BeginFreeTurnPhase);
                               },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = 37 + index,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        button->SetPosition(0x1bb, 0x13f);
        button->Resize(20, 20);
        button->SetVisible(index == 1);
    }
    screen_.game.combat->party_move_percent = 100;
}

PartyMovementPanel::~PartyMovementPanel() = default;

void PartyMovementPanel::showGauge(bool force)
{
    const int percent = screen_.game.combat->party_move_percent;
    if (!force && percent == percent_shown_) return;
    requestRepaint();
    percent_shown_ = percent;
}

// Show the free-turn button while the party can move, the cancel button otherwise.
void PartyMovementPanel::syncButtons()
{
    const int visible = CanPartyMove() ? 1 : 0;
    if (visible != visible_button_) requestRepaint();
    visible_button_ = visible;
    for (int index = 0; index < 2; ++index) buttons_[index]->SetVisible(index == visible);
}

void PartyMovementPanel::enableInput()
{
    input_.setEnabled(true);
    syncButtons();
}

void PartyMovementPanel::disableInput()
{
    input_.setEnabled(false);
}

/* The gauge image: 0 while the party can move, 1 while it runs out, 2 once the
   level stopped its movement. */
static int PartyMovementGaugeImage()
{
    if (CanPartyMove()) return 0;
    return IsLevelMovementStopped() ? 2 : 1;
}

// Whether the gauge shows the action under way rather than the one planned next.
static bool ShowsCurrentAction(const Combat& combat)
{
    return !CanPartyMove() || (combat.execution_active != 0 && IsPartyEngaged() == 0);
}

void PartyMovementPanel::update()
{
    const auto& combat = *screen_.game.combat;
    syncButtons();
    if (visible_button_ == 1) buttons_[1]->SetEnabled(combat.round_active);
    const bool running = PartyMovementGaugeImage() == 1;
    if (running && TimeUntilDeadline(animation_clock_) != 0) return;
    if (running) {
        animation_frame_ = (animation_frame_ + 1) % 12;
        requestRepaint();
    }
    const W8PartyAction action =
        ShowsCurrentAction(combat) ? combat.uiCurrentPartyAction : combat.uiNextPartyAction;
    animation_clock_ = MonotonicMilliseconds() + (action == W8_PARTY_ACTION_RUN ? 0x32 : 0x5a);
}

/* The panel, its live button, the gauge bar clipped to the party's remaining
   move with its animated end, the caption, and the portrait plate it covers. */
void PartyMovementPanel::paint(bool requested) const
{
    const auto& combat = *screen_.game.combat;
    assert((combat.uiCurrentPartyAction != W8_PARTY_ACTION_NONE &&
            combat.uiCurrentPartyActionStatus != W8_PARTY_ACTION_FINISHED) ||
           combat.uiNextPartyAction != W8_PARTY_ACTION_NONE);
    auto& button = *buttons_[visible_button_];
    if (!requested && !button.m_dirty) return;
    auto& target = GetFrameBufferSurface();
    CatalogArtwork::Load(0x93, 0).drawAndInvalidate(target, 0, 0xb1, 0x13f);
    button.Refresh(true);

    const int percent = combat.party_move_percent;
    const short right = 0x1b9 - percent * 0xf6 / 100;
    const int image = PartyMovementGaugeImage();
    int caption;
    if (image == 1) {
        caption = ((percent != 100) - 1 & 0x77f) - 1;
    } else if (image == 2) {
        caption = 0x77d;
    } else if (combat.execution_active != 0 && IsPartyEngaged() == 0) {
        caption = (combat.uiCurrentPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
    } else {
        caption = (combat.uiNextPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
    }
    {
        ScopedClipRect clipped(target, SDL_Rect{right, 0, 0x1b9 - right, 0x1e0});
        CatalogArtwork::Load(0x96, 0).drawAndInvalidate(target, image, 0xc3, 0x144);
    }

    const W8PartyAction action =
        ShowsCurrentAction(combat) ? combat.uiCurrentPartyAction : combat.uiNextPartyAction;
    const int frame_base = action == W8_PARTY_ACTION_RUN ? 0 : 0x24;
    const int frame = image == 0 ? 0x18 : image == 2 ? 0xc : 0;
    CatalogArtwork::Load(0x95, 0).drawAndInvalidate(target, animation_frame_ + frame_base + frame,
                                                    right - 0xf, 0x142);
    InvalidateRegion(right - 0xf, 0x142, right - 1, 0x150);
    if (caption != -1) {
        caption_->RenderString(Data().strings[caption], g_wiz_text_font_secondary, false, target);
    }
    screen_.paintPortraitPlate();
}

void PartyMovementPanel::requestRepaint()
{
    screen_.hud->repaint.movement_panel = true;
}

void PartyMovementPanel::setFreeTurnEnabled(bool enabled)
{
    buttons_[0]->SetEnabled(enabled);
}

void ShowPartyMovementPanel(void)
{
    auto* screen = MainGame();
    if (!screen) return;
    if (screen->party_movement)
        screen->party_movement->requestRepaint();
    else
        screen->party_movement = std::make_unique<PartyMovementPanel>(*screen);
}

void ReleasePartyMovement(void)
{
    auto* screen = MainGame();
    if (!screen || !screen->party_movement) return;
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) EraseRegion(0xb1, 0x13f, 0x1cf, 0x153);
    screen->hud->repaint.portrait_panel = true;
    screen->party_movement.reset();
}

void ShowPartyMovementGauge(bool force)
{
    if (auto* screen = MainGame(); screen && screen->party_movement)
        screen->party_movement->showGauge(force);
}

void RepaintPartyMovementPanel(void)
{
    if (auto* screen = MainGame(); screen && screen->party_movement)
        screen->party_movement->requestRepaint();
}

void SetFreeTurnEnabled(bool enabled)
{
    if (auto* screen = MainGame(); screen && screen->party_movement)
        screen->party_movement->setFreeTurnEnabled(enabled);
}
