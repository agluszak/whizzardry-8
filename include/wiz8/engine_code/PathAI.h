#ifndef WIZ8_ENGINE_CODE_PATH_AI_H
#define WIZ8_ENGINE_CODE_PATH_AI_H

#include "wiz8/filesystem.h"

#include "wiz8/geometry.h"
#include <vector>
#include <algorithm>

class srNode;
class W8AnimRepBase;

/* Runtime path data. File records are read field by field by LoadPathAI. */

/* The tagged AI record family W8GrObject stores at +0x0c: the leading byte
   selects the record type, kind 0 being W8PathAI and kind 3 being
   Missile.cpp's W8AIMissile. The dispatcher entry points below take this base;
   operations on record-specific tails keep the concrete type. */
/* The discriminator is a byte in the packed AI records. Kind 1 has no
   recovered payload; only the path and missile domains are named. */
enum W8AIRecordKind : unsigned char { W8_AI_RECORD_PATH = 0, W8_AI_RECORD_MISSILE = 3 };
W8_ABI_ASSERT(sizeof(W8AIRecordKind) == 1, "W8AIRecordKind_size");

struct W8AIRecord {
    W8AIRecordKind kind; /* 0x00 */
};

struct W8PathAI : W8AIRecord {
    /* Serialized record selector: value 2 adds a per-point scale array. */
    unsigned char version;
    /* Normalized for interpolated movement, point units in discrete mode. */
    float position;
    unsigned int unknown_08;             /* serialized */
    std::vector<srVector3T<float>> nodes;
    int entry_index;                     /* level camera/path entry index, assigned at load */
    std::vector<glm::mat3> rotations;
    std::vector<srVector3T<float>> scales;
    bool discrete_mode;
    unsigned int point_index;
    float interpolation_fraction;
    uint64_t last_update_tick;
    float speed;
    float distance_travelled;
    float total_length;
    bool looping;
    bool step_by_node;
    bool animated;
    /* When set the emitter target is pitched upright (rotateX pi/2); camera
       paths raise it. */
    bool upright;
    bool timed;
};


/* Tagged-record dispatchers: the body switches on kind and hands the
   record to the path or missile implementation. */
bool PathAIUpdate(W8AIRecord* record, signed char direction);
void PathAIResetRecord(W8PathAI* path);
W8AIRecordKind GetAIRecordKind(const W8AIRecord* record);
void PathAIApplyToRep(W8AIRecord* record, W8AnimRepBase* representation);
/* Places one srNode (model instance, light, camera, …) through a path. The
   body only calls srNode child/location/rotation/scale APIs; retail callers
   pass those node kinds interchangeably. */
void PathAIApply(W8PathAI* path, srNode* node); /* 0x004AA520 */
float PathAIGetScale(W8PathAI* path);           /* 0x004AAA50 */
void DestroyAIRecord(W8AIRecord* record);
void DestroyPathAI(W8PathAI* path);
void PathAIClearOwned(W8PathAI* path);
void PathAISetAnimated(W8PathAI* path, bool value);
void PathAIEnableTimedMode(W8PathAI* path);
void PathAIResetTick(W8PathAI* path);
float PathAIGetValue(W8PathAI* path);
bool PathAINextPoint(W8PathAI* path, srVector3T<float>* point);
bool PathAIIsComplete(W8PathAI* path);
unsigned int PathAIEntryCount(W8PathAI* path);
void PathAISetValue(W8PathAI* path, float value);
void PathAIAdvanceNormalized(W8PathAI* path, float amount);
bool PathAITick(W8PathAI* path, signed char direction);
void PathAIPosition(W8PathAI* path, srVector3T<float>* value);
void PathAISetLooping(W8PathAI* path, bool value);
void PathAISetScale(W8PathAI* path, float value);
void PathAISetDiscreteMode(W8PathAI* path, bool value);
bool LoadPathAI(W8PathAI** path, wiz8::File* handle);
void PathAIAddPoint(W8PathAI& path, const srVector3T<float>& point);

W8PathAI* CreatePathAI();

/* Release a path only when the AI record tag identifies a path. */
void DestroyOwnedPathAI(W8PathAI* path);

/* The dispatcher every AI-record copy goes through; the kind tag, not the
   declaration, decides which concrete record it clones. */
W8AIRecord* CloneAIRecord(const W8AIRecord* record);
W8PathAI* ClonePathAI(const W8PathAI* path);

void PathAIAdvanceByDistance(W8PathAI* path, float value);

#endif
