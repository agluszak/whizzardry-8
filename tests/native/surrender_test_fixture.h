#pragma once

#include <catch2/catch_test_macros.hpp>
#include "surrender/srCore.h"

struct SurRenderTestFixture
{
    SurRenderTestFixture() { REQUIRE(srInit()); }
    ~SurRenderTestFixture() { srExit(); }
    SurRenderTestFixture(const SurRenderTestFixture&) = default;
    SurRenderTestFixture& operator=(const SurRenderTestFixture&) = default;
};
