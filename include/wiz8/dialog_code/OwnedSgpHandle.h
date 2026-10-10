#pragma once

#include "Button System.h"

#include <utility>

// An SGP handle owns its resource until the handle itself is destroyed.
// Buttons must be destroyed before their associated images.
struct SgpButtonCloser {
    void operator()(int handle) const { RemoveButton(handle); }
};

struct SgpButtonImageCloser {
    void operator()(int handle) const { UnloadButtonImage(handle); }
};

template <class Closer>
class OwnedSgpHandle {
public:
    OwnedSgpHandle() = default;
    explicit OwnedSgpHandle(int handle) : handle_(handle) {}
    ~OwnedSgpHandle() { reset(); }

    OwnedSgpHandle(const OwnedSgpHandle&) = delete;
    OwnedSgpHandle& operator=(const OwnedSgpHandle&) = delete;

    OwnedSgpHandle(OwnedSgpHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, -1))
    {
    }

    OwnedSgpHandle& operator=(OwnedSgpHandle&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.handle_, -1));
        }
        return *this;
    }

    int get() const { return handle_; }

    void reset(int replacement = -1)
    {
        if (handle_ == replacement) {
            return;
        }
        if (handle_ != -1) {
            Closer{}(handle_);
        }
        handle_ = replacement;
    }

private:
    int handle_ = -1;
};

using OwnedSgpButton = OwnedSgpHandle<SgpButtonCloser>;
using OwnedSgpButtonImage = OwnedSgpHandle<SgpButtonImageCloser>;
