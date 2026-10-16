#pragma once

#include <memory>

#include "srBoundingBox.h"
#include "srBoundingSphere.h"
#include "srClass.h"

class srGERD;

// VTABLE: SURRENDER 0x10076D08 srModel
// class srModel
class srModel : public srClass {
public:
    srModel() = default;

    srModel& operator=(const srModel& other) = default;

    ~srModel() override = default;

public:
    struct Bounds {
        srBoundingBox box;
        srBoundingSphere sphere;
    };

    virtual Bounds getBounds() = 0;
    virtual void render(class srGERD& renderer) = 0;
};
