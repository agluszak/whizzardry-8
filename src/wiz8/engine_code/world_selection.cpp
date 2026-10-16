#include <functional>
#include <initializer_list>

#include "layouts/world.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/World.h"

/* World selection. Live query: 0x00451280 sits in the gap between
   Engine Code\3dapi.cpp (upper 0x004511D0) and Engine Code\Navigator.cpp
   (lower 0x00452F50). */

// FUNCTION: WIZ8 0x00451280
W8World* GetWorld(void)
{
    return g_world;
}

// FUNCTION: WIZ8 0x00451290
void SetCurrentWorld(W8World* world)
{
    g_world = world;
}

// FUNCTION: WIZ8 0x004512B0
void SetSecondaryWorld(W8World* world)
{
    g_secondary_world = world;
}

static void VisitNodes(srNode* node, const std::function<void(srNode*)>& visit)
{
    for (; node != 0; node = node->getNext()) {
        visit(node);
        VisitNodes(node->getChild(), visit);
    }
}

void ForEachWorldNode(const std::function<void(srNode*)>& visit)
{
    for (W8World* world : {g_world, g_secondary_world}) {
        if (world != 0 && world->static_scene != 0) {
            VisitNodes(world->static_scene->getChild(), visit);
        }
    }
}
