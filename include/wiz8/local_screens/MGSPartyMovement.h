#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "wiz8/ui_layer.h"

class W8ButtonWidget;
class W8TextBuffer;
struct MainGameScreen;

/* The combat party-movement panel: the gauge of the party's remaining move, its
   caption and the free-turn and cancel buttons. Owned by the main game while it
   is shown. */
class PartyMovementPanel {
public:
    explicit PartyMovementPanel(MainGameScreen& screen);
    ~PartyMovementPanel();

    // Turn the panel's input on and sync which button shows.
    void enableInput();
    void disableInput();
    /* Before the draw: sync the buttons and step the gauge's running animation,
       requesting a repaint when either changed. */
    void update();
    // Paint the panel when its repaint was requested or its live button changed.
    void paint(bool requested) const;
    void requestRepaint();
    /* Repaint the gauge when the party's remaining move changed since it was last
       shown, or always with `force`. */
    void showGauge(bool force);
    void setFreeTurnEnabled(bool enabled);

private:
    void syncButtons();
    MainGameScreen& screen_;
    UiLayer input_{UiLayerOrder::PartyMovement};
    std::array<std::unique_ptr<W8ButtonWidget>, 2> buttons_;
    std::unique_ptr<W8TextBuffer> caption_;
    uint64_t animation_clock_ = 0;
    unsigned int animation_frame_ = 0;
    int visible_button_ = 1;
    int percent_shown_ = 100;
};

/* Show the panel, creating it on first use, or request its repaint. */
void ShowPartyMovementPanel(void);
/* Erase and release the panel; called when the party regains movement or combat
   ends. */
void ReleasePartyMovement(void);
void ShowPartyMovementGauge(bool force);
void RepaintPartyMovementPanel(void);
void SetFreeTurnEnabled(bool enabled);
