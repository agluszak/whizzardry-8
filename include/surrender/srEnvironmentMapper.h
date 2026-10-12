#pragma once

#include <cstdint>
#include "srVertexProcessor.h"

// VTABLE: SURRENDER 0x10076c68 srEnvironmentMapper
class srEnvironmentMapper : public srVertexProcessor {
public:
    virtual std::int32_t isActive(srVertexPipe& pipe) override;
    virtual void process(srVertexPipe& pipe) override;
};

