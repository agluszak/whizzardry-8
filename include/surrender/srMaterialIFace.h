#pragma once

#include "srClass.h"
#include "srVertexProcessor.h"

class srVertexPipe;

class SR_DLL_IMPORT srMaterialIFace : public srClass {
public:
    virtual srVertexProcessor::MaterialInfo getMaterialInfo() = 0;
    virtual void preProcess(srVertexPipe& pipe) = 0;
    virtual void postProcess(srVertexPipe& pipe) = 0;
};
