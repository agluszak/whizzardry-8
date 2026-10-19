#pragma once

#include <cstdint>

#include "srClass.h"
#include "srMath.h"
#include "srVertexProcessor.h"

class srVertexPipe;

class srMaterial : public srClass {
public:
    /* Bit indices, not masks. */
    enum e_oper {
        OPER_DEPTH_CUE = 0,
        OPER_ALPHA = 1,
        OPER_SPECULAR = 2,
        OPER_DIFFUSE = 3,
        OPER_DIFFUSE_SPECULAR = 4
    };

    // RECOMP: ??0srMaterial@@QAE@XZ
    inline srMaterial() { reset(); }

    virtual srVertexProcessor::MaterialInfo getMaterialInfo();
    virtual void preProcess(srVertexPipe& pipe);
    virtual void postProcess(srVertexPipe& pipe);

protected:
    void updateParms();
    void reset();

public:
    srMaterial(const srMaterial&) = default;
    srMaterial& operator=(const srMaterial& other);
    void disable(e_oper operation);
    void enable(e_oper operation);
    glm::vec4 getEmissive() const;
    // RECOMP: ?setAmbient@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    void setAmbient(const glm::vec4& ambient) { setVector(parms.ambient, ambient); }
    // RECOMP: ?setDiffuse@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    inline void setDiffuse(const glm::vec4& diffuse) { setVector(parms.diffuse, diffuse); }
    // RECOMP: ?setEmissive@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    inline void setEmissive(const glm::vec4& emissive) { setVector(parms.emissive, emissive); }
    // RECOMP: ?setMapper@srMaterial@@QAEXPAVsrVertexProcessor@@@Z
    inline void setMapper(srVertexProcessor* mapper) { this->mapper = mapper; }
    // RECOMP: ?setOpacity@srMaterial@@QAEXN@Z
    inline void setOpacity(double opacity)
    {
        parms.diffuse.w = static_cast<float>(opacity);
        dirty = 1;
    }
    // RECOMP: ?setSpecular@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    inline void setSpecular(const glm::vec4& specular) { setVector(parms.specular, specular); }

protected:
    void setVector(glm::vec4& destination, const glm::vec4& source);

public:
    srVertexProcessor::MaterialInfo parms; /* 0x18 */
    std::uint32_t operations = 0;            /* 0x6c */
    srVertexProcessor* mapper;             /* 0x70 */
    int dirty;                             /* 0x74 */
};
