#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "wiz8/filesystem.h"

struct W8ChunkHead {
    unsigned int chunk_id; /* 0x00 */
    unsigned char grouped; /* 0x04 */
    unsigned char at_end;  /* 0x05 */
    unsigned char unknown_06[2];
    int extent; /* 0x08 */
};

// A chunk id is its four-character code stored little-endian.
constexpr unsigned int W8ChunkId(const char (&tag)[5])
{
    return static_cast<unsigned char>(tag[0]) | static_cast<unsigned char>(tag[1]) << 8 |
           static_cast<unsigned char>(tag[2]) << 16 |
           static_cast<unsigned>(static_cast<unsigned char>(tag[3])) << 24;
}

// Runtime navigation is owned value storage; only the ten-byte disk header
// and four-byte counts form the serialized contract.
struct W8Chunk {
private:
    // Shared with a StagedFile that publishes the finished RIFF.
    std::shared_ptr<wiz8::File> m_hFile;
    bool m_fWriting = false;
    std::vector<W8ChunkHead> m_heads;
    std::vector<int> m_group_counts;
    std::vector<std::int64_t> m_offsets;
    std::vector<int> m_group_progress;

public:
    W8Chunk() = default;
    W8Chunk(const W8Chunk&) = delete;
    W8Chunk& operator=(const W8Chunk&) = delete;
    ~W8Chunk() = default;

    bool IsOpen() const { return m_hFile != nullptr; }
    /* Writer access for existing format encoders. Readers use bounded transfers. */
    wiz8::File& Stream()
    {
        if (!m_hFile || !m_fWriting) throw std::logic_error("raw stream requires RIFF writer");
        return *m_hFile;
    }
    bool OpenRead(const std::filesystem::path& path);
    bool OpenExistingRiff(const std::filesystem::path& path, wiz8::OpenMode mode);
    bool OpenWrite(const std::filesystem::path& path);
    bool OpenReadWrite(const std::filesystem::path& path);
    bool OpenAppend(const std::filesystem::path& path);
    bool OpenWrite(std::shared_ptr<wiz8::File> file);
    void Close();
    bool CopyCurrentChunkFrom(W8Chunk* source);
    bool SkipCurrentChunk();
    bool OpenGroup();
    bool ReleaseGroup();
    unsigned int CurrentChunkId();
    int CurrentChunkExtent();
    std::int64_t CurrentChunkEnd() const;
    int ChunkCount();
    bool OpenChunk(unsigned int chunk_id, unsigned char grouped);

    // Payload transfer within the active chunk. Reads never cross its extent;
    // both directions throw instead of reporting partial transfers.
    std::int64_t RemainingBytes() const;
    void ReadBytes(void* buffer, std::size_t size);
    void SkipBytes(std::size_t size);
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
    template<class F> void WriteChunk(unsigned int chunk_id, F&& body)
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
    template<class F> void WriteGroup(unsigned int chunk_id, F&& body)
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
        for (int index = 0, count = ChunkCount(); index < count; ++index) {
            OpenChunk(0, 0);
            const auto depth = m_heads.size();
            bool more = true;
            if (!CurrentChunkAtEnd()) {
                if constexpr (std::is_void_v<std::invoke_result_t<F&, unsigned int>>)
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
    unsigned char CurrentChunkAtEnd();

private:
    W8ChunkHead& CurrentHead();
};
