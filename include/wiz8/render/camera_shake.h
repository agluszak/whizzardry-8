#pragma once
#include <functional>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "wiz8/platform/game_timer.h"
// Reusable animation event data. Starting a shake copies this definition;
// neither the definition nor its caller owns a running instance.
struct ShakeDefinition {
    ShakeDefinition(float duration, bool preset, float intensity, float distance_cap,
                    const glm::vec3* position);
    float duration;
    float intensity;
    float distance_cap;
    glm::vec3 position{0, 0, 0};
    bool continuous = false;
    bool limit_distance = false;
    bool quadratic_falloff = false;
    bool fade_out = false;
    bool fade_in = false;
    int cycle = 0;
    int frame = 0;
    int subcycle = 0;
    std::function<void()> completion_callback;
};

class ShakeSystem {
public:
    // A key identifies a reusable definition that can be restarted/stopped.
    // Unkeyed instances run once. The system exclusively owns every instance.
    void Start(const ShakeDefinition&, const ShakeDefinition* key = nullptr);
    void Stop(const ShakeDefinition& key);
    bool IsActive(const ShakeDefinition& key) const;
    float Update(const glm::vec3& camera);

private:
    struct Instance {
        explicit Instance(const ShakeDefinition& definition, const ShakeDefinition* key);
        ShakeDefinition definition;
        W8GameTimer timer;
        const ShakeDefinition* key;
        bool Evaluate(const glm::vec3& camera, float& amount);
    };
    std::vector<std::unique_ptr<Instance>> active;
};

ShakeSystem& CameraShakes();
/* The frame's shake: an offset the prepared views apply to the eye. It never moves the
   party. */
extern bool g_camera_shake_active;
extern glm::vec3 g_camera_shake_offset;
void UpdateShakeEffects();
