#pragma once

#include <ostream>
#include "srHeap.h"

w8_long __cdecl srDebugPrintf(w8_ulong level, const char* format, ...) W8_PRINTF(2, 3);
w8_long __cdecl srPrintf(const char* format, ...);
w8_long __cdecl srStreamPrintf(std::ostream& stream, const char* format, ...) W8_PRINTF(2, 3);
const char* __cdecl srBoolToString(int value);

/* Consumers see the fixed-arity form declared in wiz8's sr_api.h. */
void __cdecl srAssertFail(const char* expression, const char* source_path, w8_long line,
                                        const char* message, ...);

typedef void(__cdecl* srAssertHandler)(const char* expression, const char* source_path, w8_long line,
                                       const char* message);

srAssertHandler __cdecl srAssertGetFunc();
void __cdecl srAssertSetFunc(srAssertHandler handler);
void __cdecl srDefaultAssertFailFunc(const char* expression, const char* source_path,
                                                   w8_long line, const char* message);

/* Sink that discards every insertion. */
// VTABLE: SURRENDER 0x10076C00 srDummyStreamBuf
// class srDummyStreamBuf
class
#if defined(SURRENDER_BUILD)

#endif
    srDummyStreamBuf : public std::streambuf {
public:
    srDummyStreamBuf();

private:
    virtual int overflow(int ch);
    virtual int underflow();

    /* The copy constructor reinitializes a fresh stream buffer rather than copying get/put state. */
    srDummyStreamBuf(const srDummyStreamBuf& other);
    srDummyStreamBuf& operator=(const srDummyStreamBuf& other);
};

// VTABLE: SURRENDER 0x10076C34 srOStream_withassign
class srOStream_withassign : public std::ostream {
public:
    srOStream_withassign(std::streambuf* buffer);
};

extern class srOStream_withassign srDummyStream;
extern class srOStream_withassign srErr;
extern class srOStream_withassign srLog;
extern class srOStream_withassign srOut;
