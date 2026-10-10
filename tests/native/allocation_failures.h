#pragma once

namespace allocation_failures
{
// Fail one C++ allocation after the given number of successful allocations.
extern thread_local int after;
extern thread_local bool mallocFails;
extern thread_local bool reallocFails;
}
