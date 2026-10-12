#pragma once

#include <cstdint>
#include <ostream>

/* SurRender's diagnostic-console stream. The constructor creates a window-backed stream buffer and
   gives it focus; the destructor deletes the buffer. */
// VTABLE: SURRENDER 0x10076E20 srWindowOut
// class srWindowOut
class srWindowOut : public std::ostream {
public:
    srWindowOut(std::uint32_t handle, const char* title, std::int32_t width, std::uint32_t height);
    virtual ~srWindowOut();
};
