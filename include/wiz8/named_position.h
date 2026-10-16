#pragma once

#include "glm/ext/vector_float3.hpp"

struct W8NamedPosition {
    W8NamedPosition()
    {
        name[0] = '\0';
        position = glm::vec3(0);
        angle = 0.0f;
        direction = glm::vec3(0);
    }

    char name[0x80];
    glm::vec3 position;
    float angle;
    glm::vec3 direction;
};
