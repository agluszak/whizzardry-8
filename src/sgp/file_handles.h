#pragma once

#include "FileMan.h"
#include <wiz8/filesystem.h>

#include <memory>
#include <optional>
#include <string>

namespace sgp
{
struct ArchiveExtent
{
    INT16 library;
    UINT32 offset;
    UINT32 length;
    SGP_FILETIME modified;
};

struct OpenFile
{
    std::unique_ptr<wiz8::File> stream;
    UINT32 access;
    std::string delete_on_close;
    std::optional<ArchiveExtent> archive;

    wiz8::ReadResult read(void* data, std::size_t bytes);
    void seek(std::int64_t offset, wiz8::SeekOrigin origin);
    std::int64_t tell() const;
    std::int64_t size() const;
};

OpenFile* find_file(HWFILE handle);
HWFILE register_file(OpenFile file);
void close_files(INT16 library = -1);
} // namespace sgp
