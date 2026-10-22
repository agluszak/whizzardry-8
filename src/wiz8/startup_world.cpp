#include "wiz8/startup_world.h"

#include <memory>

#include "wiz8/gameplay_constants.h"
#include "wiz8/render/quality.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/level_session.h"

static std::unique_ptr<Navigator> party_navigator;

Navigator* PartyNavigator()
{
    return party_navigator.get();
}

/* Original translation-unit ownership of startup navigation and its globals is unknown. */
bool InitializeStartupNavigation(void)
{
    InitializeRenderQuality();

    party_navigator = std::make_unique<Navigator>();
    auto* navigator = party_navigator.get();
    navigator->configureStartupRange(500.0f);
    navigator->configureStartupDepth(
        g_default_world_height < kStartupNearLimit ? kStartupNearLimit : g_default_world_height,
        g_default_world_height);
    return true;
}
/* Tears down the startup navigation state InitializeStartupNavigation
   built: the party navigator. */
void ShutdownStartupNavigation(void)
{
    party_navigator.reset();
}
