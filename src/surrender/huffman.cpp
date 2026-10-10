#include "surrender/srHuffman.h"

#include <algorithm>
#include <string.h>

static void sortSymbolPairs(srHuffman::Sampler::Symbol* pairs, w8_ulong count);

// FUNCTION: SURRENDER 0x100013D0
void srHuffman::BitIStream::fetchCache(w8_long position)
{
    cache_base = position;
    stream->seek(position);
    stream->read(cache, 0x80);
    stream->setState(srBinStream::SR_STREAM_OK);
}

// FUNCTION: SURRENDER 0x100015C0
w8_ulong srHuffman::BitIStream::getByte(w8_long position)
{
    w8_ulong offset = position - cache_base;
    if (offset >= 0x80) {
        fetchCache(position);
        offset = 0;
    }
    return cache[offset];
}

// FUNCTION: SURRENDER 0x10001490
w8_ulong srHuffman::BitIStream::getDWord(w8_long position)
{
    w8_ulong offset = position - cache_base;
    if (offset >= 0x7d) {
        fetchCache(position);
        offset = 0;
    }
    w8_ulong value;
    memcpy(&value, cache + offset, sizeof(value));
    return value;
}

// FUNCTION: SURRENDER 0x10001340
w8_ulong srHuffman::BitIStream::getWordOrLess(w8_ulong bits)
{
    w8_long position = bit_pos / 8;
    w8_ulong shift = bit_pos & 7;
    w8_ulong data = getDWord(position);
    bit_pos += bits;
    return (data & ((1 << (shift + bits)) - 1)) >> shift;
}

// FUNCTION: SURRENDER 0x100010A0
srHuffman::BitIStream::BitIStream(srBinIStream& stream)
{
    this->stream = &stream;
    cache_base = -0x100;
    bit_pos = this->stream->tell() << 3;
}

// FUNCTION: SURRENDER 0x100010E0
w8_ulong srHuffman::BitIStream::get(w8_ulong bits)
{
    if (bits <= 0x10) {
        return getWordOrLess(bits);
    }
    w8_ulong low = getWordOrLess(0x10);
    w8_ulong high = getWordOrLess(bits - 0x10);
    return (high << 0x10) | low;
}

// FUNCTION: SURRENDER 0x10001290
w8_ulong srHuffman::BitIStream::getBit()
{
    w8_ulong data = getByte(bit_pos / 8);
    w8_ulong bit = (data >> (bit_pos & 7)) & 1;
    ++bit_pos;
    return bit;
}

// FUNCTION: SURRENDER 0x10001320
void srHuffman::BitIStream::rewind(w8_long bits)
{
    bit_pos -= bits;
}

// FUNCTION: SURRENDER 0x10001420
srHuffman::BitOStream& srHuffman::BitOStream::operator=(const BitOStream& stream)
{
    return *this;
}

// FUNCTION: SURRENDER 0x100016D0
srHuffman::BitOStream::BitOStream(srBinOStream& stream)
{
    this->stream = &stream;
    pending = 0;
    bit_count = 0;
    bytes = 0;
    buffered = 0;
}

// FUNCTION: SURRENDER 0x10001730
srHuffman::BitOStream::~BitOStream()
{
    flush();
}

// FUNCTION: SURRENDER 0x10001760
void srHuffman::BitOStream::put(w8_ulong value, w8_ulong bits)
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
        buffer[buffered] = (unsigned char)pending;
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
        stream->write(buffer, buffered);
    }
    buffered = 0;
}

// FUNCTION: SURRENDER 0x10001810
void srHuffman::BitOStream::putBit(w8_ulong bit)
{
    pending |= (bit & 1) << bit_count;
    ++bit_count;
    if (bit_count == 8) {
        flushByte();
    }
}

// FUNCTION: SURRENDER 0x10001990
void srHuffman::Sampler::insert(w8_ulong symbol)
{
    if (table.FindNextEntry(&symbol, -1) == -1) {
        const auto count = static_cast<int>(symbols.size());
        symbols.push_back({symbol, 1});
        table.Insert(&symbol, &count);
    } else {
        ++symbols[table.Lookup(&symbol)].frequency;
    }
}

// FUNCTION: SURRENDER 0x10001BA0
w8_ulong srHuffman::Sampler::getNumSymbols() const
{
    return static_cast<w8_ulong>(symbols.size());
}

// FUNCTION: SURRENDER 0x10001BB0
w8_ulong srHuffman::Sampler::getSymbolValue(w8_ulong index) const
{
    return symbols[index].symbol;
}

// FUNCTION: SURRENDER 0x10001BC0
w8_ulong srHuffman::Sampler::getSymbolFrequency(w8_ulong index) const
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
        for (w8_ulong index = 0; index < num_symbols * 2; ++index) {
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
void srHuffman::Compressor::setupPath(Node* node, w8_ulong code, w8_ulong depth)
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
        table.Insert(&node->symbol, &node);
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
        for (w8_ulong merged = 0; merged < num_symbols - 1; ++merged) {
            Node* node = free_list;
            free_list = node->next;
            node->next = 0;
            Node** slot = node->children;
            for (int remaining = 2; remaining != 0; --remaining) {
                int which = 0;
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
    w8_ulong max_symbol = 0;
    w8_ulong index;
    for (index = 0; index < num_symbols; ++index) {
        pairs[index].symbol = sampler.getSymbolValue(index);
        pairs[index].frequency = sampler.getSymbolFrequency(index);
        if (max_symbol < pairs[index].symbol) {
            max_symbol = pairs[index].symbol;
        }
    }
    int width;
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
    w8_ulong key = stream->get(8);
    stream->rewind(8 - depth[key]);
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
static void sortSymbolPairs(srHuffman::Sampler::Symbol* pairs, w8_ulong count)
{
    if (count <= 1) {
        return;
    }
    // Stable frequency order preserves the radix sort's tie order and therefore the bitstream.
    std::stable_sort(pairs, pairs + count, [](const auto& first, const auto& second) {
        return first.frequency < second.frequency;
    });
}
