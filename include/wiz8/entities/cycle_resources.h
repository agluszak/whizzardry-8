#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "wiz8/entities/animation.h"
#include "wiz8/render/light.h"

// Owns animation lights; the world update list and scene links only borrow.
// The vector itself is a value; visual cycles borrow its stable address.
class EmitterLights {
public:
    EmitterLights() = default;
    EmitterLights(const EmitterLights& other) { CloneFrom(other); }
    void CloneFrom(const EmitterLights&);
    EmitterLights(EmitterLights&& other) noexcept { lights.swap(other.lights); }
    EmitterLights& operator=(const EmitterLights&) = delete;
    ~EmitterLights() = default;
    std::vector<std::unique_ptr<Light>> lights;
};

struct EmitterCycleResources {
    // Each cycle owns its independent animation and lights.
    EmitterLights lights;
    std::unique_ptr<Animation> animation;

    EmitterCycleResources() = default;
    EmitterCycleResources(EmitterCycleResources&&) noexcept = default;
    EmitterCycleResources(const EmitterCycleResources& other)
        : lights(other.lights),
          animation(other.animation ? std::make_unique<Animation>(*other.animation) : nullptr)
    {
    }
    ~EmitterCycleResources() = default;
    EmitterCycleResources& operator=(const EmitterCycleResources&) = delete;
    std::vector<std::unique_ptr<Light>>* lightView()
    {
        return lights.lights.empty() ? nullptr : &lights.lights;
    }
    void swap(EmitterCycleResources& other) noexcept
    {
        lights.lights.swap(other.lights.lights);
        animation.swap(other.animation);
    }
};

// Ordinary value ownership: typed scene membership no longer imposes cross-cycle teardown order.
template<std::size_t Count> struct EmitterResources {
    std::array<EmitterCycleResources, Count> cycles;
};
