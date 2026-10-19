#ifndef WIZ8_ENGINE_CODE_ANIM_REP_H
#define WIZ8_ENGINE_CODE_ANIM_REP_H

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"

/* Direction state at AnimRep+0x6e. Props record completion at either
   endpoint; GrCycle advances only the two running directions. Byte storage
   is retained because prop save data reads/writes this field directly. */
enum W8AnimationDirection : unsigned char {
    W8_ANIMATION_DIRECTION_NONE = 0,
    W8_ANIMATION_FORWARD = 1,
    W8_ANIMATION_FORWARD_COMPLETE = 2,
    W8_ANIMATION_REVERSE = 3,
    W8_ANIMATION_REVERSE_COMPLETE = 4
};

/* Proven frame methods. The serialized domain also accepts four; its
   original name remains unknown. */
enum W8AnimationFrameMethod : unsigned char {
    W8_ANIMATION_FRAME_METHOD_NONE = 0,
    W8_ANIMATION_WRAP = 1,
    W8_ANIMATION_PING_PONG = 2,
    W8_ANIMATION_RANDOM_FRAME = 3,
    W8_ANIMATION_UNKNOWN_FRAME_METHOD = 4
};

/* AdvanceAnimationFrame clamps PLAY_ONCE at an endpoint and repeats the other
   two modes. MonsterQuery reports an endpoint as complete for REPEAT; the
   NEVER_STOP mode completes only when playback stops. NEVER_STOP is named by
   StartMonsterCycle's retail assertion. Representation and file fields stay
   byte-sized; pending_behaviour uses 0xff for no queued change. */
enum W8AnimationBehaviour : unsigned char {
    W8_ANIMATION_BEHAVIOUR_NONE = 0,
    W8_ANIMATION_PLAY_ONCE = 1,
    W8_ANIMATION_REPEAT = 2,
    W8_ANIMATION_NEVER_STOP = 3,
    W8_ANIMATION_NO_PENDING_BEHAVIOUR = 0xff
};

/* The 2D instance has a different sixteen-byte block at the same class offset. */
struct W8ModelInstance2DRenderState {
    w8_ulong render_depth;
    unsigned short width;
    unsigned short height;
    short position_x;
    short position_y;
    unsigned char display_state;
    /* 0x0d: enables the pulsing glow pass over mesh.materials[0]. */
    bool glow_enabled;
};

extern float g_lod_range_default0;
extern float g_lod_range_default1;

#endif
