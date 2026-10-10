#pragma once

#include "srModel.h"
#include "srNode.h"

#include <math.h>

// VTABLE: SURRENDER 0x10077194
// class srClassSupport<srModelInstance, srNode, 0, 4352>

// VTABLE: SURRENDER 0x10077150 srModel::Client
// VTABLE: SURRENDER 0x10077160 srClassSupport<srModelInstance, srNode, 0, 4352>
// class srModelInstance
class srModelInstance : public srClassSupport<srModelInstance, srNode, 0, 0x1100>,
                                      public srModel::Client {
public:
    SR_DLL_IMPORT srModelInstance(srNode* parent = 0);

    SR_DLL_IMPORT srModelInstance& operator=(const srModelInstance& other);

    // FUNCTION: SURRENDER 0x1004FFE0
    static const char* sGetClassName()
    {
        return "srModelInstance";
    }

    SR_DLL_IMPORT virtual void dump(std::ostream& stream) override;
    SR_DLL_IMPORT virtual srClass* vInstance() override;
    SR_DLL_IMPORT virtual void traverse(TraverseInfo& info) override;
    SR_DLL_IMPORT virtual void process(const ProcessInfo& info, e_processType type) override;
    SR_DLL_IMPORT virtual void getLocalBounds(BoundInfo& bounds) override;
    SR_DLL_IMPORT virtual void updateClient(srModel::Client::e_update update) override;

    double getAlignAngle() const;
    srVector3T<float> getAlignAxis() const;
    w8_ulong getExclusionMask() const;
    int isAligned() const;
    void setAlignAngle(double angle);
    void setAlignAxis(srVector3T<float> axis);
    void setAlignment(int enabled);
    void setExclusionMask(w8_ulong mask);

    srFlags<int> alignment_flags;

protected:
    SR_DLL_IMPORT virtual ~srModelInstance() override;

    srVector3T<float> align_axis;
    float align_angle;
    w8_ulong exclusion_mask;
};

W8_ABI_ASSERT((sizeof(srModelInstance) == 0x160), "srModelInstance_must_be_0x160");
W8_ASSERT_BASE_END(srModelInstance, srModel::Client, alignment_flags, 0x138);
