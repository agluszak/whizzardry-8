#pragma once

#include <cstddef>
#include <cstdint>

namespace w8_native
{
int open_shared_file(const char* path, int flags, int permissions);
void* map_file_readonly(int fd, uint64_t offset, size_t size);
bool unmap_file(const void* view);
bool copy_file_times(int input, int output);
uint64_t available_physical_memory();
uint64_t committed_memory();
}
