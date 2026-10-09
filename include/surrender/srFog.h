#pragma once

#include "srIlluminator.h"

// VTABLE: SURRENDER 0x10076FE8 srVertexProcessor
// class srClassSupport<srFog, srIlluminator, 0, 4624>
// VTABLE: SURRENDER 0x10076FF4 srClassSupport<srIlluminator, srNode, 0, 4608>
// class srClassSupport<srFog, srIlluminator, 0, 4624>
// VTABLE: SURRENDER 0x10076FA8 srVertexProcessor
// VTABLE: SURRENDER 0x10076FB4 srClassSupport<srIlluminator, srNode, 0, 4608>
// class srFog
class srFog : public srClassSupport<srFog, srIlluminator, false, 0x1210> {
public:
    typedef srClientSupport<srFog, 0x1210> ClientType;

    srFog(srNode* parent = 0);


    // FUNCTION: SURRENDER 0x1004C1A0
    static const char* sGetClassName()
    {
        return "srFog";
    }
    void setDensity(float density);
    float getDensity() const;
    void setRange(double start, double end);
    void getRange(double& start, double& end);

    virtual void dump(std::ostream& stream) override;
    virtual void verify(srRuntimeClass::e_verify mode) override;

    virtual ~srFog() override;

public:
    virtual srClass* vInstance() override;

    using srClassSupport<srFog, srIlluminator, false, 0x1210>::process;
    virtual int isActive(srVertexPipe& pipe) override;
    virtual void process(srVertexPipe& pipe) override;

    double fog_start; /* 0x150 */
    double fog_end;   /* 0x158 */
    float density;    /* 0x160 */
};

W8_ABI_ASSERT(sizeof(srFog) == 0x168, "srFog_must_be_0x168");
