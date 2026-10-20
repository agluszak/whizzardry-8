#pragma once
class WorldScene;
#include <cstdint>
#include <string>
#include <utility>

#include "wiz8/render/vertex_channels.h"

class FogParams {
public:
    FogParams(WorldScene* scene = 0);

    void setDensity(float density);
    float getDensity() const;
    void setRange(double start, double end);

    ~FogParams();

    FogParams(const FogParams& other);
    FogParams& operator=(const FogParams& other);

public:
    double fog_start; /* 0x150 */
    double fog_end;   /* 0x158 */
    float density;    /* 0x160 */

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    WorldScene* scene = nullptr; // Borrowed typed render membership.
    void setScene(WorldScene* value);
    std::uint32_t group_mask = 0x80000000u;
    glm::vec3 eye_location{0};
    void setGroupMask(std::uint32_t value) { group_mask = value; }
    std::uint32_t getGroupMask() const { return group_mask; }
};
