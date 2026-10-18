#pragma once

#include <span>
#include <vector>

#include "compat/compiler.h"

class srHuffman {
public:
    class BitIStream {
    public:
        explicit BitIStream(std::span<const unsigned char> data) : data(data) {}
        w8_ulong get(w8_ulong bits);
        // Padded lookahead does not consume bits; get validates consumption.
        w8_ulong peekByte() const;
        w8_ulong getBit() { return get(1); }

    private:
        std::span<const unsigned char> data;
        size_t byte_pos = 0;
        unsigned bit_offset = 0;
    };

    class BitOStream {
    public:
        void put(w8_ulong value, w8_ulong bits);
        std::vector<unsigned char> bytes;

    private:
        unsigned bit_offset = 0;
    };

    static std::vector<unsigned char> compress(std::span<const w8_ulong> words);

    class Decompressor {
    public:
        Decompressor(BitIStream& stream);
        w8_ulong decompressSymbol();
        w8_ulong getDataCount() const;

    private:
        struct Symbol {
            w8_ulong symbol;
            Symbol* children[2];
        };

        Decompressor(const Decompressor& other);
        Decompressor& operator=(const Decompressor& other);

        void setupSymbolTable(Symbol* node);

        BitIStream* stream;
        std::vector<Symbol> symbols;
        w8_ulong next_node;
        w8_ulong code_width;
        w8_ulong num_symbols;
        w8_ulong data_count;
        Symbol* lookup[0x100];
        unsigned char depth[0x100];
    };
};
