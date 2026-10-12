#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string_view>

#include "wiz8/engine_code/World.h"
#include "surrender/srScene.h"
#include "compat/compiler.h"
#include "layouts/world.h"
#include "surrender/srNode.h"

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

static void VisitNodes(srNode* node, std::uint32_t class_id, const std::function<void(srNode*)>& visit)
{
    for (; node != 0; node = node->getNext()) {
        if (node->matchClassID(class_id)) {
            visit(node);
        }
        VisitNodes(node->getChild(), class_id, visit);
    }
}

srNode* FindWorldNode(std::string_view name, std::uint32_t class_id)
{
    for (W8World* world : {g_world, g_secondary_world}) {
        if (world != 0 && world->static_scene != 0) {
            if (srNode* node = world->static_scene->findChildByNameAndType(name, class_id)) {
                return node;
            }
        }
    }
    return 0;
}

void ForEachWorldNode(std::uint32_t class_id, const std::function<void(srNode*)>& visit)
{
    for (W8World* world : {g_world, g_secondary_world}) {
        if (world != 0 && world->static_scene != 0) {
            VisitNodes(world->static_scene->getChild(), class_id, visit);
        }
    }
}
