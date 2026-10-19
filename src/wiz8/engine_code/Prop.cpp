#include "wiz8/engine_code/Prop.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "application.h"
#include "compat/native.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "layouts/world.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srDebug.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/unicode.h"
#include "wiz8/xstatus.h"

/* Reset before the world Prop update; set when a Prop whose animation
   behaviour is 1 rebuilds its pathing geometry.  GameData keeps the world
   updating while it is set. */
bool g_animated_prop_present;

static int g_legacy_prop_save_id = 1;

int AllocateLegacyPropSaveId()
{
    return ++g_legacy_prop_save_id;
}

void ResetLegacyPropSaveIds()
{
    g_legacy_prop_save_id = 1;
}

W8Prop::W8Prop()
{
    legacy_save_id = AllocateLegacyPropSaveId();
    timer = GameClockMilliseconds();
    trigger = 0;
    flags = 0;
    anim_frame_fraction = 0;
    animation_position = glm::vec3(0);
    previous_animation_position = glm::vec3(0);
}

void W8Prop::SetFrameMethod(W8AnimationFrameMethod method)
{
    if (method < 1 || method > 4) {
        Fail("bFrameMethod >= DIR_FIRST && bFrameMethod <= DIR_LAST");
    }
    frame_method = method;
}

/* Visit every prop attached to the world and activate those whose companion
   object can be resolved and whose owned GDProp exists. */
void UpdateWorldProps(W8World* world)
{
    std::size_t count;
    int index;

    if (world == 0) {
        Fail("pWorld");
    }
    count = world->plsProps.size();
    for (index = 0; index < static_cast<int>(count); ++index) {
        W8Prop* prop = GetWorldProp(world, index);
        Trigger* trigger;

        if (prop != 0 && (trigger = FindTriggerForProp(world, prop)) != 0 && prop->m_gd_prop != 0) {
            prop->flags |= W8_PROP_GD_TRIGGER_BOUND;
            prop->m_gd_prop->BindTrigger(trigger, WorldPathing(*world));
        }
    }
}

W8Prop::~W8Prop() = default;

W8Prop* FindPropByName(W8World* world, std::string_view name)
{
    int index;

    if (world != 0 && !name.empty()) {
        const auto count = world->plsProps.size();

        for (index = 0; index < static_cast<int>(count); ++index) {
            W8Prop* prop = GetWorldProp(world, index);
            if (!prop->name.empty() && CompareTextIgnoreAsciiCase(prop->name, name) == 0) {
                return prop;
            }
        }
    }
    return 0;
}

glm::vec3 W8Prop::GetPosition() const
{
    if (this->Animation()->path_lists == 1) {
        return animation_position;
    }
    return this->GetLocation();
}

void W8Prop::SetAnimationSpeed(float speed)
{
    if (speed > 0.0f) {
        this->animation_speed = speed;
        {
            animation_timer.SetDuration(1.0f / this->animation_speed);
        }
    }
}

/* Four accessors reaching through the owned member at 0x14. */
unsigned char W8Prop::GetActivationState()
{
    return this->active;
}

void W8Prop::SetPendingAnimationSubcycle(char value)
{
    this->pending_subcycle = value;
}

/* Whether the animation bounces between its first and last frames. */
bool W8Prop::IsAnimationPingPong()
{
    return this->frame_method == W8_ANIMATION_PING_PONG;
}

/* Switch between forward and reverse playback, preserving the retail
   forward fallback for every state other than running forward. */
void W8Prop::ReverseAnimationDirection()
{
    this->frame_direction =
        this->frame_direction == W8_ANIMATION_FORWARD ? W8_ANIMATION_REVERSE : W8_ANIMATION_FORWARD;
}

/* The prop's own trigger at 0x18. */
Trigger* W8Prop::GetTrigger()
{
    return this->trigger;
}

/* The trigger that owns this prop's GDProp, once the flag that says the
   GDProp is attached is up. */
Trigger* W8Prop::GetGDPropOwnerTrigger()
{
    if ((this->flags & W8_PROP_GD_TRIGGER_BOUND) != 0 && this->m_gd_prop != 0) {
        return this->m_gd_prop->m_trigger;
    }
    return 0;
}

/* Store activation state, restarting the animation clock whenever the old
   state is inactive. */
// FUNCTION: WIZ8 0x0044d4b0
void W8Prop::SetActivationState(bool active)
{
    if (this->active == 0) {
        animation_timer.Restart();
    }
    this->active = active;
}

std::optional<glm::vec3> W8Prop::GetCenterPosition()
{
    return GetFrameBounds().transform(
        [](const auto& bounds) { return glm::vec3(bounds.center()); });
}

static Trigger* g_selected_prop_trigger;
static int g_selected_prop_index = -1;

/* Resolve the renderer's picked model instance back to the prop and trigger
   that own it. A pick is accepted only while the prop is active, the trigger
   permits selection, and the prop centre lies inside the trigger's distance
   interval. */
bool ResolvePickedProp()
{
    srModelInstance* selected;
    glm::vec3 camera_position;
    std::size_t prop_count;
    int prop_index;
    bool valid;

    g_selected_prop_trigger = 0;
    g_selected_prop_index = -1;
    selected = g_current_model_instance;
    if (selected == 0) {
        return false;
    }

    valid = true;
    camera_position = cameraLocation();
    prop_count = GetWorld()->plsProps.size();
    for (prop_index = 0; prop_index < static_cast<int>(prop_count); ++prop_index) {
        W8Prop* prop;
        W8Prop* representation;
        srModelInstance* instance;
        int instance_index;

        if (!valid) {
            return false;
        }
        if (g_selected_prop_trigger != 0) {
            return valid;
        }
        prop = GetWorldProp(GetWorld(), prop_index);
        representation = prop;
        if (representation->active == 0) {
            continue;
        }

        instance = 0;
        if (representation->Animation()->path_lists == 1) {
            int count = static_cast<int>(AnimObjListCount(representation->Animation(), 2));
            for (instance_index = 0; instance_index < count; ++instance_index) {
                instance = AnimObjDispatchList(representation->Animation(), 2,
                                               static_cast<signed char>(instance_index));
                if (g_current_model_instance == instance) {
                    break;
                }
            }
            if (instance_index == count) {
                continue;
            }
        } else {
            instance = representation->ToggleAnimation(representation->subcycle);
            if (g_current_model_instance != instance) {
                continue;
            }
        }

        {
            Trigger* trigger = prop->trigger;
            g_selected_prop_trigger = trigger;
            if (trigger != 0 && (trigger->flags & W8_TRIGGER_ENABLED) != 0 &&
                ((trigger->flags & W8_TRIGGER_ONCE) == 0 ||
                 (trigger->flags & W8_TRIGGER_FIRED) == 0) &&
                (g_combat_inactive ||
                 (trigger->doorData() != nullptr && !trigger->doorData()->open)) &&
                representation->active != 0) {
                if (const auto bounds = prop->GetFrameBounds()) {
                    const float distance =
                        glm::length((glm::vec3(bounds->center()) - camera_position));
                    if (trigger->range_minimum <= distance) {
                        g_selected_prop_index = prop_index;
                        if (distance <= trigger->range_maximum) continue;
                    }
                }
            }
            valid = false;
            g_current_model_instance = 0;
            g_selected_prop_trigger = 0;
            g_selected_prop_index = -1;
        }
    }
    return valid;
}

/* Start or stop the prop's own animation, whichever it is not doing. */
srModelInstance* W8Prop::ToggleAnimation(int argument)
{
    if (Animation()->path_lists == 0) {
        return AnimObjDispatch(Animation(), 2, argument);
    }
    return AnimObjDispatchList(Animation(), 2, 0);
}

/* Select the Animation() slot whose second byte carries the requested tag.
   The slot's first byte is the target frame; the current subcycle and that
   frame become the ordered range the Animation() plays through. */
unsigned char W8Prop::SelectAnimationSlot(unsigned char tag)
{
    int index;

    for (index = 0; index < static_cast<int>(slots.size()); ++index) {
        W8PropAnimationSegment* slot = &slots[index];

        if (slot->tag == tag) {
            signed char selected = slot->frame;

            if (selected < 0) {
                return 0;
            }
            first_frame = subcycle;
            last_frame = static_cast<unsigned char>(selected);
            if (static_cast<unsigned char>(selected) < subcycle) {
                last_frame = subcycle;
                first_frame = static_cast<unsigned char>(selected);
            }
            if (last_frame > subcycle) {
                frame_direction = W8_ANIMATION_FORWARD;
            } else {
                frame_direction = W8_ANIMATION_REVERSE;
            }
            animation_playing = 1;
            return 1;
        }
    }
    return 0;
}

/* Which slot carries the current tag. The tag is matched against each slot's
   own leading byte rather than used as an index, so the slots need not be in
   tag order. */
int W8Prop::FindCurrentAnimationSlot()
{
    int index;

    for (index = 0; index < static_cast<int>(slots.size()); ++index) {
        if (slots[index].frame == first_frame) {
            return index;
        }
    }
    return -1;
}

unsigned char W8Prop::AdvanceAnimationSegment()
{
    int count;
    int index;
    int segment;

    count = static_cast<int>(slots.size());
    if (count < 3) {
        return 0;
    }
    segment = -1;
    for (index = 0; index < count; ++index) {
        if (slots[index].frame == first_frame) {
            segment = index;
            break;
        }
    }
    if (segment == -1) {
        Fail("lSegment!=(-1)");
    }
    if (segment == count - 2) {
        segment = 0;
    } else {
        ++segment;
    }
    first_frame = slots[segment].frame;
    last_frame = slots[segment + 1].frame;
    frame_direction = W8_ANIMATION_FORWARD;
    animation_playing = 1;
    subcycle = first_frame;
    return static_cast<unsigned char>(segment);
}

/* The same toggle reached through the prop rather than through the member. */
srModelInstance* W8Prop::ToggleRepAnimation(int argument)
{
    W8Prop* rep = this;

    if (!rep->Animation()->path_lists) {
        return AnimObjDispatch(rep->Animation(), 2, argument);
    }
    return AnimObjDispatchList(rep->Animation(), 2, 0);
}

/* And again with the member's own stored argument instead of a caller's -
   which is what makes 0x64 the animation's default argument. */
srModelInstance* W8Prop::ToggleRepAnimationDefault()
{
    W8Prop* rep = this;
    unsigned char argument = rep->subcycle;

    if (!rep->Animation()->path_lists) {
        return AnimObjDispatch(rep->Animation(), 2, argument);
    }
    return AnimObjDispatchList(rep->Animation(), 2, 0);
}

/* Resolve the bounds of the prop's current animation frame. */
std::optional<srBoundingBox> W8Prop::GetFrameBounds()
{
    return AnimObjGetBounds(this->Animation(), 2, this->subcycle);
}

/* Set the running direction or completed endpoint state of the animation. */
void W8Prop::SetAnimationDirection(W8AnimationDirection direction)
{
    this->frame_direction = direction;
}

/* Set the live representation state. When requested, choose the direction
   and endpoint of the transition from the current and target animation tags. */
void W8Prop::SetRepresentationActive(unsigned char active, bool update_animation)
{
    this->animation_playing = active;
    if (active == 0) {
        return;
    }

    animation_timer.Restart();
    if (!update_animation) {
        return;
    }

    if (this->animation_behaviour == W8_ANIMATION_PLAY_ONCE ||
        this->animation_behaviour == W8_ANIMATION_REPEAT) {
        if ((this->frame_direction == W8_ANIMATION_FORWARD_COMPLETE &&
             this->frame_method != W8_ANIMATION_PING_PONG) ||
            (this->frame_direction != W8_ANIMATION_FORWARD_COMPLETE &&
             this->frame_method == W8_ANIMATION_PING_PONG)) {
            this->frame_direction = W8_ANIMATION_FORWARD;
            this->subcycle = this->first_frame;
            return;
        }
        this->frame_direction = W8_ANIMATION_REVERSE;
        this->subcycle = this->last_frame;
    }
}

/* Per-frame prop animation service called from WorldUpdateProps.  While the
   rep is animating it converts the game timer's progress into whole elapsed
   frames and drives AdvanceAnimationValue; a finished run leaves the
   0x20 pathing-dirty bit set for the next call to rebuild.  A pending
   pending_subcycle frame is latched into subcycle first, and an idle prop with the
   random flag may start a fresh run on its own. */
void W8Prop::UpdatePropAnimation()
{
    W8Prop* rep = this;
    unsigned int total;
    int frames;

    if (rep->active == 0 &&
        (rep->animation_behaviour != W8_ANIMATION_PLAY_ONCE || rep->animation_playing == 0) &&
        (flags & W8_PROP_ANIMATION_GEOMETRY_DIRTY) == 0) {
        return;
    }
    total = AnimObjValue(rep->Animation(), 2);
    anim_frame_fraction = 0.0f;
    if (rep->pending_subcycle != 0xffff) {
        if (static_cast<short>(rep->pending_subcycle) < static_cast<int>(total)) {
            rep->subcycle = static_cast<unsigned char>(rep->pending_subcycle);
        }
        rep->pending_subcycle = 0xffff;
    }
    if (static_cast<int>(total) < 2 || rep->animation_running) {
        return;
    }
    if (rep->animation_playing == 0 && rep->random_play &&
        Random15() * (1.0f / static_cast<float>(0x7fff)) < rep->play_chance) {
        if (rep->frame_direction == W8_ANIMATION_FORWARD_COMPLETE) {
            rep->subcycle = rep->first_frame;
            rep->frame_direction = W8_ANIMATION_FORWARD;
        } else {
            rep->subcycle = rep->last_frame;
            rep->frame_direction = W8_ANIMATION_REVERSE;
        }
        animation_timer.Restart();
        rep->animation_playing = 1;
    }
    if (rep->animation_playing == 0) {
        if ((flags & W8_PROP_ANIMATION_GEOMETRY_DIRTY) == 0) {
            return;
        }
        ApplyAnimationPaths();
        BuildOrRefreshPathingRepresentation(GetWorldPathing());
        flags &= ~W8_PROP_ANIMATION_GEOMETRY_DIRTY;
        return;
    }
    anim_frame_fraction = animation_timer.GetProgress();
    BuildOrRefreshPathingRepresentation(GetWorldPathing());
    frames = static_cast<int>(anim_frame_fraction);
    anim_frame_fraction -= frames;
    rep->frame_steps = static_cast<unsigned char>(frames);
    if (frames != 0) {
        W8AnimObj* animation;

        AdvanceAnimationValue(frames, static_cast<char>(total));
        animation = this->Animation();
        if (animation->path != 0) {
            if ((flags & W8_PROP_ACCUMULATE_PATH_FRAMES) != 0) {
                rep->frame_index += frames;
                if (rep->frame_index >= animation->frame_count) {
                    rep->frame_index = animation->frame_count;
                }
            } else {
                rep->frame_index = rep->subcycle;
            }
            PathAISetValue(animation->path.get(), static_cast<float>(rep->frame_index));
        }
    }
    if (rep->animation_playing == 0) {
        flags |= W8_PROP_ANIMATION_GEOMETRY_DIRTY;
    }
}

/* Walk every path list entry, apply the path to its mesh, and roll the prop's
   position snapshots: the previous current becomes the old position and the
   path's location becomes the new current. */
void W8Prop::ApplyAnimationPaths()
{
    std::size_t count;
    int index;

    if (GetWorld() == nullptr) {
        Fail("pWorld");
    }
    if (this->Animation()->path_lists == 1) {
        count = AnimObjListCount(this->Animation(), 2);
        for (index = 0; index < static_cast<int>(count); ++index) {
            stModelInstance* mesh = static_cast<stModelInstance*>(
                AnimObjDispatchList(this->Animation(), 2, static_cast<signed char>(index)));
            W8PathAI* path;

            if (mesh == 0) {
                Fail("psrMesh");
            }
            path = AnimObjListEntry(this->Animation(), 2, static_cast<signed char>(index));
            if (path != 0) {
                glm::vec3 location;

                PathAIApply(path, mesh);
                location = glm::vec3(static_cast<srNode*>(mesh)->getLocation());
                previous_animation_position = animation_position;
                animation_position = location;
            }
        }
    }
}

/* Advance the rep's animation value by `frames` in the rep's direction.
   Transitive animations clamp at either end and complete: the run stops, the
   linked triggers fire and the global redraw flag goes up.  Looping kinds
   wrap or, when frame_method is two, bounce off the ends and flip direction; a
   step larger than the counter range folds through whole trips.  The new
   value is clamped and pushed to every bound path. */
void W8Prop::AdvanceAnimationValue(int frames, char total)
{
    W8Prop* rep = this;
    unsigned int frame = rep->subcycle;
    W8AnimationFrameMethod behaviour = rep->frame_method;
    int end = rep->last_frame;
    int start = rep->first_frame;
    std::size_t count;
    int index;

    if (behaviour == W8_ANIMATION_RANDOM_FRAME) {
        rep->subcycle = static_cast<unsigned char>(Random(total));
    } else if (rep->animation_behaviour == W8_ANIMATION_PLAY_ONCE) {
        if (rep->frame_direction == W8_ANIMATION_FORWARD) {
            if (static_cast<int>(frame) + frames < end) {
                rep->subcycle = static_cast<unsigned char>(frame + frames);
            } else {
                rep->subcycle = rep->last_frame;
                rep->animation_playing = 0;
                rep->frame_direction = W8_ANIMATION_FORWARD_COMPLETE;
                if (trigger != 0) {
                    trigger->RunLinkedTriggers();
                }
                gXStatus.sight_refresh_pending = true;
            }
        } else if (rep->frame_direction == W8_ANIMATION_REVERSE) {
            if (static_cast<int>(frame) - frames > start) {
                rep->subcycle = static_cast<unsigned char>(frame - frames);
            } else {
                rep->subcycle = rep->first_frame;
                rep->animation_playing = 0;
                rep->frame_direction = W8_ANIMATION_REVERSE_COMPLETE;
                if (trigger != 0) {
                    trigger->RunLinkedTriggers();
                }
                gXStatus.sight_refresh_pending = true;
            }
        }
    } else {
        int range = end - start;

        if (frames < range) {
            if (rep->frame_direction == W8_ANIMATION_FORWARD) {
                frame += frames;
                if (static_cast<int>(frame) <= end) {
                    rep->subcycle = static_cast<unsigned char>(frame);
                } else if (behaviour == W8_ANIMATION_PING_PONG) {
                    rep->frame_direction = W8_ANIMATION_REVERSE;
                    rep->subcycle = static_cast<unsigned char>(2 * end - static_cast<int>(frame));
                } else {
                    rep->subcycle = static_cast<unsigned char>(static_cast<int>(frame) - range - 1);
                }
            } else if (rep->frame_direction == W8_ANIMATION_REVERSE) {
                frame -= frames;
                if (static_cast<int>(frame) >= start) {
                    rep->subcycle = static_cast<unsigned char>(frame);
                } else if (behaviour == W8_ANIMATION_PING_PONG) {
                    rep->subcycle = static_cast<unsigned char>(2 * start - static_cast<int>(frame));
                    rep->frame_direction = W8_ANIMATION_FORWARD;
                } else {
                    rep->subcycle = static_cast<unsigned char>(range + static_cast<int>(frame) + 1);
                }
            }
        } else if (rep->frame_direction == W8_ANIMATION_FORWARD) {
            int effective = static_cast<int>(frame) - start + frames;

            if (behaviour == W8_ANIMATION_PING_PONG) {
                int trips = effective / range;
                int remainder = effective - trips * range;

                if (trips % 2 == 0) {
                    rep->subcycle = static_cast<unsigned char>(start + remainder);
                } else {
                    rep->frame_direction = W8_ANIMATION_REVERSE;
                    rep->subcycle = static_cast<unsigned char>(end - remainder);
                }
            } else {
                rep->subcycle = static_cast<unsigned char>(start + effective -
                                                           effective / (range + 1) * (range + 1));
            }
        } else if (rep->frame_direction == W8_ANIMATION_REVERSE) {
            int effective = 2 * start - static_cast<int>(frame) + frames;

            if (behaviour == W8_ANIMATION_PING_PONG) {
                int trips = effective / range;
                int remainder = effective - trips * range;

                if (trips % 2 == 0) {
                    rep->subcycle = static_cast<unsigned char>(start + remainder);
                    rep->frame_direction = W8_ANIMATION_FORWARD;
                } else {
                    rep->subcycle = static_cast<unsigned char>(end - remainder);
                }
            } else {
                int folded = effective - 1;

                rep->subcycle =
                    static_cast<unsigned char>(end + folded / (range + 1) * (range + 1) - folded);
            }
        }
    }
    if (rep->subcycle < rep->first_frame) {
        rep->subcycle = rep->first_frame;
    } else if (rep->subcycle > rep->last_frame) {
        rep->subcycle = rep->last_frame;
    }
    if (rep->Animation()->path_lists == 1) {
        count = AnimObjListCount(rep->Animation(), 2);
        for (index = 0; index < static_cast<int>(count); ++index) {
            W8PathAI* path = AnimObjListEntry(rep->Animation(), 2, static_cast<signed char>(index));

            if (path != 0) {
                PathAISetValue(path, static_cast<float>(rep->subcycle));
            }
        }
    }
}

/* The animation value one step ahead, exactly as
   AdvanceAnimationValue would compute a single step: transitive kinds
   clamp at the ends, looping kinds wrap to the opposite end, and behaviour
   two steps back instead.  Retail returns the literal one rather than
   first_frame + 1 when a bouncing run sits at the start. */
char W8Prop::NextAnimationValue()
{
    W8Prop* rep = this;
    W8AnimationDirection direction = rep->frame_direction;

    if (rep->animation_behaviour == W8_ANIMATION_PLAY_ONCE) {
        if (direction == W8_ANIMATION_FORWARD) {
            if (rep->subcycle < rep->last_frame) {
                return rep->subcycle + 1;
            }
        } else if (direction == W8_ANIMATION_REVERSE && rep->first_frame < rep->subcycle) {
            return rep->subcycle - 1;
        }
        return rep->subcycle;
    }
    if (direction == W8_ANIMATION_FORWARD) {
        if (rep->subcycle != rep->last_frame) {
            return rep->subcycle + 1;
        }
        if (rep->frame_method == W8_ANIMATION_PING_PONG) {
            return rep->subcycle - 1;
        }
        return rep->first_frame;
    }
    if (direction != W8_ANIMATION_REVERSE) {
        return rep->subcycle;
    }
    if (rep->subcycle > rep->first_frame) {
        return rep->subcycle - 1;
    }
    if (rep->frame_method == W8_ANIMATION_PING_PONG) {
        return 1;
    }
    return rep->last_frame;
}

/* A single-frame step moves by the difference between the position snapshots;
   discontinuous animation changes contribute no motion. */
glm::vec3 W8Prop::GetDelta()
{
    unsigned char next = static_cast<unsigned char>(NextAnimationValue());

    if (abs(next - this->subcycle) == 1) {
        return animation_position - previous_animation_position;
    }
    return {0, 0, 0};
}

/* Whether the prop can be used from where the caller is. Retail clears the
   action pointer for non-door actions, then dereferences it when the lock
   state does not short-circuit the test. */
bool W8Prop::CanBeUsedFrom(int path_x, int path_z, bool notify)
{
    if ((flags & W8_PROP_GD_TRIGGER_BOUND) == 0 || m_gd_prop == 0) {
        return false;
    }
    Trigger* owner = m_gd_prop->m_trigger;
    if (owner == 0) {
        return false;
    }
    const auto* door = owner->doorData();
    if ((owner->lock_state.lock_type != W8LockType::none &&
         owner->lock_state.device_state.completed == 0) ||
        !door || door->open || door->locked) {
        return false;
    }
    if (!m_gd_prop->ContainsPathCoordinate(static_cast<unsigned short>(path_x),
                                           static_cast<unsigned short>(path_z))) {
        return false;
    }
    if (notify) {
        owner->Activate();
    }
    return true;
}

/* After a successful load, bind the current animation frame's mesh to its
   path and, when the animation is already running, snapshot the live
   position into the prop. */
void W8Prop::ApplyAnimationFrame(W8PathingService* pathing)
{
    auto* animation = Animation();
    if (!animation) return;

    std::size_t count;
    int index;

    if (animation->path_lists != 1) {
        W8Prop* rep = this;
        unsigned char frame = rep->subcycle;
        srModelInstance* mesh;
        W8PathAI* path;

        if (rep->Animation()->path_lists == 0) {
            mesh = AnimObjDispatch(rep->Animation(), 2, frame);
        } else {
            mesh = AnimObjDispatchList(rep->Animation(), 2, 0);
        }
        if (mesh == 0) {
            Fail("psrMesh");
        }
        path = animation->path.get();
        if (path != 0) {
            PathAISetValue(path, static_cast<float>(this->subcycle));
            PathAIApply(animation->path.get(), mesh);
        }
        return;
    }

    count = AnimObjListCount(animation, 2);
    for (index = 0; index < static_cast<int>(count); ++index) {
        srModelInstance* mesh = AnimObjDispatchList(animation, 2, static_cast<signed char>(index));
        W8PathAI* path;

        if (mesh == 0) {
            Fail("psrMesh");
        }
        path = AnimObjListEntry(animation, 2, static_cast<signed char>(index));
        if (path != 0) {
            glm::vec3 location;

            PathAISetValue(path, static_cast<float>(this->subcycle));
            PathAIApply(path, mesh);
            location = glm::vec3(static_cast<srNode*>(mesh)->getLocation());
            animation_position = location;
            previous_animation_position = location;
        }
    }
    flags |= W8_PROP_ANIMATION_GEOMETRY_DIRTY;
    BuildOrRefreshPathingRepresentation(pathing);
}

/* Per-frame prop update: while the animation is running this binds every
   dispatched instance to the world's dynamic scene, interpolates between the
   current and next keyframe records (position lerp, quaternion slerp for
   rotations - the same algorithm as PathAIApply), pushes the transform
   onto the instance or its child chain, and rolls the position snapshots
   forward. A stopped animation binds the single current instance and applies
   either the rep's path or its stored transform. */
void W8Prop::AttachAnimationInstances(W8World* world)
{
    auto* animation = Animation();
    if (!animation) return;

    int index;
    int count;
    bool has_scales;
    unsigned char next_frame;
    stModelInstance* instance;
    stMeshModel* mesh;
    W8PathAI* path;
    srNode* node;
    glm::vec3 position(0);
    glm::vec3 current(0);
    glm::vec3 next_pos(0);
    glm::vec3 current_scale(0);
    glm::vec3 next_scale(0);
    glm::vec3 scale_vector(0);
    glm::vec3 rep_position;
    glm::dvec3 location;
    glm::dvec3 scale_location;
    glm::mat3 rotation;
    glm::mat3 next;
    glm::mat3 rep_rotation;
    float inv;

    if (this->active == 0) {
        return;
    }
    if (world == 0) {
        Fail("pWorld");
    }
    if (animation->path_lists == 1) {
        has_scales = false;
        count = static_cast<int>(AnimObjListCount(animation, 2));
        for (index = 0; index < count; ++index) {
            instance = static_cast<stModelInstance*>(
                AnimObjDispatchList(animation, 2, static_cast<signed char>(index)));
            if (instance == 0) {
                Fail("psrMesh");
            }
            instance->clearFlag(srNode::FLAG_DISABLE);
            instance->setParent(world->dynamic_scene, 1);
            instance->light_scale = glm::vec3(0);
            if (g_settings.smooth_world_animations != 0) {
                instance->frame_interpolation = anim_frame_fraction;
            } else {
                instance->frame_interpolation = 0.0f;
            }
            mesh = static_cast<stMeshModel*>(instance->getModel());
            if (mesh != 0 && (mesh->flags & W8_MESH_SORTED_RENDERING) != 0 && trigger == 0) {
                instance->render_flags |= stModelInstance::RENDER_NO_PICK;
            }
            path = AnimObjListEntry(animation, 2, static_cast<signed char>(index));
            if (path == 0) {
                continue;
            }
            next_frame = static_cast<unsigned char>(NextAnimationValue());
            if (next_frame > this->last_frame) {
                anim_frame_fraction = 0.0f;
            }
            rotation = path->rotations[this->subcycle];
            next = path->rotations[next_frame];
            if (!(rotation == next)) {
                const auto first_rotation = glm::dquat(glm::quat_cast(rotation));
                auto next_rotation = glm::dquat(glm::quat_cast(next));
                double rotation_dot = glm::dot(first_rotation, next_rotation);
                if (rotation_dot < 0.0) {
                    next_rotation = -next_rotation;
                    rotation_dot = -rotation_dot;
                }
                const auto interpolated_rotation =
                    1.0 - rotation_dot <= kSlerpEpsilon
                        ? first_rotation * (1.0 - anim_frame_fraction) +
                              next_rotation * double(anim_frame_fraction)
                        : glm::slerp(first_rotation, next_rotation, double(anim_frame_fraction));
                rotation = glm::mat3_cast(glm::normalize(interpolated_rotation));
            }
            current = path->nodes[this->subcycle];
            next_pos = path->nodes[next_frame];
            inv = 1.0f - anim_frame_fraction;
            position = current * inv + next_pos * anim_frame_fraction;
            if (!path->scales.empty()) {
                has_scales = true;
                current_scale = path->scales[this->subcycle];
                next_scale = path->scales[next_frame];
                scale_vector = current_scale * inv + next_scale * anim_frame_fraction;
            }
            node = instance->getChild();
            if (node == 0) {
                instance->setRotation(rotation);
                location = glm::dvec3(position);
                instance->setLocation(location);
                if (has_scales) {
                    scale_location = glm::dvec3(scale_vector);
                    instance->setScale(scale_location);
                }
            } else {
                do {
                    node->setRotation(rotation);
                    location = glm::dvec3(position);
                    node->setLocation(location);
                    if (has_scales) {
                        scale_location = glm::dvec3(scale_vector);
                        node->setScale(scale_location);
                    }
                    node = node->getNext();
                } while (node != 0);
            }
            previous_animation_position = animation_position;
            animation_position = position;
        }
    } else {
        instance = static_cast<stModelInstance*>(this->ToggleAnimation(this->subcycle));
        if (instance == 0) {
            Fail("psrMesh");
        }
        instance->clearFlag(srNode::FLAG_DISABLE);
        instance->setParent(world->dynamic_scene, 1);
        instance->light_scale = glm::vec3(0);
        if (g_settings.smooth_world_animations != 0) {
            instance->frame_interpolation = anim_frame_fraction;
        } else {
            instance->frame_interpolation = 0.0f;
        }
        mesh = static_cast<stMeshModel*>(instance->getModel());
        if (mesh != 0 && (mesh->flags & W8_MESH_SORTED_RENDERING) != 0 && trigger == 0) {
            instance->render_flags |= stModelInstance::RENDER_NO_PICK;
        }
        if (animation->path != 0) {
            PathAIApply(animation->path.get(), instance);
        } else {
            rep_position = this->GetLocation();
            rep_rotation = this->GetRotation();
            node = instance->getChild();
            if (node == 0) {
                location = glm::dvec3(rep_position);
                instance->setLocation(location);
                instance->setRotation(rep_rotation);
            } else {
                do {
                    location = glm::dvec3(rep_position);
                    node->setLocation(location);
                    node->setRotation(rep_rotation);
                    node = node->getNext();
                } while (node != 0);
            }
        }
        BakeInstanceVertexLightingIfNeeded(instance, world->dynamic_scene);
    }
    if (m_gd_prop == 0) {
        BuildOrRefreshPathingRepresentation(WorldPathing(*world));
    }
}

/* The detach counterpart to AttachAnimationInstances: the current frame is stashed in
   saved_subcycle, then every dispatched instance is flagged disabled and detached
   from the scene.  While the animation runs the whole list is walked;
   otherwise only the snapshot frame's instance (or the first dispatch-list
   entry when a run is in progress) is pulled. */
void W8Prop::DetachAnimationInstances(W8World* world)
{
    stModelInstance* instance;
    std::size_t count;
    int index;

    if (world == 0) {
        Fail("pWorld");
    }
    this->saved_subcycle = this->subcycle;
    if (this->Animation()->path_lists == 1) {
        count = AnimObjListCount(this->Animation(), 2);
        for (index = 0; index < static_cast<int>(count); ++index) {
            instance = static_cast<stModelInstance*>(
                AnimObjDispatchList(this->Animation(), 2, static_cast<signed char>(index)));
            if (instance == 0) {
                Fail("psrMesh");
            }
            instance->setFlag(srNode::FLAG_DISABLE);
            instance->setParent(0, 1);
        }
    } else {
        unsigned char frame = this->subcycle;

        if (this->Animation()->path_lists == 0) {
            instance = static_cast<stModelInstance*>(AnimObjDispatch(this->Animation(), 2, frame));
        } else {
            instance = static_cast<stModelInstance*>(AnimObjDispatchList(this->Animation(), 2, 0));
        }
        if (instance == 0) {
            Fail("psrMesh");
        }
        instance->setFlag(srNode::FLAG_DISABLE);
        instance->setParent(0, 1);
    }
}

/* Apply a completely decoded APST record: each saved index is clamped to the
   loaded animation's frame count, path values are re-synced while an animation
   runs, and ApplyAnimationFrame reapplies the frame against `pathing`. */
void W8Prop::ApplyAnimationState(const SavedAnimationState& state, W8PathingService* pathing)
{
    std::size_t count;
    int index;
    int total;
    W8PathAI* path;
    this->subcycle = state.subcycle;
    this->first_frame = state.first_frame;
    this->last_frame = state.last_frame;
    this->frame_direction = state.frame_direction;
    this->animation_playing = state.animation_playing;
    if (this->Animation() != 0) {
        total = static_cast<int>(AnimObjValue(this->Animation(), 2));
        if (this->last_frame >= total) {
            this->last_frame = static_cast<unsigned char>(total - 1);
        }
        if (this->first_frame >= total) {
            this->first_frame = static_cast<unsigned char>(total - 1);
        }
        if (this->subcycle >= total) {
            this->subcycle = static_cast<unsigned char>(total - 1);
        }
        if (this->Animation()->path_lists == 1) {
            count = AnimObjListCount(this->Animation(), 2);
            for (index = 0; index < static_cast<int>(count); ++index) {
                path = AnimObjListEntry(this->Animation(), 2, static_cast<signed char>(index));
                if (path != 0) {
                    PathAISetValue(path, static_cast<float>(this->subcycle));
                }
            }
        }
        ApplyAnimationFrame(pathing);
    }
}

/* Union of the per-frame bounds over every frame of the rep's animation.
   The rep's current frame is saved, the bounds for frame zero seed the merge,
   and each remaining frame expands the result before the frame is restored. */
std::optional<srBoundingBox> W8Prop::GetBounds()
{
    auto* animation = this->Animation();
    auto bounds = AnimObjGetBounds(animation, 2, 0);
    if (!bounds) return std::nullopt;
    const auto total = AnimObjValue(animation, 2);
    for (unsigned int frame = 1; frame < total; ++frame) {
        const auto frame_bounds = AnimObjGetBounds(animation, 2, frame);
        if (!frame_bounds) return std::nullopt;
        bounds->include(*frame_bounds);
    }
    return bounds;
}

/* Build or refresh the pathing representation for a collidable Prop.  Retail
   requires a transitive animation with one mesh, then either constructs the
   owned GDProp or reinitializes it for the current animation frame. */
int W8Prop::BuildOrRefreshPathingRepresentation(W8PathingService* pathing)
{
    srModelInstance* instance;

    if ((flags & W8_PROP_COLLIDABLE) == 0) {
        return 0;
    }
    if (this->Animation()->path_lists != 1) {
        ShutdownWithErrorBox("Collidable props can be of Transform type only.");
    }
    if (AnimObjListCount(this->Animation(), 2) != 1) {
        ShutdownWithErrorBox("Collideable props should have a single LOD.");
    }
    instance = AnimObjDispatchList(this->Animation(), 2, 0);
    if (instance == 0) {
        Fail("pstInstance");
    }

    if (m_gd_prop == 0) {
        m_gd_prop = std::make_unique<GDProp>(pathing, instance, name.c_str(),
                                             static_cast<unsigned short>(this->subcycle),
                                             this->footstep_surface, this->footstep_material);
    } else {
        if ((flags & W8_PROP_ANIMATION_GEOMETRY_DIRTY) != 0) {
            m_gd_prop->Initialize(pathing, instance, true,
                                  static_cast<unsigned short>(this->subcycle),
                                  this->footstep_surface, this->footstep_material);
        } else {
            m_gd_prop->Initialize(pathing, instance, false, 0, this->footstep_surface,
                                  this->footstep_material);
        }
        if (this->animation_behaviour == W8_ANIMATION_PLAY_ONCE) {
            g_animated_prop_present = true;
        }
    }
    return static_cast<int>(m_gd_prop->m_pGDSurfaces.size());
}

/* Run the prop's own trigger for a missile impact.  Only the three prop
   animation actions (0x3a..0x3c) accept the missile's table index as their
   source; every other trigger setup is ignored. */
void W8Prop::RunMissileTrigger(W8AIMissile* record)
{
    if (record != 0 && record->missile != 0 && trigger != 0 &&
        (trigger->initial_action == 0x3a || trigger->initial_action == 0x3b ||
         trigger->initial_action == 0x3c)) {
        trigger->Run(record->missile->missile_table_index);
    }
}

/* Mirror of GetPosition: while the rep node reports itself current
   the position is stored in animation_position, otherwise it goes through the rep
   node's own location. */
void W8Prop::SetPosition(glm::vec3* position)
{
    if (this->Animation()->path_lists == 1) {
        animation_position = *position;
        return;
    }
    this->SetLocation(position);
}

bool W8Prop::TriggerHasActionMessage()
{
    return trigger != 0 && trigger->HasActionMessage();
}

bool W8Prop::TriggerRequiresItem()
{
    return trigger != 0 && trigger->RequiresItem();
}

/* Whether a prop with a selectable trigger is visible from `position`: the
   trigger's distance interval has to contain the centre of the current
   frame's bounds and the centre, minimum or maximum has to project
   on-screen through the active world's camera. */
bool W8Prop::IsTriggerInView(glm::vec3* position)
{
    const auto* trigger = this->trigger;
    if (!trigger || (trigger->flags & W8_TRIGGER_ENABLED) == 0 ||
        ((trigger->flags & W8_TRIGGER_ONCE) != 0 && (trigger->flags & W8_TRIGGER_FIRED) != 0))
        return false;
    const auto bounds = GetFrameBounds();
    if (!bounds) return false;
    const glm::vec3 center(bounds->center());
    const float distance = glm::length((center - *position));
    if (!(distance < trigger->range_maximum && trigger->range_minimum <= distance)) return false;
    const std::array points{bounds->center(), bounds->minimum, bounds->maximum};
    return GetWorld()->camera->projectsAny(points);
}

std::unique_ptr<W8Prop> CreateAndLoadProp(W8ReadLevelInfo* info)
{
    if (!info) throw std::invalid_argument("missing prop load context");
    auto prop = std::make_unique<W8Prop>();
    if (!prop->LoadProp(info, prop.get())) return nullptr;
    prop->animation_timer.SetDuration(1.0f / prop->animation_speed);
    prop->ApplyAnimationFrame(info->world ? WorldPathing(*info->world) : nullptr);
    return prop;
}

/* Resource loader rooted at CreateAndLoadProp.  The call site loads
   the prop representation into ECX before the two stack arguments, so this is a
   PropRep method: LoadProp(pInfo, pProp). */
bool W8Prop::LoadProp(W8ReadLevelInfo* info, W8Prop* prop)
try {
    wiz8::File* hFile;
    bool success;
    signed char version;
    unsigned char frame_count = 0;
    unsigned char option_byte = 0;
    unsigned char attach_flag = 0;
    float playback_scale = 0.0f;
    unsigned int flag_bits = 0;
    W8AnimObj* animation = 0;
    bool result = false;

    if (info == 0 || info->hFile == 0 || prop == 0) {
        Fail("pInfo && pInfo->hFile && pProp");
    }
    hFile = info->hFile;
    success = (hFile->read(&version, 1).bytes == static_cast<std::size_t>(1));
    if (version < 4) {
        unsigned char b0 = 0;
        unsigned char b1 = 0;
        unsigned char b2 = 0;
        int ok;
        std::unique_ptr<W8AniMesh> mesh;

        if (!success ||
            (success = (hFile->read(&b0, 1).bytes == static_cast<std::size_t>(1)), !success) ||
            (success = (hFile->read(&b1, 1).bytes == static_cast<std::size_t>(1)), !success) ||
            (success = (hFile->read(&b2, 1).bytes == static_cast<std::size_t>(1)), !success)) {
            ok = 0;
        } else {
            ok = 1;
        }
        if (version < 2) {
            playback_scale = 15.0f;
            if (!ok) {
                goto fail;
            }
        } else {
            if (!ok) {
                goto fail;
            }
            success = (hFile->read(&playback_scale, 4).bytes == static_cast<std::size_t>(4));
            if (!success) {
                goto fail;
            }
        }
        mesh = ReadAnimatedMesh(*hFile, info->bitmap_folder, true);
        result = mesh != nullptr;
        if (!result) {
            result = false;
            goto fail_with_result;
        }
        animation = &this->animation.emplace();
        animation->entries[2] = std::move(mesh);
        animation->group_count = 1;
        animation->animation_playing = b0;
        animation->frame_method = static_cast<W8AnimationFrameMethod>(b1);
        animation->behaviour = static_cast<W8AnimationBehaviour>(b2);
        animation->cycle = 0;
        animation->path_lists = 0;
        animation->playback_scale = playback_scale;
    } else {
        unsigned int entry_index;
        unsigned int list_count;
        signed char slot_count;
        int slot_i;

        flag_bits = 0;
        if (success) {
            hFile->read_exact(&frame_count, 1);
        }
        if (version > 4) {
            float lx = 0.0f;
            float ly = 0.0f;
            float lz = 0.0f;

            hFile->read_exact(&option_byte, 1);
            hFile->read_exact(&lx, 4);
            hFile->read_exact(&ly, 4);
            hFile->read_exact(&lz, 4);
            lx *= 500.0;
            ly *= 500.0;
            lz *= 500.0;
            this->local_location.x = lx;
            this->location.x = lx;
            this->local_location.y = ly;
            this->location.y = ly;
            this->local_location.z = lz;
            this->location.z = lz;
        }
        if (version > 5) {
            hFile->read_exact(&flag_bits, 4);
            prop->flags |= flag_bits;
        }
        if (version > 6) {
            std::array<char, 64> name;
            hFile->read_exact(name.data(), name.size());
            const auto end = std::find(name.begin(), name.end(), '\0');
            if (end == name.end()) throw std::runtime_error("unterminated prop name");
            if (end != name.begin()) {
                prop->name.assign(name.data(), end - name.begin());
            }
        }
        if (version > 7) {
            slot_count = 0;
            hFile->read_exact(&slot_count, 1);
            for (slot_i = 0; slot_i < slot_count; ++slot_i) {
                unsigned short frame_tmp = 0;
                unsigned short tag_tmp = 0;
                W8PropAnimationSegment slot{};

                hFile->read_exact(&frame_tmp, 2);
                slot.frame = static_cast<signed char>(frame_tmp);
                hFile->read_exact(&tag_tmp, 2);
                slot.tag = static_cast<unsigned char>(tag_tmp);
                if (frame_count <= frame_tmp) {
                    Fail("(usTemp < (UINT16)ubNumFrames)",
                         fmt::format(
                             "{} Prop Error:Segment {:d} frame number is out of range ({:d})",
                             prop->name, static_cast<unsigned int>(tag_tmp),
                             static_cast<unsigned int>(frame_tmp)));
                }
                this->slots.push_back(slot);
            }
        }
        animation = &this->animation.emplace();
        result = AnimObjReadFromFile(info, animation, 1, 0, 1);
        if (animation->path_lists == 1) {
            animation->frame_count = frame_count;
        }
        this->play_chance = animation->play_chance;
        this->random_play = animation->random_play != 0;
        list_count = AnimObjValue(animation, 2);
        for (entry_index = 0; entry_index < list_count; ++entry_index) {
            srModelInstance* instance;
            stMeshModel* mesh_model;
            std::string named;

            if (Animation()->path_lists == 0) {
                instance = AnimObjDispatch(Animation(), 2, static_cast<unsigned char>(entry_index));
            } else {
                instance = AnimObjDispatchList(Animation(), 2, 0);
            }
            named = fmt::format("Prop: {}", prop->name);
            instance->setName(named.c_str());
            mesh_model = static_cast<stMeshModel*>(instance->getModel());
            for (; mesh_model != 0; mesh_model = mesh_model->Next()) {
                if (animation->path_lists == 0) {
                    mesh_model->GetVertexSunlight(true);
                }
            }
            if (option_byte != 0) {
                srNode* child;
                const glm::vec3 axis(0, 1, 0);
                instance->setAlignment(1);
                instance->setAlignAxis(axis);
                child = instance->getChild();
                if (child != 0) {
                    srModelInstance* child_instance = static_cast<srModelInstance*>(child);
                    child_instance->setAlignment(1);
                    child_instance->setAlignAxis(axis);
                }
            }
        }
        list_count = AnimObjListCount(animation, 2);
        for (entry_index = 0; entry_index < list_count; ++entry_index) {
            W8PathAI* path = AnimObjListEntry(animation, 2, static_cast<signed char>(entry_index));
            if (path != 0) {
                path->looping = true;
                path->discrete_mode = true;
                path->speed = animation->playback_scale;
            }
        }
    }

    this->active = 1;
    this->animation_behaviour = animation->behaviour;
    this->frame_method = animation->frame_method;
    this->animation_playing = animation->animation_playing;
    this->frame_direction = W8_ANIMATION_FORWARD;
    this->animation_speed = animation->playback_scale;
    this->timer = GameClockMilliseconds();
    if (this->animation_behaviour == W8_ANIMATION_PLAY_ONCE ||
        this->animation_behaviour == W8_ANIMATION_REPEAT) {
        this->animation_playing = 0;
        this->frame_direction = this->frame_method == W8_ANIMATION_PING_PONG
                                    ? W8_ANIMATION_REVERSE_COMPLETE
                                    : W8_ANIMATION_FORWARD_COMPLETE;
    }

    if (animation->path_lists == 0) {
        W8AniMesh* mesh = AnimObjEntry(animation, 2, 0);
        unsigned char value_count = AniMeshValue(mesh);
        stModelInstance* frame = GetAniMeshFrame(mesh, 0);
        unsigned int frame_i;

        if (frame == 0) {
            Fail("psrMesh");
        }
        auto box = frame->getModel()->getBounds().box;
        for (frame_i = 0; frame_i < value_count; ++frame_i) {
            frame = GetAniMeshFrame(mesh, frame_i);
            if (frame == 0) {
                Fail("psrMesh");
            }
            box.include(frame->getModel()->getBounds().box);
        }
        bounds_min = box.minimum;
        bounds_max = box.maximum;
        const auto size = box.maximum - box.minimum;
        bounds_extent = std::max({size.x, size.y, size.z}) * 0.5f;
    }

    if (version > 2) {
        if (!result) {
            result = false;
        } else {
            success = (hFile->read(&attach_flag, 1).bytes == static_cast<std::size_t>(1));
            result = success;
        }
        if (attach_flag != 0) {
            Trigger* trigger;

            trigger = Trigger::CreateAndLoadLevelTrigger(hFile, info->world);
            /* Retail writes the attach fields first, then tests type at +0x22a
               (the stores do not touch that word). */
            trigger->m_bRepType = W8_TRIGGER_REP_PROP;
            trigger->m_pProp = prop;
            if (trigger->initial_action == 0x40) {
                InitializeStateDrivenPropVariables(trigger);
            }
            if (trigger->trigger_kind == W8TriggerKind::item && trigger->initial_action == 8) {
                unsigned int path_count;
                unsigned int path_i;

                this->animation_running = true;
                if (Animation()->path_lists == 1) {
                    path_count = AnimObjListCount(Animation(), 2);
                    for (path_i = 0; path_i < path_count; ++path_i) {
                        W8PathAI* path =
                            AnimObjListEntry(Animation(), 2, static_cast<signed char>(path_i));
                        path->step_by_node = 1;
                    }
                }
            }
            switch (trigger->initial_action) {
            case 1:
            case 2:
            case 3:
            case 0x2c:
            case 0x32:
            case 0x33:
            case 0x40:
                this->animation_playing = 0;
                break;
            }
            prop->trigger = trigger;
            hFile = info->hFile;
        }
    }

    if (version >= 9) {
        unsigned char extra = 0;

        if (result) {
            success = (hFile->read(&extra, 1).bytes == static_cast<std::size_t>(1));
            result = success;
        }
        if (extra != 0) {
            if (result &&
                (success =
                     (hFile->read(&this->footstep_surface, 1).bytes == static_cast<std::size_t>(1)),
                 success) &&
                (success = (hFile->read(&this->footstep_material, 1).bytes ==
                            static_cast<std::size_t>(1)),
                 success)) {
                result = true;
            } else {
                result = false;
            }
        }
    }

    this->first_frame = 0;
    this->subcycle = 0;
    {
        unsigned int frames = AnimObjValue(animation, 2);
        this->last_frame = static_cast<unsigned char>(frames) - 1;
    }
    return result;

fail:
    Fail("fSuccess");
    return false;

fail_with_result:
    Fail("fSuccess");
    return result;
} catch (const std::exception&) {
    return false;
}

/* The prop representation's current animation value, or -1 while it owns no
   animation. */
int W8Prop::GetAnimationState() const
{
    const W8AnimObj* animation = this->Animation();
    if (animation != 0) {
        return static_cast<int>(AnimObjValue(animation, 2));
    }
    return -1;
}

/* Every model instance the representation's animation can display: one per
   frame of each mesh entry (a single-instance mesh contributes frame zero
   only), or one per frame of every mesh in each mesh list. */
void W8Prop::CollectModelInstances(std::vector<stModelInstance*>& instances)
{
    const W8AnimObj* animation = this->Animation();
    if (animation == 0) {
        return;
    }
    if (!animation->path_lists) {
        for (const auto& entry : animation->entries) {
            W8AniMesh* mesh = entry.get();
            if (mesh == 0) {
                continue;
            }
            int frame_count = AniMeshValue(mesh);
            if (mesh->flags.single_instance) {
                instances.push_back(GetAniMeshFrame(mesh, 0));
            } else {
                for (int frame = 0; frame < frame_count; ++frame) {
                    instances.push_back(GetAniMeshFrame(mesh, frame));
                }
            }
        }
    } else if (animation->path_lists == 1) {
        for (auto& frame_meshes : animation->meshes) {
            for (const auto& owned_mesh : frame_meshes) {
                auto* mesh = owned_mesh.get();
                if (mesh == 0) {
                    continue;
                }
                int frame_count = AniMeshValue(mesh);
                for (int frame = 0; frame < frame_count; ++frame) {
                    instances.push_back(GetAniMeshFrame(mesh, frame));
                }
            }
        }
    }
}

/* APST chunk writer: a 0xDEADD00D signature, the format version and the prop
   count, then one record per prop - a fixed 64-byte name plus the six rep
   bytes that LoadAnimationState reads back (frame, counters, direction
   flags and the active byte). The per-prop byte writes only run while the
   previous writes succeed. */
void SaveWorldProps(wiz8::File& handle)
{
    int index;
    int count;
    unsigned int signature;
    unsigned int version;
    W8Prop* prop;

    signature = 0xDEADD00D;
    version = 1;
    handle.write(&signature, 4);
    handle.write(&version, 4);
    count = static_cast<int>(GetWorld()->plsProps.size());
    handle.write(&count, 4);
    for (index = 0; index < count; ++index) {
        prop = GetWorldProp(GetWorld(), index);
        handle.write_padded(prop->name.c_str(), 0x40);
        if ((handle.write(&prop->subcycle, 1), true) != 0 &&
            (handle.write(&prop->first_frame, 1), true) != 0 &&
            (handle.write(&prop->last_frame, 1), true) != 0 &&
            (handle.write(&prop->frame_direction, 1), true) != 0 &&
            (handle.write(&prop->animation_playing, 1), true) != 0) {
            handle.write(&prop->active, 1);
        }
    }
}

/* Both APST formats decode six animation bytes before updating a matching
   prop. Unknown records are decoded and ignored within the chunk boundary. */
void LoadWorldProps(W8World* world, W8Chunk& chunk)
{
    const auto signature = chunk.ReadValue<unsigned int>();
    const bool named = signature == 0xDEADD00D;
    if (named) chunk.ReadValue<unsigned int>(); // version
    const auto count = named ? chunk.ReadValue<unsigned int>() : signature;
    const std::size_t key_size = named ? 64 : sizeof(int);
    if (std::uint64_t{count} * (key_size + 6) > static_cast<std::uint64_t>(chunk.RemainingBytes()))
        throw std::runtime_error("invalid saved prop count");
    for (unsigned int index = 0; index < count; ++index) {
        char name[65]{};
        int key = 0;
        if (named) {
            chunk.ReadBytes(name, 64);
            if (std::find(name, name + 64, '\0') == name + 64)
                throw std::runtime_error("unterminated saved prop name");
        } else {
            chunk.ReadValue(key);
        }
        W8Prop::SavedAnimationState state;
        static_assert(sizeof(state) == 6);
        chunk.ReadBytes(&state, sizeof(state));
        W8Prop* prop = nullptr;
        if (named) {
            prop = FindPropByName(world, name);
        } else {
            for (const auto& candidate : world->plsProps) {
                if (candidate->legacy_save_id == key) {
                    prop = candidate.get();
                    break;
                }
            }
        }
        if (prop) prop->ApplyAnimationState(state, WorldPathing(*world));
    }
}

/* The renderer owns the selected model instance. Without one, retail also
   resets the cached prop index to -1 before returning it. */
int GetSelectedPropIndex(void)
{
    if (g_current_model_instance == 0) {
        return g_selected_prop_index = -1;
    }
    return g_selected_prop_index;
}

/* When the renderer still holds a pick and ResolvePickedProp latched a
   trigger, run that trigger and post the nothing-happened / special-item
   notice. Clearing the pick also clears the latch. */
bool ActivateSelectedProp(void)
{
    if (g_current_model_instance == 0) {
        g_selected_prop_trigger = 0;
        return false;
    }
    if (g_selected_prop_trigger != 0) {
        g_trigger_feedback = false;
        g_selected_prop_trigger->Run(-1);
        g_selected_prop_trigger->PrintNothingHappenedOrSpecialItemRequired();
        return true;
    }
    return false;
}
