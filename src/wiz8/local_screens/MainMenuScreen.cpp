#include <SDL3/SDL_rect.h>
#include <string_view>
#include <string>
#include <array>
#include <cstddef>
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
// GLOBAL: WIZ8 0x0069c4bc
std::string g_pending_main_menu_message;
// The modal dialog belongs to this screen, not a global registry.
static std::unique_ptr<W8MessageDialogBase> main_menu_dialog;

namespace {
constexpr std::array<W8ControlsRect, 6> main_menu_item_bounds = {{
    {174, 138, 467, 182}, {140, 187, 501, 231}, {204, 235, 436, 279},
    {239, 284, 403, 328}, {234, 335, 408, 379}, {279, 423, 364, 467},
}};
constexpr std::array<int, 4> main_menu_sprite_objects = {0xea, 0xec, 0xeb, 0xed};
}

// The original six item rectangles are now screen-owned data, not global
// region registrations. The load-game item uses the disabled sprite when
// the party has no saves.
// FUNCTION: WIZ8 0x005bcab0
unsigned char DrawMainMenuItem(short item, short state)
{
    if (item < 0 || static_cast<std::size_t>(item) >= main_menu_item_bounds.size()) return 0;
    if (item == 2 && !g_main_menu_has_save_games) state = 3;

    const auto& bounds = main_menu_item_bounds[item];
    if (state >= 0 && static_cast<std::size_t>(state) < main_menu_sprite_objects.size())
        DrawCatalogImage(GetFrameBufferSurface(), main_menu_sprite_objects[state], 0, item, 0x98,
                         bounds.top, VO_BLT_SRCTRANSPARENCY, 0);

    InvalidateRegion(0x98, bounds.top, 0x1f2, bounds.bottom, 0);
    return 1;
}

static void DrawMainMenuItems()
{
    for (short item = 0; item < 6; ++item) DrawMainMenuItem(item, 0);
    DrawMainMenuItem(g_main_menu_selected_item, 1);
}

static void SelectMainMenuItem(short item)
{
    DrawMainMenuItem(g_main_menu_selected_item, 0);
    g_main_menu_selected_item = item;
    DrawMainMenuItem(item, 1);
}


/* The six menu items are widgets owned by this screen. The Controls panel
   creates their regions at runtime; the global region catalog is not involved. */
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
    MainMenuItemWidget(short item, const W8ControlsRect& bounds)
        : item_(item)
    {
        SetBounds(bounds.left, bounds.top, bounds.right, bounds.bottom);
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
unsigned int main_menu_region_set;

void CreateMainMenuControls()
{
    main_menu_controls = std::make_unique<Controls>();
    main_menu_controls->AcquireRegionSet(&main_menu_region_set);
    for (short item = 0; item < static_cast<short>(main_menu_item_bounds.size()); ++item) {
        main_menu_controls->AdoptControl(
            std::make_unique<MainMenuItemWidget>(item, main_menu_item_bounds[item]));
    }
    main_menu_controls->SetEnabled(true);
    main_menu_controls->EnableRegionSet(true);
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

    DrawMainMenuItems();

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
        main_menu_dialog.reset(static_cast<W8MessageDialogBase*>(CreateDialogByKind(W8_DIALOG_MESSAGE)));
        dialog = main_menu_dialog.get();
        dialog->SetClientExtent(0xfa, 200);
        dialog->SetMessage(pending, 1, 0x32, true, false, true, true, 0, 0x15e);
        SetDialogDestroyCallback(dialog, 0);
        g_pending_main_menu_message.clear();
        return 1;
    }
    if (!HasEnoughFreeDiskSpace() && !g_main_menu_warning_shown) {
        main_menu_dialog.reset(static_cast<W8MessageDialogBase*>(CreateDialogByKind(W8_DIALOG_MESSAGE)));
        dialog = main_menu_dialog.get();
        dialog->SetClientExtent(0xfa, 200);
        dialog->SetMessage(g_localized_strings[0x7ee], 1, 0x32, true, false, true, true, 0, 0x15e);
        SetDialogDestroyCallback(dialog, 0);
        g_main_menu_warning_shown = true;
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
    if (main_menu_dialog != nullptr) {
        DrawDialog(main_menu_dialog.get());
        if (!ProcessDialogInput(main_menu_dialog.get())) {
            main_menu_dialog.reset();
            g_main_menu_redraw = true;
            DrawCatalogImage(GetFrameBufferSurface(), 0xe8, 0, 0, 0, 0, VO_BLT_SRCTRANSPARENCY, 0);
            DrawMainMenuItems();
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
    if (g_main_menu_redraw || IsMessageBoxActive() || main_menu_dialog != nullptr) {
        if (main_menu_dialog != nullptr) {
            DrawDialog(main_menu_dialog.get());
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
    main_menu_dialog.reset();
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
