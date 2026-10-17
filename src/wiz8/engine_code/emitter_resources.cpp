#include "wiz8/engine_code/emitter_resources.h"

#include <memory>
#include <utility>

#include "wiz8/engine_code/stLight.hpp"

void EmitterLights::CloneFrom(const EmitterLights& source)
{
    lights.reserve(lights.size() + source.lights.size());
    for (const auto& light : source.lights) {
        auto cloned = std::make_unique<stLight>();
        *cloned = *light;
        cloned->ConfigureMonsterCopy();
        cloned->setLocation(light->positionalX(), light->positionalY(), light->positionalZ());
        cloned->setParent(nullptr, 0);
        lights.push_back(std::move(cloned));
    }
}
