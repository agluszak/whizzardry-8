#pragma once

#include "wiz8/filesystem.h"

#include <vector>

struct W8ChunkHead {
    unsigned int chunk_id; /* 0x00 */
    unsigned char grouped; /* 0x04 */
    unsigned char at_end;  /* 0x05 */
    unsigned char unknown_06[2];
    int extent; /* 0x08 */
};

W8_ABI_ASSERT(sizeof(W8ChunkHead) == 0x0c, "W8ChunkHead_size_must_be_0x0c");

// Runtime navigation is owned value storage; only the ten-byte disk header
// and four-byte counts form the serialized contract.
struct W8Chunk {
    std::shared_ptr<wiz8::File> m_hFile;
private:
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

    bool OpenRead(const char* path);
    bool OpenExistingRiff(const char* path, wiz8::OpenMode mode);
    bool OpenWrite(const char* path);
    bool OpenWrite(std::shared_ptr<wiz8::File> file);
    bool OpenReadWrite(const char* path);
    bool OpenAppend(const char* path);
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
    unsigned char Read(void* buffer, unsigned int size, unsigned int* transferred);
    unsigned char Write(const void* buffer, unsigned int size, unsigned int* transferred);
    void RewindCurrentChunk();
    void SetCurrentChunkAtEnd();
    bool ReleaseCurrentChunk();
    unsigned char CurrentChunkAtEnd();
private:
    W8ChunkHead& CurrentHead();
};
