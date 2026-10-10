#pragma once

#include <vector>
#include "srBinStream.h"
#include "srQuadWord.h"

// VTABLE: SURRENDER 0x10076AE8 srBinStream
// VTABLE: SURRENDER 0x10076AFC srBinOStream
// class srBinOStream
class srBinOStream : public virtual srBinStream {
public:
    SR_DLL_IMPORT srBinOStream& putChar(char value);
    SR_DLL_IMPORT srBinOStream& putDWord(w8_ulong value);
    SR_DLL_IMPORT srBinOStream& putDouble(double value);
    SR_DLL_IMPORT srBinOStream& putFloat(float value);
    SR_DLL_IMPORT srBinOStream& putQWord(srQuadWord value);
    SR_DLL_IMPORT srBinOStream& putWord(unsigned short value);
    SR_DLL_IMPORT srBinOStream& write(const void* source, w8_ulong size);

protected:
    virtual SR_DLL_IMPORT unsigned short vput(char value);

private:
    virtual w8_ulong vwrite(const void* source, w8_ulong size) = 0;
};

// Memory-backed output stream.
// VTABLE: SURRENDER 0x10076BB0 srBinStream
// VTABLE: SURRENDER 0x10076BC4 srBinOStream
// class srBinOMStream
class SR_DLL_IMPORT srBinOMStream : public srBinOStream {
public:
    srBinOMStream();
    void* getPtr();
    virtual w8_ulong getSize() override;
    virtual srBinStream& seek(w8_ulong position, srBinStream::e_seekDir direction) override;
    virtual srBinStream& seek(w8_ulong position) override;
    virtual w8_ulong tell() override;

private:
    virtual w8_ulong vwrite(const void* source, w8_ulong size) override;

    std::vector<unsigned char> buffer;
    w8_ulong position0;
};

W8_ABI_ASSERT(sizeof(srBinOStream) == 0x18, "srBinOStream_must_be_0x18");
W8_ABI_ASSERT(sizeof(srBinOMStream) == 0x2c, "srBinOMStream_must_be_0x2c");
