#ifndef WIZ8_ENGINE_CODE_PATH_AI_H
#define WIZ8_ENGINE_CODE_PATH_AI_H

#include <memory>
#include <vector>
#include <cstdint>

#include "glm/fwd.hpp"
#include "surrender/srMath.h"

class srNode;
class W8AnimRepBase;
namespace wiz8 {
class File;
}

/* Runtime path data. File records are read field by field by LoadPathAI. */

enum W8AIRecordKind { W8_AI_RECORD_PATH, W8_AI_RECORD_MISSILE = 3 };

struct W8AIRecord {
    explicit W8AIRecord(W8AIRecordKind kind) : kind(kind) {}
    virtual ~W8AIRecord() = default;
    virtual std::unique_ptr<W8AIRecord> Clone() const = 0;
    const W8AIRecordKind kind;
};

struct W8PathAI : W8AIRecord {
    W8PathAI() : W8AIRecord(W8_AI_RECORD_PATH) {}
    std::unique_ptr<W8AIRecord> Clone() const override;

    /* Serialized record selector: value 2 adds a per-point scale array. */
    unsigned char version{};
    /* Normalized for interpolated movement, point units in discrete mode. */
    float position{};
    unsigned int unknown_08{}; /* serialized */
    std::vector<srVector3T<float>> nodes;
    int entry_index{}; /* level camera/path entry index, assigned at load */
    std::vector<glm::mat3> rotations;
    std::vector<srVector3T<float>> scales;
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

/* Tagged-record dispatchers: the body switches on kind and hands the
   record to the path or missile implementation. */
bool PathAIUpdate(W8AIRecord* record, signed char direction);
void PathAIResetRecord(W8AIRecord* record);
void PathAIApplyToRep(W8AIRecord* record, W8AnimRepBase* representation);
/* Places one srNode (model instance, light, camera, …) through a path. The
   body only calls srNode child/location/rotation/scale APIs; retail callers
   pass those node kinds interchangeably. */
void PathAIApply(W8PathAI* path, srNode* node); /* 0x004AA520 */
void PathAIClearOwned(W8PathAI* path);
void PathAIEnableTimedMode(W8PathAI* path);
void PathAIResetTick(W8PathAI* path);
float PathAIGetValue(W8PathAI* path);
bool PathAINextPoint(W8PathAI* path, srVector3T<float>* point);
bool PathAIIsComplete(W8PathAI* path);
void PathAISetValue(W8PathAI* path, float value);
void PathAIAdvanceNormalized(W8PathAI* path, float amount);
bool PathAITick(W8PathAI* path, signed char direction);
void PathAIPosition(W8PathAI* path, srVector3T<float>* value);
bool LoadPathAI(W8PathAI** path, wiz8::File* handle);
void PathAIAddPoint(W8PathAI& path, const srVector3T<float>& point);

void PathAIAdvanceByDistance(W8PathAI* path, float value);

#endif
