#pragma once

#include "srBoundingBox.h"
#include "srBoundingSphere.h"
#include <memory>
#include "srTypeRegistry.h"

class srGERD;

// VTABLE: SURRENDER 0x10076CE8
// class srClassSupport<srModel, srClass, 8192>

// VTABLE: SURRENDER 0x10076D08 srModel
// class srModel
class SR_DLL_IMPORT srModel : public srClassSupport<srModel, srClass, 0x2000> {
public:
    srModel();

    srModel& operator=(const srModel& other);

    // FUNCTION: SURRENDER 0x1003C6F0
    static const char* sGetClassName()
    {
        return "srModel";
    }

    virtual ~srModel() override;

public:
    struct Bounds {
        srBoundingBox box;
        srBoundingSphere sphere;
    };

    virtual Bounds getBounds() = 0;
    virtual void render(class srGERD& renderer) = 0;
};
