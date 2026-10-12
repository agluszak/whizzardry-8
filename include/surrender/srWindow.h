#pragma once


#include <cstdint>
class srWindow {
public:
    static std::int32_t isWindow(std::uintptr_t handle);
    static std::int32_t getWidth(std::uintptr_t handle);
    static std::int32_t getHeight(std::uintptr_t handle);
};
