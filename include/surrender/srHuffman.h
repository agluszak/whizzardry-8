#pragma once

#include <unordered_map>
#include <vector>

#include "srIO.h"

class srHuffman {
public:
    class BitIStream {
    public:
        SR_DLL_IMPORT BitIStream(SDL_IOStream* stream);
        SR_DLL_IMPORT w8_ulong get(w8_ulong bits);
        // Padded lookahead does not consume bits; get validates consumption.
        w8_ulong peekByte();
        SR_DLL_IMPORT w8_ulong getBit();
        SR_DLL_IMPORT void rewind(w8_long bits);

    private:
        BitIStream(const BitIStream& stream);
        BitIStream& operator=(const BitIStream& stream);

        void fetchCache(Sint64 position);
        w8_ulong getByte(Sint64 position);
        w8_ulong getDWord(Sint64 position);
        w8_ulong getWordOrLess(w8_ulong bits);

        SDL_IOStream* stream;
        unsigned char cache[0x80]{};
        size_t cache_valid = 0;
        void requireBits(w8_ulong bits) const;
        Sint64 cache_base;
        Sint64 bit_pos, bit_begin, bit_end;
    };

    class BitOStream {
    public:
        SR_DLL_IMPORT BitOStream(SDL_IOStream* stream);
        SR_DLL_IMPORT ~BitOStream();
        // Explicitly flush before inspecting output or returning success.
        void flush();
        SR_DLL_IMPORT void put(w8_ulong value, w8_ulong bits);

    private:
        BitOStream(const BitOStream& stream);
        BitOStream& operator=(const BitOStream& stream);

        void flushByte();
        void flushBuffer();
        void putBit(w8_ulong bit);

        SDL_IOStream* stream;
        w8_ulong bytes;
        w8_ulong bit_count;
        w8_ulong pending;
        unsigned char buffer[0x40];
        w8_ulong buffered;
    };

    class Sampler {
    public:
        struct Symbol {
            w8_ulong symbol;
            w8_ulong frequency;
        };

        // FUNCTION: SURRENDER 0x10001840
        SR_DLL_IMPORT Sampler() = default;

        SR_DLL_IMPORT void insert(w8_ulong symbol);
        SR_DLL_IMPORT w8_ulong getNumSymbols() const;
        SR_DLL_IMPORT w8_ulong getSymbolValue(w8_ulong index) const;
        SR_DLL_IMPORT w8_ulong getSymbolFrequency(w8_ulong index) const;

    private:
        std::unordered_map<w8_ulong, size_t> table;
        std::vector<Symbol> symbols;
    };

    class Compressor {
    public:
        struct Node {
            w8_ulong symbol;
            w8_ulong frequency;
            w8_ulong code;
            w8_ulong bits;
            Node* next;
            Node* children[2];
        };

        SR_DLL_IMPORT Compressor(const Sampler& sampler);

        SR_DLL_IMPORT void storeSymbolTable(BitOStream& stream);
        SR_DLL_IMPORT void buildSymbolTree();
        SR_DLL_IMPORT void collectSymbols(const Sampler& sampler);

        // FUNCTION: SURRENDER 0x10001430
        void compressSymbol(BitOStream& stream, w8_ulong symbol)
        {
            const auto found = table.find(symbol);
            if (found != table.end()) {
                stream.put(found->second->code, found->second->bits);
            }
        }

        std::unordered_map<w8_ulong, Node*> table;
        std::vector<Node> nodes;
        Node* free_list;
        Node* root;
        w8_ulong num_symbols;
        w8_ulong code_width;
        w8_ulong total;

    private:
        Compressor(const Compressor& other);

        void dumpNode(BitOStream& stream, Node* node);

    public:
        void setupPath(Node* node, w8_ulong code, w8_ulong depth);
    };

    class Decompressor {
    public:
        SR_DLL_IMPORT Decompressor(BitIStream& stream);
        SR_DLL_IMPORT w8_ulong decompressSymbol();
        SR_DLL_IMPORT w8_ulong getDataCount() const;

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
        w8_ulong unknown_10;
        w8_ulong num_symbols;
        w8_ulong data_count;
        Symbol* lookup[0x100];
        unsigned char depth[0x100];
    };
};
