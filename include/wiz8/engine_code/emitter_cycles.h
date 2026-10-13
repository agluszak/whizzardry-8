#pragma once

#include <cmath>
#include <span>
#include "wiz8/engine_code/emitter_resources.h"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/game_timer.h"
#include "surrender/srClock.h"

inline void SelectInitialEmitterLod(W8EmitterHost& host, signed char cycle)
{
    if (host.SetCycleFrameLod(cycle, 0, 2)) host.m_bLOD = 2;
    else if (host.SetCycleFrameLod(cycle, 0, 1)) host.m_bLOD = 1;
    else host.m_bLOD = 0;
}

inline void StartEmitterCycle(W8EmitterHost& host, const W8AnimObj& animation, signed char cycle)
{
    host.current_cycle = cycle;
    host.active = 1;
    host.frame_direction = W8_ANIMATION_FORWARD;
    SelectInitialEmitterLod(host, cycle);
    host.timer = g_shared_timer_base->milliseconds();
    host.frame_method = animation.frame_method;
    host.animation_playing = animation.animation_playing;
    host.subcycle = 0;
}

inline bool ReadEmitterCycle(W8EmitterHost& host, std::span<EmitterCycleResources> cycles,
                             W8ReadLevelInfo* info, W8GrCycle& visual, int cycle_index,
                             bool select_lod)
{
    if (cycle_index < -1 || cycle_index >= static_cast<int>(cycles.size())) {
        return false;
    }
    EmitterCycleResources loaded;
    loaded.animation = std::make_unique<W8AnimObj>();
    if (!AnimObjReadFromFile(info, loaded.animation.get(), 1, &loaded.lights.lights, 1)) {
        return false;
    }
    const int emitter = cycle_index == -1 ? loaded.animation->cycle : cycle_index;
    if (emitter < 0 || emitter >= static_cast<int>(cycles.size()) ||
        !std::isfinite(loaded.animation->playback_scale) || loaded.animation->playback_scale <= 0) {
        return false;
    }

    // Resolve any deferred frame resources before publishing the new cycle.
    signed char lod = host.m_bLOD;
    if (select_lod) {
        if (AnimObjDispatch(loaded.animation.get(), 2, 0)) {
            lod = 2;
        } else if (AnimObjDispatch(loaded.animation.get(), 1, 0)) {
            lod = 1;
        } else {
            lod = 0;
        }
    }

    // No allocations from here through installation. The retired resources
    // remain owned by loaded and release animation before lights on return.
    auto& destination = cycles[emitter];
    const auto timer = g_shared_timer_base->milliseconds();
    destination.swap(loaded);
    visual.SetLights(destination.lightView());
    host.current_cycle = static_cast<signed char>(emitter);
    host.active = 1;
    host.frame_direction = W8_ANIMATION_FORWARD;
    host.timer = timer;
    host.animation_behaviour = destination.animation->behaviour;
    host.frame_method = destination.animation->frame_method;
    host.animation_playing = destination.animation->animation_playing;
    if (select_lod) {
        host.m_bLOD = lod;
    }
    return true;
}
