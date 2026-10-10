#include "surrender/srIO.h"
#include "surrender/srHuffman.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); return 1; } } while (0)

int main() try
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
    srIO::writeInteger(output.get(), Sint8(-128));
    srIO::writeInteger(output.get(), Sint16(-2));
    srIO::writeInteger(output.get(), Uint32(0x12345678));
    srIO::writeQuadWord(output.get(), {0x89abcdef, 0x01234567}, srIO::ByteOrder::big);
    srIO::writeFloat(output.get(), -0.0f);
    srIO::writeDouble(output.get(), std::numeric_limits<double>::infinity(), srIO::ByteOrder::big);
    srIO::writeInteger(output.get(), Uint16(0x3412), srIO::ByteOrder::big);
    srIO::writeInteger(output.get(), Uint32(0x9abc5678));
    CHECK(SDL_GetIOSize(output.get()) == Sint64(golden.size()));
    const auto* bytes = SDL_GetPointerProperty(SDL_GetIOProperties(output.get()),
                        SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    CHECK(memcmp(bytes, golden.data(), golden.size()) == 0);
    auto input = srIO::own(SDL_IOFromConstMem(golden.data(), golden.size()));
    CHECK(srIO::readInteger<Sint8>(input.get()) == -128);
    CHECK(srIO::readInteger<Sint16>(input.get()) == -2);
    CHECK(srIO::readInteger<Uint32>(input.get()) == 0x12345678);
    srQuadWord quad;
    srIO::readValue(input.get(), quad, srIO::ByteOrder::big);
    CHECK(quad.lo == 0x89abcdef && quad.hi == 0x01234567);
    CHECK(std::bit_cast<Uint32>(srIO::readFloat(input.get())) == 0x80000000);
    CHECK(srIO::readDouble(input.get(), srIO::ByteOrder::big) == std::numeric_limits<double>::infinity());
    CHECK(srIO::readInteger<Uint16>(input.get(), srIO::ByteOrder::big) == 0x3412);
    CHECK(srIO::readInteger<Uint32>(input.get()) == 0x9abc5678);
    bool rejected = false;
    try { srIO::readInteger<Uint32>(input.get()); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected && SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_EOF);
    CHECK(SDL_SeekIO(input.get(), 0, SDL_IO_SEEK_SET) == 0);
    CHECK(srIO::readInteger<Sint8>(input.get()) == -128);
    rejected = false;
    try { srIO::writeInteger(input.get(), Uint32(1)); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);

    // Quaternion order is w,x,y,z; matrix order is vectors then components.
    const Uint8 structures[] = {0,0,0x80,0x3f, 0,0,0,0x40, 0,0,0x40,0x40, 0,0,0x80,0x40};
    auto structure_input = srIO::own(SDL_IOFromConstMem(structures, sizeof(structures)));
    srQuaternion quaternion;
    srIO::readValue(structure_input.get(), quaternion);
    CHECK(quaternion.w == 1 && quaternion.v.x == 2 && quaternion.v.y == 3 && quaternion.v.z == 4);
    CHECK(SDL_SeekIO(structure_input.get(), 0, SDL_IO_SEEK_SET) == 0);
    srMatrix2T<float> matrix;
    srIO::readValue(structure_input.get(), matrix);
    CHECK(matrix.vectors[0].x == 1 && matrix.vectors[0].y == 2 &&
          matrix.vectors[1].x == 3 && matrix.vectors[1].y == 4);
    auto structure_output = srIO::own(SDL_IOFromDynamicMem());
    srIO::writeValue(structure_output.get(), quaternion);
    srIO::writeValue(structure_output.get(), matrix);
    CHECK(SDL_GetIOSize(structure_output.get()) == 2 * sizeof(structures));
    bytes = SDL_GetPointerProperty(SDL_GetIOProperties(structure_output.get()),
            SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    CHECK(memcmp(bytes, structures, sizeof(structures)) == 0);
    CHECK(memcmp(static_cast<const Uint8*>(bytes) + sizeof(structures), structures, sizeof(structures)) == 0);

    // A short final cache is valid; requesting a bit past it is not. Cache
    // lookahead must never reuse bytes from an earlier block.
    const Uint8 bits_data[] = {0x81, 0x42};
    auto bits_input = srIO::own(SDL_IOFromConstMem(bits_data, sizeof(bits_data)));
    srHuffman::BitIStream bits(bits_input.get());
    CHECK(bits.get(9) == 0x81 && bits.get(7) == 0x21);
    rejected = false;
    try { bits.getBit(); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    puts("ok: SDL serialized bytes, math structures, EOF and Huffman cache bounds");
    return 0;
}
catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
