#include "wiz8/sr_api.h"
#include "wiz8/chunk.h"
#include "wiz8/virtual_file.h"
#include "wiz8/filesystem.h"

#define CHUNK_CPP "C:\\Projects\\Wizardry 8\\Local Code\\chunk.cpp"

enum { W8_RIFF_CHUNK_ID = 0x46464952 };

/* The two scalars are initialised, not assigned: retail sets them before any of
   the four vectors is built, and they are declared first, so they can only come
   from a member initialiser list. The vectors are ordinary members and build
   themselves in declaration order after them. */
// FUNCTION: WIZ8 0x0055bce0
W8Chunk::W8Chunk() : m_fWriting(false) {}

/* Implicit member destruction releases the four backing arrays in reverse
   order. Retail 0x0055bde0 neither deletes remaining heads nor closes the file. */
// FUNCTION: WIZ8 0x0055bde0
W8Chunk::~W8Chunk()
{
    while (m_heads.GetCount()) m_heads.RemoveAtAndDelete(m_heads.GetCount() - 1);
}

// FUNCTION: WIZ8 0x0055ca20
unsigned char W8Chunk::Read(void* buffer, unsigned int size, unsigned int* transferred)
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
unsigned char W8Chunk::Write(const void* buffer, unsigned int size, unsigned int* transferred)
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

bool W8Chunk::OpenExistingRiff(char* path, wiz8::OpenMode mode)
try
{
    if (m_hFile != 0) {
        return false;
    }
    m_hFile = [&]() { try { return wiz8::open_file(path, mode); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (m_hFile == 0) {
        return false;
    }
    m_fWriting = false;
    OpenChunk(0, 0);
    W8ChunkHead* head = m_heads[m_heads.GetCount() - 1];
    if (head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x1f0, 0);
        throw std::runtime_error("missing RIFF header");
    }
    if (head->chunk_id != W8_RIFF_CHUNK_ID)
        throw std::runtime_error("invalid RIFF identifier");
    OpenGroup();
    return true;
}
catch (const std::exception&)
{
    m_heads.RemoveAllAndDelete();
    m_group_counts.Clear(); m_offsets.Clear(); m_group_progress.Clear();
    m_hFile.reset(); m_fWriting = false;
    return false;
}

// FUNCTION: WIZ8 0x0055c000
bool W8Chunk::OpenRead(char* path)
{
    return OpenExistingRiff(path, wiz8::OpenMode::read);
}

// FUNCTION: WIZ8 0x0055be30
bool W8Chunk::OpenWrite(char* path)
try
{
    if (m_hFile != 0) {
        return false;
    }
    m_hFile = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::replace); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (m_hFile == 0) {
        return false;
    }
    m_fWriting = true;
    OpenChunk(W8_RIFF_CHUNK_ID, 0);
    OpenGroup();
    return true;
}
catch (const std::exception&)
{
    m_heads.RemoveAllAndDelete();
    m_group_counts.Clear(); m_offsets.Clear(); m_group_progress.Clear();
    m_hFile.reset(); m_fWriting = false;
    return false;
}

/* Reopen an existing RIFF for append. Children are skipped under a temporary
   read so the file sits at the end of the group; the original child count is
   then pushed onto the write-side progress stack before writing is armed. */
// FUNCTION: WIZ8 0x0055be80
bool W8Chunk::OpenAppend(char* path)
{
    W8ChunkHead* head;
    int child_count;
    int remaining;
    int position;
    int distance;

    if (!OpenExistingRiff(path, wiz8::OpenMode::update)) {
        return false;
    }
    child_count = m_group_counts[m_group_counts.GetCount() - 1];
    remaining = child_count;
    if (child_count > 0) {
        do {
            OpenChunk(0, 0);
            head = m_heads[m_heads.GetCount() - 1];
            if (head == 0) {
                srAssertFail("pHead", CHUNK_CPP, 0x136, 0);
                throw std::runtime_error("missing RIFF header");
            }
            position = m_hFile->tell();
            distance = m_offsets[m_offsets.GetCount() - 1] + (head->extent - position);
            if (distance != 0) {
                m_hFile->seek(distance, wiz8::SeekOrigin::current);
            }
            ReleaseCurrentChunk();
            --remaining;
        } while (remaining != 0);
    }
    m_group_progress.Add(child_count);
    m_fWriting = true;
    return true;
}

// FUNCTION: WIZ8 0x0055c080
bool W8Chunk::OpenReadWrite(char* path)
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
            int position = m_hFile->tell();
            int count;

            RewindCurrentChunk();
            count = m_group_progress.RemoveAt(m_group_progress.GetCount() - 1);
            Write(&count, sizeof(count), 0);
            m_hFile->seek(position, wiz8::SeekOrigin::begin);
        } else {
            m_group_counts.RemoveAt(m_group_counts.GetCount() - 1);
        }
        ReleaseCurrentChunk();
        if (m_hFile != 0) {
            m_hFile->close();
            m_hFile.reset();
            m_hFile = 0;
        }
        m_fWriting = false;
    }
}

/* Materialize the source's active payload, reproduce its tag and grouping bit
   in this write stream, and finalize the copy as one complete chunk. */
// FUNCTION: WIZ8 0x0055c1e0
bool W8Chunk::CopyCurrentChunkFrom(W8Chunk* source)
{
    W8ChunkHead* source_head = source->m_heads[source->m_heads.GetCount() - 1];
    unsigned int transferred;
    unsigned int extent;

    if (source_head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x204, 0);
        throw std::runtime_error("missing RIFF header");
    }
    extent = source_head->extent;
    if (!m_fWriting) {
        return false;
    }
    auto contents = std::make_unique<unsigned char[]>(extent);
    source->Read(contents.get(), extent, &transferred);
    if (extent != transferred) {
        return false;
    }
    source_head = source->m_heads[source->m_heads.GetCount() - 1];
    if (source_head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x166, 0);
        throw std::runtime_error("missing RIFF header");
    }
    OpenChunk(source->CurrentChunkId(), source_head->grouped);
    Write(contents.get(), extent, &transferred);
    if (extent != transferred) {
        ReleaseCurrentChunk();
        return false;
    }
    ReleaseCurrentChunk();
    return true;
}

/* Move past the unread remainder of the current head. Its absolute extent is
   combined with the active group's base offset and the current file position. */
// FUNCTION: WIZ8 0x0055c390
bool W8Chunk::SkipCurrentChunk()
{
    W8ChunkHead* head = m_heads[m_heads.GetCount() - 1];
    int position;
    int distance;

    if (head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x136, 0);
        throw std::runtime_error("missing RIFF header");
    }
    position = m_hFile->tell();
    distance = m_offsets[m_offsets.GetCount() - 1] + (head->extent - position);
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
    unsigned int transferred;

    if (!m_fWriting) {
        int count;

        Read(&count, sizeof(count), &transferred);
        const int extent = m_heads[m_heads.GetCount() - 1]->extent;
        if (count < 0 || extent < static_cast<int>(sizeof(count)) ||
            static_cast<unsigned>(count) > (extent - sizeof(count)) / 10)
            throw std::runtime_error("invalid RIFF group count");
        m_group_counts.Add(count);
        return true;
    } else {
        W8ChunkHead* head = m_heads[m_heads.GetCount() - 1];
        int position = m_hFile->tell();
        int count = 0;

        if (head == 0) {
            srAssertFail("pHead", CHUNK_CPP, 0x185, 0);
            throw std::runtime_error("missing RIFF header");
        }
        head->grouped = 1;
        m_group_progress.Add(0);
        m_hFile->seek(m_offsets[m_offsets.GetCount() - 1] - 6, wiz8::SeekOrigin::begin);
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
        m_group_counts.RemoveAt(m_group_counts.GetCount() - 1);
        return true;
    } else {
        unsigned int transferred;
        int position = m_hFile->tell();
        int count;

        RewindCurrentChunk();
        count = m_group_progress.RemoveAt(m_group_progress.GetCount() - 1);
        Write(&count, sizeof(count), &transferred);
        m_hFile->seek(position, wiz8::SeekOrigin::begin);
        return true;
    }
}

// FUNCTION: WIZ8 0x0055c660
unsigned int W8Chunk::CurrentChunkId()
{
    W8ChunkHead* head = m_heads[m_heads.GetCount() - 1];

    if (head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x1f0, 0);
        throw std::runtime_error("missing RIFF header");
    }
    return head->chunk_id;
}

// FUNCTION: WIZ8 0x0055c690
int W8Chunk::CurrentChunkExtent()
{
    W8ChunkHead* head = m_heads[m_heads.GetCount() - 1];

    if (head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x204, 0);
        throw std::runtime_error("missing RIFF header");
    }
    return head->extent;
}

// FUNCTION: WIZ8 0x0055c6c0
int W8Chunk::ChunkCount()
{
    return m_group_counts[m_group_counts.GetCount() - 1];
}

/* The on-disk header deliberately writes its four established fields
   separately: bytes 6 and 7 are object padding, not file data. A zero tag in
   read mode asks the stream to populate the header; write mode requires the
   caller to supply the tag. */
// FUNCTION: WIZ8 0x0055c6d0
bool W8Chunk::OpenChunk(unsigned int chunk_id, unsigned char grouped)
{
    auto head = std::make_unique<W8ChunkHead>();
    if (m_fWriting) {
        if (chunk_id == 0) {
            srAssertFail("chunkID!=0", CHUNK_CPP, 0x22d, 0);
        }
        head->chunk_id = chunk_id;
        head->extent = 0;
        head->grouped = grouped;
        m_hFile->write(&head->chunk_id, 4);
        m_hFile->write(&head->grouped, 1);
        m_hFile->write(&head->at_end, 1);
        m_hFile->write(&head->extent, 4);
    } else {
        m_hFile->read_exact(&head->chunk_id, 4);
        m_hFile->read_exact(&head->grouped, 1);
        m_hFile->read_exact(&head->at_end, 1);
        m_hFile->read_exact(&head->extent, 4);
        if (head->extent < 0 || head->extent > m_hFile->size() - m_hFile->tell())
            throw std::runtime_error("invalid RIFF chunk extent");
    }
    m_offsets.Add(m_hFile->tell());
    m_heads.Add(head.release());
    return true;
}

/* Remove the active header. Writers patch its extent in place; readers only
   discard the saved payload offset. A nested group advances its parent's
   completed-child count. */
// FUNCTION: WIZ8 0x0055c930
bool W8Chunk::ReleaseCurrentChunk()
{
    m_heads.RemoveAtAndDelete(m_heads.GetCount() - 1);
    if (m_fWriting) {
        int end = m_hFile->tell();
        int payload = m_offsets.RemoveAt(m_offsets.GetCount() - 1);
        int extent = end - payload;

        m_hFile->seek(payload - 4, wiz8::SeekOrigin::begin);
        Write(&extent, sizeof(extent), 0);
        m_hFile->seek(end, wiz8::SeekOrigin::begin);
    } else {
        m_offsets.RemoveAt(m_offsets.GetCount() - 1);
    }
    if (m_group_progress.GetCount() != 0) {
        m_group_progress[m_group_progress.GetCount() - 1] =
            m_group_progress[m_group_progress.GetCount() - 1] + 1;
    }
    return true;
}

// FUNCTION: WIZ8 0x0055cae0
void W8Chunk::RewindCurrentChunk()
{
    m_hFile->seek(m_offsets[m_offsets.GetCount() - 1], wiz8::SeekOrigin::begin);
}

/* Mark the active chunk's own at_end byte in the file. This is how the save
   code invalidates a level section it has already consumed in place. */
// FUNCTION: WIZ8 0x0055cb00
void W8Chunk::SetCurrentChunkAtEnd()
{
    unsigned char value = 1;
    int position = m_hFile->tell();

    m_hFile->seek(m_offsets[m_offsets.GetCount() - 1] - 5, wiz8::SeekOrigin::begin);
    m_hFile->write(&value, 1);
    m_hFile->seek(position, wiz8::SeekOrigin::begin);
}

// FUNCTION: WIZ8 0x0055cb60
unsigned char W8Chunk::CurrentChunkAtEnd()
{
    W8ChunkHead* head = m_heads[m_heads.GetCount() - 1];

    if (head == 0) {
        srAssertFail("pHead", CHUNK_CPP, 0x303, 0);
        throw std::runtime_error("missing RIFF header");
    }
    return head->at_end;
}
