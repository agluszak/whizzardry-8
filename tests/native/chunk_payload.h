#pragma once
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"

class ChunkPayload {
    ScopedTemporaryDirectory directory{"chunk-payload"};
    W8Chunk chunk;

public:
    explicit ChunkPayload(wiz8::File& file)
    {
        std::vector<std::byte> bytes(file.size() - file.tell());
        file.read_exact(bytes.data(), bytes.size());
        const auto path = directory.path / "payload.riff";
        if (!chunk.OpenWrite(path)) throw std::runtime_error("cannot create payload fixture");
        chunk.WriteChunk(W8ChunkId("TEST"), [&] { chunk.WriteBytes(bytes.data(), bytes.size()); });
        chunk.Close();
        if (!chunk.OpenRead(path)) throw std::runtime_error("cannot open payload fixture");
        chunk.OpenChunk(0, 0);
    }
    W8Chunk& get() { return chunk; }
};
