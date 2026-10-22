#include "wiz8/render/fog.h"

void FogParams::setDensity(float density)
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

float FogParams::getDensity() const
{
    return density;
}

void FogParams::setRange(double start, double end)
{
    fog_start = start;
    fog_end = end;
}

FogParams::FogParams(WorldScene* scene)
{
    setScene(scene);
    fog_start = 0.0;
    fog_end = 1000.0;
    density = 0.5f;
}

FogParams::~FogParams()
{
    setScene(nullptr);
}

FogParams::FogParams(const FogParams& other) : FogParams()
{
    *this = other;
}
FogParams& FogParams::operator=(const FogParams& other)
{
    if (this == &other) return *this;
    name = other.name;
    visible = other.visible;
    group_mask = other.group_mask;
    fog_start = other.fog_start;
    fog_end = other.fog_end;
    density = other.density;
    return *this;
}
