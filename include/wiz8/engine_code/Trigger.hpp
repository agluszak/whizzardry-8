#pragma once
struct W8Chunk;

#include <memory>
#include <optional>
#include <source_location>
#include <string>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srClass.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/game_timer.h"

class stLight;
struct W8Item;
class W8Prop;
class Trigger;
struct W8World;
struct W8WorldItem;
namespace wiz8 {
class File;
}

// Trigger events own both timers; the active queue borrows event pointers.
class W8TriggerEvent {
public:
    W8TriggerEvent();
    virtual ~W8TriggerEvent();
    virtual void Update();

    short action;
    unsigned short unknown_006;
    W8GameTimer timer;
    std::optional<W8GameTimer> m_pCountdown;
    Trigger* trigger;
    bool repeat;
    bool completed;
};

void UpdateTimedTriggerEvents(void);

/* Payload tags remain signed bytes in the common prefix and on disk. */
enum W8TriggerPayloadKind : signed char {
    W8_TRIGGER_PAYLOAD_NONE = -1,
    W8_TRIGGER_PAYLOAD_ENVIRONMENT = 5,
    W8_TRIGGER_PAYLOAD_STRING = 6,
    W8_TRIGGER_PAYLOAD_DOOR = 10
};

/* The common polymorphic prefix of the trigger action payload family. */
class W8TriggerActionData {
public:
    W8TriggerActionData();
    virtual ~W8TriggerActionData();

    W8TriggerPayloadKind type;
};

/* Type 5 installs its own final table and retains the previous environment value. */
class W8EnvironmentTriggerActionData : public W8TriggerActionData {
public:
    float previous_environment;
};

/* The level loader allocates 0x98 bytes for type 10. Its first twelve bytes
   are the common polymorphic payload above; the remaining bytes are the
   linked trigger name and optional world position read from the save.
   The door state is a run of one-bit fields: retail stores them with VC6's
   single-bit insert sequences, and mipe's editor table labels the first nine
   in order (Open .. Jammed) with the key id as the tenth field. */
class W8DoorTriggerActionData : public W8TriggerActionData {
public:
    /* Both allocation sites (0x0043C1B0 and 0x004417C0) expand this body; the
       eight leading bits fold to one 0x40 byte store and has_position stays
       uninitialized. */
    W8DoorTriggerActionData()
    {
        open = false;
        lockable = false;
        locked = false;
        auto_locking = false;
        one_way = false;
        secret = false;
        found = true;
        has_position = false;
        jammable = false;
        jammed = false;
        item = -1;
        type = W8_TRIGGER_PAYLOAD_DOOR;
        position = glm::vec3(0);
        linked_trigger[0] = 0;
    }

    bool open : 1;
    bool lockable : 1;
    bool locked : 1;
    bool auto_locking : 1;
    bool one_way : 1;
    bool secret : 1;
    bool found : 1;
    bool jammable : 1;
    bool jammed : 1;
    bool has_position : 1;
    short item; /* key item id; -1 for none */
    char linked_trigger[0x80];
    glm::vec3 position;
};

/* Type 6 (built for action 17) owns a copy of the trigger's action string. */
class W8StringTriggerActionData : public W8TriggerActionData {
public:
    virtual ~W8StringTriggerActionData() override;
    std::string owned_string;
};

enum W8TriggerFlag {
    W8_TRIGGER_ANIMATE_STATES = 0x1,
    W8_TRIGGER_ANIMATE_ACTION = 0x2,
    W8_TRIGGER_PLANE = 0x4,
    W8_TRIGGER_KEEP_ON_FINISH = 0x8,
    W8_TRIGGER_ON = 0x10,
    W8_TRIGGER_RUNNING = 0x40,
    W8_TRIGGER_FIRE_LINKED = 0x80,
    W8_TRIGGER_ENABLED = 0x100,
    W8_TRIGGER_LINK_ON_DEACTIVATE = 0x200,
    W8_TRIGGER_POSITIONED = 0x800,
    W8_TRIGGER_HAS_ALTERNATE = 0x2000,
    W8_TRIGGER_USE_ALTERNATE = 0x4000,
    W8_TRIGGER_ALTERNATE_TOGGLES = 0x8000,
    W8_TRIGGER_CONSUME_ITEM = 0x10000,
    W8_TRIGGER_CAN_RUN_LINKED = 0x20000,
    W8_TRIGGER_ONCE = 0x40000,
    W8_TRIGGER_FIRED = 0x80000,
    W8_TRIGGER_EXCLUSIVE = 0x100000,
    W8_TRIGGER_REACTIVATE_LINKED = 0x200000,
    W8_TRIGGER_ALTERNATE_ACTION = 0x800000,
    W8_TRIGGER_ALTERNATE_SELECTED = 0x1000000,
    W8_TRIGGER_ITEM_PICKER = 0x2000000,
    W8_TRIGGER_SEARCHED = 0x4000000,
};

/* Persisted lock/trap device state: `completed` latches once the pick/disarm
   interaction finishes; `pins` holds the eight tumbler bytes of a pickable
   lock, re-rolled by W8LockState::Reset. */
struct W8TriggerDeviceState {
    unsigned char completed;
    unsigned char pins[8];
};

enum class W8LockType : int { none = 0, pickable_lock = 1, trap = 2, key_lock = 3 };

/* The locks & traps device record embedded at the tail of Trigger. `lock_type` is the editor "Type" (none,
   pickable lock, trap, or key lock); `difficulty` is the editor "Difficulty"
   grade — it doubles as the pickable lock's pin budget; `device_id` indexes
   the tumbler/trap tables (-1 = roll on first use); `lock_countdown` ticks the
   pick interaction (pins remaining, difficulty * 3); `last_interaction_clock`
   is the world clock of the last attempt (-1 = never). */
struct W8LockState {
    /* Re-rolls the eight pin bytes of a pickable lock and
       resets its difficulty-derived countdown and interaction state. */
    void Reset(); /* 0x00445730 */
    void ReadRuntimeRecord(W8Chunk& handle, int version, int restoring);
    /* Spends one point of the lock countdown and reports whether one
       remained to spend. */
    bool ConsumeCountdown(); /* 0x004457A0 */

    W8LockType lock_type;
    int difficulty;
    W8TriggerDeviceState device_state;
    unsigned char unknown_011[3];
    int device_id;
    int key_id;
    int lock_countdown;
    int last_interaction_clock;
};

enum W8TriggerRepresentationKind : unsigned char {
    W8_TRIGGER_REP_NONE = 0,
    W8_TRIGGER_REP_ITEM = 1,
    W8_TRIGGER_REP_PROP = 2,
    W8_TRIGGER_REP_POSITION = 3
};

enum class W8TriggerKind : int { none = 0, item = 1, prop = 2 };
enum class W8TriggerActionState : unsigned char {
    inactive = 1,
    initial_action = 2,
    alternate_action = 3,
    fallback_action = 4
};
enum class W8TriggerStateModificationMode : unsigned char {
    none = 0,
    set = 1,
    clear = 2,
    toggle = 3
};

class Trigger : public srClass {
public:
    typedef bool (*ActivationCallback)(Trigger* trigger);

    explicit Trigger(W8World* world = nullptr);
    virtual ~Trigger() override;

    static Trigger* CreateAndLoadLevelTrigger(wiz8::File* handle, W8World* world);

    W8Prop* GetProp() const
    {
        if (m_bRepType != W8_TRIGGER_REP_PROP) {
            srAssertFail("m_bRepType == TRIGGER_REP_PROP", "..\\Engine Code\\Include\\Trigger.hpp",
                         0x3ed, 0);
        }
        return m_pProp;
    }

    bool HasActorWithinRadius(float radius, bool include_party);
    bool PlayActionSound(const char* sound_name, int volume);
    void UpdateActionAnimation();
    void CommitActionResult(bool apply_state_changes);
    void CompleteItemInteraction();
    void Activate();
    bool Save(wiz8::File& hFile);
    bool Load(W8Chunk& hFile, char version, bool from_assets = false);
    void RunLinkedTriggers();
    void FinishAction();
    void GetPosition(glm::vec3* position) const;
    bool CanRunLinkedTriggers();
    /* flags: loaded from the level record's message packed flag; gates
       the m_lData1..3 action message at the end of Run. */
    bool HasActionMessage();
    /* Whether the trigger takes an item: required_item_id >= 0 (the special-item
       notice path) or a type-10 action payload naming item. */
    bool RequiresItem();
    bool SelectAction();
    void GenerateItemGroup();
    W8WorldItem* GetOrCreateItemGroup(bool create);
    /* After a selected-prop Run: while g_trigger_feedback is clear, post either
       the special-item notice (required_item_id != -1) or the nothing-happened notice. */
    void PrintNothingHappenedOrSpecialItemRequired(); /* 0x004456E0 */
    void RunDestination(const char* destination);
    void Run(int source);

    W8TriggerKind trigger_kind;
    char name[0x80];
    int trigger_id;
    /* W8TriggerFlag bits with established producer/consumer semantics. The
       rest of the word is record-loaded or unresolved state and stays masked
       by literal. */
    unsigned int flags;
    float range_minimum;
    float range_maximum;
    int action_value;
    unsigned char state_count;
    unsigned char state_index;
    unsigned char state_direction;
    unsigned char cycle_bounce;
    W8TriggerStateModificationMode state_mod_mode;
    unsigned char unknown_0b5[3];
    int surface_id;
    int m_lData1;
    int m_lData2;
    int m_lData3;
    unsigned short searchable;
    unsigned char unknown_0ca[2];
    glm::vec3 representation_vectors[4];
    float angle;
    glm::vec3 direction;
    W8TriggerRepresentationKind m_bRepType;
    unsigned char unknown_10d[3];
    W8Prop* m_pProp;
    W8Item* rep_item;
    glm::vec3 position;
    W8World* m_pWorld;
    char action_data[0x80];
    char alternate_action_data[0x80];
    signed char action_data_mode;
    signed char sound_volume;
    unsigned short initial_action;
    unsigned short alternate_action;
    unsigned short fallback_action;
    unsigned short action;
    W8TriggerActionState action_state;
    unsigned char unknown_233;
    std::unique_ptr<W8TriggerActionData> m_pActionData;
    std::string recipients;
    int required_item_id;
    std::string required_states;
    std::string state_to_modify;
    std::unique_ptr<W8TriggerEvent> m_pEvent;
    char inline_action_data[0x100];
    std::unique_ptr<W8WorldItem> world_item_group;
    unsigned char items_generated;
    unsigned char unknown_351[3];
    /* Sampled during construction, persisted in saves and used to reseed
       item-table generation so a trigger's generated loot is repeatable. */
    unsigned int item_group_seed;
    int gold;
    int uses_remaining;
    ActivationCallback activation_callback;
    bool running;
    unsigned char unknown_365[3];
    W8LockState lock_state;
};

void InitializeStateDrivenPropVariables(Trigger* trigger);

Trigger* FindTriggerByName(const char* name);
Trigger* FindRequiredTriggerByName(const char* name,
                                   std::source_location caller = std::source_location::current());

inline void RunNamedTrigger(const char* name, int source)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger != 0) {
        trigger->Run(source);
    }
}

std::unique_ptr<W8TriggerActionData> ReadDoorTriggerActionData(wiz8::File* handle);
/* The TRES save chunk: the world's triggers, their runtime states, and their
   action data. */
int ResetNextTriggerId(void);
void SaveWorldTriggers(wiz8::File& handle);
bool LoadWorldTriggers(W8World* world, W8Chunk& handle, bool from_assets = false);
void SaveTriggerRuntimeStates(wiz8::File& handle);
bool LoadTriggerRuntimeStates(W8World& world, W8Chunk& handle);
void SaveTriggerActionData(wiz8::File& handle);
bool LoadTriggerActionData(W8World& world, W8Chunk& handle);

extern bool g_trigger_feedback;
/* Camera position cached by the per-frame trigger walk. */
extern glm::vec3 g_trigger_camera;
/* Trigger's action camera offset, added to the world scene position while an
   action is active, and the flag that says one is. */
extern bool g_trigger_action_active;
extern glm::vec3 g_trigger_action_scene_offset;
extern int g_container_event_alt;
extern int g_trap_notice_event;
extern int g_lock_notice_event;
extern int g_container_event;

bool AnyPropTriggerInView();

void UpdateWorldTriggers(W8World* world);
Trigger* FindTriggerForProp(W8World* world, W8Prop* prop);

stLight* FindLightByName(const char* name);
void DestroyAllWorldTriggers(W8World* world);
/* Walk the world's triggers for a type-0x34 RunDestination trigger whose
   annulus contains the position; answers true when one does. */
bool InsideDestinationTrigger(float x, float y, float z);

// Stop callbacks before retiring the primary world; trigger owners survive until teardown.
