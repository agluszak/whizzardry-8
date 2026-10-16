#pragma once

#include <utility>

#include "srIlluminator.h"

// VTABLE: SURRENDER 0x10076FE8 srVertexProcessor
// VTABLE: SURRENDER 0x10076FA8 srVertexProcessor
// class srFog
class srFog : public srIlluminator {
public:
    SR_DLL_IMPORT srFog(srNode* parent = 0);

    SR_DLL_IMPORT void setDensity(float density);
    SR_DLL_IMPORT float getDensity() const;
    SR_DLL_IMPORT void setRange(double start, double end);
    SR_DLL_IMPORT std::pair<double, double> getRange() const;

    virtual SR_DLL_IMPORT ~srFog() override;

    srFog& operator=(const srFog&) = default;

public:
    using srIlluminator::process;
    virtual SR_DLL_IMPORT int isActive(srVertexPipe& pipe) override;
    virtual SR_DLL_IMPORT void process(srVertexPipe& pipe) override;

    double fog_start; /* 0x150 */
    double fog_end;   /* 0x158 */
    float density;    /* 0x160 */
};
