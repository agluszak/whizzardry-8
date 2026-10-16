#pragma once

#include <array>
#include <memory>

#include "surrender/srCamera.h"
#include "surrender/srScene.h"

// Video owns these scene roots. Widgets and temporary models borrow parents;
// srNode ownership decides which children a scene destroys.
struct VideoOverlayScenes {
    // Declared first so detached camera ownership outlives every scene.
    std::unique_ptr<srCamera> camera;
    srCamera* square_camera = nullptr; // Owned by the square scene.
    std::unique_ptr<srScene> user;
    std::unique_ptr<srScene> fullscreen;
    std::array<std::unique_ptr<srScene>, 2> frame;
    std::unique_ptr<srScene> square;
    std::array<std::unique_ptr<srScene>, 2> prerender;
    std::unique_ptr<srScene> cursor;
};

extern VideoOverlayScenes g_overlay_scenes;
