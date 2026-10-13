#include "wiz8/engine_code/emitter_resources.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/3d.h"
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
        auto cloned = std::make_unique<stLight>(nullptr);
        *cloned = *light;
        cloned->ConfigureMonsterCopy();
        cloned->setLocation(light->positionalX(), light->positionalY(), light->positionalZ());
        cloned->setParent(nullptr, 0);
        g_world->transient_lights.push_back(cloned.get());
        lights.push_back(cloned.release()); // Capacity reserved before registration.
    }
}
