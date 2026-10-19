#pragma once

#include <cmath>
#include <span>

#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/emitter_resources.h"
#include "wiz8/platform/game_timer.h"

inline void SelectInitialEmitterLod(W8GrCycle& visual, signed char cycle)
{
    if (visual.SetCycleFrameLod(cycle, 0, 2))
        visual.m_bLOD = 2;
    else if (visual.SetCycleFrameLod(cycle, 0, 1))
        visual.m_bLOD = 1;
    else
        visual.m_bLOD = 0;
}

inline void StartEmitterCycle(W8GrCycle& visual, const W8AnimObj& animation, signed char cycle)
{
    visual.current_cycle = cycle;
    visual.rep_active = 1;
    visual.frame_direction = W8_ANIMATION_FORWARD;
    SelectInitialEmitterLod(visual, cycle);
    visual.timer = GameClockMilliseconds();
    visual.frame_method = animation.frame_method;
    visual.animation_playing = animation.animation_playing;
    visual.subcycle = 0;
}

inline bool ReadEmitterCycle(W8GrCycle& visual, std::span<EmitterCycleResources> cycles,
                             W8ReadLevelInfo* info, int cycle_index, bool select_lod)
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
    signed char lod = visual.m_bLOD;
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
    const auto timer = GameClockMilliseconds();
    destination.swap(loaded);
    visual.SetLights(destination.lightView());
    visual.current_cycle = static_cast<signed char>(emitter);
    visual.rep_active = 1;
    visual.frame_direction = W8_ANIMATION_FORWARD;
    visual.timer = timer;
    visual.animation_behaviour = destination.animation->behaviour;
    visual.frame_method = destination.animation->frame_method;
    visual.animation_playing = destination.animation->animation_playing;
    if (select_lod) {
        visual.m_bLOD = lod;
    }
    return true;
}
