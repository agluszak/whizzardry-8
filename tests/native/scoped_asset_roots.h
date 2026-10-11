#pragma once

#include "wiz8/asset_paths.h"

// Keep path roots test-local, including when a REQUIRE throws.
class ScopedAssetRoots
{
  public:
    ScopedAssetRoots() : previous_(w8_native::path_roots()) {}

    explicit ScopedAssetRoots(w8_native::Roots roots) : ScopedAssetRoots()
    {
        configure(roots);
    }

    void configure(const w8_native::Roots& roots) { w8_native::configure_paths(roots); }

    ~ScopedAssetRoots() { w8_native::configure_paths(previous_); }

    ScopedAssetRoots(const ScopedAssetRoots&) = delete;
    ScopedAssetRoots& operator=(const ScopedAssetRoots&) = delete;

  private:
    w8_native::Roots previous_;
};
