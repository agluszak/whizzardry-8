#pragma once

#include "srTypeRegistry.h"
#include "srVertexProcessor.h"

class srVertexPipe;

// VTABLE: SURRENDER 0x100755A8
// class srClassSupport<srMaterialIFace, srClass, 1, 8704>

class SR_DLL_IMPORT srMaterialIFace
    : public srClassSupport<srMaterialIFace, srClass, true, 0x2200> {
public:
    static const char* sGetClassName();



    virtual void getMaterialInfo(srVertexProcessor::MaterialInfo& info) = 0;
    virtual void preProcess(srVertexPipe& pipe) = 0;
    virtual void postProcess(srVertexPipe& pipe) = 0;
};
