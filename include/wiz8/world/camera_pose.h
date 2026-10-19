#pragma once
#include <glm/glm.hpp>

#include "wiz8/retail_opaque_records.h"
struct CameraOrientation {
    float yaw = 0, pitch = 0;
};
struct CameraPose {
    glm::vec3 position{0};
    CameraOrientation orientation;
    wiz8::retail::CameraPoseOpaque retail;
};
