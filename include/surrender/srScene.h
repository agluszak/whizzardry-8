#pragma once

#include "srNode.h"

class srCamera;
class srGERD;
class srModeler;

// VTABLE: SURRENDER 0x10077264
// class srClassSupport<srScene, srNode, 0, 4112>

// VTABLE: SURRENDER 0x10077230 srScene
// class srScene
class SR_DLL_IMPORT srScene : public srClassSupport<srScene, srNode, 0, 0x1010> {
public:
    enum e_enable { ENABLE_NODE_PICK_KEYS = 0 };



    struct Statistics {
        double elapsed;              /* seconds since the last reset */
        w8_ulong render_calls;  /* scene renders accumulated */
        w8_ulong node_calls;    /* node visits accumulated per render */
        w8_ulong process_calls; /* per-node process calls accumulated */
        w8_ulong value_14;
    };

    srScene(srNode* parent = 0);

    srScene& operator=(const srScene& other);

    virtual void dump(std::ostream& stream) override;
    virtual srClass* vInstance() override;
    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;

    void disable(e_enable option);
    void enable(e_enable option);
    void getAmbientLight(srVector3T<float>& color) const;
    srVector3T<float> getAmbientLight() const;
    // FUNCTION: SURRENDER 0x10056C70 SYMBOL
    // RECOMP: ?getFogColor@srScene@@QBEXAAV?$srVector3T@M@@@Z
    void getFogColor(srVector3T<float>& color) const
    {
        color = fog_color;
    }
    srVector3T<float> getFogColor() const;
    void getStatistics(Statistics& statistics);
    int isEnabled(e_enable option) const;
    void render(srGERD& renderer, class srCamera* camera);
    void resetStatistics();
    // FUNCTION: SURRENDER 0x10056C10
    static const char* sGetClassName()
    {
        return "srScene";
    }
    void setAmbientLight(float red, float green, float blue);
    void setAmbientLight(const srVector3T<float>& color);
    void setFogColor(float red, float green, float blue);
    // FUNCTION: SURRENDER 0x10056D40 SYMBOL
    // RECOMP: ?setFogColor@srScene@@QAEXABV?$srVector3T@M@@@Z
    void setFogColor(const srVector3T<float>& color)
    {
        fog_color = color;
    }

protected:
    srFlags<e_enable> enabled;       /* 0x138 */
    Statistics statistics;           /* 0x140 */
    TraverseInfo traversal;          /* 0x158 (renderer at 0x170) */
    srVector3T<float> ambient_light; /* 0x174 */
    srVector3T<float> fog_color;     /* 0x180 */
};

W8_ABI_ASSERT((sizeof(srScene) == 0x190), "srScene_must_be_0x190");
W8_ABI_ASSERT((sizeof(srScene::Statistics) == 0x18), "srScene_Statistics_must_be_0x18");
