#include "wiz8/local_code/npc_story_events.h"

#include <cstdint>
#include <string>

#include <fmt/chrono.h> // IWYU pragma: keep - std::tm formatter specialization.
#include <fmt/format.h>

#include "compat/native.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/targeting.h"
#include "local_code/MonsterManager.h"
#include "monster_actions.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/audio_resources.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/conditions.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/level_specific_code/Ascension.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/message_box.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

static void ResolveSedexusCapture(void);
static void NpcScriptHenchmanArrives(Monster* monster);
static void NpcScriptHenchmanDeparted(Monster* monster);
static void NpcScriptSavantHackDone(Monster* monster);

static W8MonsterInfo* FindMonsterByLocation(int location_id)
{
    return FindMonster(location_id);
}

static W8MonsterInfo* FindMonsterGroupLeader(int species)
{
    W8MonsterGroup* group = FindFirstMonsterByID(species);
    return group == 0 ? 0 : FindMonsterByLocation(group->leader_location_id);
}

static W8MonsterInfo* FindNpcKindMonster(int npc_kind)
{
    W8NpcState* npc = GetNpcStateByKind(npc_kind);
    return npc == 0 ? 0 : GetNpcMonsterInfo(npc);
}

static void RemoveMonsterNow(W8MonsterInfo* monster_info)
{
    RemoveMonster(monster_info, W8MonsterRemoval::Despawn);
}

/* Fade an active NPC monster out of the world; one that is not active is
   removed immediately. */
static void RemoveNpcKindMonster(int npc_kind)
{
    W8MonsterInfo* monster_info = FindNpcKindMonster(npc_kind);
    if (monster_info == 0) {
        return;
    }
    if (!monster_info->fActive) {
        RemoveMonsterNow(monster_info);
    } else if (monster_info->p3D != 0) {
        monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
    }
}

static void FadeOutNpcKindMonster(int npc_kind)
{
    if (W8MonsterInfo* monster_info = FindNpcKindMonster(npc_kind)) {
        monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
    }
}

static void SetMonsterGroupScript(int species, const char* script)
{
    if (W8MonsterInfo* monster_info = FindMonsterGroupLeader(species)) {
        monster_info->p3D->SetScript(script, true);
    }
}

/* The fade-completion callback NpcScriptTurnToBook schedules: while either
   queued endgame stage is pending - and stage three still finds PHOONZANG -
   ENDGAME2's script notice runs the book sequence. */
static void NpcScriptQueueEndgame(void)
{
    if (g_status.endgame2_queued != 0 ||
        (g_status.endgame3_queued != 0 && FindNpcOfKind(W8_NPC_PHOONZANG))) {
        W8NpcState* npc = GetNpcStateByKind(W8_NPC_ENDGAME2);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, -1, false, 0);
        }
    }
}

/* CameraPath4 swings the view to the ascension book; the endgame queue runs
   when the fade completes. */
static void NpcScriptTurnToBook(void)
{
    UpdateCameraPathStateByName(GetWorld(), "CameraPath4", 1);
    BeginScreenFade(0, 1, 1000, NpcScriptQueueEndgame, false, 0);
}

static void NpcScriptEndgameScreen(void)
{
    RequestScreen({.id = W8_SCREEN_INTRO, .mode = W8_INTRO_VIDEO_DARK_ENDING});
}

/* Fact 0x1bf: raise the scripted-scene gate, drop the level's transient data
   vectors, force the single-target mode, and reopen the party-member region
   sets (7 + slot, member region 0x5a + slot) for every occupied slot. */
void BeginNpcScriptedScene(void)
{
    int party_slot;

    g_npc_dialogue.HoldForScene();
    ResetLevelDataVectors();
    gXStatus.scripted_scene = true;
    SetTargetingMode(W8_TARGET_NEED_ALLY);
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (g_status.party.rows[party_slot].fOccupied) {
            EnableMainGamePortraitSlot(party_slot);
            MainGame()->edge_targets[party_slot]->setEnabled(true);
        }
    }
}
/* The scripted-scene wind-down: clears the targeting/level state raised by
   BeginNpcScriptedScene, resets the script facts, then classifies the picked
   character. A female pick with any non-female party member present raises
   fact 0x1c0 and queues the special event; an unanimated pick is further
   tested for the three 0x1fd-0x1ff items (success leaves fact 0x1c1 at zero
   and fills the alternate-name display), and anything else falls back to
   facts 0x1c1 or 0x227. The party-member region sets reopen on the way out. */
void EndScriptedPortraitPick(int party_slot)
{
    W8Character* character;
    /* 0x00529EDC compares the slot counter as slot - 7 against 8 with a
       signed jump, so the counter is not unsigned. */
    int slot;
    bool other_gender_present;

    if (!gXStatus.scripted_scene) {
        return;
    }
    if (!g_combat.active() || gXStatus.fPartyMovementMode) {
        ClearLevelMovementStopped();
    }
    SetTargetingMode(W8_TARGET_NEED_NONE);
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.party.rows[slot].fOccupied) {
            DisableMainGamePortraitSlot(slot);
            MainGame()->edge_targets[slot]->setEnabled(false);
        }
    }
    g_npc_dialogue.ReleaseScene();
    gXStatus.scripted_scene = false;
    other_gender_present = false;
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_WOMAN, 0, false);
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_ALL_WOMAN, 0, false);
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_DRESSED, 0, false);
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_ABLE, 0, false);
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.party.rows[slot].fOccupied &&
            g_status.party.characters[slot].gender != W8_GENDER_FEMALE) {
            other_gender_present = true;
            break;
        }
    }
    character = &g_status.party.characters[party_slot];
    if (character->gender == W8_GENDER_FEMALE && other_gender_present) {
        SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_WOMAN, 1, false);
        QueueCharacterEvent(character, 43, kCharacterEventNoNpcDefer, kCharacterEventFullVolume);
        return;
    }
    if (g_status.party.rows[party_slot].npc_index == -1 &&
        character->highest_condition < W8_CONDITION_ASLEEP) {
        if (FindCharacterItem(*character, ItemWithId(0x1fd), W8ItemReach::Worn) &&
            FindCharacterItem(*character, ItemWithId(0x1fe), W8ItemReach::Worn) &&
            FindCharacterItem(*character, ItemWithId(0x1ff), W8ItemReach::Worn)) {
            SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_DRESSED, 0, false);
            g_status.monster_name_buffer = fmt::format("Al-{}", character->name);
            g_status.sedexus_party_slot = party_slot;
            g_status.rpc_active = true;
            g_status.infatuation_pending = true;
            QueueCharacterEvent(character, g_special_event2, 0, kCharacterEventFullVolume);
        } else {
            SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_DRESSED, 1, false);
        }
    } else {
        SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_ABLE, 1, false);
    }
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.party.rows[slot].fOccupied) {
            EnableMainGamePortraitSlot(slot);
            MainGame()->edge_targets[slot]->setEnabled(true);
        }
    }
}
/* The Al-Sedexus capture scene. Capture and release are each picked up by
   the next scripting frame that runs in a suitable environment; the capture
   stays active, with the moaning ambience streaming, until the release. */
struct W8SedexusScene {
    bool capture_pending = false;
    bool release_pending = false;
    bool capture_active = false;
    wiz8::sound::Playback moaning;
};
static W8SedexusScene g_sedexus_scene;

bool IsSedexusCaptureActive(void)
{
    return g_sedexus_scene.capture_active;
}

void ResetSedexusScene(void)
{
    g_sedexus_scene.capture_pending = false;
    g_sedexus_scene.release_pending = false;
    g_sedexus_scene.capture_active = false;
}

void UpdateSedexusScene(void)
{
    if (!g_sedexus_scene.capture_pending && !g_sedexus_scene.release_pending) {
        return;
    }
    const W8EnvironmentLightingMode environment = EnvironmentLightingMode();
    if (environment != W8EnvironmentLightingMode::FadedOut &&
        environment != W8EnvironmentLightingMode::DayNight) {
        return;
    }
    g_npc_dialogue.ReleaseScene();
    if (!g_sedexus_scene.release_pending) {
        g_sedexus_scene.moaning =
            wiz8::sound::stream("Data\\Sound\\Ambients\\Al_Sedexus Moaning.wav");
    } else {
        ClearMainGameTargetState();
        int sedexus_party_slot = g_status.sedexus_party_slot;
        for (int party_slot = 0; party_slot < 8; ++party_slot) {
            const W8PartySlotRow& row = g_status.party.rows[party_slot];
            const W8Character& character = g_status.party.characters[party_slot];
            if (row.fOccupied &&
                (character.hp_current > 0 || character.highest_condition < W8_CONDITION_DEAD) &&
                party_slot != sedexus_party_slot) {
                RemoveCharacterCondition(party_slot, W8_CONDITION_UNCONSCIOUS, false);
                sedexus_party_slot = g_status.sedexus_party_slot;
            }
        }
        W8Character* character = &g_status.party.characters[sedexus_party_slot];
        if (character->gender == W8_GENDER_MALE) {
            QueueCharacterEvent(character, 43, 0, kCharacterEventFullVolume);
        }
        g_sedexus_scene.capture_active = false;
        if (g_sedexus_scene.moaning != nullptr) {
            wiz8::sound::stop(g_sedexus_scene.moaning);
            g_sedexus_scene.moaning = nullptr;
        }
    }
    g_sedexus_scene.capture_pending = false;
    g_sedexus_scene.release_pending = false;
}

/* Al-Sedexus takes her pick: the scripted scene opens, the lighting fades,
   and every still-living occupied slot other than the selected character is
   put under condition 0x11. */
void BeginSedexusCapture(void)
{
    unsigned int party_slot;

    g_npc_dialogue.HoldForScene();
    BeginScriptedWorldAction();
    g_sedexus_scene.capture_pending = true;
    g_sedexus_scene.capture_active = true;
    BeginWorldLightingFade(-1000.0f);
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (g_status.party.rows[party_slot].fOccupied &&
            (g_status.party.characters[party_slot].hp_current > 0 ||
             g_status.party.characters[party_slot].highest_condition < W8_CONDITION_DEAD) &&
            party_slot != static_cast<unsigned int>(g_status.sedexus_party_slot)) {
            SetCharacterCondition(party_slot, W8_CONDITION_UNCONSCIOUS, W8_CONDITION_INDEFINITE, 0,
                                  0, 0);
        }
    }
}
/* The capture resolves into the templar spawn: the two NPC kinds are
   released, the LezboDemonAppeared trigger variable drops, monster 0xd8 is
   spawned on np_al-adryian51 and given proximitytemplar.msf, and the two
   gate triggers fire while the lighting fades back up. */
static void ResolveSedexusCapture(void)
{
    W8MonsterGroup* group;
    glm::vec3 position;
    int location_id;
    W8MonsterInfo* info;

    ReleaseNpcMonsterByKind(0x3b);
    ReleaseNpcMonsterByKind(0x40);
    SetTriggerVariableByName("LezboDemonAppeared", 0);
    if (FindEntityByName("np_al-adryian51", &position, 0, 0)) {
        group = SpawnMonsters(0xd8, 1, &position, 2, true, false, false);
        location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            info = FindMonsterByLocation(location_id);
            if (info != 0) {
                info->p3D->SetScript("proximitytemplar.msf", true);
            }
        }
    }
    g_npc_dialogue.HoldForScene();
    g_sedexus_scene.release_pending = true;
    BeginWorldLightingFade(1000.0f);
    RunNamedTrigger("al-seduxusgate", -1);
    RunNamedTrigger("templargate14", -1);
}
/* Cycle-0x12 arrival callback: the henchmen group spawns on the NP_HENCHMEN
   waypoint and its first member's bound NPC gets the script notice; the
   callback monster itself is parked at the origin with its position flagged
   dirty either way. */
static void NpcScriptHenchmanArrives(Monster* monster)
{
    glm::vec3 position;
    glm::vec3 origin;

    if (FindEntityByName("NP_HENCHMEN", &position, 0, 0)) {
        PointCameraAtTarget(&position, false, true);
        W8MonsterGroup* group = SpawnMonsters(0xdc, 1, &position, 2, true, false, false);
        int location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            W8MonsterInfo* monster_info = FindMonsterByLocation(location_id);
            if (monster_info != 0) {
                W8NpcState* npc = GetNpcStateForMonsterInfo(monster_info, false);
                if (npc != 0) {
                    QueueNpcScriptNotice(npc, 0, -1, false, 0);
                }
            }
        }
    }
    origin = glm::vec3(0);
    monster->SetPosition(&origin);
    monster->runtime_flags |= W8_MONSTER_PARKED;
}

/* Cycle-0x12 departure callback: park the monster at the origin and flag its
   position dirty. */
static void NpcScriptHenchmanDeparted(Monster* monster)
{
    glm::vec3 origin;

    monster->runtime_flags |= W8_MONSTER_PARKED;
    origin = glm::vec3(0);
    monster->SetPosition(&origin);
}

/* Cycle-0x19 completion callback: stamping savant_hack_tick lets
   UpdateNpcEvents retire NPC group 0x1b3 fifty ticks later. */
static void NpcScriptSavantHackDone(Monster* monster)
{
    g_status.savant_hack_tick = int(uint32_t(w8_get_ticks()));
}

void RunNpcStoryAction(W8NpcStoryAction action)
{
    W8NpcState* npc;

    switch (action) {
    case W8NpcStoryAction::begin_endgame:
        ClearMainGameTargetState();
        BeginEndgameSequence();
        break;
    case W8NpcStoryAction::turn_to_book:
        BeginScreenFade(0, 0, 500, NpcScriptTurnToBook, true, 1);
        break;
    case W8NpcStoryAction::phoonzang_notice:
        npc = GetNpcStateByKind(0x87);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, -1, false, 0);
        }
        break;
    case W8NpcStoryAction::move_gari: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        SetMonsterGroupScript(0x162, "MoveGari.msf");
        break;
    }
    case W8NpcStoryAction::move_rubble: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        SetMonsterGroupScript(0x83, "MoveRubble.msf");
        break;
    }
    case W8NpcStoryAction::milano_rat_door: {
        Trigger* door = FindRequiredTriggerByName("RatDoor02");
        door->CompleteItemInteraction();
        EndNpcDialogueSession(false);
        SetMonsterGroupScript(0xcf, "Milano.msf");
        break;
    }
    case W8NpcStoryAction::spawn_alfie_chaos:
        SpawnAlfieChaos(0);
        break;
    case W8NpcStoryAction::spawn_alfie_life:
        SpawnAlfieLife(0);
        break;
    case W8NpcStoryAction::spawn_alfie_know:
        SpawnAlfieKnow(0);
        break;
    case W8NpcStoryAction::remove_aletheides_ad: {
        EndNpcDialogueSession(false);
        if (W8MonsterInfo* monster_info = FindNpcKindMonster(0x2e)) {
            RemoveMonsterNow(monster_info);
        }
        break;
    }
    case W8NpcStoryAction::remove_aletheides_cm: {
        EndNpcDialogueSession(false);
        if (W8MonsterInfo* monster_info = FindNpcKindMonster(0x2d)) {
            RemoveMonsterNow(monster_info);
        }
        break;
    }
    case W8NpcStoryAction::remove_aletheides_dd: {
        EndNpcDialogueSession(false);
        if (W8MonsterInfo* monster_info = FindNpcKindMonster(0x2f)) {
            RemoveMonsterNow(monster_info);
        }
        break;
    }
    case W8NpcStoryAction::move_savant: {
        EndNpcDialogueSession(false);
        ResetLevelDataVectors();
        SetMonsterGroupScript(0xc2, "MoveSavant.msf");
        break;
    }
    case W8NpcStoryAction::aletheides_leaves:
        RemoveAletheides();
        break;
    case W8NpcStoryAction::mook_comment: {
        unsigned int eligible = 0;
        int party_slot;
        for (party_slot = 2; party_slot < 8; ++party_slot) {
            if (g_status.party.rows[party_slot].fOccupied &&
                g_status.party.characters[party_slot].highest_condition < W8_CONDITION_ASLEEP) {
                ++eligible;
            }
        }
        if (eligible > 1) {
            for (party_slot = 0; party_slot < 8; ++party_slot) {
                W8Character* character = &g_status.party.characters[party_slot];
                if (g_status.party.rows[party_slot].fOccupied && character->iRace == W8_RACE_MOOK &&
                    character->highest_condition < W8_CONDITION_ASLEEP) {
                    QueueCharacterEvent(character, 51, kCharacterEventNoNpcDefer,
                                        kCharacterEventFullVolume);
                    break;
                }
            }
        }
        break;
    }
    case W8NpcStoryAction::sedexus_passout:
        ResolveSedexusCapture();
        break;
    case W8NpcStoryAction::henchman_leaves: {
        EndNpcDialogueSession(false);
        if (W8MonsterInfo* monster_info = FindMonsterGroupLeader(0xdc)) {
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL,
                              W8_ANIMATION_PLAY_ONCE);
            monster_info->p3D->SetCycleCallback(0x12, NpcScriptHenchmanDeparted);
        }
        ClearMainGameTargetState();
        break;
    }
    case W8NpcStoryAction::prince_disappears: {
        EndNpcDialogueSession(false);
        if (W8MonsterInfo* monster_info = FindMonsterGroupLeader(0x1ab)) {
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        if (W8MonsterGroup* group = FindFirstMonsterByID(0x15d)) {
            SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
        }
        break;
    }
    case W8NpcStoryAction::prince_not_home: {
        EndNpcDialogueSession(false);
        if (W8MonsterInfo* monster_info = FindMonsterGroupLeader(0x1aa)) {
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        break;
    }
    case W8NpcStoryAction::pillargate_lure: {
        RunNamedTrigger("pillargate05", -1);
        FadeOutNpcKindMonster(0x3f);
        break;
    }
    case W8NpcStoryAction::pillargate_madeus: {
        RunNamedTrigger("pillargate04", -1);
        FadeOutNpcKindMonster(0x3e);
        break;
    }
    case W8NpcStoryAction::pillargate_asaiz: {
        RunNamedTrigger("pillargate01", -1);
        FadeOutNpcKindMonster(0x3d);
        break;
    }
    case W8NpcStoryAction::balbrak_home: {
        W8MonsterInfo* monster_info = FindNpcKindMonster(0xc);
        glm::vec3 position;
        if (monster_info != 0 && FindEntityByName("NP_Balbrakhome", &position, 0, 0)) {
            monster_info->p3D->SetPosition(&position);
        }
        break;
    }
    case W8NpcStoryAction::call_henchman: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        if (W8MonsterInfo* monster_info = FindMonsterGroupLeader(0x112)) {
            monster_info->p3D->SetCycleCallback(0x12, NpcScriptHenchmanArrives);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL,
                              W8_ANIMATION_PLAY_ONCE);
        }
        break;
    }
    case W8NpcStoryAction::move_to_book: {
        EndNpcDialogueSession(false);
        SetMonsterGroupScript(0x1b4, "belapath1.msf");
        Trigger* trigger = FindTriggerByName("CC_TRIGGERPLANE3");
        if (trigger != 0) {
            trigger->flags &= ~W8_TRIGGER_ON;
        }
        trigger = FindTriggerByName("CC_TRIGGERPLANE2");
        if (trigger != 0) {
            trigger->flags |= W8_TRIGGER_ON;
            trigger->Run(-1);
        }
        break;
    }
    case W8NpcStoryAction::move_to_book2: {
        Trigger* trigger = FindTriggerByName("CC_TRIGGERPLANE3");
        if (trigger != 0) {
            trigger->flags |= W8_TRIGGER_ON;
            trigger->Run(-1);
        }
        npc = GetNpcStateByKind(0x8d);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, 7, false, 0);
        }
        break;
    }
    case W8NpcStoryAction::savant_appears: {
        glm::vec3 position;
        if (FindEntityByName("NP_DS1", &position, 0, 0)) {
            W8MonsterGroup* group = SpawnMonsters(0x234, 1, &position, 0, true, false, false);
            int location_id = group->MemberLocationId(0);
            if (location_id != 0) {
                W8MonsterInfo* monster_info = FindMonsterByLocation(location_id);
                monster_info->p3D->ForwardReferencePosition(0);
            }
            npc = GetNpcStateByKind(0x84);
            if (npc != 0) {
                QueueNpcScriptNotice(npc, 0, -1, false, 0);
            }
        }
        break;
    }
    case W8NpcStoryAction::phoonzang_split: {
        W8MonsterInfo* monster_info = FindNpcKindMonster(0x84);
        if (monster_info != 0) {
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_SPECIAL, W8_ANIMATION_PLAY_ONCE);
        }
        glm::vec3 position;
        if (FindEntityByName("NP_PHOONZANGLEE", &position, 0, 0) &&
            (monster_info = FindMonsterGroupLeader(0x197)) != 0) {
            monster_info->p3D->SetPosition(&position);
            monster_info->p3D->ForwardReferencePosition(0);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL,
                              W8_ANIMATION_PLAY_ONCE);
        }
        npc = GetNpcStateByKind(0x8d);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, 0x12, false, 0);
        }
        break;
    }
    case W8NpcStoryAction::remove_rpc_vi:
        if (NpcLeadHasNameStyle(W8_NPC_VI_DOMINA)) {
            npc = GetNpcStateByKind(0x18);
            if (npc != 0) {
                DismissNpcFromParty(npc->group_index, 0, true, false);
            }
            glm::vec3 position;
            if (FindEntityByName("NP_VI1", &position, 0, 0)) {
                W8MonsterGroup* group = SpawnMonsters(0x1b9, 1, &position, 2, true, false, false);
                int location_id = group->MemberLocationId(0);
                if (location_id != 0) {
                    W8MonsterInfo* monster_info = FindMonsterByLocation(location_id);
                    if (FindEntityByName("NP_DS1", &position, 0, 0)) {
                        monster_info->p3D->nav.AimAtPosition(&position);
                    }
                }
            }
        }
        break;
    case W8NpcStoryAction::move_golem: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        SetMonsterGroupScript(0x13e, "MoveGolem.msf");
        break;
    }
    case W8NpcStoryAction::savant_hack: {
        if (W8MonsterInfo* monster_info = FindMonsterGroupLeader(0x1b6)) {
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_SPELL, W8_ANIMATION_PLAY_ONCE);
            monster_info->p3D->SetCycleCallback(0x19, NpcScriptSavantHackDone);
        }
        break;
    }
    case W8NpcStoryAction::trigger_fix: {
        RunNamedTrigger("triggerFix", -1);
        break;
    }
    case W8NpcStoryAction::endgame_screen:
        BeginScreenFade(0, 0, 500, NpcScriptEndgameScreen, true, 1);
        break;
    case W8NpcStoryAction::move_bela: {
        EndNpcDialogueSession(false);
        ResetLevelDataVectors();
        SetMonsterGroupScript(0x18c, "MoveBela.msf");
        glm::vec3 position;
        if (FindEntityByName("NP_DSExit", &position, 0, 0)) {
            PointCameraAtTarget(&position, false, true);
        }
        break;
    }
    case W8NpcStoryAction::path2_trigger: {
        RunNamedTrigger("Path2Trigger", -1);
        break;
    }
    case W8NpcStoryAction::remove_shaman: {
        EndNpcDialogueSession(false);
        RemoveNpcKindMonster(0x4d);
        break;
    }
    case W8NpcStoryAction::remove_janette: {
        EndNpcDialogueSession(false);
        W8MonsterInfo* monster_info = FindNpcKindMonster(0x62);
        if (monster_info != 0) {
            if (monster_info->fActive) {
                MonsterStartsDying(monster_info, true);
            } else {
                RemoveMonsterNow(monster_info);
            }
        }
        break;
    }
    case W8NpcStoryAction::sedexus_leaves: {
        EndNpcDialogueSession(false);
        SetTriggerVariableByName("LezboDemonAppeared", 0);
        RemoveNpcKindMonster(0x40);
        break;
    }
    case W8NpcStoryAction::remove_sedexus_rift: {
        EndNpcDialogueSession(false);
        RemoveNpcKindMonster(0x42);
        break;
    }
    case W8NpcStoryAction::remove_marten: {
        EndNpcDialogueSession(false);
        RemoveNpcKindMonster(100);
        break;
    }
    }
}
