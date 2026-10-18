#include "wiz8/startup_world.h"

#include <memory>

#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/gameplay_constants.h"

// GLOBAL: WIZ8 0x00659c0c
static std::unique_ptr<W8Navigator> party_navigator;

W8Navigator* PartyNavigator()
{
    return party_navigator.get();
}
// GLOBAL: WIZ8 0x006081e8

/* Original translation-unit ownership of startup navigation and its globals is unknown. */
// FUNCTION: WIZ8 0x0044f060
unsigned char InitializeStartupNavigation(void)
{
    InitializeAniMeshCache(-1, -1);
    InitializeRenderQuality();
    InitializeEnvironmentColours();

    party_navigator = std::make_unique<W8Navigator>();
    auto* navigator = party_navigator.get();
    navigator->configureStartupRange(500.0f);
    navigator->configureStartupDepth(
        g_default_world_height < kStartupNearLimit ? kStartupNearLimit : g_default_world_height,
        g_default_world_height);
    return 1;
}
/* Tears down the startup navigation state InitializeStartupNavigation
   built: the ani-mesh cache, environment globals, and the
   party navigator itself. */
// FUNCTION: WIZ8 0x0044F190
void ShutdownStartupNavigation(void)
{
    FreeAniMeshCache();
    ClearEnvironmentObjects();
    party_navigator.reset();
}
