#include "wiz8/engine_code/emitter_resources.h"

#include <memory>
#include <utility>

#include "surrender/srNode.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/layouts/world.h"

EmitterLights::~EmitterLights()
{
    for (auto* light : lights) {
        light->setParent(nullptr, 1);
        WorldRemoveLight(g_world, light);
    }
}

void EmitterLights::CloneFrom(const EmitterLights& source)
{
    lights.reserve(lights.size() + source.lights.size());
    for (auto* light : source.lights) {
        auto cloned = std::make_unique<stLight>(srNode::Ownership::independent);
        *cloned = *light;
        cloned->ConfigureMonsterCopy();
        cloned->setLocation(light->positionalX(), light->positionalY(), light->positionalZ());
        cloned->setParent(nullptr, 0);
        lights.push_back(AddWorldLight(g_world, std::move(cloned))); // Capacity is reserved.
    }
}
