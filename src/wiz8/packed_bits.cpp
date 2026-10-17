#include "wiz8/packed_bits.h"

#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

#include "surrender/srHuffman.h"
#include "surrender/srIO.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"

namespace {
constexpr Uint32 kMagic = 0xdeadd00d;
constexpr std::size_t kBitsPerWord = 32;

std::size_t WordCount(std::size_t bit_count)
{
    return bit_count / kBitsPerWord + 1;
}
} // namespace

template<class Read> static bool ReadPackedBits(Read read, std::int64_t remaining,
                                                std::vector<bool>& bits)
try {
    Uint32 magic, bit_count, packed_size;
    read(&magic, sizeof(magic));
    if (magic != kMagic) return false;
    read(&bit_count, sizeof(bit_count));
    if (!bit_count) return false;
    read(&packed_size, sizeof(packed_size));
    if (remaining < 16 || Uint64(packed_size) + 16 > Uint64(remaining)) return false;
    std::vector<unsigned char> packed(packed_size);
    read(packed.data(), packed.size());
    auto stream = srIO::own(SDL_IOFromConstMem(packed.data(), packed.size()));
    srHuffman::BitIStream input(stream.get());
    srHuffman::Decompressor decoder(input);
    if (decoder.getDataCount() != WordCount(bit_count)) return false;
    std::vector<bool> decoded(bit_count);
    for (std::size_t word = 0; word < WordCount(bit_count); ++word) {
        const unsigned int value = decoder.decompressSymbol();
        for (std::size_t bit = 0; bit < kBitsPerWord; ++bit) {
            if ((value >> bit) & 1) {
                SetBit(decoded, word * kBitsPerWord + bit);
            }
        }
    }
    read(&magic, sizeof(magic));
    if (magic != kMagic) return false;
    bits = std::move(decoded);
    return true;
} catch (const std::exception&) {
    return false;
}

bool LoadPackedBits(wiz8::File& file, std::vector<bool>& bits)
{
    return ReadPackedBits([&](void* data, std::size_t size) { file.read_exact(data, size); },
                          file.size() - file.tell(), bits);
}

bool LoadPackedBits(W8Chunk& chunk, std::vector<bool>& bits)
{
    return ReadPackedBits([&](void* data, std::size_t size) { chunk.ReadBytes(data, size); },
                          chunk.RemainingBytes(), bits);
}

bool SavePackedBits(wiz8::File& handle, const std::vector<bool>& bits)
{
    std::vector<unsigned int> words(WordCount(bits.size()));
    for (std::size_t bit = 0; bit < bits.size(); ++bit) {
        words[bit / kBitsPerWord] |= static_cast<unsigned int>(bits[bit]) << (bit % kBitsPerWord);
    }

    srIO::check(SDL_WriteU32LE(handle.io(), kMagic));
    srIO::check(SDL_WriteU32LE(handle.io(), static_cast<Uint32>(bits.size())));

    auto stream = srIO::own(SDL_IOFromDynamicMem());
    {
        srHuffman::BitOStream output(stream.get());
        srHuffman::Sampler sampler;
        for (unsigned int word : words) {
            sampler.insert(word);
        }
        srHuffman::Compressor compressor(sampler);
        output.put(compressor.num_symbols, 32);
        output.put(compressor.code_width, 6);
        output.put(static_cast<unsigned int>(words.size()), 32);
        compressor.storeSymbolTable(output);
        for (unsigned int word : words) {
            compressor.compressSymbol(output, word);
        }
        output.flush();
    }

    const auto packed_size = static_cast<Uint32>(SDL_GetIOSize(stream.get()));
    srIO::check(SDL_WriteU32LE(handle.io(), packed_size));
    srIO::writeExact(handle.io(),
                     SDL_GetPointerProperty(SDL_GetIOProperties(stream.get()),
                                            SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
                     packed_size);
    srIO::check(SDL_WriteU32LE(handle.io(), kMagic));
    return true;
}
