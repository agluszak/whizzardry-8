#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"

/* The six radar blip classes. Remembered monsters that cannot be rendered use
   the gray class; items and missiles use the final two. */
enum W8RadarBlipClass {
    W8_RADAR_BLIP_NEUTRAL = 0,
    W8_RADAR_BLIP_HOSTILE = 1,
    W8_RADAR_BLIP_FRIENDLY = 2,
    W8_RADAR_BLIP_LAST_SEEN = 3,
    W8_RADAR_BLIP_ITEM = 4,
    W8_RADAR_BLIP_MISSILE = 5,
    W8_RADAR_BLIP_CLASS_COUNT = 6
};

class Game;
struct OverlayQuad;
struct W8TargetingPresentation;

class RadarMap {
public:
    RadarMap(Game& game, bool zoomed);
    ~RadarMap();
    void captureBackdrop();
    void refresh();
    void update(const W8TargetingPresentation& presentation);
    void setZoom(bool zoomed);
    void toggleZoom() { setZoom(!zoomed_); }
    void setHovered(bool hovered);

private:
    float outerRadius() const;
    void placeBlip(glm::vec3 delta, W8RadarBlipClass group, bool lit, std::size_t index);
    Game& game_;
    bool zoomed_ = false;
    bool hovered_ = false;
    float party_radius_ = 0;
    std::unique_ptr<OverlayQuad> backdrop_, compass_, frame_, map_;
    std::vector<std::unique_ptr<OverlayQuad>> blips_;
};
