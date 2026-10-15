#pragma once

#include "srMaterialIFace.h"
#include "srMath.h"
#include "srTypeRegistry.h"
#include "srVertexPipe.h"

// VTABLE: SURRENDER 0x1007556C
// class srClassSupport<srMaterial, srMaterialIFace, 8720>

// VTABLE: SURRENDER 0x10075538 srMaterial
class srMaterial : public srClassSupport<srMaterial, srMaterialIFace, 0x2210> {
public:
    /* Bit indices, not masks. */
    enum e_oper {
        OPER_DEPTH_CUE = 0,
        OPER_ALPHA = 1,
        OPER_SPECULAR = 2,
        OPER_DIFFUSE = 3,
        OPER_DIFFUSE_SPECULAR = 4
    };

    // FUNCTION: SURRENDER 0x10034700 SYMBOL
    // RECOMP: ??0srMaterial@@QAE@XZ
    inline srMaterial() { reset(); }

    static const char* sGetClassName();

    virtual void dump(std::ostream& stream) override;
    virtual void verify(srRuntimeClass::e_verify mode) override;

    virtual srVertexProcessor::MaterialInfo getMaterialInfo() override;
    virtual void preProcess(srVertexPipe& pipe) override;
    virtual void postProcess(srVertexPipe& pipe) override;

protected:
    virtual void updateParms();
    virtual void reset();

public:
    srMaterial(const srMaterial&) = default;
    srMaterial& operator=(const srMaterial& other);
    void disable(e_oper operation);
    void enable(e_oper operation);
    glm::vec4 getAmbient() const;
    glm::vec4 getDiffuse() const;
    glm::vec4 getEmissive() const;
    srVertexProcessor* getMapper() const;
    float getOpacity() const;
    float getShininess() const;
    glm::vec4 getSpecular() const;
    float getTranslucency() const;
    int isEnabled(e_oper operation) const;
    // FUNCTION: SURRENDER 0x10034930 SYMBOL
    // RECOMP: ?setAmbient@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    void setAmbient(const glm::vec4& ambient) { setVector(parms.ambient, ambient); }
    void setAmbientAndDiffuse(const glm::vec4& color);
    // FUNCTION: SURRENDER 0x10034960 SYMBOL
    // RECOMP: ?setDiffuse@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    inline void setDiffuse(const glm::vec4& diffuse) { setVector(parms.diffuse, diffuse); }
    // FUNCTION: SURRENDER 0x10034990 SYMBOL
    // RECOMP: ?setEmissive@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    inline void setEmissive(const glm::vec4& emissive) { setVector(parms.emissive, emissive); }
    // FUNCTION: SURRENDER 0x10034B40 SYMBOL
    // RECOMP: ?setMapper@srMaterial@@QAEXPAVsrVertexProcessor@@@Z
    inline void setMapper(srVertexProcessor* mapper) { this->mapper = mapper; }
    // FUNCTION: SURRENDER 0x10034A80 SYMBOL
    // RECOMP: ?setOpacity@srMaterial@@QAEXN@Z
    inline void setOpacity(double opacity)
    {
        parms.diffuse.w = static_cast<float>(opacity);
        dirty = 1;
    }
    void setShininess(double shininess);
    // FUNCTION: SURRENDER 0x100349C0 SYMBOL
    // RECOMP: ?setSpecular@srMaterial@@QAEXABV?$srVector4T@M@@@Z
    inline void setSpecular(const glm::vec4& specular) { setVector(parms.specular, specular); }
    void setTranslucency(double translucency);

protected:
    void setVector(glm::vec4& destination, const glm::vec4& source);

public:
    srVertexProcessor::MaterialInfo parms; /* 0x18 */
    srFlags<e_oper> operations;            /* 0x6c */
    srVertexProcessor* mapper;             /* 0x70 */
    int dirty;                             /* 0x74 */
};
