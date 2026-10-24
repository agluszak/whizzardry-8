#include "wiz8/render/camera_shake.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"

ShakeDefinition::ShakeDefinition(float duration, bool preset, float intensity, float distance_cap,
                                 const glm::vec3* position)
    : duration(duration), intensity(intensity), distance_cap(distance_cap), limit_distance(preset),
      quadratic_falloff(preset), fade_out(preset)
{
    if (position) {
        this->position = *position;
    }
}

ShakeSystem::Instance::Instance(const ShakeDefinition& definition, const ShakeDefinition* key,
                                TimerClockView clocks)
    : definition(definition),
      timer(clocks, std::isfinite(definition.duration) ? definition.duration : 0.0f,
            TimerClock::World),
      key(key)
{
    timer.Restart();
}

void ShakeSystem::Start(const ShakeDefinition& definition, const ShakeDefinition* key)
{
    auto instance = std::make_unique<Instance>(definition, key, clocks_);
    if (key) {
        for (auto& current : active) {
            if (current->key == key) {
                current.swap(instance);
                return;
            }
        }
    }
    active.push_back(std::move(instance));
}

void ShakeSystem::Stop(const ShakeDefinition& key)
{
    std::erase_if(active, [&key](const auto& instance) { return instance->key == &key; });
}

bool ShakeSystem::IsActive(const ShakeDefinition& key) const
{
    return std::ranges::any_of(active,
                               [&key](const auto& instance) { return instance->key == &key; });
}

bool ShakeSystem::Instance::Evaluate(const glm::vec3& camera, float& amount)
{
    if (!definition.continuous && !(definition.duration > 0.0f)) {
        return false;
    }
    const float progress =
        (definition.continuous || std::isinf(definition.duration)) ? 0.0f : timer.GetProgress();
    if (progress >= 1.0f) {
        return false;
    }
    amount = 1.0f;
    if (definition.limit_distance) {
        const auto delta = definition.position - camera;
        const float distance = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (definition.distance_cap < distance) {
            amount = 0.0f;
        } else if (definition.quadratic_falloff) {
            const float weight = distance / definition.distance_cap - 1.0f;
            amount = weight * weight;
        }
    }
    if (definition.fade_out) {
        amount *= 1.0f - progress;
    } else if (definition.fade_in) {
        amount *= progress;
    }
    return true;
}

float ShakeSystem::Update(const glm::vec3& camera)
{
    float intensity = 0.0f;
    std::vector<std::function<void()>> callbacks;
    callbacks.reserve(active.size());
    std::erase_if(active, [&](const auto& instance) {
        float amount;
        if (instance->Evaluate(camera, amount)) {
            intensity += amount;
            return false;
        }
        if (instance->definition.completion_callback) {
            callbacks.push_back(std::move(instance->definition.completion_callback));
        }
        return true;
    });
    // Callbacks may start/stop shakes or destroy the definitions that keyed
    // them. Retire all completed instances before invoking any callback.
    for (const auto& callback : callbacks) {
        callback();
    }
    return intensity;
}

void ShakeSystem::advance(const glm::vec3& camera)
{
    const float intensity = Update(camera);
    if (intensity <= 0.0f) {
        offset_ = {};
        return;
    }
    const float amplitude = std::min(intensity, 1.0f) * 2.5e+02f;
    const int span = static_cast<int>(amplitude) << 1;
    offset_.x = PresentationRng().below(span) - amplitude;
    offset_.y = PresentationRng().below(span) - amplitude;
    offset_.z = PresentationRng().below(span) - amplitude;
}
