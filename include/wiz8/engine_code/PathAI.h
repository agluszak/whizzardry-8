#ifndef WIZ8_ENGINE_CODE_PATH_AI_H
#define WIZ8_ENGINE_CODE_PATH_AI_H

#include <cstdint>
#include <memory>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"

class srNode;
class W8GrCycle;
namespace wiz8 {
class File;
}

/* Runtime path data. File records are read field by field by LoadPathAI. */

struct W8PathAI {
    W8PathAI() = default;
    W8PathAI(const W8PathAI&) = default;
    W8PathAI& operator=(const W8PathAI&) = delete;
    ~W8PathAI() = default;
    std::unique_ptr<W8PathAI> Clone() const;

    /* Serialized record selector: value 2 adds a per-point scale array. */
    unsigned char version{};
    /* Normalized for interpolated movement, point units in discrete mode. */
    float position{};
    unsigned int unknown_08{};             /* serialized */
    std::vector<glm::vec3> nodes;
    int entry_index{};                     /* level camera/path entry index, assigned at load */
    std::vector<glm::mat3> rotations;
    std::vector<glm::vec3> scales;
    bool discrete_mode{};
    unsigned int point_index{};
    float interpolation_fraction{};
    uint64_t last_update_tick{};
    float speed{};
    float distance_travelled{};
    float total_length{};
    bool looping{};
    bool step_by_node{};
    bool animated{};
    /* When set the emitter target is pitched upright (rotateX pi/2); camera
       paths raise it. */
    bool upright{};
    bool timed{};
};

bool PathAIUpdate(W8PathAI* record, signed char direction);
void PathAIResetRecord(W8PathAI* record);
void PathAIApplyToRep(W8PathAI* record, W8GrCycle* representation);
/* Places one srNode (model instance, light, camera, …) through a path. The
   body only calls srNode child/location/rotation/scale APIs; retail callers
   pass those node kinds interchangeably. */
void PathAIApply(W8PathAI* path, srNode* node); /* 0x004AA520 */
void PathAIClearOwned(W8PathAI* path);
void PathAIEnableTimedMode(W8PathAI* path);
void PathAIResetTick(W8PathAI* path);
float PathAIGetValue(W8PathAI* path);
bool PathAINextPoint(W8PathAI* path, glm::vec3* point);
bool PathAIIsComplete(W8PathAI* path);
void PathAISetValue(W8PathAI* path, float value);
void PathAIAdvanceNormalized(W8PathAI* path, float amount);
bool PathAITick(W8PathAI* path, signed char direction);
void PathAIPosition(W8PathAI* path, glm::vec3* value);
std::unique_ptr<W8PathAI> LoadPathAI(wiz8::File& file);
void PathAIAddPoint(W8PathAI& path, const glm::vec3& point);

void PathAIAdvanceByDistance(W8PathAI* path, float value);

#endif
