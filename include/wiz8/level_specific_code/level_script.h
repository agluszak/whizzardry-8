#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "wiz8/location_variables.h"
#include "wiz8/script_context.h"

class Trigger;
struct TriggerDecision;
struct W8WorldCursorNode;
struct World;

/* The loaded level's script: its trigger callbacks, the per-frame masters they arm and
   the state they keep between frames. The level's world owns it (`World::script`), so
   entering a level starts from fresh script state and nothing outlives the level. A
   level without level-specific code still has one, for its world-cursor nodes. */
class LevelScript {
public:
    explicit LevelScript(const ScriptContext& context);
    virtual ~LevelScript();
    LevelScript(const LevelScript&) = delete;
    LevelScript& operator=(const LevelScript&) = delete;

    /* Install the level's trigger callbacks and restore its script state from the
       location variables. Runs once, after the level's world is published. */
    virtual void enter() {}

    /* Run the current batch of masters once. Completed masters leave; masters armed
       meanwhile wait until the next frame. */
    void runMasters();
    // The masters' persistent state; never steps gameplay.
    W8SavedLocationState captureState() const;
    // Dispatch movement or interaction against the nodes covering the party's ground position.
    bool dispatchCursorNodeCommand(int command);
    // Forget the node the party stands in, so the next sweep enters it again.
    void clearActiveCursorNode() { active_cursor_node_ = nullptr; }
    // Item kSlopeOverrideItem, worn or held, lets the party ignore this level's slopes.
    bool slopeOverrideActive() const { return slope_override_active_; }

    /* The NPC dialogue closed since a watch began waiting for the script notice it
       queued. The dialogue raises it; the watch lowers it when it starts waiting. */
    bool npc_dialogue_closed = true;

protected:
    using Update = std::function<bool(int)>;
    using Capture = std::function<void(W8SavedLocationState&)>;
    using Callback = std::function<TriggerDecision(Trigger*)>;

    World& world() const { return *ctx.world; }

    /* Arm a master: the dispatcher runs `update(0)` every frame until it answers true.
       `capture` records its persistent state when the level is left or saved. */
    void addMaster(Update update, Capture capture = {});
    template<class Script>
    void addMaster(bool (Script::*update)(int))
    {
        addMaster(Update(std::bind_front(update, static_cast<Script*>(this))));
    }
    template<class Script>
    void addMaster(bool (Script::*update)(int), void (Script::*capture)(W8SavedLocationState&))
    {
        auto* script = static_cast<Script*>(this);
        addMaster(Update(std::bind_front(update, script)),
                  Capture(std::bind_front(capture, script)));
    }

    // Install `callback` on the trigger the level must have.
    void bindTrigger(std::string_view trigger, Callback callback);
    template<class Script>
    void bindTrigger(std::string_view trigger, TriggerDecision (Script::*callback)(Trigger*))
    {
        bindTrigger(trigger, callbackOf(callback));
    }
    template<class Script>
    Callback callbackOf(TriggerDecision (Script::*callback)(Trigger*))
    {
        return std::bind_front(callback, static_cast<Script*>(this));
    }

    const ScriptContext ctx;
    bool slope_override_active_ = false;

private:
    struct Master {
        Update update;
        Capture capture;
    };

    bool runCursorNodeHandler(int command, W8WorldCursorNode* node);
    bool showMessageOnce(int command, W8WorldCursorNode* node);
    bool showMessage(int command, W8WorldCursorNode* node);
    bool seenBodies(int command);
    bool partyVoice(int command, W8WorldCursorNode* node);
    bool maleCharacterEvent(int command);

    std::vector<Master> masters_;
    // The node the party stands in, tracked so enter and leave fire once per crossing.
    W8WorldCursorNode* active_cursor_node_ = nullptr;
};

/* The script of `level`, acting through `context`, whose world is the level's. The
   caller publishes it on the world and then enters it. */
std::unique_ptr<LevelScript> MakeLevelScript(int level, const ScriptContext& context);
