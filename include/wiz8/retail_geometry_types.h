#pragma once

namespace wiz8::retail {
#pragma pack(push, 1)
struct Vec3f {
    float x, y, z;
};
#pragma pack(pop)
static_assert(sizeof(Vec3f) == 12 && alignof(Vec3f) == 1);
}
