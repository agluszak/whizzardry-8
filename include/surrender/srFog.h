#pragma once

#include <utility>

#include "srIlluminator.h"

// VTABLE: SURRENDER 0x10076FE8 srVertexProcessor
// class srClassSupport<srFog, srIlluminator, 4624>
// VTABLE: SURRENDER 0x10076FF4 srClassSupport<srIlluminator, srNode, 4608>
// class srClassSupport<srFog, srIlluminator, 4624>
// VTABLE: SURRENDER 0x10076FA8 srVertexProcessor
// VTABLE: SURRENDER 0x10076FB4 srClassSupport<srIlluminator, srNode, 4608>
// class srFog
class srFog : public srClassSupport<srFog, srIlluminator, 0x1210> {
public:
    srFog(srNode* parent = 0);

    // FUNCTION: SURRENDER 0x1004C1A0
    static const char* sGetClassName() { return "srFog"; }
    void setDensity(float density);
    float getDensity() const;
    void setRange(double start, double end);
    std::pair<double, double> getRange() const;

    virtual void dump(std::ostream& stream) override;
    virtual void verify(srRuntimeClass::e_verify mode) override;

    virtual ~srFog() override;

    srFog& operator=(const srFog&) = default;

public:
    using srClassSupport<srFog, srIlluminator, 0x1210>::process;
    virtual int isActive(srVertexPipe& pipe) override;
    virtual void process(srVertexPipe& pipe) override;

    double fog_start; /* 0x150 */
    double fog_end;   /* 0x158 */
    float density;    /* 0x160 */
};
