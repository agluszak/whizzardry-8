#ifndef WIZ8_ENGINE_CODE_SPELL_EMITTER_HOST_H
#define WIZ8_ENGINE_CODE_SPELL_EMITTER_HOST_H

#include <cstdint>
#include "wiz8/engine_code/Emitter.h"
#include <vector>
#include <algorithm>

struct W8ReadLevelInfo;
class stLight;
class W8SpellVisual;

/* Engine Code\Spells.cpp's concrete five-slot emitter host.  The allocation
   in clone slot 0x004ADE70 and the constructor/destructor pair at 0x004AAD20
   and 0x004AB1C0 prove the complete 0x37c-byte extent. */
class W8SpellEmitterHost : public W8EmitterHost {
public:
    W8SpellEmitterHost();
    W8SpellEmitterHost(const W8SpellEmitterHost& other);
    virtual ~W8SpellEmitterHost() override;
    virtual W8AnimRepBase* Clone() override;
    virtual srModelInstance* SetCycleFrameLod(std::int8_t cycle, std::int8_t frame,
                                              std::int8_t lod) override;
    virtual std::uint32_t ApplyEmitterSetting(std::int8_t emitter) override;
    virtual W8AniMesh* GetEmitterAniMesh(std::int8_t emitter) override;
    std::uint8_t ReadCycleData(W8ReadLevelInfo* info, W8SpellVisual* visual, std::int32_t,
                                std::int32_t emitter_index);

    std::uint32_t value_0ac;
    std::uint32_t value_0b0;
    std::uint8_t unknown_0b4[0x24];
    W8AnimObj* emitters[28];                                       /* 0x0d8 */
    float emitter_playback_scales[28];                             /* 0x148 */
    std::vector<std::vector<stLight*>*> light_lists[28];           /* 0x1b8 */
    /* Set for spawned visuals: UpdateRepresentation then adds the
       camera-facing yaw rotation on top of the mode's orientation. */
    bool billboard;
    std::uint8_t padding_379[3];
}; /* 0x37c */

#endif
