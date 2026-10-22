#pragma once

#include <memory>
#include <string>

#include "wiz8/screen.h"
#include "wiz8/ui_layer.h"

class GameSession;
class W8MessageDialogBase;

/* A notice the main menu shows in a dialog the next time it opens, such as
   the name of the save written when the game ends. */
extern std::string g_pending_main_menu_message;

class MainMenuScreen final : public Screen {
public:
    static constexpr W8ScreenId id = W8_SCREEN_MAIN_MENU;

    explicit MainMenuScreen(GameSession& games);
    ~MainMenuScreen() override;
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;

    /* Whether the load item is available. */
    bool has_save_games = false;

private:
    void drawItem(short item, short state);
    void drawItems();
    void selectItem(short item);
    void activateItem(short item);
    UiResult handleItem(short item, const UiPointerEvent& event);

    // Entering the menu ends the game in progress.
    GameSession& games_;
    bool redraw_ = false;
    short selected_item_ = 0;
    /* The six item targets, registered while the menu is open. */
    std::unique_ptr<UiLayer> input_;
    std::unique_ptr<W8MessageDialogBase> dialog_;
};
