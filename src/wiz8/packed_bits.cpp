#include "wiz8/packed_bits.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <queue>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

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

struct Node {
    std::uint32_t symbol = 0, frequency = 0;
    Node* children[2]{};
};

class BitReader {
public:
    explicit BitReader(std::span<const unsigned char> data) : data(data) {}
    std::uint32_t get(std::uint32_t bits);
    std::uint32_t peekByte() const;

private:
    std::span<const unsigned char> data;
    std::size_t byte_pos = 0;
    unsigned bit_offset = 0;
};

class BitWriter {
public:
    void put(std::uint32_t value, std::uint32_t bits);
    std::vector<unsigned char> bytes;

private:
    unsigned bit_offset = 0;
};

std::uint32_t BitReader::get(std::uint32_t bits)
{
    if (bits > 32) throw std::runtime_error("Invalid Huffman bit width");
    const auto needed = (bit_offset + bits + 7) / 8;
    if (needed > data.size() - byte_pos) throw std::out_of_range("Truncated Huffman bits");
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < bits;) {
        const auto count = std::min<unsigned>(bits - shift, 8 - bit_offset);
        value |= std::uint32_t((data[byte_pos] >> bit_offset) & ((1u << count) - 1)) << shift;
        shift += count;
        bit_offset += count;
        byte_pos += bit_offset / 8;
        bit_offset %= 8;
    }
    return value;
}

std::uint32_t BitReader::peekByte() const
{
    if (byte_pos == data.size()) return 0;
    std::uint32_t value = data[byte_pos] >> bit_offset;
    if (bit_offset && byte_pos + 1 < data.size())
        value |= std::uint32_t(data[byte_pos + 1]) << (8 - bit_offset);
    return value & 0xff;
}

void BitWriter::put(std::uint32_t value, std::uint32_t bits)
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

std::vector<unsigned char> EncodeWords(std::span<const std::uint32_t> words)
{
    struct Encoding {
        std::uint32_t frequency = 0, code = 0, bits = 0;
    };
    std::unordered_map<std::uint32_t, Encoding> encodings;
    for (auto word : words) ++encodings[word].frequency;

    // Allocate once: heap entries and child pointers borrow this storage.
    std::vector<Node> nodes;
    nodes.reserve(encodings.size() * 2 - 1);
    auto lessFrequent = [](const Node* a, const Node* b) { return a->frequency > b->frequency; };
    std::priority_queue<Node*, std::vector<Node*>, decltype(lessFrequent)> queue(lessFrequent);
    std::uint32_t code_width = 1;
    for (const auto& [symbol, encoding] : encodings) {
        nodes.push_back({symbol, encoding.frequency});
        queue.push(&nodes.back());
        code_width = std::max(code_width, std::uint32_t(std::bit_width(symbol)));
    }
    while (queue.size() > 1) {
        auto* left = queue.top();
        queue.pop();
        auto* right = queue.top();
        queue.pop();
        nodes.push_back({0, left->frequency + right->frequency, {left, right}});
        queue.push(&nodes.back());
    }

    BitWriter output;
    output.put(encodings.size(), 32);
    output.put(code_width, 6);
    output.put(words.size(), 32);
    // Assign codes during the same traversal that serializes the complete tree.
    auto storeTree = [&](auto&& self, Node* node, std::uint32_t code, std::uint32_t depth) -> void {
        const bool leaf = node->children[0] == nullptr;
        output.put(leaf, 1);
        if (leaf) {
            output.put(node->symbol, code_width);
            auto& encoding = encodings.at(node->symbol);
            encoding.code = code;
            encoding.bits = depth;
        } else {
            if (depth >= 32) throw std::runtime_error("Huffman code exceeds 32 bits");
            self(self, node->children[0], code, depth + 1);
            self(self, node->children[1], code | (std::uint32_t(1) << depth), depth + 1);
        }
    };
    storeTree(storeTree, queue.top(), 0, 0);
    for (auto word : words) {
        const auto& encoding = encodings.at(word);
        output.put(encoding.code, encoding.bits);
    }
    return std::move(output.bytes);
}

std::vector<bool> DecodeBits(std::span<const unsigned char> packed, std::size_t bit_count)
{
    BitReader input(packed);
    const auto num_symbols = input.get(32), code_width = input.get(6), count = input.get(32);
    if (count != WordCount(bit_count) || !num_symbols || !code_width || code_width > 32 ||
        num_symbols > count ||
        std::uint64_t(num_symbols) * (code_width + 2) - 1 > std::uint64_t(packed.size()) * 8 - 70)
        throw std::runtime_error("Invalid Huffman header");

    // The serialized preorder tree has exactly n leaves and n - 1 branches.
    std::vector<Node> nodes(std::size_t(num_symbols) * 2 - 1);
    Node* root = nodes.data();
    std::vector<Node**> pending{&root};
    for (auto& node : nodes) {
        if (pending.empty()) throw std::runtime_error("Invalid Huffman tree");
        *pending.back() = &node;
        pending.pop_back();
        if (input.get(1))
            node.symbol = input.get(code_width);
        else {
            pending.push_back(&node.children[1]);
            pending.push_back(&node.children[0]);
        }
    }
    if (!pending.empty()) throw std::runtime_error("Truncated Huffman tree");

    struct Lookup {
        Node* node;
        unsigned bits;
    };
    std::array<Lookup, 256> lookup;
    for (unsigned key = 0; key < lookup.size(); ++key) {
        auto* node = root;
        unsigned depth = 0;
        while (node->children[0] && depth < 8) node = node->children[(key >> depth++) & 1];
        lookup[key] = {node, depth};
    }
    std::vector<bool> decoded(bit_count);
    for (std::size_t word = 0; word < count; ++word) {
        const auto [start, bits] = lookup[input.peekByte()];
        input.get(bits);
        auto* node = start;
        while (node->children[0]) node = node->children[input.get(1)];
        for (std::size_t bit = 0; bit < kBitsPerWord && word * kBitsPerWord + bit < bit_count;
             ++bit)
            decoded[word * kBitsPerWord + bit] = (node->symbol >> bit) & 1;
    }
    return decoded;
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
    auto decoded = DecodeBits(packed, bit_count);
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
    std::vector<std::uint32_t> words(WordCount(bits.size()));
    for (std::size_t bit = 0; bit < bits.size(); ++bit) {
        words[bit / kBitsPerWord] |= static_cast<unsigned int>(bits[bit]) << (bit % kBitsPerWord);
    }

    srIO::check(SDL_WriteU32LE(handle.io(), kMagic));
    srIO::check(SDL_WriteU32LE(handle.io(), static_cast<Uint32>(bits.size())));

    const auto packed = EncodeWords(words);
    srIO::check(SDL_WriteU32LE(handle.io(), static_cast<Uint32>(packed.size())));
    srIO::writeExact(handle.io(), packed.data(), packed.size());
    srIO::check(SDL_WriteU32LE(handle.io(), kMagic));
    return true;
}
