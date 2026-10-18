#pragma once

/* Engine Code\GrObject.cpp owns these declarations. GrObject.cpp, Navigator.cpp
   and GrCycle.cpp have separate direct headers. */

#include <memory>
#include <vector>

class W8SoundEvent;
struct W8PathAI;

/* GrObject.cpp owns this base. */
class W8GrObject {
public:
    W8GrObject();                        /* 0x004B6900 */
    W8GrObject(const W8GrObject& other); /* 0x004B69A0 */
    W8GrObject& operator=(const W8GrObject&) = delete;
    virtual ~W8GrObject();               /* 0x004B6B60 */

    void AddSoundEvent(std::unique_ptr<W8SoundEvent> event);

public:
    unsigned char kind;
    int id;
    std::unique_ptr<W8PathAI> m_pAI;
    std::vector<std::unique_ptr<W8SoundEvent>> sound_events;
};

/* The shared object-id counter both lifecycle callers advance. */
int AllocateGrObjectId(void);
void ResetGrObjectIdCounter(void);
