#pragma once

#include <unordered_map>
#include <vector>

#include "glm/ext/vector_float4.hpp"

struct W8MonsterPresentation {
    glm::vec4 tint{0};
    unsigned char party_targets = 0;
    bool hovered = false;
};

// One draw's replaceable output, shared by the scene, radar and list.
struct W8TargetingPresentation {
    std::unordered_map<int, W8MonsterPresentation> monsters;
    std::vector<int> area_targets;
    bool subject_valid = false;
};

W8TargetingPresentation BuildTargetingPresentation();
void ApplyTargetingPresentation(const W8TargetingPresentation& presentation);
bool CurrentCombatSubjectIsValid();
