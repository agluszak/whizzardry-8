#pragma once

class srGERD;

// Own one model-view stack entry, including across early exits or exceptions.
class srModelViewScope {
public:
    explicit srModelViewScope(srGERD& renderer);
    ~srModelViewScope();
    srModelViewScope(srModelViewScope&& other) noexcept;
    srModelViewScope(const srModelViewScope&) = delete;
    srModelViewScope& operator=(const srModelViewScope&) = delete;

private:
    srGERD* renderer;
};
