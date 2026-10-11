#pragma once

#include <utility>

#include "wiz8/asset_paths.h"

// Keep path roots test-local, including when a REQUIRE throws.
class ScopedAssetRoots
{
  public:
    explicit ScopedAssetRoots(w8_native::Roots roots)
        : previous_(w8_native::path_roots())
    {
        w8_native::configure_paths(std::move(roots));
    }

    ~ScopedAssetRoots() { w8_native::configure_paths(previous_); }

    ScopedAssetRoots(const ScopedAssetRoots&) = delete;
    ScopedAssetRoots& operator=(const ScopedAssetRoots&) = delete;

  private:
    w8_native::Roots previous_;
};
