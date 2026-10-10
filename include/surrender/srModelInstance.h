#pragma once

#include "srModel.h"
#include "srNode.h"

#include <math.h>

// VTABLE: SURRENDER 0x10077194
// class srClassSupport<srModelInstance, srNode, 4352>

// VTABLE: SURRENDER 0x10077160 srClassSupport<srModelInstance, srNode, 4352>
// class srModelInstance
class srModelInstance : public srClassSupport<srModelInstance, srNode, 0x1100> {
public:
    SR_DLL_IMPORT virtual ~srModelInstance() override;
    SR_DLL_IMPORT srModelInstance(srNode* parent = 0);

    SR_DLL_IMPORT srModelInstance& operator=(const srModelInstance& other);

    // FUNCTION: SURRENDER 0x1004FFE0
    static const char* sGetClassName()
    {
        return "srModelInstance";
    }

    SR_DLL_IMPORT virtual void dump(std::ostream& stream) override;
    SR_DLL_IMPORT virtual void traverse(TraverseInfo& info) override;
    SR_DLL_IMPORT virtual void process(const ProcessInfo& info, e_processType type) override;
    SR_DLL_IMPORT virtual void getLocalBounds(BoundInfo& bounds) override;

    // The instance shares ownership of its model; renderers borrow it.
    srModel* getModel() const
    {
        return model_.get();
    }
    void setModel(std::shared_ptr<srModel> model)
    {
        model_ = std::move(model);
    }

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

    srVector3T<float> align_axis;
    float align_angle;
    w8_ulong exclusion_mask;

private:
    std::shared_ptr<srModel> model_;
};

