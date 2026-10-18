#include <algorithm>
#include <bit>
#include <queue>
#include <stdexcept>

#include "surrender/srHuffman.h"

w8_ulong srHuffman::BitIStream::get(w8_ulong bits)
{
    if (bits > 32) throw std::runtime_error("Invalid Huffman bit width");
    const auto needed = (bit_offset + bits + 7) / 8;
    if (needed > data.size() - byte_pos) throw std::out_of_range("Truncated Huffman bits");
    w8_ulong value = 0;
    for (unsigned shift = 0; shift < bits;) {
        const auto count = std::min<unsigned>(bits - shift, 8 - bit_offset);
        value |= w8_ulong((data[byte_pos] >> bit_offset) & ((1u << count) - 1)) << shift;
        shift += count;
        bit_offset += count;
        byte_pos += bit_offset / 8;
        bit_offset %= 8;
    }
    return value;
}

w8_ulong srHuffman::BitIStream::peekByte() const
{
    if (byte_pos == data.size()) return 0;
    w8_ulong value = data[byte_pos] >> bit_offset;
    if (bit_offset && byte_pos + 1 < data.size())
        value |= w8_ulong(data[byte_pos + 1]) << (8 - bit_offset);
    return value & 0xff;
}

void srHuffman::BitOStream::put(w8_ulong value, w8_ulong bits)
{
    if (bits > 32) throw std::runtime_error("Invalid Huffman bit width");
    for (unsigned shift = 0; shift < bits;) {
        if (bit_offset == 0) bytes.push_back(0);
        const auto count = std::min<unsigned>(bits - shift, 8 - bit_offset);
        bytes.back() |= ((value >> shift) & ((1u << count) - 1)) << bit_offset;
        shift += count;
        bit_offset = (bit_offset + count) % 8;
    }
}

srHuffman::Compressor::Compressor(const Sampler& sampler)
    : root(nullptr), num_symbols(sampler.size()), code_width(1)
{
    // Allocate once: the heap and code table borrow pointers into this storage.
    nodes.reserve(sampler.empty() ? 0 : sampler.size() * 2 - 1);
    auto lessFrequent = [](const Node* a, const Node* b) { return a->frequency > b->frequency; };
    std::priority_queue<Node*, std::vector<Node*>, decltype(lessFrequent)> queue(lessFrequent);
    for (const auto& [symbol, frequency] : sampler) {
        nodes.push_back({symbol, frequency, 0, 0, {nullptr, nullptr}});
        queue.push(&nodes.back());
        code_width = std::max(code_width, w8_ulong(std::bit_width(symbol)));
    }
    while (queue.size() > 1) {
        auto* left = queue.top();
        queue.pop();
        auto* right = queue.top();
        queue.pop();
        nodes.push_back({0, left->frequency + right->frequency, 0, 0, {left, right}});
        queue.push(&nodes.back());
    }
    if (!queue.empty()) root = queue.top();
    setupPath(root, 0, 0);
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
void srHuffman::Compressor::setupPath(Node* node, w8_ulong code, w8_ulong depth)
{
    if (node == nullptr) return;
    if (node->children[0] != nullptr) {
        if (depth >= 32) throw std::runtime_error("Huffman code exceeds 32 bits");
        setupPath(node->children[0], code, depth + 1);
        setupPath(node->children[1], code | (w8_ulong(1) << depth), depth + 1);
        return;
    }
    node->code = code;
    node->bits = depth;
    table.emplace(node->symbol, node);
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
        for (w8_ulong index = 0; index < 0x100; ++index) {
            Symbol* node = symbols.data();
            w8_ulong depth;
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
            this->depth[index] = (unsigned char)depth;
        }
    }
}

// FUNCTION: SURRENDER 0x10002500
w8_ulong srHuffman::Decompressor::getDataCount() const
{
    return data_count;
}

// FUNCTION: SURRENDER 0x10002510
w8_ulong srHuffman::Decompressor::decompressSymbol()
{
    BitIStream* stream = this->stream;
    w8_ulong key = stream->peekByte();
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
