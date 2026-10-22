/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "wiz8/platform/random.h"

#include <cstdint>
#include <ctime>

#include "wiz8/rules/random.h"

Rng& PresentationRng()
{
    static Rng stream(static_cast<std::uint32_t>(std::time(nullptr)));
    return stream;
}
