#include "wiz8/party_presentation.h"

#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/live_character_rules.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/party_encumbrance.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/xstatus.h"

void PresentInventoryWeightChange(W8Character& character, InventoryWeightChange change)
{
    if (change.became_overloaded) {
        const int event = CurrentScreenState().id == W8_SCREEN_CAMP ? 8 : g_special_event13;
        QueueCharacterEvent(&character, event, 0, kCharacterEventFullVolume);
    }
}
void PresentPartyLoadChange(bool redistributed)
{
    if (!redistributed) return;
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && !gXStatus.fNpcDialogueMode) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
    } else if (CurrentScreenState().id == W8_SCREEN_CAMP && g_camp_screen) {
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO | W8_CAMP_REDRAW_PORTRAIT;
    }
}

void RedistributePartyEncumbrance()
{
    if (g_status.game_started)
        PresentPartyLoadChange(g_status.party.refreshLoad(CaptureLivePartyStatRules()));
}
