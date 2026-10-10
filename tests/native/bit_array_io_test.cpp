#include "wiz8/engine_code/BitArray.h"
#include "surrender/srIO.h"
#include "temporary_directory.h"

#include <cstdio>
#include <fstream>
#include <vector>

#define CHECK(e) do { if (!(e)) throw std::runtime_error(#e); } while (0)

int main() try
{
    const auto root = std::filesystem::path(make_temporary_directory("bit-array-io"));
    const auto path = root / "bits.bin";
    const unsigned int words[] = {0x81, 0x12345678, 0};
    BitArray original(64);
    std::copy(std::begin(words), std::end(words), original.puiIndex);
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        CHECK(original.Save(file.get()));
        CHECK(file->seek(0, wiz8::SeekOrigin::begin) == 0);
        CHECK(srIO::readInteger<Uint32>(file->io()) == 0xdeadd00d);
        CHECK(srIO::readInteger<Uint32>(file->io()) == 64);
        const auto packed_size = srIO::readInteger<Uint32>(file->io());
        CHECK(packed_size + 16 == file->size());
        CHECK(SDL_SeekIO(file->io(), -4, SDL_IO_SEEK_END) == file->size() - 4);
        CHECK(srIO::readInteger<Uint32>(file->io()) == 0xdeadd00d);
    }
    auto file = wiz8::open_host_file(path);
    std::vector<unsigned char> encoded(file->size());
    file->read_exact(encoded.data(), encoded.size());
    file->seek(0, wiz8::SeekOrigin::begin);
    BitArray restored(1);
    CHECK(restored.Load(file.get()) && restored.bit_count == 64 && restored.word_count == 3);
    CHECK(std::equal(std::begin(words), std::end(words), restored.puiIndex));
    file->close();
    // Missing framing and Huffman bytes fail without changing the destination.
    for (const auto size : {size_t(0), size_t(12), encoded.size() - 4, encoded.size() - 1}) {
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(encoded.data()), size);
        }
        auto truncated = wiz8::open_host_file(path);
        BitArray destination(32);
        destination.Set(7);
        CHECK(!destination.Load(truncated.get()));
        CHECK(destination.bit_count == 32 && destination.Test(7));
    }
    std::filesystem::remove_all(root);
    puts("ok: SDL BitArray framing, Huffman round trip and truncation");
    return 0;
}
catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
