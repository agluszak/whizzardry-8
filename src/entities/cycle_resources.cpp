#include "wiz8/entities/cycle_resources.h"

#include <memory>
#include <utility>

#include "wiz8/render/light.h"

void EmitterLights::CloneFrom(const EmitterLights& source)
{
    lights.reserve(lights.size() + source.lights.size());
    for (const auto& light : source.lights) {
        auto cloned = std::make_unique<Light>();
        *cloned = *light;
        cloned->ConfigureMonsterCopy();
        cloned->transform.setLocation(light->positionalX(), light->positionalY(),
                                      light->positionalZ());
        cloned->setScene(nullptr);
        lights.push_back(std::move(cloned));
    }
}
