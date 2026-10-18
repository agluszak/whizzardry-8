#pragma once

#include <cstddef>
#include <utility>
#include <vector>

// Replaces a global record database for one test and restores it afterwards.
template<class T> class ScopedRecords {
public:
    ScopedRecords(std::vector<T>& target, std::size_t count)
        : target_(target), saved_(std::exchange(target, std::vector<T>(count)))
    {
    }
    ScopedRecords(const ScopedRecords&) = delete;
    ScopedRecords& operator=(const ScopedRecords&) = delete;
    ScopedRecords(ScopedRecords&&) = delete;
    ScopedRecords& operator=(ScopedRecords&&) = delete;
    ~ScopedRecords() { target_ = std::move(saved_); }

private:
    std::vector<T>& target_;
    std::vector<T> saved_;
};
