#pragma once

#include <optional>
#include <variant>

#include "glm/ext/vector_float3.hpp"

// A level surface stopped the probe.
struct GroundSurfaceContact {
    int surface;
};

// A collidable prop stopped the probe before any level surface.
struct GroundPropContact {
    int prop;
};

using GroundContact = std::variant<GroundSurfaceContact, GroundPropContact>;

struct GroundProbe {
    // The settled position on a contact; otherwise the input, lowered under
    // the level's ceiling.
    glm::vec3 position;
    std::optional<GroundContact> contact;

    // The collidable prop the probe settled on, if a prop stopped it.
    std::optional<int> prop() const
    {
        if (const auto* hit = contact ? std::get_if<GroundPropContact>(&*contact) : nullptr)
            return hit->prop;
        return std::nullopt;
    }
};
