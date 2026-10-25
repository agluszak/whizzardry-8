#pragma once

#include <cmath>
#include <span>

#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"

inline void SelectInitialEmitterLod(CyclePlayer& visual)
{
    if (visual.frameModel(0, 2))
        visual.m_bLOD = 2;
    else if (visual.frameModel(0, 1))
        visual.m_bLOD = 1;
    else
        visual.m_bLOD = 0;
}

void StartEmitterCycle(CyclePlayer& visual, Animation& animation, signed char cycle);

// Load one .mon animation into its emitter cycle (the one it names, for -1).
inline bool ReadEmitterCycle(std::span<EmitterCycleResources> cycles, LevelLoadContext& info,
                             int cycle_index)
{
    if (cycle_index < -1 || cycle_index >= static_cast<int>(cycles.size())) {
        return false;
    }
    EmitterCycleResources loaded;
    loaded.animation = std::make_unique<Animation>();
    if (!AnimObjReadFromFile(&info, loaded.animation.get(), &loaded.lights.lights)) {
        return false;
    }
    const int emitter = cycle_index == -1 ? loaded.animation->cycle : cycle_index;
    if (emitter < 0 || emitter >= static_cast<int>(cycles.size()) ||
        !std::isfinite(loaded.animation->playback_scale) || loaded.animation->playback_scale <= 0) {
        return false;
    }
    cycles[emitter].swap(loaded);
    return true;
}
