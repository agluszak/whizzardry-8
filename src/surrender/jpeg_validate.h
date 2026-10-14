#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
// libjpeg validates all scans and reports corrupt/truncated data as warnings.
// Kept in C so its setjmp error boundary cannot skip C++ destructors.
bool srValidateJpeg(const unsigned char* data, size_t size, unsigned* width, unsigned* height,
                    unsigned* components);
#ifdef __cplusplus
}
#endif
