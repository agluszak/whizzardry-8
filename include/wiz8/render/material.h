#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "wiz8/math.h"
#include "wiz8/render/vertex_channels.h"

class Material {
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
    inline Material() { reset(); }

    VertexChannels::MaterialInfo getMaterialInfo();

protected:
    void updateParms();
    void reset();

public:
    Material(const Material&) = default;
    Material& operator=(const Material& other);
    void disable(e_oper operation);
    void enable(e_oper operation);
    glm::vec4 getEmissive() const;
    // RECOMP: ?setAmbient@Material@@QAEXABV?$srVector4T@M@@@Z
    void setAmbient(const glm::vec4& ambient) { setVector(parms.ambient, ambient); }
    // RECOMP: ?setDiffuse@Material@@QAEXABV?$srVector4T@M@@@Z
    inline void setDiffuse(const glm::vec4& diffuse) { setVector(parms.diffuse, diffuse); }
    // RECOMP: ?setEmissive@Material@@QAEXABV?$srVector4T@M@@@Z
    inline void setEmissive(const glm::vec4& emissive) { setVector(parms.emissive, emissive); }
    // RECOMP: ?setOpacity@Material@@QAEXN@Z
    inline void setOpacity(double opacity)
    {
        parms.diffuse.w = static_cast<float>(opacity);
        dirty = 1;
    }
    // RECOMP: ?setSpecular@Material@@QAEXABV?$srVector4T@M@@@Z
    inline void setSpecular(const glm::vec4& specular) { setVector(parms.specular, specular); }

protected:
    void setVector(glm::vec4& destination, const glm::vec4& source);

public:
    VertexChannels::MaterialInfo parms; /* 0x18 */
    std::uint32_t operations = 0;            /* 0x6c */
    enum class UvMapping { Mesh, EyeNormals, Scroll };
    UvMapping uv_mapping = UvMapping::Mesh;
    glm::vec2 uv_velocity{0};
    std::string name;
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty(); }
    //             /* 0x70 */
    int dirty;                             /* 0x74 */
};
