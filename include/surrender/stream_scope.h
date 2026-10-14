#pragma once

#include <ios>

// Dump fields are left-aligned; restore the caller's native formatting on exit.
class srDumpStreamScope {
public:
    explicit srDumpStreamScope(std::ios_base& stream) : stream_(stream), flags_(stream.flags())
    {
        stream_.setf(std::ios_base::left, std::ios_base::adjustfield);
    }
    srDumpStreamScope(const srDumpStreamScope&) = delete;
    srDumpStreamScope& operator=(const srDumpStreamScope&) = delete;
    ~srDumpStreamScope() { stream_.flags(flags_); }

private:
    std::ios_base& stream_;
    std::ios_base::fmtflags flags_;
};
