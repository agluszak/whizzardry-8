#include "wiz8/engine_code/GrObject.h"

#include <memory>
#include <string>
#include <utility>

#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/SoundEvent.h"

W8GrObject::W8GrObject() : kind(0), id(-1), m_pAI(nullptr) {}

W8GrObject::W8GrObject(const W8GrObject& other) : kind(other.kind), id(other.id), m_pAI(nullptr)
{
    sound_events.reserve(other.sound_events.size());
    for (const auto& event : other.sound_events) {
        // Clones start with fresh playback state, as in the original factory path.
        sound_events.push_back(std::make_unique<W8SoundEvent>(event->kind, event->cycle,
                                                              event->frame, event->subcycle,
                                                              event->wave_name.c_str(), false));
    }
    m_pAI = other.m_pAI ? other.m_pAI->Clone() : nullptr;
}

W8GrObject::~W8GrObject() = default;

void W8GrObject::AddSoundEvent(std::unique_ptr<W8SoundEvent> event)
{
    if (event) sound_events.push_back(std::move(event));
}

static int g_gr_object_id_counter = 1;

int AllocateGrObjectId(void)
{
    ++g_gr_object_id_counter;
    return g_gr_object_id_counter;
}

void ResetGrObjectIdCounter(void)
{
    g_gr_object_id_counter = 1;
}
