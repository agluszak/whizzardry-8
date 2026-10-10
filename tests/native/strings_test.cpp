#include "surrender/srIO.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

int main()
{
    auto output = srIO::own(SDL_IOFromDynamicMem());
    const std::string memory_text = "zażółć-雪";
    srIO::writeExact(output.get(), memory_text.data(), memory_text.size());
    CHECK(SDL_GetIOSize(output.get()) == Sint64(memory_text.size()));
    CHECK(SDL_SeekIO(output.get(), 0, SDL_IO_SEEK_SET) == 0);
    std::string copied(memory_text.size(), '\0');
    srIO::readExact(output.get(), copied.data(), copied.size());
    CHECK(copied == memory_text);
    char byte;
    CHECK(SDL_ReadIO(output.get(), &byte, 1) == 0);
    CHECK(SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_EOF);
    CHECK(SDL_SeekIO(output.get(), 0, SDL_IO_SEEK_SET) == 0);
    CHECK(SDL_ReadIO(output.get(), &byte, 1) == 1);
    CHECK(SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY);
    puts("ok: UTF-8 strings and SDL memory stream lifetimes");

}
