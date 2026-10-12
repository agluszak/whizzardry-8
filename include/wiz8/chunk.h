#pragma once

#include "wiz8/filesystem.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct W8ChunkHead {
    std::uint32_t chunk_id; /* 0x00 */
    std::uint8_t grouped; /* 0x04 */
    std::uint8_t at_end;  /* 0x05 */
    std::uint8_t unknown_06[2];
    std::int32_t extent; /* 0x08 */
};

// A chunk id is its four-character code stored little-endian.
constexpr std::uint32_t W8ChunkId(const char (&tag)[5])
{
    return static_cast<std::uint8_t>(tag[0]) | static_cast<std::uint8_t>(tag[1]) << 8 |
           static_cast<std::uint8_t>(tag[2]) << 16 | static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag[3])) << 24;
}

// Runtime navigation is owned value storage; only the ten-byte disk header
// and four-byte counts form the serialized contract.
struct W8Chunk {
private:
    // Shared with a StagedFile that publishes the finished RIFF.
    std::shared_ptr<wiz8::File> m_hFile;
    bool m_fWriting = false;
    std::vector<W8ChunkHead> m_heads;
    std::vector<std::int32_t> m_group_counts;
    std::vector<std::int64_t> m_offsets;
    std::vector<std::int32_t> m_group_progress;

public:
    W8Chunk() = default;
    W8Chunk(const W8Chunk&) = delete;
    W8Chunk& operator=(const W8Chunk&) = delete;
    ~W8Chunk() = default;

    bool IsOpen() const { return m_hFile != nullptr; }
    /* The open stream, for section payloads written by format-specific code. */
    wiz8::File& Stream()
    {
        if (!m_hFile) throw std::logic_error("closed RIFF");
        return *m_hFile;
    }
    bool OpenRead(std::string_view path);
    bool OpenExistingRiff(std::string_view path, wiz8::OpenMode mode);
    bool OpenWrite(std::string_view path);
    bool OpenReadWrite(std::string_view path);
    bool OpenAppend(std::string_view path);
    bool OpenWrite(std::shared_ptr<wiz8::File> file);
    void Close();
    bool CopyCurrentChunkFrom(W8Chunk* source);
    bool SkipCurrentChunk();
    bool OpenGroup();
    bool ReleaseGroup();
    std::uint32_t CurrentChunkId();
    std::int32_t CurrentChunkExtent();
    std::int64_t CurrentChunkEnd() const;
    std::int32_t ChunkCount();
    bool OpenChunk(std::uint32_t chunk_id, std::uint8_t grouped);
    bool Read(void* buffer, std::uint32_t size, std::uint32_t* transferred);
    std::uint8_t Write(const void* buffer, std::uint32_t size, std::uint32_t* transferred);

    // Payload transfer within the active chunk. Reads never cross its extent;
    // both directions throw instead of reporting partial transfers.
    std::int64_t RemainingBytes() const;
    void ReadBytes(void* buffer, std::size_t size);
    void WriteBytes(const void* buffer, std::size_t size);
    template<class T> void ReadValue(T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        ReadBytes(&value, sizeof(value));
    }
    template<class T> T ReadValue()
    {
        T value{};
        ReadValue(value);
        return value;
    }
    template<class T> void WriteValue(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        WriteBytes(&value, sizeof(value));
    }

    // Write one complete chunk: body transfers the payload and the header
    // extent is patched when it returns. A body that leaves a nested chunk
    // open is rejected. Failures throw; the caller abandons the file.
    template<class F> void WriteChunk(std::uint32_t chunk_id, F&& body)
    {
        if (!m_fWriting || chunk_id == 0) throw std::logic_error("invalid RIFF chunk write");
        OpenChunk(chunk_id, 0);
        const auto depth = m_heads.size();
        std::invoke(std::forward<F>(body));
        if (m_heads.size() != depth) throw std::logic_error("unbalanced RIFF chunk");
        ReleaseCurrentChunk();
    }
    // A grouped chunk: body writes the group's prefix and its child chunks,
    // and the child count is patched when it returns.
    template<class F> void WriteGroup(std::uint32_t chunk_id, F&& body)
    {
        WriteChunk(chunk_id, [&] {
            OpenGroup();
            std::invoke(std::forward<F>(body));
            ReleaseGroup();
        });
    }
    // Read the current chunk as a group; body consumes the prefix and may
    // walk the children with ForEachChunk.
    template<class F> void ReadGroup(F&& body)
    {
        OpenGroup();
        const auto depth = m_heads.size();
        std::invoke(std::forward<F>(body));
        if (m_heads.size() != depth) throw std::logic_error("unbalanced RIFF group");
        ReleaseGroup();
    }
    // Visit each child of the innermost open group with its chunk id. Chunks
    // marked at_end hold consumed sections and are skipped. Whatever payload
    // the visitor leaves unread is skipped; a visitor returning bool stops
    // the walk by returning false.
    template<class F> void ForEachChunk(F&& visit)
    {
        for (std::int32_t index = 0, count = ChunkCount(); index < count; ++index) {
            OpenChunk(0, 0);
            const auto depth = m_heads.size();
            bool more = true;
            if (!CurrentChunkAtEnd()) {
                if constexpr (std::is_void_v<std::invoke_result_t<F&, std::uint32_t>>)
                    visit(CurrentChunkId());
                else
                    more = visit(CurrentChunkId());
            }
            if (m_heads.size() != depth) throw std::logic_error("unbalanced RIFF chunk");
            SkipCurrentChunk();
            ReleaseCurrentChunk();
            if (!more) return;
        }
    }
    void RewindCurrentChunk();
    void SetCurrentChunkAtEnd();
    bool ReleaseCurrentChunk();
    std::uint8_t CurrentChunkAtEnd();
private:
    W8ChunkHead& CurrentHead();
};
