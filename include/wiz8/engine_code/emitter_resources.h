#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/stLight.hpp"

// Owns animation lights; the world update list and scene links only borrow.
// The vector itself is a value; visual cycles borrow its stable address.
class EmitterLights {
public:
    EmitterLights() = default;
    EmitterLights(const EmitterLights&) = delete;
    void CloneFrom(const EmitterLights&);
    EmitterLights(EmitterLights&& other) noexcept { lights.swap(other.lights); }
    EmitterLights& operator=(const EmitterLights&) = delete;
    ~EmitterLights();
    std::vector<std::unique_ptr<stLight>> lights;
};

struct EmitterCycleResources {
    // Reverse member destruction releases animation before its lights.
    EmitterLights lights;
    std::unique_ptr<W8AnimObj> animation;

    EmitterCycleResources() = default;
    EmitterCycleResources(EmitterCycleResources&&) noexcept = default;
    EmitterCycleResources(const EmitterCycleResources&) = delete;
    ~EmitterCycleResources() = default;
    EmitterCycleResources& operator=(const EmitterCycleResources&) = delete;
    std::vector<std::unique_ptr<stLight>>* lightView()
    {
        return lights.lights.empty() ? nullptr : &lights.lights;
    }
    void swap(EmitterCycleResources& other) noexcept
    {
        lights.lights.swap(other.lights.lights);
        animation.swap(other.animation);
    }
};

// All animations must disappear before ANY light collection: animation scene
// registrations currently depend on lights in other cycles as well.
template<std::size_t Count> class EmitterResources {
public:
    EmitterResources() = default;
    EmitterResources(const EmitterResources& other)
    {
        // Build a fully owned temporary so an exception follows the same order.
        EmitterResources copy;
        for (std::size_t i = 0; i < Count; ++i) {
            if (other.cycles[i].animation) {
                copy.cycles[i].animation = std::make_unique<W8AnimObj>(*other.cycles[i].animation);
            }
        }
        for (std::size_t i = 0; i < Count; ++i) {
            copy.cycles[i].lights.CloneFrom(other.cycles[i].lights);
        }
        for (std::size_t i = 0; i < Count; ++i) {
            cycles[i].swap(copy.cycles[i]);
        }
    }
    EmitterResources& operator=(const EmitterResources&) = delete;
    ~EmitterResources()
    {
        for (auto& cycle : cycles) {
            cycle.animation.reset();
        }
    }
    std::array<EmitterCycleResources, Count> cycles;
};
