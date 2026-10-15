#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "compat/compiler.h"
#include "surrender/srHuffman.h"
#include "surrender/srIO.h"

TEST_CASE("binary_io_test")
{
    bool rejected = false;
    // A short final cache is valid; requesting a bit past it is not. Cache
    // lookahead must never reuse bytes from an earlier block.
    const Uint8 bits_data[] = {0x81, 0x42};
    auto bits_input = srIO::own(SDL_IOFromConstMem(bits_data, sizeof(bits_data)));
    srHuffman::BitIStream bits(bits_input.get());
    REQUIRE((bits.get(9) == 0x81 && bits.get(7) == 0x21));
    rejected = false;
    try {
        bits.getBit();
    } catch (const std::exception&) {
        rejected = true;
    }
    REQUIRE(rejected);
    REQUIRE(bits.peekByte() == 0); // Padding is for lookahead, not consumption.
    for (const auto rewind : {w8_long(-1), std::numeric_limits<w8_long>::min(), w8_long(17),
                              std::numeric_limits<w8_long>::max()}) {
        rejected = false;
        try {
            bits.rewind(rewind);
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        REQUIRE((rejected && bits.peekByte() == 0));
    }
    bits.rewind(16);
    rejected = false;
    try {
        bits.rewind(1);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    REQUIRE((rejected && bits.get(8) == 0x81));
    rejected = false;
    try {
        bits.get(32);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    REQUIRE((rejected && bits.get(8) == 0x42)); // A rejected read does not advance.
    REQUIRE(SDL_SeekIO(bits_input.get(), 1, SDL_IO_SEEK_SET) == 1);
    srHuffman::BitIStream offset_bits(bits_input.get());
    rejected = false;
    try {
        offset_bits.rewind(1);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    REQUIRE((rejected && offset_bits.get(8) == 0x42));

    // Exercise a cache refill and rewind across its boundary.
    std::array<Uint8, 130> long_bits{};
    long_bits[127] = 0x81;
    long_bits[128] = 0x42;
    long_bits[129] = 0xff;
    auto long_input = srIO::own(SDL_IOFromConstMem(long_bits.data(), long_bits.size()));
    srHuffman::BitIStream long_reader(long_input.get());
    for (unsigned i = 0; i < 127; ++i) REQUIRE(long_reader.get(8) == 0);
    REQUIRE(long_reader.get(24) == 0xff4281);
    long_reader.rewind(24);
    REQUIRE((long_reader.get(24) == 0xff4281 && long_reader.peekByte() == 0));
    // A sparse SDL backend exercises positions beyond the former 32-bit bit
    // cursor without allocating a gigabyte-sized fixture.
    struct SparseInput {
        Sint64 position, size;
    } sparse{Sint64(1) << 30, (Sint64(1) << 30) + 1};
    SDL_IOStreamInterface interface;
    SDL_INIT_INTERFACE(&interface);
    interface.size = [](void* cookie) -> Sint64 { return static_cast<SparseInput*>(cookie)->size; };
    interface.seek = [](void* cookie, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
        auto& self = *static_cast<SparseInput*>(cookie);
        const auto base = whence == SDL_IO_SEEK_SET   ? 0
                          : whence == SDL_IO_SEEK_CUR ? self.position
                                                      : self.size;
        if (offset < -base || offset > self.size - base) return -1;
        return self.position = base + offset;
    };
    interface.read = [](void* cookie, void* data, size_t requested,
                        SDL_IOStatus* status) -> size_t {
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
    try {
        srHuffman::BitIStream invalid(sparse_io.get());
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    REQUIRE(rejected);
    puts("ok: Huffman cursor/cache bounds");
}
