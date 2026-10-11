#include <SDL3/SDL_rect.h>
#include <string_view>
#include <string>
#include <array>
#include <memory>
#include <vector>

#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/regions.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/cursor.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/fonts.h"
#include "wiz8/xstatus.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/music_playlist.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/sr_api.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/utility.h"
#include "wiz8/version.h"
#include "input.h"
#include "english.h"
#include "Font.h"
#include "himage.h"
#include "mousesystem.h"
#include "vsurface.h"
#include "dialog_code/DialogBase.h"
#include "vobject.h"


/* The screen's own state. */
// GLOBAL: WIZ8 0x0069c4ba
bool g_main_menu_has_save_games;
// GLOBAL: WIZ8 0x0069c4b6
bool g_main_menu_redraw;
// GLOBAL: WIZ8 0x0069c4b4
static short g_main_menu_selected_item;
// GLOBAL: WIZ8 0x0069c4c4
bool g_main_menu_warning_shown;
// GLOBAL: WIZ8 0x0069c4bb
bool g_main_menu_overlay_enabled;
// GLOBAL: WIZ8 0x0069c4ac
SGPVSurface* g_main_menu_overlay_surface;
// GLOBAL: WIZ8 0x0069c4b0
// GLOBAL: WIZ8 0x0069c4bc
std::string g_pending_main_menu_message;
// GLOBAL: WIZ8 0x0069c4c0
W8MessageDialogBase* g_main_menu_dialog;

/* Draws one of the six menu items. The first switch turns the item index into
   its sprite slot and its top and bottom rows; the second turns the requested
   state into a sprite id. Item two is forced to state three whenever the flag
   0x005BC810 stores from 0x00512FB0 is clear, which is the only item whose
   state the screen overrides.

   An unrecognised state draws no sprite but
   still redraws the row. */
// FUNCTION: WIZ8 0x005bcab0
unsigned char DrawMainMenuItem(short item, short state)
{
    int slot;
    int top;
    int bottom;

    switch (item) {
    case 0:
        slot = 0;
        top = 0x8a;
        bottom = 0xb1;
        break;
    case 1:
        slot = 1;
        top = 0xbb;
        bottom = 0xe1;
        break;
    case 2:
        slot = 2;
        top = 0xeb;
        bottom = 0x112;
        if (!g_main_menu_has_save_games) {
            state = 3;
        }
        break;
    case 3:
        slot = 3;
        top = 0x11c;
        bottom = 0x145;
        break;
    case 4:
        slot = 4;
        top = 0x14f;
        bottom = 0x193;
        break;
    case 5:
        slot = 5;
        top = 0x1a7;
        bottom = 0x1d3;
        break;
    default:
        return 0;
    }

    switch (state) {
    case 0:
        DrawCatalogImage(GetFrameBufferSurface(), 0xea, 0, slot, 0x98, top, VO_BLT_SRCTRANSPARENCY, 0);
        break;
    case 1:
        DrawCatalogImage(GetFrameBufferSurface(), 0xec, 0, slot, 0x98, top, VO_BLT_SRCTRANSPARENCY, 0);
        break;
    case 2:
        DrawCatalogImage(GetFrameBufferSurface(), 0xeb, 0, slot, 0x98, top, VO_BLT_SRCTRANSPARENCY, 0);
        break;
    case 3:
        DrawCatalogImage(GetFrameBufferSurface(), 0xed, 0, slot, 0x98, top, VO_BLT_SRCTRANSPARENCY, 0);
        break;
    }

    InvalidateRegion(0x98, top, 0x1f2, bottom, 0);
    return 1;
}

static void SelectMainMenuItem(short item)
{
    DrawMainMenuItem(g_main_menu_selected_item, 0);
    g_main_menu_selected_item = item;
    DrawMainMenuItem(item, 1);
}


/* The catalog still supplies the six hit rectangles, but the controls own the
   event targets and share the same dispatch as other panel widgets. */
namespace {
void ActivateMainMenuItem(short item)
{
    switch (item) {
    case 0:
        RequestScreenTransition();
        g_settings.intro_seen = false;
        SetIntroVideoIndex(0);
        SetPendingScreenState(W8_SCREEN_INTRO);
        break;
    case 1:
        SetPendingScreenState(W8_SCREEN_PARTY_SELECTION);
        break;
    case 2:
        if (g_main_menu_has_save_games) {
            g_pending_screen_state.mode = 1;
            SetPendingScreenState(W8_SCREEN_OPTIONS);
        }
        break;
    case 3:
        SetPendingScreenState(W8_SCREEN_CREDITS);
        break;
    case 4:
        ClearScreenWait();
        break;
    case 5:
        RequestExitScreen();
        break;
    }
}

class MainMenuItemWidget final : public W8Widget {
public:
    MainMenuItemWidget(unsigned int region, short item)
        : W8Widget(nullptr, region, g_regions[region].x1, g_regions[region].y1,
                   g_regions[region].x2, g_regions[region].y2),
          item_(item)
    {
    }

    void OnMouseEnter(int) override
    {
        if (Available()) SelectMainMenuItem(item_);
    }

    void OnMouseLeave(int) override
    {
        DrawMainMenuItem(g_main_menu_selected_item, 0);
        g_main_menu_selected_item = -1;
    }

    void OnLeftButtonDown(int) override
    {
        if (Available()) DrawMainMenuItem(g_main_menu_selected_item, 2);
    }

    void OnLeftButtonUp(int) override
    {
        if (!Available()) return;
        DrawMainMenuItem(g_main_menu_selected_item, 1);
        ActivateMainMenuItem(item_);
    }

private:
    bool Available() const { return item_ != 2 || g_main_menu_has_save_games; }
    short item_;
};

std::unique_ptr<Controls> main_menu_controls;

void CreateMainMenuControls()
{
    main_menu_controls = std::make_unique<Controls>();
    // Set 1 has six fixed menu rectangles. Use the existing widget dispatcher
    // rather than six custom region callbacks.
    for (unsigned int region = 1; region <= 6; ++region) {
        main_menu_controls->AdoptControl(
            std::make_unique<MainMenuItemWidget>(region, static_cast<short>(region - 1)));
        SetRegionCallback(region, DispatchControlRegionEvent, 0);
    }
    main_menu_controls->SetEnabled(true);
}
} // namespace

/* Zero is the retail BSS state. The first screen synchronization replaces it
   with the default cursor and then records the normal -1 state. */

// FUNCTION: WIZ8 0x005bc800
unsigned char MainMenuScreenInitialize(void)
{
    g_main_menu_selected_item = 0;
    return 1;
}

// FUNCTION: WIZ8 0x005bc810
unsigned char MainMenuScreenEnter(void)
{
    std::string text;
    std::string wide;
    unsigned short colour;
    W8MessageDialogBase* dialog;
    std::string_view pending;
    short measured;

    ResetVideoFrameState();
    MSYS_Init();
    g_status.game_started = false;
    g_main_menu_has_save_games = SaveGameExists();
    g_main_menu_redraw = true;
    ClearPrimarySurface();
    colour = Get16BPPColor(0x10101);
    ColorFillVideoSurfaceArea(GetFrameBufferSurface(), 0, 0, 0x280, 0x1e0, colour);
    SetViewport(0, 0, 0x280, 0x1e0);
    g_main_menu_selected_item = 0;
    DrawCatalogImage(GetFrameBufferSurface(), 0xe8, 0, 0, 0, 0, VO_BLT_SRCTRANSPARENCY, 0);

    DrawMainMenuItem(0, 0);
    DrawMainMenuItem(1, 0);
    DrawMainMenuItem(2, 0);
    DrawMainMenuItem(3, 0);
    DrawMainMenuItem(4, 0);
    DrawMainMenuItem(5, 0);
    DrawMainMenuItem(g_main_menu_selected_item, 1);

    text = FormatVersionBanner(false, false, false);
    wide = text;
    SetFont(g_wiz_text_font_secondary);
    g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_WHITE]);
    measured = g_wiz_text_font_secondary.textWidth(wide);
    DrawFontText(0x27b - measured, 5, wide);
    g_wiz_text_font_secondary.resetPalette();
    main_menu_controls.reset();
    ResetRegions();
    CreateMainMenuControls();
    RegionSetEnable(1);

    if (gXStatus.uiMonstersInDatabase > 1000) {
        srAssertFail("gXStatus.uiMonstersInDatabase <= MAX_MONSTERS_IN_DATABASE",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\MainMenuScreen.cpp", 0x87, 0);
    }
    if (g_previous_screen_id != W8_SCREEN_OPTIONS) {
        StartMusicResource("MainMenu.MPL", 0, 1);
    }
    UpdateHeldItemCursor();

    pending = g_pending_main_menu_message;
    if (!pending.empty()) {
        dialog = static_cast<W8MessageDialogBase*>(CreateDialogByKind(W8_DIALOG_MESSAGE));
        dialog->SetClientExtent(0xfa, 200);
        dialog->SetMessage(pending, 1, 0x32, true, false, true, true, 0, 0x15e);
        SetDialogDestroyCallback(dialog, 0);
        g_main_menu_dialog = dialog;
        g_pending_main_menu_message.clear();
        return 1;
    }
    if (!HasEnoughFreeDiskSpace() && !g_main_menu_warning_shown) {
        dialog = static_cast<W8MessageDialogBase*>(CreateDialogByKind(W8_DIALOG_MESSAGE));
        dialog->SetClientExtent(0xfa, 200);
        dialog->SetMessage(g_localized_strings[0x7ee], 1, 0x32, true, false, true, true, 0, 0x15e);
        SetDialogDestroyCallback(dialog, 0);
        g_main_menu_warning_shown = true;
        g_main_menu_dialog = dialog;
    }
    return 1;
}

/* The main menu's developer-mode key hook. Retail never consumes the key: its
   linker folded this body into IgnoreSpellCastingInput, which compiles to the
   same bytes. */
static bool HandleDeveloperModeKey(const InputAtom* input)
{
    return false;
}

/* The canonical state-1 frame services a modal dialog first.  Without one it
   dispatches queued region and keyboard input, including the direct New Game,
   Load, Options and exit shortcuts, then completes the shared 2D redraw and
   renderer transaction. */
// FUNCTION: WIZ8 0x005bcbf0
void MainMenuScreenFrame()
{
    SDL_Point point;
    InputAtom input;

    if (g_dev_mode) {
        RequestExitScreen();
    }
    if (g_main_menu_dialog != 0) {
        DrawDialog(g_main_menu_dialog);
        if (!ProcessDialogInput(g_main_menu_dialog)) {
            delete g_main_menu_dialog;
            g_main_menu_dialog = 0;
            g_main_menu_redraw = true;
            DrawCatalogImage(GetFrameBufferSurface(), 0xe8, 0, 0, 0, 0, VO_BLT_SRCTRANSPARENCY, 0);
            DrawMainMenuItem(0, 0);
            DrawMainMenuItem(1, 0);
            DrawMainMenuItem(2, 0);
            DrawMainMenuItem(3, 0);
            DrawMainMenuItem(4, 0);
            DrawMainMenuItem(5, 0);
            DrawMainMenuItem(g_main_menu_selected_item, 1);
        }
    } else if (IsMessageBoxActive()) {
        ProcessMessageBoxInput();
    } else {
        SGPMouseGetPos(&point);
        UpdateRegionMousePosition(point.x, point.y);
        while (DequeueEvent(&input) == 1) {
            if (!DispatchRegionInput(&input) && input.usEvent == KEY_DOWN) {
                if (HandleDeveloperModeKey(&input)) {
                    if (g_dev_mode) {
                        SetFont(g_wiz_text_font_secondary);
                        g_wiz_text_font_secondary.resetPalette();
                        DrawFontTextDirty(5, 5, "Developer mode enabled.");
                    }
                } else {
                    switch (input.usParam) {
                    case ENTER:
                        DrawMainMenuItem(g_main_menu_selected_item, 2);
                        ActivateMainMenuItem(static_cast<short>(g_main_menu_selected_item));
                        break;
                    case ESC:
                    case 'E':
                    case 'X':
                        RequestExitScreen();
                        break;
                    case VK_PRIOR:
                        SelectMainMenuItem(0);
                        break;
                    case VK_NEXT:
                        SelectMainMenuItem(5);
                        break;
                    case VK_UP:
                        DrawMainMenuItem(g_main_menu_selected_item, 0);
                        if (g_main_menu_selected_item > 0) {
                            --g_main_menu_selected_item;
                        } else {
                            g_main_menu_selected_item = 5;
                        }
                        DrawMainMenuItem(g_main_menu_selected_item, 1);
                        break;
                    case VK_DOWN:
                        DrawMainMenuItem(g_main_menu_selected_item, 0);
                        if (g_main_menu_selected_item < 5) {
                            ++g_main_menu_selected_item;
                        } else {
                            g_main_menu_selected_item = 0;
                        }
                        DrawMainMenuItem(g_main_menu_selected_item, 1);
                        break;
                    case 'L':
                        ActivateMainMenuItem(2);
                        break;
                    case 'O':
                        ActivateMainMenuItem(4);
                        break;
                    case 'S':
                        ActivateMainMenuItem(1);
                        break;
                    }
                }
            }
        }
    }

    NoOp();
    if (g_main_menu_redraw || IsMessageBoxActive() || g_main_menu_dialog != 0) {
        if (g_main_menu_dialog != 0) {
            DrawDialog(g_main_menu_dialog);
        }
        if (g_main_menu_overlay_enabled && g_main_menu_overlay_surface) {
            BltVideoSurfaceToVideoSurface(GetFrameBufferSurface(), *g_main_menu_overlay_surface, 0, 0x1d1, 6, 0);
        }
        RenderMessageBox();
        ResetTransientRenderScenes();
        g_main_menu_redraw = false;
    }
    RenderFrame();
}

// FUNCTION: WIZ8 0x00591870
unsigned char MainMenuScreenLeave(int)
{
    main_menu_controls.reset();
    ReleaseLoadedVideoFrames();
    ResetRegions();
    MSYS_Shutdown();
    return 1;
}

// FUNCTION: WIZ8 0x005bd010
void SetMainMenuMessage(std::string_view message)
{
    g_pending_main_menu_message = message;
}
