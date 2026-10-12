#include <cstdint>
#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <limits>
#include <utility>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "surrender/srHuffman.h"
#include "surrender/srIO.h"

static void sortSymbolPairs(srHuffman::Sampler::Symbol* pairs, std::uint32_t count);

// FUNCTION: SURRENDER 0x100013D0
void srHuffman::BitIStream::fetchCache(Sint64 position)
{
    if (position < 0 || position > bit_end / 8)
        throw std::out_of_range("Huffman cursor outside input");
    cache_base = position;
    if (SDL_SeekIO(stream, position, SDL_IO_SEEK_SET) != position)
        throw std::runtime_error(SDL_GetError());
    std::fill(std::begin(cache), std::end(cache), 0);
    cache_valid = SDL_ReadIO(stream, cache, sizeof(cache));
    if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_READY &&
        SDL_GetIOStatus(stream) != SDL_IO_STATUS_EOF)
        throw std::runtime_error(SDL_GetError());
}

// FUNCTION: SURRENDER 0x100015C0
std::uint32_t srHuffman::BitIStream::getByte(Sint64 position)
{
    const auto distance = position - cache_base;
    size_t offset = distance < 0 ? sizeof(cache) : size_t(distance);
    if (offset >= 0x80) {
        fetchCache(position);
        offset = 0;
    }
    if (offset >= cache_valid) throw std::runtime_error("Truncated Huffman bits");
    return cache[offset];
}

// FUNCTION: SURRENDER 0x10001490
std::uint32_t srHuffman::BitIStream::getDWord(Sint64 position)
{
    const auto distance = position - cache_base;
    size_t offset = distance < 0 ? sizeof(cache) : size_t(distance);
    if (offset >= 0x7d) {
        fetchCache(position);
        offset = 0;
    }
    Uint32 value;
    memcpy(&value, cache + offset, sizeof(value));
    return SDL_Swap32LE(value);
}

// FUNCTION: SURRENDER 0x10001340
std::uint32_t srHuffman::BitIStream::getWordOrLess(std::uint32_t bits)
{
    Sint64 position = bit_pos / 8;
    std::uint32_t shift = bit_pos & 7;
    std::uint32_t data = getDWord(position);
    if (size_t(position - cache_base) + (shift + bits + 7) / 8 > cache_valid)
        throw std::runtime_error("Truncated Huffman bits");
    bit_pos += bits;
    return (data & ((1 << (shift + bits)) - 1)) >> shift;
}

// FUNCTION: SURRENDER 0x100010A0
srHuffman::BitIStream::BitIStream(SDL_IOStream* stream)
{
    this->stream = stream;
    cache_base = -0x100;
    const auto position = SDL_TellIO(stream), size = SDL_GetIOSize(stream);
    if (position < 0 || size < position || size > std::numeric_limits<Sint64>::max() / 8)
        throw std::out_of_range("Invalid Huffman cursor or length");
    bit_begin = bit_pos = position * 8;
    bit_end = size * 8;

}

void srHuffman::BitIStream::requireBits(std::uint32_t bits) const
{
    if (bits > bit_end - bit_pos)
        throw std::out_of_range("Truncated Huffman bits");
}

// FUNCTION: SURRENDER 0x100010E0
std::uint32_t srHuffman::BitIStream::get(std::uint32_t bits)
{
    if (bits == 0) return 0;
    if (bits > 32) throw std::runtime_error("Invalid Huffman bit width");
    requireBits(bits);
    if (bits <= 0x10) {
        return getWordOrLess(bits);
    }
    std::uint32_t low = getWordOrLess(0x10);
    std::uint32_t high = getWordOrLess(bits - 0x10);
    return (high << 0x10) | low;
}

std::uint32_t srHuffman::BitIStream::peekByte()
{
    return (getDWord(bit_pos / 8) >> (bit_pos & 7)) & 0xff;
}

// FUNCTION: SURRENDER 0x10001290
std::uint32_t srHuffman::BitIStream::getBit()
{
    requireBits(1);
    std::uint32_t data = getByte(bit_pos / 8);
    std::uint32_t bit = (data >> (bit_pos & 7)) & 1;
    ++bit_pos;
    return bit;
}

// FUNCTION: SURRENDER 0x10001320
void srHuffman::BitIStream::rewind(std::int32_t bits)
{
    if (bits < 0 || bits > bit_pos - bit_begin)
        throw std::out_of_range("Rewind outside Huffman input");
    bit_pos -= bits;
}

// FUNCTION: SURRENDER 0x10001420
srHuffman::BitOStream& srHuffman::BitOStream::operator=(const BitOStream& stream)
{
    return *this;
}

// FUNCTION: SURRENDER 0x100016D0
srHuffman::BitOStream::BitOStream(SDL_IOStream* stream)
{
    this->stream = stream;
    pending = 0;
    bit_count = 0;
    bytes = 0;
    buffered = 0;
}

// FUNCTION: SURRENDER 0x10001730
srHuffman::BitOStream::~BitOStream()
{
    try { flush(); } catch (...) { /* Explicit flush reports failures to the caller. */ }
}

// FUNCTION: SURRENDER 0x10001760
void srHuffman::BitOStream::put(std::uint32_t value, std::uint32_t bits)
{
    for (; bits > 0; --bits) {
        putBit(value & 1);
        value >>= 1;
    }
}

// FUNCTION: SURRENDER 0x10001790
void srHuffman::BitOStream::flush()
{
    flushByte();
    flushBuffer();
}

// FUNCTION: SURRENDER 0x100017B0
void srHuffman::BitOStream::flushByte()
{
    if (bit_count != 0) {
        buffer[buffered] = (std::uint8_t)pending;
        ++bytes;
        ++buffered;
        pending = 0;
        bit_count = 0;
        if (buffered == 0x40) {
            flushBuffer();
        }
    }
}

// FUNCTION: SURRENDER 0x100017F0
void srHuffman::BitOStream::flushBuffer()
{
    if (buffered != 0) {
        const auto count = std::exchange(buffered, 0);
        srIO::writeExact(stream, buffer, count);
    }
}

// FUNCTION: SURRENDER 0x10001810
void srHuffman::BitOStream::putBit(std::uint32_t bit)
{
    pending |= (bit & 1) << bit_count;
    ++bit_count;
    if (bit_count == 8) {
        flushByte();
    }
}

// FUNCTION: SURRENDER 0x10001990
void srHuffman::Sampler::insert(std::uint32_t symbol)
{
    const auto found = table.find(symbol);
    if (found != table.end()) {
        ++symbols[found->second].frequency;
        return;
    }
    // The vector, not hash iteration, defines the format's first-seen order.
    const auto index = symbols.size();
    symbols.push_back({symbol, 1});
    try {
        table.emplace(symbol, index);
    } catch (...) {
        symbols.pop_back();
        throw;
    }
}

// FUNCTION: SURRENDER 0x10001BA0
std::uint32_t srHuffman::Sampler::getNumSymbols() const
{
    return static_cast<std::uint32_t>(symbols.size());
}

// FUNCTION: SURRENDER 0x10001BB0
std::uint32_t srHuffman::Sampler::getSymbolValue(std::uint32_t index) const
{
    return symbols[index].symbol;
}

// FUNCTION: SURRENDER 0x10001BC0
std::uint32_t srHuffman::Sampler::getSymbolFrequency(std::uint32_t index) const
{
    return symbols[index].frequency;
}

// FUNCTION: SURRENDER 0x10001BD0
srHuffman::Compressor::Compressor(const Sampler& sampler)
{
    num_symbols = sampler.getNumSymbols();
    code_width = 0;
    total = 0;
    root = 0;
    free_list = 0;
    if (num_symbols != 0) {
        nodes.resize(num_symbols * 2);
        free_list = nodes.data();
        for (std::uint32_t index = 0; index < num_symbols * 2; ++index) {
            nodes[index].symbol = 0;
            nodes[index].frequency = 0;
            nodes[index].next = nodes.data() + index + 1;
            nodes[index].children[0] = 0;
            nodes[index].children[1] = 0;
            nodes[index].code = 0;
            nodes[index].bits = 0;
        }
        nodes[num_symbols - 1].next = 0;
        free_list = nodes.data() + num_symbols;
        nodes[num_symbols * 2 - 1].next = 0;
        collectSymbols(sampler);
        buildSymbolTree();
        total = 0;
        setupPath(root, 0, 0);
    }
}

// FUNCTION: SURRENDER 0x10001E40
void srHuffman::Compressor::storeSymbolTable(BitOStream& stream)
{
    dumpNode(stream, root);
}

// FUNCTION: SURRENDER 0x10001E60
void srHuffman::Compressor::dumpNode(BitOStream& stream, Node* node)
{
    while (node != 0 && node->children[0] != 0) {
        stream.put(0, 1);
        dumpNode(stream, node->children[0]);
        node = node->children[1];
    }
    if (node != 0) {
        stream.put(1, 1);
        stream.put(node->symbol, code_width);
    }
}

// FUNCTION: SURRENDER 0x10001EC0
void srHuffman::Compressor::setupPath(Node* node, std::uint32_t code, std::uint32_t depth)
{
    if (node != 0) {
        while (node->children[0] != 0) {
            setupPath(node->children[0], code, depth + 1);
            node = node->children[1];
            code |= 1 << depth;
            ++depth;
            if (node == 0) {
                return;
            }
        }
        node->code = code;
        node->bits = depth;
        table.insert_or_assign(node->symbol, node);
        total += node->frequency * node->bits;
    }
}

// FUNCTION: SURRENDER 0x10001F90
void srHuffman::Compressor::buildSymbolTree()
{
    if (num_symbols != 1) {
        Node* heads[2];
        Node* tails[2];
        Node* root = 0;
        heads[0] = nodes.data();
        heads[1] = 0;
        tails[1] = 0;
        for (std::uint32_t merged = 0; merged < num_symbols - 1; ++merged) {
            Node* node = free_list;
            free_list = node->next;
            node->next = 0;
            Node** slot = node->children;
            for (std::int32_t remaining = 2; remaining != 0; --remaining) {
                std::int32_t which = 0;
                if (heads[1] != 0 &&
                    (heads[0] == 0 || heads[1]->frequency <= heads[0]->frequency)) {
                    which = 1;
                }
                Node* picked = heads[which];
                *slot = picked;
                heads[which] = picked->next;
                if (heads[which] == 0) {
                    tails[which] = 0;
                }
                picked = *slot;
                ++slot;
                picked->next = 0;
            }
            node->frequency = node->children[1]->frequency + node->children[0]->frequency;
            if (tails[1] == 0) {
                heads[1] = node;
            } else {
                tails[1]->next = node;
            }
            root = heads[1];
            tails[1] = node;
        }
        this->root = root;
    } else {
        this->root = nodes.data();
    }
}

// FUNCTION: SURRENDER 0x10002080
void srHuffman::Compressor::collectSymbols(const Sampler& sampler)
{
    std::vector<Sampler::Symbol> pairs(num_symbols);
    std::uint32_t max_symbol = 0;
    std::uint32_t index;
    for (index = 0; index < num_symbols; ++index) {
        pairs[index].symbol = sampler.getSymbolValue(index);
        pairs[index].frequency = sampler.getSymbolFrequency(index);
        if (max_symbol < pairs[index].symbol) {
            max_symbol = pairs[index].symbol;
        }
    }
    std::int32_t width;
    if (max_symbol == 0) {
        width = -1;
    } else {
        width = 0;
        if ((max_symbol & 0xffff0000) != 0) {
            width = 0x10;
            max_symbol >>= 0x10;
        }
        if ((max_symbol & 0xff00) != 0) {
            width += 8;
            max_symbol >>= 8;
        }
        if ((max_symbol & 0xf0) != 0) {
            width += 4;
            max_symbol >>= 4;
        }
        if ((max_symbol & 0x0c) != 0) {
            width += 2;
            max_symbol >>= 2;
        }
        if ((max_symbol & 0x02) != 0) {
            width += 1;
        }
    }
    code_width = width + 1;
    if (code_width < 1) {
        code_width = 1;
    }
    if (num_symbols > 1) {
        sortSymbolPairs(pairs.data(), num_symbols);
    }
    for (index = 0; index < num_symbols; ++index) {
        nodes[index].symbol = pairs[index].symbol;
        nodes[index].frequency = pairs[index].frequency;
    }
}

// FUNCTION: SURRENDER 0x10002340
srHuffman::Decompressor::Decompressor(BitIStream& stream)
{
    next_node = 0;
    this->stream = &stream;
    num_symbols = this->stream->get(0x20);
    code_width = this->stream->get(6);
    data_count = this->stream->get(0x20);
    if (num_symbols != 0) {
        symbols.resize(num_symbols * 2);
        setupSymbolTable(symbols.data());
        for (std::uint32_t index = 0; index < 0x100; ++index) {
            Symbol* node = symbols.data();
            std::uint32_t depth;
            for (depth = 0; depth < 8; ++depth) {
                Symbol* next = node->children[0];
                if (next == 0) {
                    break;
                }
                if ((index & (1 << depth)) != 0) {
                    next = node->children[1];
                }
                node = next;
            }
            lookup[index] = node;
            this->depth[index] = (std::uint8_t)depth;
        }
    }
}

// FUNCTION: SURRENDER 0x10002500
std::uint32_t srHuffman::Decompressor::getDataCount() const
{
    return data_count;
}

// FUNCTION: SURRENDER 0x10002510
std::uint32_t srHuffman::Decompressor::decompressSymbol()
{
    BitIStream* stream = this->stream;
    std::uint32_t key = stream->peekByte();
    stream->get(depth[key]);
    Symbol* node = lookup[key];
    while (node->children[0] != 0) {
        node = node->children[stream->getBit()];
    }
    return node->symbol;
}

// FUNCTION: SURRENDER 0x10002630
void srHuffman::Decompressor::setupSymbolTable(Symbol* node)
{
    while (true) {
        ++next_node;
        if (stream->get(1) != 0) {
            break;
        }
        node->symbol = 0xffffffff;
        node->children[0] = &symbols[next_node];
        setupSymbolTable(node->children[0]);
        node->children[1] = &symbols[next_node];
        node = node->children[1];
    }
    node->symbol = stream->get(code_width);
    node->children[0] = 0;
    node->children[1] = 0;
}

// FUNCTION: SURRENDER 0x100027F0
static void sortSymbolPairs(srHuffman::Sampler::Symbol* pairs, std::uint32_t count)
{
    if (count <= 1) {
        return;
    }
    // Stable frequency order preserves the radix sort's tie order and therefore the bitstream.
    std::stable_sort(pairs, pairs + count, [](const auto& first, const auto& second) {
        return first.frequency < second.frequency;
    });
}
