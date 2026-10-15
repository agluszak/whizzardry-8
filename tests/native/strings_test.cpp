#include <cstdio>
#include <string>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "surrender/srIO.h"

TEST_CASE("strings_test")
{

    auto output = srIO::own(SDL_IOFromDynamicMem());
    const std::string memory_text = "zażółć-雪";
    srIO::writeExact(output.get(), memory_text.data(), memory_text.size());
    REQUIRE(SDL_GetIOSize(output.get()) == Sint64(memory_text.size()));
    REQUIRE(SDL_SeekIO(output.get(), 0, SDL_IO_SEEK_SET) == 0);
    std::string copied(memory_text.size(), '\0');
    srIO::readExact(output.get(), copied.data(), copied.size());
    REQUIRE(copied == memory_text);
    char byte;
    REQUIRE(SDL_ReadIO(output.get(), &byte, 1) == 0);
    REQUIRE(SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_EOF);
    REQUIRE(SDL_SeekIO(output.get(), 0, SDL_IO_SEEK_SET) == 0);
    REQUIRE(SDL_ReadIO(output.get(), &byte, 1) == 1);
    REQUIRE(SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY);
    puts("ok: UTF-8 strings and SDL memory stream lifetimes");
}
