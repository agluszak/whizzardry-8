#pragma once

class
#if defined(SURRENDER_BUILD)

#endif
    srWindow {
public:
    static int isWindow(w8_ulong_ptr handle);
    static w8_long getWidth(w8_ulong_ptr handle);
    static w8_long getHeight(w8_ulong_ptr handle);
};
