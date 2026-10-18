#include "wiz8/engine_code/AnimRep.hpp"

#include "glm/ext/vector_float3.hpp"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/game_timer.h"

float g_lod_range_default0 = 3500.0f;

float g_lod_range_default1 = 8500.0f;

W8AnimRepBase::W8AnimRepBase()
{
    location = glm::vec3(0);
    local_location = glm::vec3(0);
    parent_location = glm::vec3(0);
    rotation = glm::mat3(1);
    highlight_colour = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    instance_scale = 1.0f;
    flag = false;
    apply_instance_scale = false;
}

void W8AnimRep::SetFrameMethod(signed char method)
{
    if (method < 1 || method > 4) {
        Fail("bFrameMethod >= DIR_FIRST && bFrameMethod <= DIR_LAST");
    }
    frame_method = method;
}

/* The root copy preserves five aggregate values, but deliberately resets its
   runtime scale and flags instead of copying them.  Bytes 0x62 and 0x63 are
   not touched by the canonical constructor. */
W8AnimRepBase::W8AnimRepBase(const W8AnimRepBase& other)
    : location(other.location), local_location(other.local_location),
      parent_location(other.parent_location), rotation(other.rotation),
      highlight_colour(other.highlight_colour), instance_scale(1.0f), flag(false),
      apply_instance_scale(0)
{
}

/* Store the representation's local location, then rebuild the world location
   from its parent location.  The three floating-point additions establish
   these as vectors rather than opaque twelve-byte values. */
void W8AnimRepBase::SetLocation(const glm::vec3* location)
{
    local_location = *location;
    this->location = parent_location + local_location;
}

glm::vec3 W8AnimRepBase::GetLocation() const
{
    return location;
}

glm::vec3 W8AnimRepBase::GetLocalLocation() const
{
    return local_location;
}

void W8AnimRepBase::SetRotation(const glm::mat3* rotation)
{
    this->rotation = *rotation;
}

glm::mat3 W8AnimRepBase::GetRotation() const
{
    return rotation;
}

W8AnimRep::W8AnimRep()
    : subcycle(0), pending_subcycle(0xffff), timer(0), active(0), animation_playing(0),
      frame_direction(W8_ANIMATION_DIRECTION_NONE), frame_method(0), animation_behaviour(0),
      pending_behaviour(-1), bounds_min(0.0f, 0.0f, 0.0f), bounds_max(0.0f, 0.0f, 0.0f),
      bounds_extent(0), value(0), first_frame(0xff), last_frame(0xff)
{
    timer = GameClockMilliseconds();
}

/* Copies persistent animation state, then timestamps the new representation. */
W8AnimRep::W8AnimRep(const W8AnimRep& other)
    : W8AnimRepBase(other), subcycle(other.subcycle), pending_subcycle(other.pending_subcycle),
      timer(other.timer), active(other.active), animation_playing(other.animation_playing),
      frame_direction(other.frame_direction), frame_method(other.frame_method),
      animation_behaviour(other.animation_behaviour), pending_behaviour(other.pending_behaviour),
      bounds_min(other.bounds_min), bounds_max(other.bounds_max),
      bounds_extent(other.bounds_extent), value(other.value), first_frame(other.first_frame),
      last_frame(other.last_frame)
{
    timer = GameClockMilliseconds();
}

/* The abstract emitter host copies its stable settings, but starts with no
   selected emitter and the canonical 00 00 FF FF transient byte pattern. */
W8EmitterHost::W8EmitterHost(const W8EmitterHost& other)
    : W8AnimRep(other), m_bLOD(other.m_bLOD), lod_near(other.lod_near), lod_far(other.lod_far),
      current_cycle(0), current_subcycle(0), forced_subcycle(-1), pending_cycle(-1),
      animation_radius(other.animation_radius)
{
}

W8EmitterHost::W8EmitterHost()
    : m_bLOD(0), lod_near(g_lod_range_default0), lod_far(g_lod_range_default1), current_cycle(0),
      current_subcycle(0), forced_subcycle(-1), pending_cycle(-1), animation_radius(0)
{
}
