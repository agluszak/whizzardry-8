#pragma once

#include "srVertexProcessor.h"

// VTABLE: SURRENDER 0x10076c68 srEnvironmentMapper
class srEnvironmentMapper : public srVertexProcessor {
public:
    virtual int isActive(srVertexPipe& pipe) override;
    virtual void process(srVertexPipe& pipe) override;
};
