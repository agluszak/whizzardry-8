#include <string_view>
#include <array>
#include <algorithm>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "wiz8/sr_api.h"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"

#define CHUNK_CPP "C:\\Projects\\Wizardry 8\\Local Code\\chunk.cpp"

enum { W8_RIFF_CHUNK_ID = 0x46464952 };

W8ChunkHead& W8Chunk::CurrentHead()
{
    if (m_heads.empty()) throw std::logic_error("no current RIFF chunk");
    return m_heads.back();
}

// FUNCTION: WIZ8 0x0055ca20
bool W8Chunk::Read(void* buffer, std::uint32_t size, std::uint32_t* transferred)
{
    if (transferred) *transferred = 0;
    if (!m_hFile) throw std::logic_error("read closed RIFF");
    if (m_fWriting) {
        srAssertFail("!m_fWriting", CHUNK_CPP, 0x280, 0);
    }
    m_hFile->read_exact(buffer, size);
    if (transferred) {
        *transferred = size;
    }
    return true;
}

// FUNCTION: WIZ8 0x0055ca80
std::uint8_t W8Chunk::Write(const void* buffer, std::uint32_t size, std::uint32_t* transferred)
{
    if (transferred) *transferred = 0;
    if (!m_hFile) throw std::logic_error("write closed RIFF");
    if (!m_fWriting) {
        srAssertFail("m_fWriting", CHUNK_CPP, 0x29d, 0);
    }
    m_hFile->write(buffer, size);
    if (transferred) {
        *transferred = size;
    }
    return true;
}

std::int64_t W8Chunk::RemainingBytes() const
{
    if (!m_hFile) throw std::logic_error("closed RIFF");
    return CurrentChunkEnd() - m_hFile->tell();
}

void W8Chunk::ReadBytes(void* buffer, std::size_t size)
{
    if (m_fWriting) throw std::logic_error("read from RIFF writer");
    const auto remaining = RemainingBytes();
    if (remaining < 0 || size > static_cast<std::uint64_t>(remaining))
        throw std::runtime_error("read past RIFF chunk");
    m_hFile->read_exact(buffer, size);
}

void W8Chunk::WriteBytes(const void* buffer, std::size_t size)
{
    if (!m_hFile || !m_fWriting || m_heads.empty()) throw std::logic_error("write outside RIFF chunk");
    m_hFile->write(buffer, size);
}

bool W8Chunk::OpenExistingRiff(std::string_view path, wiz8::OpenMode mode)
try
{
    if (m_hFile != 0) {
        return false;
    }
    m_hFile = wiz8::open_file(path, mode);
    m_fWriting = false;
    OpenChunk(0, 0);
    W8ChunkHead* head = &CurrentHead();
    if (head->chunk_id != W8_RIFF_CHUNK_ID)
        throw std::runtime_error("invalid RIFF identifier");
    OpenGroup();
    return true;
}
catch (const std::exception&)
{
    m_heads.clear();
    m_group_counts.clear(); m_offsets.clear(); m_group_progress.clear();
    m_hFile.reset(); m_fWriting = false;
    return false;
}

// FUNCTION: WIZ8 0x0055c000
bool W8Chunk::OpenRead(std::string_view path)
{
    return OpenExistingRiff(path, wiz8::OpenMode::read);
}

// FUNCTION: WIZ8 0x0055be30
bool W8Chunk::OpenWrite(std::string_view path)
try
{
    if (m_hFile) return false;
    return OpenWrite(wiz8::open_file(path, wiz8::OpenMode::replace));
}
catch (const std::exception&) { return false; }

bool W8Chunk::OpenWrite(std::shared_ptr<wiz8::File> file)
try
{
    if (m_hFile || !file) return false;
    m_hFile = std::move(file);
    m_fWriting = true;
    OpenChunk(W8_RIFF_CHUNK_ID, 0);
    OpenGroup();
    return true;
}
catch (const std::exception&)
{
    m_heads.clear(); m_group_counts.clear(); m_offsets.clear(); m_group_progress.clear();
    m_hFile.reset(); m_fWriting = false;
    return false;
}

/* Reopen an existing RIFF for append. Children are skipped under a temporary
   read so the file sits at the end of the group; the original child count is
   then pushed onto the write-side progress stack before writing is armed. */
// FUNCTION: WIZ8 0x0055be80
bool W8Chunk::OpenAppend(std::string_view path)
{
    W8ChunkHead* head;
    std::int32_t child_count;
    std::int32_t remaining;
    std::int64_t position;
    std::int64_t distance;

    if (!OpenExistingRiff(path, wiz8::OpenMode::update)) {
        return false;
    }
    child_count = m_group_counts.back();
    remaining = child_count;
    if (child_count > 0) {
        do {
            OpenChunk(0, 0);
            head = &CurrentHead();
            position = m_hFile->tell();
            distance = m_offsets.back() + (head->extent - position);
            if (distance != 0) {
                m_hFile->seek(distance, wiz8::SeekOrigin::current);
            }
            ReleaseCurrentChunk();
            --remaining;
        } while (remaining != 0);
    }
    m_group_progress.push_back(child_count);
    m_fWriting = true;
    return true;
}

// FUNCTION: WIZ8 0x0055c080
bool W8Chunk::OpenReadWrite(std::string_view path)
{
    return OpenExistingRiff(path, wiz8::OpenMode::update);
}

/* Close the root chunk and then its file. In write mode the root's stored
   child count is patched before the ordinary current-chunk finalizer writes
   the root extent. */
// FUNCTION: WIZ8 0x0055c100
void W8Chunk::Close()
{
    if (m_hFile != 0) {
        if (m_fWriting) {
            if (m_heads.size() != 1 || m_offsets.size() != 1 || m_group_progress.size() != 1)
                throw std::logic_error("unfinalized RIFF children");
            const auto position = m_hFile->tell();
            std::int32_t count;

            RewindCurrentChunk();
            count = m_group_progress.back();
            m_group_progress.pop_back();
            Write(&count, sizeof(count), 0);
            m_hFile->seek(position, wiz8::SeekOrigin::begin);
        } else {
            m_group_counts.pop_back();
        }
        ReleaseCurrentChunk();
        if (m_hFile != 0) {
            if (m_fWriting) m_hFile->flush();
            m_hFile->close();
            m_hFile.reset();
            m_hFile = 0;
        }
        m_fWriting = false;
    }
}

/* Stream the source's active payload, reproduce its tag and grouping bit
   in this write stream, and finalize the copy as one complete chunk. */
// FUNCTION: WIZ8 0x0055c1e0
bool W8Chunk::CopyCurrentChunkFrom(W8Chunk* source)
{
    W8ChunkHead* source_head = &source->CurrentHead();
    std::uint32_t extent;

    extent = source_head->extent;
    if (!m_fWriting) {
        return false;
    }
    OpenChunk(source->CurrentChunkId(), source_head->grouped);
    std::array<std::uint8_t, 65536> buffer;
    while (extent) {
        const auto bytes = std::min<std::size_t>(extent, buffer.size());
        source->m_hFile->read_exact(buffer.data(), bytes);
        m_hFile->write(buffer.data(), bytes);
        extent -= bytes;
    }
    ReleaseCurrentChunk();
    return true;
}

/* Move past the unread remainder of the current head. Its absolute extent is
   combined with the active group's base offset and the current file position. */
// FUNCTION: WIZ8 0x0055c390
bool W8Chunk::SkipCurrentChunk()
{
    W8ChunkHead* head = &CurrentHead();
    std::int64_t position;
    std::int64_t distance;

    position = m_hFile->tell();
    distance = m_offsets.back() + (head->extent - position);
    if (distance != 0) {
        m_hFile->seek(distance, wiz8::SeekOrigin::current);
    }
    return true;
}

/* A grouped chunk begins with its child count. Readers retain that count for
   the group walk. Writers mark the active head as grouped, reserve the count,
   and retain a zero completed-child counter until ReleaseGroup patches it. */
// FUNCTION: WIZ8 0x0055c3f0
bool W8Chunk::OpenGroup()
{
    std::uint32_t transferred;

    if (!m_fWriting) {
        std::int32_t count;

        Read(&count, sizeof(count), &transferred);
        const std::int32_t extent = CurrentHead().extent;
        if (count < 0 || extent < static_cast<std::int32_t>(sizeof(count)) ||
            static_cast<std::uint32_t>(count) > (extent - sizeof(count)) / 10)
            throw std::runtime_error("invalid RIFF group count");
        m_group_counts.push_back(count);
        return true;
    } else {
        W8ChunkHead* head = &CurrentHead();
        const auto position = m_hFile->tell();
        std::int32_t count = 0;

        head->grouped = 1;
        m_group_progress.push_back(0);
        m_hFile->seek(m_offsets.back() - 6, wiz8::SeekOrigin::begin);
        Write(&head->grouped, 1, &transferred);
        m_hFile->seek(position, wiz8::SeekOrigin::begin);
        Write(&count, sizeof(count), &transferred);
        return true;
    }
}

/* Finish one grouped walk. Readers discard its retained child count. Writers
   patch the reserved word with the number of released children. */
// FUNCTION: WIZ8 0x0055c5a0
bool W8Chunk::ReleaseGroup()
{
    if (!m_fWriting) {
        m_group_counts.pop_back();
        return true;
    } else {
        std::uint32_t transferred;
        const auto position = m_hFile->tell();
        std::int32_t count;

        RewindCurrentChunk();
        count = m_group_progress.back();
            m_group_progress.pop_back();
        Write(&count, sizeof(count), &transferred);
        m_hFile->seek(position, wiz8::SeekOrigin::begin);
        return true;
    }
}

// FUNCTION: WIZ8 0x0055c660
std::uint32_t W8Chunk::CurrentChunkId()
{
    W8ChunkHead* head = &CurrentHead();

    return head->chunk_id;
}

// FUNCTION: WIZ8 0x0055c690
std::int32_t W8Chunk::CurrentChunkExtent()
{
    W8ChunkHead* head = &CurrentHead();

    return head->extent;
}

std::int64_t W8Chunk::CurrentChunkEnd() const
{
    if (m_heads.empty() || m_offsets.empty()) throw std::logic_error("no current RIFF chunk");
    return m_offsets.back() + m_heads.back().extent;
}

// FUNCTION: WIZ8 0x0055c6c0
std::int32_t W8Chunk::ChunkCount()
{
    return m_group_counts.back();
}

/* The on-disk header deliberately writes its four established fields
   separately: bytes 6 and 7 are object padding, not file data. A zero tag in
   read mode asks the stream to populate the header; write mode requires the
   caller to supply the tag. */
// FUNCTION: WIZ8 0x0055c6d0
bool W8Chunk::OpenChunk(std::uint32_t chunk_id, std::uint8_t grouped)
{
    W8ChunkHead head{};
    if (m_fWriting) {
        if (chunk_id == 0) {
            srAssertFail("chunkID!=0", CHUNK_CPP, 0x22d, 0);
        }
        head.chunk_id = chunk_id;
        head.extent = 0;
        head.grouped = grouped;
        m_hFile->write(&head.chunk_id, 4);
        m_hFile->write(&head.grouped, 1);
        m_hFile->write(&head.at_end, 1);
        m_hFile->write(&head.extent, 4);
    } else {
        m_hFile->read_exact(&head.chunk_id, 4);
        m_hFile->read_exact(&head.grouped, 1);
        m_hFile->read_exact(&head.at_end, 1);
        m_hFile->read_exact(&head.extent, 4);
        if (head.extent < 0 || head.extent > m_hFile->size() - m_hFile->tell())
            throw std::runtime_error("invalid RIFF chunk extent");
    }
    m_offsets.push_back(m_hFile->tell());
    m_heads.push_back(head);
    return true;
}

/* Remove the active header. Writers patch its extent in place; readers only
   discard the saved payload offset. A nested group advances its parent's
   completed-child count. */
// FUNCTION: WIZ8 0x0055c930
bool W8Chunk::ReleaseCurrentChunk()
{
    m_heads.pop_back();
    if (m_fWriting) {
        const auto end = m_hFile->tell();
        const auto payload = m_offsets.back();
        m_offsets.pop_back();
        if (end < payload || end - payload > std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("RIFF chunk too large");
        const std::int32_t extent = std::int32_t(end - payload);

        m_hFile->seek(payload - 4, wiz8::SeekOrigin::begin);
        Write(&extent, sizeof(extent), 0);
        m_hFile->seek(end, wiz8::SeekOrigin::begin);
    } else {
        m_offsets.pop_back();
    }
    if (!m_group_progress.empty()) {
        ++m_group_progress.back();
    }
    return true;
}

// FUNCTION: WIZ8 0x0055cae0
void W8Chunk::RewindCurrentChunk()
{
    m_hFile->seek(m_offsets.back(), wiz8::SeekOrigin::begin);
}

/* Mark the active chunk's own at_end byte in the file. This is how the save
   code invalidates a level section it has already consumed in place. */
// FUNCTION: WIZ8 0x0055cb00
void W8Chunk::SetCurrentChunkAtEnd()
{
    std::uint8_t value = 1;
    const auto position = m_hFile->tell();

    m_hFile->seek(m_offsets.back() - 5, wiz8::SeekOrigin::begin);
    m_hFile->write(&value, 1);
    m_hFile->seek(position, wiz8::SeekOrigin::begin);
}

// FUNCTION: WIZ8 0x0055cb60
std::uint8_t W8Chunk::CurrentChunkAtEnd()
{
    W8ChunkHead* head = &CurrentHead();

    return head->at_end;
}
