#include <algorithm>
#include <ostream>
#include <cstddef>
#include <span>
#include <utility>

#include "glm/geometric.hpp"
#include "surrender/srFog.h"
#include "surrender/channel_math.h"
#include "surrender/srStreamFlags.h"
#include "surrender/srDebug.h"
#include "surrender/srVertexPipe.h"
#include "compat/compiler.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/srVertexProcessor.h"

// FUNCTION: SURRENDER 0x1004C0E0
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

// FUNCTION: SURRENDER 0x1004C130
float srFog::getDensity() const
{
    return density;
}

// FUNCTION: SURRENDER 0x1004C140
void srFog::setRange(double start, double end)
{
    fog_start = start;
    fog_end = end;
}

// FUNCTION: SURRENDER 0x1004C170
std::pair<double, double> srFog::getRange() const
{
    return {fog_start, fog_end};
}

// FUNCTION: SURRENDER 0x1004B6F0
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

// FUNCTION: SURRENDER 0x1004B7A0
void srFog::verify(srRuntimeClass::e_verify mode)
{
    srClass::verify(mode);
    if ((density < 0.0) || (1.0 < density)) {
        srAssertFail("(density >= 0.0) && (density <= 1.0)",
                     "D:\\srsdk1x\\sources\\corelib\\srFog.cpp", 0x3c, 0);
    }
    if (fog_start < 0.0) {
        srAssertFail("fogStart >= 0.0", "D:\\srsdk1x\\sources\\corelib\\srFog.cpp", 0x3d, 0);
    }
    if (fog_end < 0.0) {
        srAssertFail("fogEnd >= 0.0", "D:\\srsdk1x\\sources\\corelib\\srFog.cpp", 0x3e, 0);
    }
    if (fog_end < fog_start) {
        srAssertFail("fogEnd >= fogStart", "D:\\srsdk1x\\sources\\corelib\\srFog.cpp", 0x3f, 0);
    }
}

// FUNCTION: SURRENDER 0x1004B870
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
            const float* distances = pipe.getEyeSpaceDist();
            float start = fog_start;
            if (count != 0) {
                if (start == 0.0f) {
                    if (values != distances) {
                        std::copy_n(distances, count, values);
                    }
                } else {
                    for (std::size_t i = 0; i < count; ++i)
                        values[i] = (-start) + distances[i];
                }
            }
            density = this->density;
            srChannelMath::normalize({values, static_cast<std::size_t>(count)}, scale, density);
            pipe.applyFog(values);
        } else {
            float* fog = pipe.getFog();
            if (this->density >= 1.0f) {
                std::fill_n(fog, count, 1.0f);
                return;
            }
            density = 1.0f - this->density;
            srChannelMath::scale({fog, static_cast<std::size_t>(count)}, density);
            if ((count != 0) && (this->density != 0.0f)) {
                for (std::size_t i = 0; i < count; ++i)
                    fog[i] = (this->density) + fog[i];
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1004BB60
srFog::srFog(srNode* parent)
{
    if (parent != 0) {
        setParent(parent, 0);
    }
    fog_start = 0.0;
    fog_end = 1000.0;
    density = 0.5f;
}

// FUNCTION: SURRENDER 0x1004BD00
void srFog::dump(std::ostream& stream)
{
    srNode::dump(stream);
    w8_long flags = srGetStreamFlags(stream);
    srSetStreamFlags(stream, (flags & 0xfffffe7fL) | 0x40);
    stream.width(0x20);
    stream << "Density: " << density << '\n';
    stream.width(0x20);
    stream << "Fog start: " << fog_start << '\n';
    stream.width(0x20);
    stream << "Fog end: " << fog_end << '\n';
    srSetStreamFlags(stream, flags & 0x7fff);
}

// FUNCTION: SURRENDER 0x1004C350
srFog::~srFog() {}
