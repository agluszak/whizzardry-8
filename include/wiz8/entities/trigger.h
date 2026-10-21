#pragma once
struct W8Chunk;
struct SavedTrigger;

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

std::vector<std::string> ParseTriggerNames(std::string_view text);

#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/locks.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"

class Light;
class Prop;
class Trigger;
struct World;
struct W8WorldItem;
namespace wiz8 {
class File;
}

// Trigger events own both timers; the active queue borrows event pointers.
class TriggerEvent {
public:
    TriggerEvent();
    ~TriggerEvent();
    void Update();
    struct Shake {
        std::optional<ShakeDefinition> effect;
        int intensity = 1;
        bool reverse = false;
    };
    std::optional<Shake> shake;
    void UpdateShake();

    short action;
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

struct W8EnvironmentTriggerActionData {
    float previous_environment = 0.0f;
};

struct W8DoorTriggerActionData {
    bool open : 1 = false;
    bool lockable : 1 = false;
    bool locked : 1 = false;
    bool auto_locking : 1 = false;
    bool one_way : 1 = false;
    bool secret : 1 = false;
    bool found : 1 = true;
    bool jammable : 1 = false;
    bool jammed : 1 = false;
    bool has_position : 1 = false;
    short item = -1; /* key item id; -1 for none */
    char linked_trigger[0x80]{};
    glm::vec3 position{0.0f};
};

struct W8StringTriggerActionData {
    std::string owned_string;
};

using W8TriggerActionPayload = std::variant<std::monostate, W8EnvironmentTriggerActionData,
                                            W8StringTriggerActionData, W8DoorTriggerActionData>;

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

enum W8TriggerRepresentationKind : unsigned char {
    W8_TRIGGER_REP_NONE = 0,
    W8_TRIGGER_REP_ITEM = 1,
    W8_TRIGGER_REP_PROP = 2,
    W8_TRIGGER_REP_POSITION = 3
};

enum class W8TriggerKind : unsigned char { none = 0, item = 1, prop = 2 };
enum class W8TriggerActionState : unsigned char {
    inactive = 1,
    initial_action = 2,
    alternate_action = 3,
    fallback_action = 4
};
enum class W8TriggerStateDirection : signed char { stopped = 0, forward = 1, backward = -1 };
enum class W8TriggerStateModificationMode : unsigned char {
    none = 0,
    set = 1,
    clear = 2,
    toggle = 3
};
enum class W8TriggerActionDataMode : signed char {
    toggle_alternate = 0,
    alternate_action = 1,
    fallback_action = 2
};

class Trigger {
public:
    /* Decides whether an activation runs the trigger's action. Level scripts and NPC
       bindings install it; it may refer to the level script that owns it. */
    using ActivationCallback = std::function<bool(Trigger* trigger)>;

    explicit Trigger(World* world = nullptr);
    ~Trigger();

    W8DoorTriggerActionData* doorData()
    {
        return std::get_if<W8DoorTriggerActionData>(&action_payload);
    }
    const W8DoorTriggerActionData* doorData() const
    {
        return std::get_if<W8DoorTriggerActionData>(&action_payload);
    }
    bool hasActionPayload() const
    {
        return !std::holds_alternative<std::monostate>(action_payload);
    }
    W8TriggerPayloadKind actionPayloadKind() const
    {
        if (std::holds_alternative<W8DoorTriggerActionData>(action_payload))
            return W8_TRIGGER_PAYLOAD_DOOR;
        if (std::holds_alternative<W8EnvironmentTriggerActionData>(action_payload))
            return W8_TRIGGER_PAYLOAD_ENVIRONMENT;
        if (std::holds_alternative<W8StringTriggerActionData>(action_payload))
            return W8_TRIGGER_PAYLOAD_STRING;
        return W8_TRIGGER_PAYLOAD_NONE;
    }

    static Trigger* CreateAndLoadLevelTrigger(wiz8::File* handle, World* world);

    Prop* GetProp() const
    {
        assert(m_bRepType == W8_TRIGGER_REP_PROP);
        return m_pProp;
    }

    bool HasActorWithinRadius(float radius, bool include_party);
    bool PlayActionSound(std::string_view sound_name, int volume);
    void UpdateActionAnimation();
    void CommitActionResult(bool apply_state_changes);
    void CompleteItemInteraction();
    void Activate();
    void Save(wiz8::File& hFile);
    void Restore(SavedTrigger saved);
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
    void RunDestination(std::string_view destination);
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
    W8TriggerStateDirection state_direction;
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
    Prop* m_pProp;
    glm::vec3 position;
    World* m_pWorld;
    char action_data[0x80];
    char alternate_action_data[0x80];
    W8TriggerActionDataMode action_data_mode;
    signed char sound_volume;
    unsigned short initial_action;
    unsigned short alternate_action;
    unsigned short fallback_action;
    unsigned short action;
    W8TriggerActionState action_state;
    unsigned char unknown_233;
    W8TriggerActionPayload action_payload;
    std::vector<std::string> recipients;
    // Scalar actions can coexist with linked recipients on the same trigger.
    std::string recipient_name;
    int recipient_group = 0;
    int required_item_id;
    std::vector<std::string> required_states;
    std::string state_to_modify;
    std::unique_ptr<TriggerEvent> m_pEvent;
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
    LockState lock_state;
};

void InitializeStateDrivenPropVariables(Trigger* trigger);

Trigger* FindTriggerByName(std::string_view name);
// Throws when the loaded level lacks the trigger.
Trigger* FindRequiredTriggerByName(std::string_view name);

inline void RunNamedTrigger(std::string_view name, int source)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger != 0) {
        trigger->Run(source);
    }
}

W8DoorTriggerActionData ReadDoorTriggerActionData(wiz8::File* handle);
/* The TRES save chunk: the world's triggers, their runtime states, and their
   action data. */
int ResetNextTriggerId(void);
void SaveWorldTriggers(wiz8::File& handle);
void SaveTriggerRuntimeStates(wiz8::File& handle);
void SaveTriggerActionData(wiz8::File& handle);

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

void UpdateWorldTriggers(World* world);
Trigger* FindTriggerForProp(World* world, Prop* prop);

Light* FindLightByName(std::string_view name);
/* Walk the world's triggers for a type-0x34 RunDestination trigger whose
   annulus contains the position; answers true when one does. */
bool InsideDestinationTrigger(const World& world, float x, float y, float z);

// Stop callbacks before retiring the primary world; trigger owners survive until teardown.

Trigger* FindTriggerInWorld(World& world, std::string_view name);
/* Trigger action 0x0b: mark the first placed group of a monster species active and
   restart its first member's animation and pathing when that member is loaded. */
void WakeMonsterGroup(int monster_id);
