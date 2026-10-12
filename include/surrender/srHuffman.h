#pragma once

#include <cstdint>
#include <vector>
#include "srIO.h"
#include <unordered_map>

class srHuffman {
public:
    class BitIStream {
    public:
        SR_DLL_IMPORT BitIStream(SDL_IOStream* stream);
        SR_DLL_IMPORT std::uint32_t get(std::uint32_t bits);
        // Padded lookahead does not consume bits; get validates consumption.
        std::uint32_t peekByte();
        SR_DLL_IMPORT std::uint32_t getBit();
        SR_DLL_IMPORT void rewind(std::int32_t bits);

    private:
        BitIStream(const BitIStream& stream);
        BitIStream& operator=(const BitIStream& stream);

        void fetchCache(Sint64 position);
        std::uint32_t getByte(Sint64 position);
        std::uint32_t getDWord(Sint64 position);
        std::uint32_t getWordOrLess(std::uint32_t bits);

        SDL_IOStream* stream;
        std::uint8_t cache[0x80]{};
        size_t cache_valid = 0;
        void requireBits(std::uint32_t bits) const;
        Sint64 cache_base;
        Sint64 bit_pos, bit_begin, bit_end;
    };

    class BitOStream {
    public:
        SR_DLL_IMPORT BitOStream(SDL_IOStream* stream);
        SR_DLL_IMPORT ~BitOStream();
        // Explicitly flush before inspecting output or returning success.
        void flush();
        SR_DLL_IMPORT void put(std::uint32_t value, std::uint32_t bits);

    private:
        BitOStream(const BitOStream& stream);
        BitOStream& operator=(const BitOStream& stream);

        void flushByte();
        void flushBuffer();
        void putBit(std::uint32_t bit);

        SDL_IOStream* stream;
        std::uint32_t bytes;
        std::uint32_t bit_count;
        std::uint32_t pending;
        std::uint8_t buffer[0x40];
        std::uint32_t buffered;
    };

    class Sampler {
    public:
        struct Symbol {
            std::uint32_t symbol;
            std::uint32_t frequency;
        };

        // FUNCTION: SURRENDER 0x10001840
        SR_DLL_IMPORT Sampler() = default;

        SR_DLL_IMPORT void insert(std::uint32_t symbol);
        SR_DLL_IMPORT std::uint32_t getNumSymbols() const;
        SR_DLL_IMPORT std::uint32_t getSymbolValue(std::uint32_t index) const;
        SR_DLL_IMPORT std::uint32_t getSymbolFrequency(std::uint32_t index) const;

    private:
        std::unordered_map<std::uint32_t, size_t> table;
        std::vector<Symbol> symbols;
    };

    class Compressor {
    public:
        struct Node {
            std::uint32_t symbol;
            std::uint32_t frequency;
            std::uint32_t code;
            std::uint32_t bits;
            Node* next;
            Node* children[2];
        };

        SR_DLL_IMPORT Compressor(const Sampler& sampler);

        SR_DLL_IMPORT void storeSymbolTable(BitOStream& stream);
        SR_DLL_IMPORT void buildSymbolTree();
        SR_DLL_IMPORT void collectSymbols(const Sampler& sampler);

        // FUNCTION: SURRENDER 0x10001430
        void compressSymbol(BitOStream& stream, std::uint32_t symbol)
        {
            const auto found = table.find(symbol);
            if (found != table.end()) {
                stream.put(found->second->code, found->second->bits);
            }
        }

        std::unordered_map<std::uint32_t, Node*> table;
        std::vector<Node> nodes;
        Node* free_list;
        Node* root;
        std::uint32_t num_symbols;
        std::uint32_t code_width;
        std::uint32_t total;

    private:
        Compressor(const Compressor& other);

        void dumpNode(BitOStream& stream, Node* node);

    public:
        void setupPath(Node* node, std::uint32_t code, std::uint32_t depth);
    };

    class Decompressor {
    public:
        SR_DLL_IMPORT Decompressor(BitIStream& stream);
        SR_DLL_IMPORT std::uint32_t decompressSymbol();
        SR_DLL_IMPORT std::uint32_t getDataCount() const;

    private:
        struct Symbol {
            std::uint32_t symbol;
            Symbol* children[2];
        };

        Decompressor(const Decompressor& other);
        Decompressor& operator=(const Decompressor& other);

        void setupSymbolTable(Symbol* node);

        BitIStream* stream;
        std::vector<Symbol> symbols;
        std::uint32_t next_node;
        std::uint32_t code_width;
        std::uint32_t unknown_10;
        std::uint32_t num_symbols;
        std::uint32_t data_count;
        Symbol* lookup[0x100];
        std::uint8_t depth[0x100];
    };
};

