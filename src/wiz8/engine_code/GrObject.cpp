#include "wiz8/engine_code/GrObject.h"

#include <string>
#include <utility>

#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/SoundEvent.h"

// FUNCTION: WIZ8 0x004b6900
W8GrObject::W8GrObject() : kind(0), id(-1), m_pAI(nullptr), m_pRep(nullptr) {}

// FUNCTION: WIZ8 0x004b69a0
W8GrObject::W8GrObject(const W8GrObject& other)
    : kind(other.kind), id(other.id), m_pAI(nullptr), m_pRep(nullptr)
{
    sound_events.reserve(other.sound_events.size());
    for (const auto& event : other.sound_events) {
        // Clones start with fresh playback state, as in the original factory path.
        sound_events.push_back(std::make_unique<W8SoundEvent>(event->kind, event->cycle, event->frame,
                                               event->subcycle, event->wave_name.c_str(), false));
    }
    m_pAI = other.m_pAI ? other.m_pAI->Clone() : nullptr;
}

// FUNCTION: WIZ8 0x004b6b60
W8GrObject::~W8GrObject() = default;

// FUNCTION: WIZ8 0x004b6bd0
void W8GrObject::AddSoundEvent(std::unique_ptr<W8SoundEvent> event)
{
    if (event) sound_events.push_back(std::move(event));
}

// GLOBAL: WIZ8 0x0060dfac
static int g_gr_object_id_counter = 1;

// FUNCTION: WIZ8 0x004B6D10
int AllocateGrObjectId(void)
{
    ++g_gr_object_id_counter;
    return g_gr_object_id_counter;
}

// FUNCTION: WIZ8 0x004B6D20
void ResetGrObjectIdCounter(void)
{
    g_gr_object_id_counter = 1;
}
