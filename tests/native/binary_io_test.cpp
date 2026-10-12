#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <algorithm>
#include <bit>
#include <exception>
#include <stdexcept>

#include "surrender/srSerialization.h"
#include "surrender/srHuffman.h"
#include "compat/compiler.h"
#include "surrender/srIO.h"
#include "surrender/srQuadWord.h"


TEST_CASE("binary_io_test")
{
    // Known bytes are independent of the writer; cover widths, signed values,
    // both byte orders, IEEE bits and the old low/high quad-word representation.
    const std::array<Uint8, 33> golden = {
        0x80, 0xfe, 0xff, 0x78, 0x56, 0x34, 0x12,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x00, 0x00, 0x00, 0x80, // float -0
        0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // double infinity, BE
        0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a // BE u16, LE u32
    };
    auto output = srIO::own(SDL_IOFromDynamicMem());
    srIO::check(SDL_WriteS8(output.get(), -128));
    srIO::check(SDL_WriteS16LE(output.get(), -2));
    srIO::check(SDL_WriteU32LE(output.get(), 0x12345678));
    srSerialization::writeQuadWordBE(output.get(), {0x89abcdef, 0x01234567});
    srSerialization::writeFloatLE(output.get(), -0.0f);
    srSerialization::writeDoubleBE(output.get(), std::numeric_limits<double>::infinity());
    srIO::check(SDL_WriteU16BE(output.get(), 0x3412));
    srIO::check(SDL_WriteU32LE(output.get(), 0x9abc5678));
    REQUIRE(SDL_GetIOSize(output.get()) == Sint64(golden.size()));
    const auto* bytes = SDL_GetPointerProperty(SDL_GetIOProperties(output.get()),
                        SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    REQUIRE(memcmp(bytes, golden.data(), golden.size()) == 0);
    auto input = srIO::own(SDL_IOFromConstMem(golden.data(), golden.size()));
    Sint8 byte;
    Sint16 word;
    Uint32 dword;
    Uint16 unsigned_word;
    REQUIRE((SDL_ReadS8(input.get(), &byte) && byte == -128));
    REQUIRE((SDL_ReadS16LE(input.get(), &word) && word == -2));
    REQUIRE((SDL_ReadU32LE(input.get(), &dword) && dword == 0x12345678));
    const auto quad = srSerialization::readQuadWordBE(input.get());
    REQUIRE((quad.lo == 0x89abcdef && quad.hi == 0x01234567));
    REQUIRE(std::bit_cast<Uint32>(srSerialization::readFloatLE(input.get())) == 0x80000000);
    REQUIRE(srSerialization::readDoubleBE(input.get()) == std::numeric_limits<double>::infinity());
    REQUIRE((SDL_ReadU16BE(input.get(), &unsigned_word) && unsigned_word == 0x3412));
    REQUIRE((SDL_ReadU32LE(input.get(), &dword) && dword == 0x9abc5678));
    REQUIRE((!SDL_ReadU32LE(input.get(), &dword) && SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_EOF));
    REQUIRE(SDL_SeekIO(input.get(), 0, SDL_IO_SEEK_SET) == 0);
    REQUIRE((SDL_ReadS8(input.get(), &byte) && byte == -128));
    REQUIRE(!SDL_WriteU32LE(input.get(), 1));

    // Opposite endian spellings, including IEEE NaN payload bits.
    const Uint8 formats[] = {0x7f,0xc1,0x23,0x45, 0,0,0,0,0,0,0,0x80,
                            0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01};
    auto formats_input = srIO::own(SDL_IOFromConstMem(formats, sizeof(formats)));
    const float nan = srSerialization::readFloatBE(formats_input.get());
    const double negative_zero = srSerialization::readDoubleLE(formats_input.get());
    const auto little_quad = srSerialization::readQuadWordLE(formats_input.get());
    REQUIRE(std::bit_cast<Uint32>(nan) == 0x7fc12345);
    REQUIRE(std::bit_cast<Uint64>(negative_zero) == 0x8000000000000000);
    REQUIRE((little_quad.lo == 0x89abcdef && little_quad.hi == 0x01234567));
    auto formats_output = srIO::own(SDL_IOFromDynamicMem());
    srSerialization::writeFloatBE(formats_output.get(), nan);
    srSerialization::writeDoubleLE(formats_output.get(), negative_zero);
    srSerialization::writeQuadWordLE(formats_output.get(), little_quad);
    bytes = SDL_GetPointerProperty(SDL_GetIOProperties(formats_output.get()),
            SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    REQUIRE(SDL_GetIOSize(formats_output.get()) == sizeof(formats));
    REQUIRE(memcmp(bytes, formats, sizeof(formats)) == 0);
    bool rejected = false;
    // A short final cache is valid; requesting a bit past it is not. Cache
    // lookahead must never reuse bytes from an earlier block.
    const Uint8 bits_data[] = {0x81, 0x42};
    auto bits_input = srIO::own(SDL_IOFromConstMem(bits_data, sizeof(bits_data)));
    srHuffman::BitIStream bits(bits_input.get());
    REQUIRE((bits.get(9) == 0x81 && bits.get(7) == 0x21));
    rejected = false;
    try { bits.getBit(); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    REQUIRE(bits.peekByte() == 0); // Padding is for lookahead, not consumption.
    for (const auto rewind : {std::int32_t(-1), std::numeric_limits<std::int32_t>::min(),
                             std::int32_t(17), std::numeric_limits<std::int32_t>::max()}) {
        rejected = false;
        try { bits.rewind(rewind); } catch (const std::out_of_range&) { rejected = true; }
        REQUIRE((rejected && bits.peekByte() == 0));
    }
    bits.rewind(16);
    rejected = false;
    try { bits.rewind(1); } catch (const std::out_of_range&) { rejected = true; }
    REQUIRE((rejected && bits.get(8) == 0x81));
    rejected = false;
    try { bits.get(32); } catch (const std::out_of_range&) { rejected = true; }
    REQUIRE((rejected && bits.get(8) == 0x42)); // A rejected read does not advance.
    REQUIRE(SDL_SeekIO(bits_input.get(), 1, SDL_IO_SEEK_SET) == 1);
    srHuffman::BitIStream offset_bits(bits_input.get());
    rejected = false;
    try { offset_bits.rewind(1); } catch (const std::out_of_range&) { rejected = true; }
    REQUIRE((rejected && offset_bits.get(8) == 0x42));

    // Exercise a cache refill and rewind across its boundary.
    std::array<Uint8, 130> long_bits{};
    long_bits[127] = 0x81; long_bits[128] = 0x42; long_bits[129] = 0xff;
    auto long_input = srIO::own(SDL_IOFromConstMem(long_bits.data(), long_bits.size()));
    srHuffman::BitIStream long_reader(long_input.get());
    for (std::uint32_t i = 0; i < 127; ++i) REQUIRE(long_reader.get(8) == 0);
    REQUIRE(long_reader.get(24) == 0xff4281);
    long_reader.rewind(24);
    REQUIRE((long_reader.get(24) == 0xff4281 && long_reader.peekByte() == 0));
    // A sparse SDL backend exercises positions beyond the former 32-bit bit
    // cursor without allocating a gigabyte-sized fixture.
    struct SparseInput { Sint64 position, size; } sparse{Sint64(1) << 30, (Sint64(1) << 30) + 1};
    SDL_IOStreamInterface interface;
    SDL_INIT_INTERFACE(&interface);
    interface.size = [](void* cookie) -> Sint64 { return static_cast<SparseInput*>(cookie)->size; };
    interface.seek = [](void* cookie, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
        auto& self = *static_cast<SparseInput*>(cookie);
        const auto base = whence == SDL_IO_SEEK_SET ? 0 :
                          whence == SDL_IO_SEEK_CUR ? self.position : self.size;
        if (offset < -base || offset > self.size - base) return -1;
        return self.position = base + offset;
    };
    interface.read = [](void* cookie, void* data, size_t requested, SDL_IOStatus* status) -> size_t {
        auto& self = *static_cast<SparseInput*>(cookie);
        const auto count = std::min(requested, size_t(self.size - self.position));
        std::fill_n(static_cast<Uint8*>(data), count, Uint8(0xa5));
        self.position += count;
        if (count < requested) *status = SDL_IO_STATUS_EOF;
        return count;
    };
    auto sparse_io = srIO::own(SDL_OpenIO(&interface, &sparse));
    srHuffman::BitIStream sparse_reader(sparse_io.get());
    REQUIRE(sparse_reader.get(8) == 0xa5);
    sparse_reader.rewind(8);
    REQUIRE(sparse_reader.get(8) == 0xa5);
    sparse.position = 0;
    sparse.size = std::numeric_limits<Sint64>::max();
    rejected = false;
    try { srHuffman::BitIStream invalid(sparse_io.get()); }
    catch (const std::out_of_range&) { rejected = true; }
    REQUIRE(rejected);
    puts("ok: SDL scalar formats, EOF and Huffman cursor/cache bounds");


}
