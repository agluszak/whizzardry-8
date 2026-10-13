#pragma once
#include "file_operations.h"
#include <cstddef>
#include <stdexcept>

struct FileFaults {
    inline static std::size_t operations = 0, fail_at = 0;
    inline static bool fired = false;
    wiz8::testing::FileOperationHook previous;
    static void hook(wiz8::testing::FileOperation) {
        if (++operations == fail_at) {
            fired = true;
            throw std::runtime_error("injected file failure");
        }
    }
    explicit FileFaults(std::size_t fail = 0) {
        operations = 0; fail_at = fail; fired = false;
        previous = wiz8::testing::set_file_operation_hook(hook);
    }
    ~FileFaults() { wiz8::testing::set_file_operation_hook(previous); }
    FileFaults(const FileFaults&) = default;
    FileFaults& operator=(const FileFaults&) = default;
};
