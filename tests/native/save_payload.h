#pragma once

#include <functional>
#include <optional>
#include <type_traits>
#include <vector>

#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"

// Put an existing retail payload fixture inside a real bounded save section.
// Keep the source cursor at the consumed payload position for trailer checks.
template<class Decode> auto ReadSavePayload(wiz8::File& source, Decode decode)
{
    const auto start = source.tell();
    std::vector<std::byte> bytes(source.size() - start);
    source.read_exact(bytes.data(), bytes.size());
    ScopedTemporaryDirectory temporary("wiz8-save-payload");
    const auto path = temporary.path / "payload.riff";
    W8Chunk chunk;
    if (!chunk.OpenWrite(path)) throw std::runtime_error("create payload fixture");
    chunk.WriteChunk(W8ChunkId("TEST"), [&] { chunk.WriteBytes(bytes.data(), bytes.size()); });
    chunk.Close();
    if (!chunk.OpenRead(path)) throw std::runtime_error("open payload fixture");
    const auto finish = [&] {
        source.seek(start + bytes.size() - chunk.RemainingBytes(), wiz8::SeekOrigin::begin);
    };
    using Result = std::invoke_result_t<Decode, W8Chunk&>;
    if constexpr (std::is_void_v<Result>) {
        chunk.ForEachChunk([&](unsigned) {
            decode(chunk);
            finish();
            return false;
        });
    } else {
        std::optional<Result> result;
        chunk.ForEachChunk([&](unsigned) {
            result.emplace(decode(chunk));
            finish();
            return false;
        });
        return std::move(result).value();
    }
}
