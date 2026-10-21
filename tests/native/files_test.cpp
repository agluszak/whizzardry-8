#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// RIFF framing and scoped ownership through native file streams.
#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"

namespace fs = std::filesystem;

static void fixture(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(content.data(), content.size());
    REQUIRE(file.good());
}
static std::string contents(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
TEST_CASE("Native RIFF streams retain framing and scoped chunk ownership", "[filesystem][io]")
{
    ScopedTemporaryDirectory directory{"wiz8-files"};
    const auto user = directory.path;
    {
        const auto path = user / "native.sav";
        fixture(path, "RIFF");
        W8Chunk chunk;
        REQUIRE((!chunk.OpenRead(path) && !chunk.IsOpen()));
        REQUIRE(chunk.OpenWrite(path));
        const unsigned word = 0x12345678;
        chunk.WriteChunk(0x54534554, [&] { chunk.WriteValue(word); });
        chunk.Close();
        REQUIRE((chunk.OpenRead(path) && chunk.ChunkCount() == 1));
        unsigned loaded = 0;
        chunk.ForEachChunk([&](unsigned id) {
            REQUIRE(id == 0x54534554);
            chunk.ReadValue(loaded);
        });
        REQUIRE(loaded == word);
        chunk.Close();
        // Destruction owns pending headers as well as the stream, even without Close.
        REQUIRE(chunk.OpenRead(path));
    }
    {
        // Scoped chunk writers patch extents and group counts; the bytes are
        // the established RIFF framing, independent of the helper API.
        const auto path = user / "scoped.sav";
        W8Chunk chunk;
        REQUIRE(chunk.OpenWrite(path));
        chunk.WriteGroup(W8ChunkId("GRP1"), [&] {
            chunk.WriteValue(7);
            chunk.WriteChunk(W8ChunkId("LEAF"), [&] { chunk.WriteValue(std::uint16_t{0xbeef}); });
        });
        chunk.WriteChunk(W8ChunkId("TAIL"), [] {});
        chunk.Close();
        const unsigned char golden[] = {'R',  'I',  'F', 'F', 1,   0,   44,  0,   0, 0, 2, 0, 0, 0,
                                        'G',  'R',  'P', '1', 1,   0,   20,  0,   0, 0, 1, 0, 0, 0,
                                        7,    0,    0,   0,   'L', 'E', 'A', 'F', 0, 0, 2, 0, 0, 0,
                                        0xef, 0xbe, 'T', 'A', 'I', 'L', 0,   0,   0, 0, 0, 0};
        REQUIRE(contents(path) ==
                std::string(reinterpret_cast<const char*>(golden), sizeof(golden)));

        std::vector<unsigned> visited;
        REQUIRE(chunk.OpenRead(path));
        chunk.ForEachChunk([&](unsigned id) {
            visited.push_back(id);
            if (id != W8ChunkId("GRP1")) return;
            chunk.ReadGroup([&] {
                REQUIRE(chunk.ReadValue<int>() == 7);
                chunk.ForEachChunk([&](unsigned leaf) {
                    visited.push_back(leaf);
                    REQUIRE(chunk.RemainingBytes() == 2);
                    REQUIRE_THROWS_AS(chunk.ReadValue<std::uint32_t>(), std::runtime_error);
                    REQUIRE(chunk.ReadValue<std::uint16_t>() == 0xbeef);
                });
            });
        });
        chunk.Close();
        REQUIRE(visited ==
                std::vector<unsigned>{W8ChunkId("GRP1"), W8ChunkId("LEAF"), W8ChunkId("TAIL")});
        // A visitor returning false ends the walk after releasing its chunk.
        visited.clear();
        REQUIRE(chunk.OpenRead(path));
        chunk.ForEachChunk([&](unsigned id) {
            visited.push_back(id);
            return false;
        });
        chunk.Close();
        REQUIRE(visited == std::vector<unsigned>{W8ChunkId("GRP1")});

        // The same framing in memory: a writer keeps its bytes once closed, and a reader
        // walks them as it walks a file.
        const std::shared_ptr<wiz8::File> memory = wiz8::open_memory_writer();
        REQUIRE(chunk.OpenWrite(memory));
        chunk.WriteGroup(W8ChunkId("GRP1"), [&] {
            chunk.WriteValue(7);
            chunk.WriteChunk(W8ChunkId("LEAF"), [&] { chunk.WriteValue(std::uint16_t{0xbeef}); });
        });
        chunk.WriteChunk(W8ChunkId("TAIL"), [] {});
        chunk.Close();
        const auto bytes = memory->written_memory();
        REQUIRE(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) ==
                std::string(reinterpret_cast<const char*>(golden), sizeof(golden)));
        visited.clear();
        REQUIRE(chunk.OpenRead(wiz8::open_memory(bytes)));
        chunk.ForEachChunk([&](unsigned id) { visited.push_back(id); });
        chunk.Close();
        REQUIRE(visited == std::vector<unsigned>{W8ChunkId("GRP1"), W8ChunkId("TAIL")});
    }
}

TEST_CASE("Fixed-width save names are terminated and zero-padded", "[filesystem][io]")
{
    // Save records hold names in fixed fields; stale bytes after the name
    // would leak memory into saves and make identical games save differently.
    ScopedTemporaryDirectory directory{"wiz8-files"};
    const auto path = directory.path / "names.bin";
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        file->write_padded("door", 8);
        file->write_padded("0123456789", 8);
    }
    REQUIRE(contents(path) == std::string("door\0\0\0\0"
                                          "0123456\0",
                                          16));
}
