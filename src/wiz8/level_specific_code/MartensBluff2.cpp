#include "wiz8/level_specific_code/MartensBluff2.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "local_code/SpellEffect.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/combat/combat.h"
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
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/location_variables.h"
#include "wiz8/math.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/string_database.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/world.h"

/* Level Specific Code\MartensBluff2.cpp (level 6).

   Attribution evidence: the spawn lookups at 0x004DCC5F and 0x004DDE4F pass
   this file's path string to MonsterGetIndexByLocationID, and the
   Trigger::m_bRepType assertions quote it. The level-6 script binds the
   surrounding cluster
   (Arrowtraptrigger, Spikeballtrigger, DoorBolt, DummyLever, Dummy,
   PerfumeBox, StoneIdol, BlueFlowers, SquisherControls, DoorControls). */

namespace {

enum class RavenQuestState : int { not_started, dummy_activated, perfume_used, rapax_scripted };
enum class W8CrusherState : int { inactive, moving, waiting_for_dummies };

RavenQuestState GetRavenQuestState()
{
    return static_cast<RavenQuestState>(GetLocationVarValueByName("RavenQuest"));
}

void SetRavenQuestState(RavenQuestState state)
{
    SetTriggerVariableByName("RavenQuest", static_cast<int>(state));
}

/* The master command that toggles a sequence. */
constexpr int kMasterToggle = static_cast<int>(0xEFFFFFFF);

void FireTrapLauncher(World& world, const char* name, W8SpellEffectDefinition* effect);

/* "DoorBolt": re-arms the side-gate text trigger when the bolt opens. */
bool DoorBolt(Trigger* pTrigger)
{
    Trigger* pText = FindTriggerByName("SideGateText");
    if (pText != 0) {
        pText->flags &= ~W8_TRIGGER_ENABLED;
    }
    return true;
}

/* "DummyLever": re-arms itself and reports whether the RavenQuest location
   variable is still zero. */
bool DummyLever(Trigger* pTrigger)
{
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    return GetRavenQuestState() == RavenQuestState::not_started;
}

/* "DoorControls": while the control trigger is off, fires the two squisher
   doors whose door action data is open. */
bool DoorControls(Trigger* pTrigger)
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

class MartensBluff2Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    void setup();
    bool arrowTrap(Trigger* pTrigger);
    bool spikeball(int command);
    void captureSpikeball(W8SavedLocationState& state);
    bool dummy(Trigger* pTrigger);
    bool perfumeBox(Trigger* pTrigger);
    bool squisherControls(Trigger* pTrigger);
    bool monsterCrusher(int command);
    void captureCrusher(W8SavedLocationState& state);
    bool stoneIdol(Trigger* pTrigger);
    bool blueFlowers(Trigger* pTrigger);
    bool idolGas(int command);
    void idolGasVictim();

    bool idol_gas_armed_ = true;
    bool crusher_active_ = false;
    std::optional<W8IntervalGate> spikeball_gate_;
    int spikeball_count_ = 0;
    Prop* squisher3_prop_ = nullptr;
    Prop* squisher4_prop_ = nullptr;
    Prop* dummy_prop_ = nullptr;
    Prop* dummy_rope_prop_ = nullptr;
    Sound3D* crusher_sound_ = nullptr;
    W8CrusherState crusher_state_ = W8CrusherState::inactive;
    std::optional<W8IntervalGate> idol_gas_gate_;
    // The kill box the crusher sweeps.
    glm::vec3 crusher_upper_{};
    glm::vec3 crusher_lower_{};
    /* Raised while the script runs the raven-quest triggers itself, and left
       raised by the stone idol until the blue flowers clear it. */
    bool running_trigger_from_script_ = false;
};

void MartensBluff2Script::enter()
{
    setup();
    bindTrigger("Arrowtraptrigger", &MartensBluff2Script::arrowTrap);
    // "Spikeballtrigger": toggles the spikeball launcher sequence.
    bindTrigger("Spikeballtrigger", [this](Trigger*) {
        spikeball(kMasterToggle);
        return true;
    });
    bindTrigger("DoorBolt", DoorBolt);
    bindTrigger("DummyLever", DummyLever);
    bindTrigger("Dummy", &MartensBluff2Script::dummy);
    bindTrigger("PerfumeBox", &MartensBluff2Script::perfumeBox);
    bindTrigger("StoneIdol", &MartensBluff2Script::stoneIdol);
    bindTrigger("BlueFlowers", &MartensBluff2Script::blueFlowers);
    bindTrigger("SquisherControls", &MartensBluff2Script::squisherControls);
    bindTrigger("DoorControls", DoorControls);
}

/* Level init: creates the RavenQuest location variable, fires the Dummy
   trigger when the quest has not started, re-arms the perfume box and dummy
   lever from the saved quest state, respawns the rapax with its move script
   at state 2, re-arms the side-gate text while the bolt is open and restores
   the spikeball/crusher master functions from their saved counts. */
void MartensBluff2Script::setup()
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
    if (pTrigger != 0) {
        if (GetLocationVarIDByName("RavenQuest") == -1) {
            CreateLocationVar("RavenQuest", 0);
            running_trigger_from_script_ = true;
            FindTriggerByName("Dummy")->Run(-1);
            running_trigger_from_script_ = false;
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
                    group = SpawnMonsters(0x183, 1, &position, 0, true, false);
                    location_id = group->MemberLocationId(0);
                    if (location_id != 0) {
                        info = FindMonster(location_id);
                        if (info != 0 && info->entity) {
                            info->entity->SetScript("MB_MoveRapax.msf", true);
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
            spikeball(value);
        }
    }
    if (GetLocationVarIDByName("MonsterCrusher") != -1) {
        value = GetLocationVarValueByName("MonsterCrusher");
        if (value != 0) {
            monsterCrusher(value);
        }
    }
}

void FireTrapLauncher(World& world, const char* name, W8SpellEffectDefinition* effect)
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
        CreateAndPlaySoundNode(world, "Data\\Sound\\Combat\\Blow_Gun_Attack_01.wav", position, 1.0f,
                               15000.0f, false);
    }
}

/* "Arrowtraptrigger": fires a missile from each named launcher entity along
   its facing direction, rotated by the entity's yaw angle. */
bool MartensBluff2Script::arrowTrap(Trigger* pTrigger)
{
    W8SpellEffectDefinition effect;
    glm::vec3 position;
    glm::vec3 offset;
    glm::vec3 direction;
    glm::mat3 rotation;
    float angle;
    Missile* missile;

    effect = {};
    effect.magnitude.base = 0;
    effect.magnitude.count = 2;
    effect.magnitude.sides = 6;
    FireTrapLauncher(world(), "Arrowlauncher1", &effect);
    FireTrapLauncher(world(), "Arrowlauncher2", &effect);
    FireTrapLauncher(world(), "Arrowlauncher3", &effect);
    if (FindEntityByName("Arrowlauncher4", &position, &angle, &direction)) {
        offset = glm::vec3(0.0, 0.0, 15000.0);
        rotation = glm::mat3(1);
        rotation *=
            glm::mat3_cast(glm::angleAxis(float(angle), normalizedOrZero(glm::vec3(direction))));
        offset = rotation * offset + position;
        missile = FireMissile(0, &position, &offset, 0, 0, 1, 50000.0f);
        missile->SetEffectDefinition(&effect);
        CreateAndPlaySoundNode(world(), "Data\\Sound\\Combat\\Blow_Gun_Attack_01.wav", position,
                               1.0f, 15000.0f, false);
    }
    return true;
}

void MartensBluff2Script::captureSpikeball(W8SavedLocationState& state)
{
    state.emplace_back("SpikedBallLauncher", spikeball_count_ > 0xf ? 0 : spikeball_count_);
}

/* The registered spikeball master function. A nonzero command arms it: -1
   persists the shot count, 0xEFFFFFFF toggles the sequence off or on, and any
   other small value is the starting shot count. Command 0 is the per-frame
   run; it fires the four Spikeball-launcher entities every two seconds until
   the count reaches sixteen, then unregisters and saves the reset. */
bool MartensBluff2Script::spikeball(int command)
{
    W8SpellEffectDefinition effect;
    glm::vec3 position;
    glm::vec3 offset;
    glm::vec3 direction;
    glm::mat3 rotation;
    float angle;
    Missile* missile;

    if (command != 0) {
        if (command == kMasterToggle) {
            if (spikeball_count_ == 0 || spikeball_count_ > 0xf) {
                spikeball_count_ = 0;
            } else {
                spikeball_count_ = 1;
            }
        } else if (static_cast<unsigned int>(command) <= 0xf) {
            spikeball_count_ = command;
        }
        if (!spikeball_gate_) {
            spikeball_gate_.emplace(2.0f, TimerClock::World, true);
            addMaster(&MartensBluff2Script::spikeball, &MartensBluff2Script::captureSpikeball);
        }
    }
    if (spikeball_count_ < 0x10) {
        if (!spikeball_gate_) return true;
        if (spikeball_count_ == 0 || spikeball_gate_->PollFinished()) {
            spikeball_gate_->Arm();
            ++spikeball_count_;
            effect = {};
            effect.magnitude.base = 0;
            effect.magnitude.count = 2;
            effect.magnitude.sides = 6;
            FireTrapLauncher(world(), "Spikeball-launcher1", &effect);
            FireTrapLauncher(world(), "Spikeball-launcher2", &effect);
            FireTrapLauncher(world(), "Spikeball-launcher3", &effect);
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
                CreateAndPlaySoundNode(world(), "Data\\Sound\\Combat\\Blow_Gun_Attack_01.wav",
                                       position, 1.0f, 15000.0f, false);
            }
        }
        return false;
    }
    spikeball_gate_.reset();
    spikeball_count_ = 0;
    if (GetLocationVarIDByName("SpikedBallLauncher") == -1) {
        CreateLocationVar("SpikedBallLauncher", spikeball_count_);
    } else {
        SetTriggerVariableByName("SpikedBallLauncher", spikeball_count_);
    }
    return true;
}

/* "Dummy": re-arms the PerfumeBox trigger and marks RavenQuest stage 1. The
   shared callback flag suppresses this while the quest state is being driven
   programmatically. */
bool MartensBluff2Script::dummy(Trigger* pTrigger)
{
    if (!running_trigger_from_script_) {
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
bool MartensBluff2Script::perfumeBox(Trigger* pTrigger)
{
    glm::vec3 position;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int location_id;

    RavenQuestState quest_state;

    if (!(ctx.inventory.held())) {
        return false;
    }
    if (ctx.inventory.heldItemId() != 0x2ea) {
        return false;
    }
    g_trigger_feedback = true;
    (void)ctx.inventory.take({W8ItemStorage::Hand});
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    quest_state = RavenQuestState::perfume_used;
    if (FindEntityByName("Ravenz", &position, 0, 0)) {
        group = SpawnMonsters(0x183, 1, &position, 0, true, false);
        location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            info = FindMonster(location_id);
            if (info != 0 && info->entity) {
                info->entity->SetScript("MB_MoveRapax.msf", true);
                quest_state = RavenQuestState::rapax_scripted;
            }
        }
    }
    SetRavenQuestState(quest_state);
    ShowString(Data().strings[0x71c]);
    return false;
}

/* "SquisherControls": toggles the monster crusher while it is idle. */
bool MartensBluff2Script::squisherControls(Trigger* pTrigger)
{
    if (!crusher_active_) {
        monsterCrusher(kMasterToggle);
        return true;
    }
    return false;
}

void MartensBluff2Script::captureCrusher(W8SavedLocationState& state)
{
    state.emplace_back("MonsterCrusher", static_cast<int>(crusher_state_));
}

/* The MonsterCrusher master function. A nonzero command looks up the two
   squisher props and arms the sequence; 0xEFFFFFFF is
   the toggle from SquisherControls, and 2 suppresses the hydraulics loop. The
   command-0 run tracks the animation: while the squisher plays it sweeps the
   kill box between the two props' facing planes for monsters, shoving those
   with room aside and crushing the rest (species 0x183 pays out through
   AwardPartyExperience); once the squisher finishes it runs the Dummy and DummyRope
   triggers, and once those props finish it unregisters. */
bool MartensBluff2Script::monsterCrusher(int command)
{
    Trigger* pTrigger;
    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 centre;
    glm::vec3 position;
    std::vector<std::uint32_t> location_ids;
    std::size_t count;
    unsigned int i;
    W8MonsterInfo* info;
    float left;
    float right;
    float radius;

    if (command != 0) {
        squisher3_prop_ = 0;
        squisher4_prop_ = 0;
        dummy_prop_ = 0;
        dummy_rope_prop_ = 0;
        pTrigger = FindTriggerByName("Squisher-3");
        if (pTrigger != 0) {
            squisher3_prop_ = pTrigger->GetProp();
        }
        pTrigger = FindTriggerByName("Squisher-4");
        if (pTrigger != 0) {
            squisher4_prop_ = pTrigger->GetProp();
        }
        if (command != kMasterToggle) {
            crusher_state_ = static_cast<W8CrusherState>(command);
        }
        if (squisher3_prop_ != 0 && squisher4_prop_ != 0) {
            const auto crusher_bounds = squisher3_prop_->GetFrameBounds();
            const auto other_bounds = squisher4_prop_->GetFrameBounds();
            if (!crusher_bounds || !other_bounds) return false;
            crusher_lower_ = crusher_bounds->minimum;
            crusher_upper_ = crusher_bounds->maximum;
            lower = other_bounds->minimum;
            upper = other_bounds->maximum;
            crusher_lower_.y -= kWorldScale;
            crusher_upper_.x = upper.x;
            centre = glm::vec3((crusher_lower_.x + crusher_upper_.x) * 0.5,
                               (crusher_lower_.y + crusher_upper_.y) * 0.5,
                               (crusher_lower_.z + crusher_upper_.z) * 0.5);
            if (command != 2) {
                crusher_sound_ = CreateAndPlaySoundNode(
                    world(), "Data\\Sound\\Ambients\\Hydraulics Squisher Loop.wav", centre, 0.7f,
                    30.0f, true);
            }
            crusher_active_ = true;
            crusher_state_ = W8CrusherState::moving;
            addMaster(&MartensBluff2Script::monsterCrusher, &MartensBluff2Script::captureCrusher);
        }
        if (command != 1 && GetRavenQuestState() != RavenQuestState::not_started) {
            running_trigger_from_script_ = true;
            FindTriggerByName("Dummy")->Run(-1);
            FindTriggerByName("DummyRope")->Run(-1);
            running_trigger_from_script_ = false;
        }
        return false;
    }
    if (squisher3_prop_ == 0) {
        return false;
    }
    if (squisher4_prop_ == 0) {
        return false;
    }
    if (dummy_prop_ != 0) {
        if (dummy_prop_->animation_playing != 0) {
            return false;
        }
        if (dummy_rope_prop_->animation_playing != 0) {
            return false;
        }

        crusher_active_ = false;
        crusher_state_ = W8CrusherState::inactive;
        return true;
    }
    if (squisher3_prop_->animation_playing == 0) {
        if (crusher_sound_ != 0) {
            crusher_sound_->Stop();
        }
        crusher_sound_ = 0;
        pTrigger = FindTriggerByName("Dummy");
        if (GetRavenQuestState() == RavenQuestState::not_started) {

            crusher_active_ = false;
            crusher_state_ = W8CrusherState::inactive;
            return true;
        }
        running_trigger_from_script_ = true;
        pTrigger->Run(-1);
        dummy_prop_ = pTrigger->GetProp();
        pTrigger = FindTriggerByName("DummyRope");
        pTrigger->Run(-1);
        dummy_rope_prop_ = pTrigger->GetProp();
        running_trigger_from_script_ = false;
        crusher_state_ = W8CrusherState::waiting_for_dummies;
        return false;
    }
    left = squisher3_prop_->computeBounds().maximum.x;
    right = squisher4_prop_->computeBounds().minimum.x;
    location_ids = GetWorldSpatialIndex()->locationsInBox({crusher_lower_, crusher_upper_}, 0);
    count = location_ids.size();
    if (count == 0) {
        return false;
    }
    for (i = 0; i < count; i++) {
        info = FindMonster(location_ids[i]);
        Monster* monster = info ? info->entity.find() : nullptr;
        if (monster && monster->nav.flags != 0x200000) {
            position = monster->nav.GetPosition();
            radius = monster->nav.movement.alternate_radius;
            if (left <= position.x - radius) {
                if (right < position.x + radius) {
                    position.x = right - radius;
                    monster->nav.SetPositionInternal(&position);
                }
            } else if (position.x + radius <= right || ctx.game.rng.below(100) < 0x24) {
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
bool MartensBluff2Script::stoneIdol(Trigger* pTrigger)
{
    ParticleSystem* particle;

    if ((ctx.inventory.held())) {
        return false;
    }
    ctx.inventory.replaceHeld(MakeItem(Data(), ctx.game.rng, 0x291, false, false, false));
    ShowString(Data().strings[0x71d]);
    particle = FindParticleByName("IdolGas");
    if (particle != 0) {
        particle->SetActive(true);
    }
    BeginSurprise();
    idolGas(1);
    addMaster(&MartensBluff2Script::idolGas);
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    running_trigger_from_script_ = true;
    SetFact(W8_FACT_QUEST_MARTEN_IDOL, 1, false);
    return true;
}

/* "BlueFlowers": while the cursor is free, puts item 0x2eb in hand. The
   shared callback flag is cleared on every run. */
bool MartensBluff2Script::blueFlowers(Trigger* pTrigger)
{
    if (!running_trigger_from_script_) {
        if ((ctx.inventory.held())) {
            return false;
        }
        ctx.inventory.replaceHeld(MakeItem(Data(), ctx.game.rng, 0x2eb, false, false, false));
    }
    running_trigger_from_script_ = false;
    return true;
}

/* The IdolGas master function. A nonzero command arms it: fades the world
   lighting out and starts the four-second gate. The command-0 run waits on
   the gate, then fades back in, picks a party member to suffer the gas when
   no kind-0x1e NPC is around and stops the particle; the following run
   unregisters. */
bool MartensBluff2Script::idolGas(int command)
{
    ParticleSystem* particle;

    if (command != 0) {
        idol_gas_armed_ = true;
        BeginWorldLightingFade(-3000.0f);
        if (idol_gas_gate_) {
            idol_gas_gate_->Arm();
            return false;
        }
        idol_gas_gate_.emplace(4.0f, TimerClock::World, true);
        return false;
    }
    if (!idol_gas_armed_) {

        idol_gas_gate_.reset();
        idol_gas_armed_ = true;
        ResolveSurpriseWake();
        return true;
    }
    if (!idol_gas_gate_) return true;
    if (!idol_gas_gate_->PollFinished()) {
        return false;
    }
    idol_gas_gate_->Arm();
    idol_gas_armed_ = false;
    if (FindNpcOfKind(0x1e) == 0) {
        idolGasVictim();
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
void MartensBluff2Script::idolGasVictim()
{
    unsigned int severities[8];
    unsigned int lowest;
    unsigned int slot;
    unsigned int count;
    unsigned int pick;
    int i;

    lowest = 10;
    for (slot = 0; slot < 8; slot++) {
        if (!ctx.party.members[slot].has_value() || slot < 2) {
            severities[slot] = 10;
        } else {
            switch (ctx.party.characters[slot].iProfession) {
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
            while (!ctx.party.members[slot].has_value() ||
                   ctx.party.characters[slot].highest_condition != W8_CONDITION_DEAD) {
                slot++;
                if (slot > 7) {
                    return;
                }
            }
        } else {
            pick = ctx.game.rng.below(count) + 1;
            for (slot = 0; slot < 8; slot++) {
                if (severities[slot] == lowest) {
                    pick--;
                    if (pick == 0) {
                        break;
                    }
                }
            }
        }
        ctx.status.party_slot = slot;
        InflictCondition(CurrentGame(), slot,
                         {.condition = W8_CONDITION_MISSING, .duration = W8_CONDITION_INDEFINITE});
        SetFact(W8_FACT_CROCK_KIDNAPPED_PLAYER, 1, false);
    }
}

} // namespace

std::unique_ptr<LevelScript> MakeMartensBluff2Script(const ScriptContext& context)
{
    return std::make_unique<MartensBluff2Script>(context);
}
