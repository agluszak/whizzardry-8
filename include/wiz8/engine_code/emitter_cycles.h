#pragma once

#include <cstdint>
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/game_timer.h"
#include "surrender/srClock.h"
#include "wiz8/engine_code/stLight.hpp"
#include <iterator>
#include <source_location>

template<class Host>
void ResetEmitterAnimations(Host& host)
{
    for (std::size_t emitter = 0; emitter < std::size(host.emitters); ++emitter) {
        host.emitters[emitter] = nullptr;
        host.emitter_playback_scales[emitter] = 15.0f;
    }
}

template<class Host>
void CloneEmitterAnimations(Host& host, const Host& source)
{
    for (std::size_t emitter = 0; emitter < std::size(host.emitters); ++emitter) {
        host.emitters[emitter] = source.emitters[emitter]
            ? new W8AnimObj(*source.emitters[emitter]) : nullptr;
        host.emitter_playback_scales[emitter] = source.emitters[emitter]
            ? source.emitter_playback_scales[emitter] : 15.0f;
    }
}

template<class Host>
void CloneEmitterLightLists(Host& host, const Host& source,
                           std::source_location caller = std::source_location::current())
{
    for (std::size_t emitter = 0; emitter < std::size(host.light_lists); ++emitter) {
        for (auto* lights : source.light_lists[emitter]) {
            host.light_lists[emitter].push_back(
                CloneAnimationLightList(lights, caller.file_name(), caller.line(), caller.line()));
        }
    }
}

inline void SelectInitialEmitterLod(W8EmitterHost& host, std::int8_t cycle)
{
    if (host.SetCycleFrameLod(cycle, 0, 2)) host.m_bLOD = 2;
    else if (host.SetCycleFrameLod(cycle, 0, 1)) host.m_bLOD = 1;
    else host.m_bLOD = 0;
}

template<class Host>
void StartEmitterCycle(Host& host, std::int8_t cycle)
{
    host.current_cycle = cycle;
    W8AnimObj* animation = host.emitters[cycle];
    host.active = 1;
    host.frame_direction = W8_ANIMATION_FORWARD;
    SelectInitialEmitterLod(host, cycle);
    host.timer = g_shared_timer_base->milliseconds();
    host.frame_method = animation->frame_method;
    host.animation_playing = animation->animation_playing;
    host.subcycle = 0;
}

// Missile and spell hosts own different-sized emitter tables, but load the same cycle state.
template<class Host, class Visual>
bool ReadEmitterCycle(Host& host, W8ReadLevelInfo* info, Visual& visual,
                      std::int32_t cycle_index, bool select_lod)
{
    auto* lights = new std::vector<stLight*>;
    auto* animation = new W8AnimObj;
    const bool success = AnimObjReadFromFile(info, animation, 1, lights, 1);
    std::int8_t emitter = static_cast<std::int8_t>(animation->cycle);
    if (lights->empty()) {
        delete lights;
        lights = nullptr;
    } else {
        visual.SetLights(lights);
    }
    host.light_lists[cycle_index].push_back(lights);
    if (cycle_index != -1) {
        emitter = static_cast<std::int8_t>(cycle_index);
        host.current_cycle = emitter;
    }
    host.emitter_playback_scales[emitter] = animation->playback_scale;
    host.active = 1;
    host.frame_direction = W8_ANIMATION_FORWARD;
    host.timer = g_shared_timer_base->milliseconds();
    host.animation_behaviour = animation->behaviour;
    host.frame_method = animation->frame_method;
    host.animation_playing = animation->animation_playing;
    host.emitters[emitter] = animation;
    if (select_lod) SelectInitialEmitterLod(host, host.current_cycle);
    return success;
}
