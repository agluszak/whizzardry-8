#pragma once

/* Engine Code\GrObject.cpp owns these declarations. GrObject.cpp, Navigator.cpp
   and GrCycle.cpp have separate direct headers. */

#include <vector>
#include <memory>

class W8AnimRepBase;

/* The sound events GrObject.cpp calls `pse`/`m_plsSoundEvents`. The class
   itself belongs to Engine Code\SoundEvent.cpp, which owns its whole
   lifecycle; only the pointer is needed here. */
class W8SoundEvent;

/* PathAI.h owns the tagged AI record family this slot points at (kind 0
   W8PathAI, kind 3 W8AIMissile). */
struct W8AIRecord;

/* GrObject.cpp owns this base.  The original Item.cpp assertion
   `pMissile->GrObject::GetAI()` independently establishes the class name. */
// VTABLE: WIZ8 0x005ed090
class W8GrObject {
public:
    W8GrObject();                        /* 0x004B6900 */
    W8GrObject(const W8GrObject& other); /* 0x004B69A0 */
    W8GrObject& operator=(const W8GrObject&) = delete;
    virtual ~W8GrObject();               /* 0x004B6B60 */

    void AddSoundEvent(std::unique_ptr<W8SoundEvent> event);

public:
    unsigned char kind;                        /* 0x04 */
    int id;                                    /* 0x08 */
    W8AIRecord* m_pAI;                         /* 0x0c: GrObject::GetAI() assertion */
    std::vector<std::unique_ptr<W8SoundEvent>> sound_events;
    // Prop and Item own their representation; the base never copies it.
    W8AnimRepBase* m_pRep;

}; /* 0x18 */

/* The shared object-id counter both lifecycle callers advance. */
int AllocateGrObjectId(void);
void ResetGrObjectIdCounter(void);
