#include <algorithm>
#include <cstddef>
#include <span>

#include "compat/compiler.h"
#include "glm/geometric.hpp"
#include "surrender/channel_math.h"
#include "surrender/srFog.h"
#include "surrender/srNode.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"

void srFog::setDensity(float density)
{
    if (density <= 0.0f) {
        this->density = 0.0f;
        return;
    }
    if (density >= 1.0f) {
        this->density = 1.0f;
        return;
    }
    this->density = density;
}

float srFog::getDensity() const
{
    return density;
}

void srFog::setRange(double start, double end)
{
    fog_start = start;
    fog_end = end;
}

int srFog::isActive(srVertexPipe& pipe)
{
    if ((group_mask & pipe.getExclusionMask()) != 0) {
        return 0;
    }
    if (density > 0.0f) {
        const auto sphere = pipe.getEyeSpaceBoundingSphere();
        if ((sphere.radius <= fog_start) &&
            (glm::dot(sphere.center, sphere.center) <
             (fog_start - sphere.radius) * (fog_start - sphere.radius))) {
            return 0;
        }
        return 1;
    }
    return 0;
}

void srFog::process(srVertexPipe& pipe)
{
    float values[64];
    float scale;
    float density;
    if (pipe.isChannelAvailable(srVertexProcessor::CHANNEL_FOG)) {
        const auto sphere = pipe.getEyeSpaceBoundingSphere();
        double limit = sphere.radius + fog_end;
        w8_long count = (w8_long)pipe.getVertexCount();
        if (glm::dot(sphere.center, sphere.center) <= limit * limit) {
            if (fog_end == fog_start) {
                scale = 1e+08f;
            } else {
                scale = 1.0 / (fog_end - fog_start);
            }
            const auto distances = pipe.getEyeSpaceDist();
            srChannelMath::distanceRamp({values, static_cast<std::size_t>(count)}, distances,
                                        fog_start, scale, this->density,
                                        srChannelMath::RampDirection::increasing);
            pipe.applyFog({values, static_cast<std::size_t>(count)});
        } else {
            const auto fog = pipe.getFog();
            if (this->density >= 1.0f) {
                std::ranges::fill(fog, 1.0f);
                return;
            }
            density = 1.0f - this->density;
            srChannelMath::scale(fog, density);
            srChannelMath::add(fog, this->density);
        }
    }
}

srFog::srFog(srNode* parent)
{
    if (parent != 0) {
        setParent(parent, 0);
    }
    fog_start = 0.0;
    fog_end = 1000.0;
    density = 0.5f;
}

srFog::~srFog() = default;
