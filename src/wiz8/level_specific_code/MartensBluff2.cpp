#include "wiz8/level_specific_code/MartensBluff2.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "local_code/SpellEffect.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/cursor.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/location_variables.h"
#include "wiz8/math.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/string_database.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/world.h"

/* Level Specific Code\MartensBluff2.cpp (level 6).

   Attribution evidence: the spawn lookups at 0x004DCC5F and 0x004DDE4F pass
   this file's path string to MonsterGetIndexByLocationID, and the
   Trigger::m_bRepType assertions quote it. The level-6 block of
   InitializeLevelMasterFunctions registers the surrounding cluster
   (Arrowtraptrigger, Spikeballtrigger, DoorBolt, DummyLever, Dummy,
   PerfumeBox, StoneIdol, BlueFlowers, SquisherControls, DoorControls). */

enum class RavenQuestState : int { not_started, dummy_activated, perfume_used, rapax_scripted };
enum class W8CrusherState : int { inactive, moving, waiting_for_dummies };

static RavenQuestState GetRavenQuestState()
{
    return static_cast<RavenQuestState>(GetLocationVarValueByName("RavenQuest"));
}

static void SetRavenQuestState(RavenQuestState state)
{
    SetTriggerVariableByName("RavenQuest", static_cast<int>(state));
}

static bool g_idol_gas_armed = true;
static bool g_crusher_excluded_flag;
static bool g_crusher_active;
static std::optional<W8IntervalGate> g_spikeball_gate;
static int g_spikeball_count;
static Monster* g_crusher_excluded;
static Prop* g_squisher3_prop;
static Prop* g_squisher4_prop;
static Prop* g_dummy_prop;
static Prop* g_dummy_rope_prop;
static Sound3D* g_crusher_sound;
static W8CrusherState g_crusher_state = W8CrusherState::inactive;
static std::optional<W8IntervalGate> g_idol_gas_gate;

/* Level init: creates the RavenQuest location variable, fires the Dummy
   trigger when the quest has not started, re-arms the perfume box and dummy
   lever from the saved quest state, respawns the rapax with its move script
   at state 2, re-arms the side-gate text while the bolt is open and restores
   the spikeball/crusher master functions from their saved counts. */
void MartensBluff2Setup(void)
{
    Trigger* pTrigger;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    glm::vec3 position;
    RavenQuestState quest_state;
    int location_id;
    int value;

    pTrigger = FindTriggerByName("PerfumeBox");
    quest_state = RavenQuestState::not_started;
    g_crusher_active = false;
    if (pTrigger != 0) {
        if (GetLocationVarIDByName("RavenQuest") == -1) {
            CreateLocationVar("RavenQuest", 0);
            g_running_trigger_from_script = true;
            FindTriggerByName("Dummy")->Run(-1);
            g_running_trigger_from_script = false;
        } else {
            quest_state = GetRavenQuestState();
        }
        pTrigger->flags &= ~W8_TRIGGER_ENABLED;
        if (quest_state != RavenQuestState::not_started) {
            if (quest_state == RavenQuestState::dummy_activated) {
                pTrigger->flags |= W8_TRIGGER_ENABLED;
            }
            FindTriggerByName("DummyLever")->flags &= ~W8_TRIGGER_ENABLED;
            if (quest_state == RavenQuestState::perfume_used) {
                if (FindEntityByName("Ravenz", &position, 0, 0)) {
                    group = SpawnMonsters(0x183, 1, &position, 0, true, false, false);
                    location_id = group->MemberLocationId(0);
                    if (location_id != 0) {
                        info = FindMonster(location_id);
                        if (info != 0 && info->p3D != 0) {
                            info->p3D->SetScript("MB_MoveRapax.msf", true);
                            SetRavenQuestState(RavenQuestState::rapax_scripted);
                        }
                    }
                }
            }
        }
    }
    pTrigger = FindTriggerByName("DoorBolt");
    if (pTrigger != 0 && pTrigger->state_index == 1) {
        pTrigger = FindTriggerByName("SideGateText");
        if (pTrigger != 0) {
            pTrigger->flags &= ~W8_TRIGGER_ENABLED;
        }
    }
    if (GetLocationVarIDByName("SpikedBallLauncher") != -1) {
        value = GetLocationVarValueByName("SpikedBallLauncher");
        if (value != 0) {
            MartensBluff2Spikeball(value);
        }
    }
    if (GetLocationVarIDByName("MonsterCrusher") != -1) {
        value = GetLocationVarValueByName("MonsterCrusher");
        if (value != 0) {
            MartensBluff2MonsterCrusher(value);
        }
    }
}

static void FireTrapLauncher(const char* name, W8SpellEffectDefinition* effect)
{
    glm::vec3 position;
    glm::vec3 offset;
    glm::vec3 direction;
    glm::mat3 rotation;
    float angle;
    Missile* missile;

    if (FindEntityByName(name, &position, &angle, &direction)) {
        offset = glm::vec3(0.0, 0.0, 15000.0);
        rotation = glm::mat3(1);
        if (angle != 0.0) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                      normalizedOrZero(glm::vec3(direction))));
        }
        offset = position + glm::vec3{rotation * offset};
        missile = FireMissile(0, &position, &offset, 0, 0, 1, 50000.0f);
        missile->SetEffectDefinition(effect);
        CreateAndPlaySoundNode("Data\\Sound\\Combat\\Blow_Gun_Attack_01.wav", position, 1.0f,
                               15000.0f, false);
    }
}

/* "Arrowtraptrigger": fires a missile from each named launcher entity along
   its facing direction, rotated by the entity's yaw angle. */
bool TriggerArrowTrap(Trigger* pTrigger)
{
    W8SpellEffectDefinition effect;
    glm::vec3 position;
    glm::vec3 offset;
    glm::vec3 direction;
    glm::mat3 rotation;
    float angle;
    Missile* missile;

    ClearAttackBlock(&effect);
    effect.magnitude.base = 0;
    effect.magnitude.count = 2;
    effect.magnitude.sides = 6;
    FireTrapLauncher("Arrowlauncher1", &effect);
    FireTrapLauncher("Arrowlauncher2", &effect);
    FireTrapLauncher("Arrowlauncher3", &effect);
    if (FindEntityByName("Arrowlauncher4", &position, &angle, &direction)) {
        offset = glm::vec3(0.0, 0.0, 15000.0);
        rotation = glm::mat3(1);
        rotation *=
            glm::mat3_cast(glm::angleAxis(float(angle), normalizedOrZero(glm::vec3(direction))));
        offset = rotation * offset + position;
        missile = FireMissile(0, &position, &offset, 0, 0, 1, 50000.0f);
        missile->SetEffectDefinition(&effect);
        CreateAndPlaySoundNode("Data\\Sound\\Combat\\Blow_Gun_Attack_01.wav", position, 1.0f,
                               15000.0f, false);
    }
    return true;
}

/* "Spikeballtrigger": toggles the spikeball master function's launcher
   sequence through its toggle command. */
bool MartensBluff2Spikeballtrigger(Trigger* pTrigger)
{
    MartensBluff2Spikeball(static_cast<int>(0xEFFFFFFF));
    return true;
}

static void CaptureSpikeball(W8SavedLocationState& state)
{
    state.emplace_back("SpikedBallLauncher", g_spikeball_count > 0xf ? 0 : g_spikeball_count);
}

/* The registered spikeball master function. A nonzero command arms it: -1
   persists the shot count, 0xEFFFFFFF toggles the sequence off or on, and any
   other small value is the starting shot count. Command 0 is the per-frame
   run; it fires the four Spikeball-launcher entities every two seconds until
   the count reaches sixteen, then unregisters and saves the reset. */
bool MartensBluff2Spikeball(int command)
{
    W8SpellEffectDefinition effect;
    glm::vec3 position;
    glm::vec3 offset;
    glm::vec3 direction;
    glm::mat3 rotation;
    float angle;
    Missile* missile;

    if (command != 0) {
        if (command == static_cast<int>(0xEFFFFFFF)) {
            if (g_spikeball_count == 0 || g_spikeball_count > 0xf) {
                g_spikeball_count = 0;
            } else {
                g_spikeball_count = 1;
            }
        } else if (static_cast<unsigned int>(command) <= 0xf) {
            g_spikeball_count = command;
        }
        if (!g_spikeball_gate) {
            g_spikeball_gate.emplace(2.0f, false, true);
            g_master_functions.push_back({MartensBluff2Spikeball, CaptureSpikeball});
        }
    }
    if (g_spikeball_count < 0x10) {
        if (!g_spikeball_gate) return true;
        if (g_spikeball_count == 0 || g_spikeball_gate->PollFinished()) {
            g_spikeball_gate->Arm();
            ++g_spikeball_count;
            ClearAttackBlock(&effect);
            effect.magnitude.base = 0;
            effect.magnitude.count = 2;
            effect.magnitude.sides = 6;
            FireTrapLauncher("Spikeball-launcher1", &effect);
            FireTrapLauncher("Spikeball-launcher2", &effect);
            FireTrapLauncher("Spikeball-launcher3", &effect);
            if (FindEntityByName("Spikeball-launcher4", &position, &angle, &direction)) {
                offset = glm::vec3(0.0, 0.0, 15000.0);
                rotation = glm::mat3(1);
                if (angle != 0.0) {
                    rotation *=
                        glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                                      normalizedOrZero(glm::vec3(direction))));
                }
                offset = rotation * offset + position;
                missile = FireMissile(0, &position, &offset, 0, 0, 1, 50000.0f);
                missile->SetEffectDefinition(&effect);
                CreateAndPlaySoundNode("Data\\Sound\\Combat\\Blow_Gun_Attack_01.wav", position,
                                       1.0f, 15000.0f, false);
            }
        }
        return false;
    }
    g_spikeball_gate.reset();
    g_spikeball_count = 0;
    if (GetLocationVarIDByName("SpikedBallLauncher") == -1) {
        CreateLocationVar("SpikedBallLauncher", g_spikeball_count);
    } else {
        SetTriggerVariableByName("SpikedBallLauncher", g_spikeball_count);
    }
    return true;
}

/* "DoorBolt": re-arms the side-gate text trigger when the bolt opens. */
bool MartensBluff2DoorBolt(Trigger* pTrigger)
{
    Trigger* pText = FindTriggerByName("SideGateText");
    if (pText != 0) {
        pText->flags &= ~W8_TRIGGER_ENABLED;
    }
    return true;
}

/* "DummyLever": re-arms itself and reports whether the RavenQuest location
   variable is still zero. */
bool MartensBluff2DummyLever(Trigger* pTrigger)
{
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    return GetRavenQuestState() == RavenQuestState::not_started;
}

/* "Dummy": re-arms the PerfumeBox trigger and marks RavenQuest stage 1. The
   shared callback flag suppresses this while the quest state is being driven
   programmatically. */
bool MartensBluff2Dummy(Trigger* pTrigger)
{
    if (!g_running_trigger_from_script) {
        Trigger* pPerfumeBox = FindTriggerByName("PerfumeBox");
        if (pPerfumeBox != 0) {
            pPerfumeBox->flags |= W8_TRIGGER_ENABLED;
            SetRavenQuestState(RavenQuestState::dummy_activated);
        }
    }
    return true;
}

/* "PerfumeBox": while item 0x2ea (the perfume) is on the cursor, consume it,
   spawn the rapax on Ravenz with its move script and post the result string.
   Always returns 0. */
bool MartensBluff2PerfumeBox(Trigger* pTrigger)
{
    glm::vec3 position;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int location_id;

    RavenQuestState quest_state;

    if (!(CurrentGame().party.held.item.iItemNo != -1)) {
        return false;
    }
    if (GetItemInHand(CurrentGame().party) != 0x2ea) {
        return false;
    }
    g_trigger_feedback = true;
    (void)TakeInventoryItem(CurrentGame(), {W8ItemStorage::Hand});
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    quest_state = RavenQuestState::perfume_used;
    if (FindEntityByName("Ravenz", &position, 0, 0)) {
        group = SpawnMonsters(0x183, 1, &position, 0, true, false, false);
        location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            info = FindMonster(location_id);
            if (info != 0 && info->p3D != 0) {
                info->p3D->SetScript("MB_MoveRapax.msf", true);
                quest_state = RavenQuestState::rapax_scripted;
            }
        }
    }
    SetRavenQuestState(quest_state);
    ShowString(Data().strings[0x71c]);
    return false;
}

/* "DoorControls": while the control trigger is off, fires the two squisher
   doors whose door action data is open. */
bool MartensBluff2DoorControls(Trigger* pTrigger)
{
    if (pTrigger->state_index == 0) {
        for (const char* name : {"SquisherDoor", "SquisherDoor1"}) {
            Trigger* pDoor = FindTriggerByName(name);
            if (pDoor == nullptr) {
                continue;
            }
            if (const auto* door = pDoor->doorData(); door && door->open) {
                pDoor->Run(-1);
            }
        }
    }
    return true;
}

/* "SquisherControls": toggles the monster crusher while it is idle. */
bool MartensBluff2SquisherControls(Trigger* pTrigger)
{
    if (!g_crusher_active) {
        MartensBluff2MonsterCrusher(static_cast<int>(0xEFFFFFFF));
        return true;
    }
    return false;
}

static void CaptureCrusher(W8SavedLocationState& state)
{
    state.emplace_back("MonsterCrusher", static_cast<int>(g_crusher_state));
}

/* The MonsterCrusher master function. A nonzero command looks up the two
   squisher props and arms the sequence; 0xEFFFFFFF is
   the toggle from SquisherControls, and 2 suppresses the hydraulics loop. The
   command-0 run tracks the animation: while the squisher plays it sweeps the
   kill box between the two props' facing planes for monsters, shoving those
   with room aside and crushing the rest (species 0x183 pays out through
   AwardPartyExperience); once the squisher finishes it runs the Dummy and DummyRope
   triggers, and once those props finish it unregisters. */
bool MartensBluff2MonsterCrusher(int command)
{
    /* The bounds box the octree query below sweeps; function-local statics,
       with the compiler's atexit destructor thunks at 0x004DE500/0x004DE510. */
    static glm::vec3 crusher_upper;
    static glm::vec3 crusher_lower;
    Trigger* pTrigger;
    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 centre;
    glm::vec3 position;
    std::vector<w8_ulong> location_ids;
    std::size_t count;
    unsigned int i;
    W8MonsterInfo* info;
    Monster* monster;
    float left;
    float right;
    float radius;

    if (command != 0) {
        g_squisher3_prop = 0;
        g_squisher4_prop = 0;
        g_dummy_prop = 0;
        g_dummy_rope_prop = 0;
        pTrigger = FindTriggerByName("Squisher-3");
        if (pTrigger != 0) {
            g_squisher3_prop = pTrigger->GetProp();
        }
        pTrigger = FindTriggerByName("Squisher-4");
        if (pTrigger != 0) {
            g_squisher4_prop = pTrigger->GetProp();
        }
        if (command != static_cast<int>(0xEFFFFFFF)) {
            g_crusher_state = static_cast<W8CrusherState>(command);
        }
        if (g_squisher3_prop != 0 && g_squisher4_prop != 0) {
            const auto crusher_bounds = g_squisher3_prop->GetFrameBounds();
            const auto other_bounds = g_squisher4_prop->GetFrameBounds();
            if (!crusher_bounds || !other_bounds) return false;
            crusher_lower = crusher_bounds->minimum;
            crusher_upper = crusher_bounds->maximum;
            lower = other_bounds->minimum;
            upper = other_bounds->maximum;
            crusher_lower.y -= kWorldScale;
            crusher_upper.x = upper.x;
            g_crusher_excluded = 0;
            g_crusher_excluded_flag = false;
            centre = glm::vec3((crusher_lower.x + crusher_upper.x) * 0.5,
                               (crusher_lower.y + crusher_upper.y) * 0.5,
                               (crusher_lower.z + crusher_upper.z) * 0.5);
            if (command != 2) {
                g_crusher_sound =
                    CreateAndPlaySoundNode("Data\\Sound\\Ambients\\Hydraulics Squisher Loop.wav",
                                           centre, 0.7f, 30.0f, true);
            }
            g_crusher_active = true;
            g_crusher_state = W8CrusherState::moving;
            g_master_functions.push_back({MartensBluff2MonsterCrusher, CaptureCrusher});
        }
        if (command != 1 && GetRavenQuestState() != RavenQuestState::not_started) {
            g_running_trigger_from_script = true;
            FindTriggerByName("Dummy")->Run(-1);
            FindTriggerByName("DummyRope")->Run(-1);
            g_running_trigger_from_script = false;
        }
        return false;
    }
    if (g_squisher3_prop == 0) {
        return false;
    }
    if (g_squisher4_prop == 0) {
        return false;
    }
    if (g_dummy_prop != 0) {
        if (g_dummy_prop->animation_playing != 0) {
            return false;
        }
        if (g_dummy_rope_prop->animation_playing != 0) {
            return false;
        }

        g_crusher_active = false;
        g_crusher_state = W8CrusherState::inactive;
        return true;
    }
    if (g_squisher3_prop->animation_playing == 0) {
        if (g_crusher_sound != 0) {
            g_crusher_sound->Stop();
        }
        g_crusher_sound = 0;
        pTrigger = FindTriggerByName("Dummy");
        if (GetRavenQuestState() == RavenQuestState::not_started) {

            g_crusher_active = false;
            g_crusher_state = W8CrusherState::inactive;
            return true;
        }
        g_running_trigger_from_script = true;
        pTrigger->Run(-1);
        g_dummy_prop = pTrigger->GetProp();
        pTrigger = FindTriggerByName("DummyRope");
        pTrigger->Run(-1);
        g_dummy_rope_prop = pTrigger->GetProp();
        g_running_trigger_from_script = false;
        g_crusher_state = W8CrusherState::waiting_for_dummies;
        return false;
    }
    left = g_squisher3_prop->computeBounds().maximum.x;
    right = g_squisher4_prop->computeBounds().minimum.x;
    location_ids = GetWorldSpatialIndex()->locationsInBox({crusher_lower, crusher_upper}, 0);
    count = location_ids.size();
    if (count == 0) {
        return false;
    }
    for (i = 0; i < count; i++) {
        info = FindMonster(location_ids[i]);
        if (info != 0 && info->p3D != 0 &&
            (!g_crusher_excluded_flag || info->p3D != g_crusher_excluded) &&
            info->p3D->nav.flags != 0x200000) {
            monster = info->p3D;
            position = monster->nav.GetPosition();
            radius = monster->nav.movement.alternate_radius;
            if (left <= position.x - radius) {
                if (right < position.x + radius) {
                    position.x = right - radius;
                    monster->nav.SetPositionInternal(&position);
                }
            } else if (position.x + radius <= right || Random(100) < 0x24) {
                position.x = left + radius;
                monster->nav.SetPositionInternal(&position);
            } else {
                MonsterStartsDying(info, true);
                if (info->monster_species == 0x183) {
                    AwardPartyExperience(10000, false);
                }
            }
        }
    }
    return false;
}

/* "StoneIdol": while the cursor is free, puts item 0x291 in hand, posts the
   text, activates the IdolGas particle and arms the IdolGas master function. */
bool MartensBluff2StoneIdol(Trigger* pTrigger)
{
    ParticleSystem* particle;

    if ((CurrentGame().party.held.item.iItemNo != -1)) {
        return false;
    }
    ReplaceHeldItem(CurrentGame(), MakeItem(0x291, false, false, false));
    SetItemCursor(0);
    ShowString(Data().strings[0x71d]);
    particle = FindParticleByName("IdolGas");
    if (particle != 0) {
        particle->SetActive(true);
    }
    BeginSurprise();
    MartensBluff2IdolGas(1);
    g_master_functions.push_back({MartensBluff2IdolGas});
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    g_running_trigger_from_script = true;
    SetFact(W8_FACT_QUEST_MARTEN_IDOL, 1, false);
    return true;
}

/* "BlueFlowers": while the cursor is free, puts item 0x2eb in hand. The
   shared callback flag is cleared on every run. */
bool MartensBluff2BlueFlowers(Trigger* pTrigger)
{
    if (!g_running_trigger_from_script) {
        if ((CurrentGame().party.held.item.iItemNo != -1)) {
            return false;
        }
        ReplaceHeldItem(CurrentGame(), MakeItem(0x2eb, false, false, false));
        SetItemCursor(0);
    }
    g_running_trigger_from_script = false;
    return true;
}

/* The IdolGas master function. A nonzero command arms it: fades the world
   lighting out and starts the four-second gate. The command-0 run waits on
   the gate, then fades back in, picks a party member to suffer the gas when
   no kind-0x1e NPC is around and stops the particle; the following run
   unregisters. */
bool MartensBluff2IdolGas(int command)
{
    ParticleSystem* particle;

    if (command != 0) {
        g_idol_gas_armed = true;
        BeginWorldLightingFade(-3000.0f);
        if (g_idol_gas_gate) {
            g_idol_gas_gate->Arm();
            return false;
        }
        g_idol_gas_gate.emplace(4.0f, false, true);
        return false;
    }
    if (!g_idol_gas_armed) {

        g_idol_gas_gate.reset();
        g_idol_gas_armed = true;
        ResolveSurpriseWake();
        return true;
    }
    if (!g_idol_gas_gate) return true;
    if (!g_idol_gas_gate->PollFinished()) {
        return false;
    }
    g_idol_gas_gate->Arm();
    g_idol_gas_armed = false;
    if (FindNpcOfKind(0x1e) == 0) {
        MartensBluff2IdolGasVictim();
    }
    BeginWorldLightingFade(3000.0f);
    particle = FindParticleByName("IdolGas");
    if (particle != 0) {
        particle->SetActive(false);
    }
    return false;
}

/* The IdolGas victim picker: scores the eight party slots by profession
   (fighters 2, rogues 3, priests 5, mages 4, all others 1; empty rows and the
   first two slots sit out at 10), picks a random least-affected member, and
   falls back to the first dead member when nothing scored - a retail
   unreachable path, since a nonzero lowest score implies a candidate. The
   winner gets condition 0x13 indefinitely and fact 0x33 records the event. */
void MartensBluff2IdolGasVictim(void)
{
    unsigned int severities[8];
    unsigned int lowest;
    unsigned int slot;
    unsigned int count;
    unsigned int pick;
    int i;

    lowest = 10;
    for (slot = 0; slot < 8; slot++) {
        if (!CurrentGame().party.rows[slot].fOccupied || slot < 2) {
            severities[slot] = 10;
        } else {
            switch (CurrentGame().party.characters[slot].iProfession) {
            case W8_PROFESSION_FIGHTER:
                severities[slot] = 2;
                break;
            case W8_PROFESSION_ROGUE:
                severities[slot] = 3;
                break;
            case W8_PROFESSION_MAGE:
                severities[slot] = 4;
                break;
            case W8_PROFESSION_PRIEST:
                severities[slot] = 5;
                break;
            default:
                severities[slot] = 1;
                break;
            }
        }
        if (severities[slot] < lowest) {
            lowest = severities[slot];
        }
    }
    if (lowest != 10) {
        count = 0;
        for (i = 0; i < 8; i++) {
            if (severities[i] == lowest) {
                count++;
            }
        }
        if (count == 0) {
            slot = 0;
            while (!CurrentGame().party.rows[slot].fOccupied ||
                   CurrentGame().party.characters[slot].highest_condition != W8_CONDITION_DEAD) {
                slot++;
                if (slot > 7) {
                    return;
                }
            }
        } else {
            pick = Random(count) + 1;
            for (slot = 0; slot < 8; slot++) {
                if (severities[slot] == lowest) {
                    pick--;
                    if (pick == 0) {
                        break;
                    }
                }
            }
        }
        CurrentGame().status.party_slot = slot;
        SetCharacterCondition(slot, W8_CONDITION_MISSING, W8_CONDITION_INDEFINITE, 0, 0, 1);
        SetFact(W8_FACT_CROCK_KIDNAPPED_PLAYER, 1, false);
    }
}

/* glm::mat3::RotateAroundAxis(double, ...) emitted out-of-line for the
   arrow trap's rotation math; the primary is in srMath.h. */
