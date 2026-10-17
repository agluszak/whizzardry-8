#pragma once

#include <utility>

#include "srIlluminator.h"

// VTABLE: SURRENDER 0x10076FE8 srVertexProcessor
// VTABLE: SURRENDER 0x10076FA8 srVertexProcessor
// class srFog
class srFog : public srIlluminator {
public:
    srFog(srNode* parent = 0);

    void setDensity(float density);
    float getDensity() const;
    void setRange(double start, double end);

    virtual ~srFog() override;

    srFog& operator=(const srFog&) = default;

public:
    using srIlluminator::process;
    virtual int isActive(srVertexPipe& pipe) override;
    virtual void process(srVertexPipe& pipe) override;

    double fog_start; /* 0x150 */
    double fog_end;   /* 0x158 */
    float density;    /* 0x160 */
};
