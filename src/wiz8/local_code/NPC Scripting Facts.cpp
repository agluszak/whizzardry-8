#include <cassert>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "message_box.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_items.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/story.h"
#include "wiz8/string_database.h"
#include "wiz8/world/world.h"

/* Potion explosion completion callback: retail stores this JMP thunk rather than
   ClearScriptedSceneActive's void() entry. */
static void ClearPotionExplosionSoundFlag()
{
    CurrentGame().npc_dialogue.ReleaseScene();
}

/* Camera-shake completion callback stored on ShakeDefinition::completion_callback. */
static void ReplayEarthquakeShake(void)
{
    CameraShakes().Start(ShakeDefinition(3.0f, false, 1.0f, 0, nullptr));
    BeginEndgameSequence();
}

void HandleFactChange(const ScriptContext& context, W8FactId fact_id, unsigned char value)
{
    W8NpcState* npc;
    W8MonsterGroup* group;
    W8WorldItem* world_item;
    W8Character* character;
    Trigger* trigger;
    ParticleSystem* particle;
    W8MonsterInfo* monster_info;
    glm::vec3 position;
    unsigned char fact_value;
    unsigned int added;
    int location_id;
    int slot;
    const char* trigger_name;

    switch (fact_id) {
    case W8_FACT_ANTONE_ZYNARYX_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(3), 0x28a, 1, 0x15180);
        return;
    case W8_FACT_ANTONE_STEELHIDE_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(3), 0x28b, 1, 0x15180);
        return;
    case W8_FACT_ANTONE_FEATHERWEIGHT_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(3), 0x28c, 1, 0x15180);
        return;
    case W8_FACT_ANTONE_BEASTSLAYER_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(3), 0x28d, 1, 0x15180);
        return;
    case W8_FACT_ANTONE_EBON_STAFF_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(3), 0x28e, 1, 0x15180);
        return;
    case W8_FACT_RAPAX_MYLES_IN_JAIL:
        if (value == 0) {
            return;
        }
        if (NpcLeadHasNameStyle(W8_NPC_MYLES)) {
            return;
        }
        npc = GetNpcStateByKind(7);
        if (npc == 0) {
            return;
        }
        ReleaseNpcMonsterBinding(npc, 0);
        RestoreNamedNpcAtLevel(npc->name_style, 0x11, "NP_MylesCell");
        return;
    case W8_FACT_ASTRAL_POSSESS:
        if (value == 0) {
            return;
        }
        npc = GetNpcStateByKind(0x48);
        assert(npc != 0);
        ClearNpcScheduledItem(npc, 0x242, 0);
        return;
    case W8_FACT_PEACE_SAVANT_BLOWUP_DONE:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::trigger_fix);
        return;
    case W8_FACT_CROCK_KIDNAPPED_PLAYER:
        if (value != 0 && FindEntityByName("NP_BlueFlowers", &position, 0, 0)) {
            world_item = CurrentGame().level.loaded().adoptItem(
                CurrentGame().spawnWorldItem(CurrentGame().level.loaded(), 0x2eb, &position,
                                             W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            if (world_item != 0) {
                CurrentGame().level.loaded().activateItem(Data(), CurrentGame().rng, world_item);
            }
            world_item = CurrentGame().level.loaded().adoptItem(
                CurrentGame().spawnWorldItem(CurrentGame().level.loaded(), 0x2eb, &position,
                                             W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            if (world_item != 0) {
                CurrentGame().level.loaded().activateItem(Data(), CurrentGame().rng, world_item);
            }
        }
        return;
    case W8_FACT_CROCK_BREKEK_ASSIGN:
        fact_value = GetFact(W8_FACT_CROCK_BREKEK_DEAD);
        if (fact_value == 0 && FindEntityByName("Brekek", &position, 0, 0)) {
            SpawnMonsters(0x131, 1, &position, 1, true, true);
        }
        return;
    case W8_FACT_CROCK_PLAYER_RETURNED_TO_PARTY:
        if (value != 0 &&
            context.party.characters[context.status.party_slot].uiCondition[W8_CONDITION_MISSING] !=
                0) {
            LiftCondition(CurrentGame(), context.status.party_slot, W8_CONDITION_MISSING, true);
        }
        return;
    case W8_FACT_ALIGNMENT_TRANG:
        trigger = FindRequiredTriggerByName("ZaHealthDoor");
        trigger->Run(-1);
        return;
    case W8_FACT_RFS81_HAS_BEEN_FIXED:
        if (value != 0) {
            npc = GetNpcStateByKind(0x20);
            if (npc != 0) {
                ReloadNpcScriptResources(npc);
                if (!npc->is_grouped) {
                    CurrentGame().npc_dialogue.Cancel();
                    CurrentGame().npc_dialogue.Begin(npc, false);
                    CurrentGame().npc_dialogue.QueueQuote(0, false, false, false);
                    return;
                }
                character = GetNpcGroupCharacter(npc);
                QueueCharacterEvent(character, 0, 0, kCharacterEventFullVolume);
            }
        }
        return;
    case W8_FACT_UMISSION_OBCOURSE_DONE:
        if (value != 0) {
            MarkNpcOfKind(0x29);
        }
        return;
    case W8_FACT_KUNAR_IS_DEAD:
        ReleaseNpcMonsterByKind(0x2c);
        return;
    case W8_FACT_UMISSION_TRAIN_COVERT_ASSIGN:
        if (value != 0) {
            for (slot = 0; slot < 8; ++slot) {
                character = &context.party.characters[slot];
                if (context.party.members[slot].has_value() &&
                    character->highest_condition < W8_CONDITION_DEAD &&
                    g_profession_skill_availability[7][character->iProfession] != 0 &&
                    character->skills[W8_SKILL_MODERN_WEAPONS].points < 10) {
                    character->skills[W8_SKILL_MODERN_WEAPONS].points = 10;
                    ApplySkillChange(context.game, character, W8_SKILL_MODERN_WEAPONS);
                }
            }
        }
        return;
    case W8_FACT_UMISSION_IUFPASS_LEVEL4:
        if (value == 0) {
            return;
        }
        npc = GetNpcStateByKind(0x29);
        assert(npc != 0);
        MarkNpcOfKind(0x29);
        return;
    case W8_FACT_UMISSION_TRAIN_COVERT_OPEN:
        if (value != 0) {
            trigger = FindTriggerByName("Door08");
            if (trigger != 0 && (trigger->flags & W8_TRIGGER_FIRED) == 0) {
                trigger->Run(-1);
            }
        }
        return;
    case W8_FACT_LOCATION_GIGAS_MOTION_SENSORS_OFF:
        if (value != 0) {
            RunNamedTrigger("SecurityLasers", -1);
            CreateLocationVar("LasersOff", 1);
        }
        return;
    case W8_FACT_LOCATION_GIGAS_OUTER_GATE_OPEN:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("ewaxxdoortrigger01", -1);
        return;
    case W8_FACT_UMISSION_TRAIN_GUNNARY_OPEN:
        if (value == 0) {
            return;
        }
        trigger = FindTriggerByName("Door07");
        if (trigger != 0 && (trigger->flags & W8_TRIGGER_FIRED) == 0) {
            trigger->Run(-1);
        }
        return;
    case W8_FACT_UMISSION_TRAIN_UTU_OPEN:
        if (value == 0) {
            return;
        }
        trigger = FindTriggerByName("Door06");
        if (trigger != 0 && (trigger->flags & W8_TRIGGER_FIRED) == 0) {
            trigger->Run(-1);
        }
        return;
    case W8_FACT_ARNIKA_HLL_OPEN:
        RunNamedTrigger("scannerdoor", -1);
        trigger = FindTriggerByName("Scanner_Trigger_Plane");
        if (trigger == 0) {
            return;
        }
        trigger->flags &= ~W8_TRIGGER_ENABLED;
        return;
    case W8_FACT_MOOK_MOOK_PC_SWAP_COMPLETE:
        if (value != 0) {
            SetFact(W8_FACT_QUEST_MOOK_BUDDY_TELLS_OF_CM, 1, false);
        }
        return;
    case W8_FACT_MOOK_MOOK_PC_REMOVED:
        if (value != 0) {
            CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::mook_comment);
        }
        return;
    case W8_FACT_MOOK_HALL_OPEN:
        RunNamedTrigger("MookFrontDoor", -1);
        RunNamedTrigger("Mookoff-01", -1);
        return;
    case W8_FACT_ARNIKA_BLACKBOX_RECEIVED:
        RunNamedTrigger("blackboxrecorder", -1);
        return;
    case W8_FACT_ARNIKA_MYLES_TROOPERS_HOSTILE:
        group = FindFirstMonsterByID(0x13);
        if (group != 0) {
            SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
        }
        return;
    case W8_FACT_ARNIKA_SAFETY_DEPOSIT_OPEN:
        RunNamedTrigger("redbutton", -1);
        return;
    case W8_FACT_FACTION_HIGARDI_HLL_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(0xb, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(0xb, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_FACTION_HIGARDI_COMMON_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(0xc, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(0xc, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_FACTION_HIGARDI_BANK_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(10, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(10, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_FACTION_HIGARDI_FELLOW_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(9, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(9, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_ARNIKA_VAULT_TELEPORT:
    case W8_FACT_ARNIKA_VAULT_ENTERED:
        if (value == 0) {
            return;
        }
        fact_value = GetFact(W8_FACT_MYLES_IN_PARTY);
        if (fact_value != 0) {
            SetFact(W8_FACT_MYLES_IN_PARTY_WHEN_BANK_DONE, 1, false);
        }
        SetFact(W8_FACT_QUEST_MYLES_BANK_VAULT, 0, false);
        return;
    case W8_FACT_MYLES_MISSION_RESCUE_VI_ASSIGNED:
        npc = GetNpcStateByKind(7);
        if (npc == 0) {
            return;
        }
        monster_info = GetNpcMonsterInfo(npc);
        if (monster_info == 0) {
            return;
        }
        monster_info->entity->SetScript("Guard.msf", true);
        return;
    case W8_FACT_MURAL_OPEN:
        if (value != 0) {
            RunNamedTrigger("MartenMural", -1);
            trigger = FindTriggerByName("Muraltrigger");
            if (trigger == 0) {
                return;
            }
            trigger->flags &= ~W8_TRIGGER_ENABLED;
            return;
        }
        RunNamedTrigger("Muraltrigger", -1);
        return;
    case W8_FACT_FACTION_TRANG_COMMON_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(5, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(5, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_AP_GOLEM_ATTACK:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_golem);
        return;
    case W8_FACT_CM_RIDDLE2:
        trigger = FindTriggerByName("ChaosBTrigger");
        if (trigger != 0) {
            trigger->flags &= ~W8_TRIGGER_ENABLED;
        }
        RunNamedTrigger("ChaosDoor01", -1);
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_aletheides_ad);
        return;
    case W8_FACT_DD_RIDDLE2:
        trigger = FindTriggerByName("KnowBTrigger");
        if (trigger != 0) {
            trigger->flags &= ~W8_TRIGGER_ENABLED;
        }
        RunNamedTrigger("DoorKnow03", -1);
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_aletheides_dd);
        return;
    case W8_FACT_AD_RIDDLE2:
        trigger = FindTriggerByName("LifeBTrigger");
        if (trigger != 0) {
            trigger->flags &= ~W8_TRIGGER_ENABLED;
        }
        RunNamedTrigger("DoorLife03", -1);
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_aletheides_cm);
        return;
    case W8_FACT_AP_SAVANT_DISAPPEARS:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_savant);
        return;
    case W8_FACT_TEMPLAR:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x3b);
        SetFactionFlag(0x10, true);
        return;
    case W8_FACT_RATTKIN_HAVE_AD:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(0x48), 0x242, 1, 0);
        return;
    case W8_FACT_BAYJIN_JANETTE_DEAD:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_janette);
        return;
    case W8_FACT_MARTEN_FINISHED:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_marten);
        return;
    case W8_FACT_UMISSION_SCUBA_SOLVED_NO_ALIGN:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x2b);
        return;
    case W8_FACT_FACTION_UMPANI_COMMON_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(4, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(4, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_LOCATION_GIGAS_TELEPORT_OPEN:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("greenLight23MeansOpen", -1);
        RunNamedTrigger("greenLight23MeansOpen01", -1);
        return;
    case W8_FACT_TRYNNIE_FOUNTAIN_ANSWERED_CORRECT:
        if (value == 0) {
            return;
        }
        for (slot = 0; slot < 8; ++slot) {
            character = &context.party.characters[slot];
            if (CanPartySlotParticipate(slot)) {
                added = 100 - character->attributes[W8_ATTRIBUTE_INTELLIGENCE].base;
                if (added > 5) {
                    added = 5;
                }
                character->attributes[W8_ATTRIBUTE_INTELLIGENCE].base += added;
                ApplyAttributeChange(context.game, character, W8_ATTRIBUTE_INTELLIGENCE);
            }
        }
        ShowString(Data().strings[0x1dc]);
        trigger = FindTriggerByName("FOUNT_RIDDLE");
        if (trigger == 0) {
            return;
        }
        trigger->flags &= ~W8_TRIGGER_ENABLED;
        return;
    case W8_FACT_RATTKIN_WHO_LOOKING_FOR:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::milano_rat_door);
        return;
    case W8_FACT_TRYNNIE_KILL_RATS_DONE:
        if (value == 0) {
            return;
        }
        SetFact(W8_FACT_QUEST_MADRAS_OPEXTERM, 1, false);
        return;
    case W8_FACT_TRYNNIE_KILL_RATS_ASSIGNED:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_gari);
        return;
    case W8_FACT_TRYNNIE_FUZZFAS_POTION:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(0x49), 0x1b0, 1, 0x15180);
        return;
    case W8_FACT_AP_BELA_DONE:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_bela);
        return;
    case W8_FACT_TRYNNIE_GOT_DESTINY:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_shaman);
        return;
    case W8_FACT_FACTION_RAPAX_COMMON_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(0xf, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(0xf, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_RAPAX_RIDDLE1:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::pillargate_asaiz);
        return;
    case W8_FACT_RAPAX_RIDDLE3:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::pillargate_lure);
        return;
    case W8_FACT_RAPAX_RIDDLE2:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::pillargate_madeus);
        return;
    case W8_FACT_RAPAX_INITIATES:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x3c);
        RunNamedTrigger("templargate07", -1);
        return;
    case W8_FACT_RAPAX_OPEN_TEMPLE:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x3a);
        RunNamedTrigger("templargate10", -1);
        return;
    case W8_FACT_ALSEDEXUS_SACRIFICE_SELECT:
        if (value == 0) {
            return;
        }
        BeginNpcScriptedScene(context);
        return;
    case W8_FACT_ALSEDEXUS_PARTY_PASSOUT:
        if (value == 0) {
            CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::sedexus_passout);
            return;
        }
        BeginSedexusCapture(context);
        return;
    case W8_FACT_RIFT_RAFE_FREE:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x44);
        return;
    case W8_FACT_FERRO_MIRROR_ARMOR_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(0x39), 500, 1, 0x15180);
        return;
    case W8_FACT_FERRO_IVORY_BLADE_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(0x39), 0x1f5, 1, 0x15180);
        return;
    case W8_FACT_FERRO_VAMPIRE_CHAIN_ALL_INGREDIENTS:
        if (value == 0) {
            return;
        }
        AddNpcItemWithDelay(GetNpcStateByKind(0x39), 0x1f8, 1, 0x15180);
        return;
    case W8_FACT_ALETHEIDES_DISAPPEARS:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::aletheides_leaves);
        return;
    case W8_FACT_QUEEN_IS_FREE:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x56);
        return;
    case W8_FACT_PRINCE_NOT_IN_CASTLE:
        if (value == 0) {
            return;
        }
        ReleaseNpcMonsterByKind(0x55);
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::prince_not_home);
        return;
    case W8_FACT_PRINCE_DISAPPEARS:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::prince_disappears);
        return;
    case W8_FACT_UMPANI_INITIATE_LANDING:
        if (value == 0) {
            return;
        }
        position = glm::vec3(53550.0f, 3516.0f, 36936.0f);
        CameraLookAt(&position);
        fact_value = GetFact(W8_FACT_TMISSION_KILL_UMPANI_CORD_SEVERED);
        if (fact_value == 0) {
            trigger_name = "TR2ShipTrigger";
        } else {
            trigger_name = "glassTrigger";
        }
        RunNamedTrigger(trigger_name, -1);
        return;
    case W8_FACT_RATTUS_BANK_ROBBERY:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x52);
        return;
    case W8_FACT_HENCHMAN_DISAPPEARS:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::henchman_leaves);
        return;
    case W8_FACT_ALETHEIDES_CALLS_HENCHMAN:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::call_henchman);
        return;
    case W8_FACT_ENDGAME_MOVE_TO_BOOK:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_to_book);
        return;
    case W8_FACT_ENDGAME_TURN_TOWARD_BOOK_WRITE:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::turn_to_book);
        return;
    case W8_FACT_SAVANT_HACK_ALETHEIDES:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::savant_hack);
        return;
    case W8_FACT_ENDGAME_SAVANT_APPEARS:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::savant_appears);
        return;
    case W8_FACT_QUE_ENDGAME2:
        context.status.endgame2_queued = 1;
        return;
    case W8_FACT_ENDGAME_SAVANT_PHOON_SPLIT:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::phoonzang_split);
        return;
    case W8_FACT_QUE_ENDGAME3:
        context.status.endgame3_queued = 1;
        return;
    case W8_FACT_UMISSION_MOVE_RUBBLE_COVERT:
        if (value == 0) {
            return;
        }
        npc = GetNpcStateByKind(0x29);
        assert(npc != 0);
        MarkNpcOfKind(0x29);
        return;
    case W8_FACT_LOCATION_GIGAS_TOGGLE_LIFT3:
    case W8_FACT_LOCATION_GIGAS_TOGGLE_LIFT2:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("greenLightMeansGo", -1);
        return;
    case W8_FACT_LOCATION_GIGAS_TOGGLE_LIFT1:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("greenLightMeansGo01", -1);
        return;
    case W8_FACT_LOCATION_GIGAS_OFFICERS_QUARTERS_GATE_OPEN2:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("Door18", -1);
        RunNamedTrigger("greenLightEwaxx02", -1);
        RunNamedTrigger("greenLightEwaxx03", -1);
        location_id = GetLocationVarIDByName("ObstacleDoors");
        if (location_id != -1) {
            SetTriggerVariableByName("ObstacleDoors",
                                     GetLocationVarValueByName("ObstacleDoors") | 2);
            return;
        }
        CreateLocationVar("ObstacleDoors", 2);
        return;
    case W8_FACT_LOCATION_GIGAS_OFFICERS_QUARTERS_GATE_OPEN1:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("Door19", -1);
        RunNamedTrigger("greenLightEwaxx", -1);
        RunNamedTrigger("greenLightEwaxx01", -1);
        location_id = GetLocationVarIDByName("ObstacleDoors");
        if (location_id != -1) {
            SetTriggerVariableByName("ObstacleDoors",
                                     GetLocationVarValueByName("ObstacleDoors") | 1);
            return;
        }
        CreateLocationVar("ObstacleDoors", 1);
        return;
    case W8_FACT_LOCATION_GIGAS_TOP_DOOR_OPEN1:
    case W8_FACT_LOCATION_GIGAS_TOP_DOOR_OPEN2:
        if (value == 0) {
            return;
        }
        RunNamedTrigger("greenLightMeansOpen02", -1);
        RunNamedTrigger("greenLightMeansOpen03", -1);
        return;
    case W8_FACT_TRANG_INTRO_DISAPPEAR_GUARD:
        CurrentGame().npc_dialogue.Queue(W8NpcCommand::remove_speaking_npc);
        return;
    case W8_FACT_INTRO_VI_DISAPPEARS:
        if (value == 0) {
            return;
        }
        MarkNpcOfKind(0x18);
        EndNpcDialogueSession(false);
        return;
    case W8_FACT_FUZZFAS_BOOM1:
    case W8_FACT_FUZZFAS_BOOM2:
        if (wiz8::sound::play("Data\\Sound\\misc\\potion_exploding.wav",
                              {.on_finished = ClearPotionExplosionSoundFlag}) != nullptr) {
            CurrentGame().npc_dialogue.HoldForScene();
        }
        particle = FindParticleByName("FuzzBlast");
        if (particle != 0) {
            particle->SetActive(false);
            particle->start_frame = 0;
            particle->emission_count = 0;
            particle->SetActive(true);
            particle->SetTraversalEnabled(true);
        }
        npc = GetNpcStateByKind(0x49);
        if (npc == 0) {
            return;
        }
        monster_info = GetNpcMonsterInfo(npc);
        if (monster_info == 0) {
            return;
        }
        if (!monster_info->entity->IsCycleInterruptable(
                monster_info->entity->visual.pending_cycle)) {
            return;
        }
        StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_GET_HIT, W8_ANIMATION_PLAY_ONCE);
        return;
    case W8_FACT_QUEST_SAVANT_BOMB_DEACTIVATED:
        npc = GetNpcStateByKind(0x57);
        if (npc == 0) {
            return;
        }
        if (npc->greeting_pending) {
            return;
        }
        ReleaseNpcMonsterByKind(0x57);
        return;
    case W8_FACT_UMPANI_BALBRAK_INTRO_DISAPPEAR:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::balbrak_home);
        return;
    case W8_FACT_UMPANI_MOVE_RUBBLE:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_rubble);
        return;
    case W8_FACT_QUEST_KILL_ALSEDEXUS:
        if (value != 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcCommand::spacer);
        return;
    case W8_FACT_ALSEDEXUS_DISAPPEAR:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::sedexus_leaves);
        return;
    case W8_FACT_ALSEDEXUS_DISAPPEARS_RIFT:
        if (value == 0) {
            return;
        }
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_sedexus_rift);
        return;
    case W8_FACT_MOVE_TO_BOOK2:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::move_to_book2);
        return;
    case W8_FACT_CC_REMOVE_RPC_VI:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::remove_rpc_vi);
        return;
    case W8_FACT_ENDGAME_JOIN_SAVANT:
        if (value == 0) {
            return;
        }
        context.status.vi_event_stage = 1;
        group = FindFirstMonsterByID(0x234);
        if (group != 0) {
            SetMonsterGroupHostility(group, W8_DISPOSITION_FRIENDLY, false);
        }
        group = FindFirstMonsterByID(0x1b4);
        if (group != 0) {
            SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
        }
        group = FindFirstMonsterByID(0x1b9);
        if (group != 0) {
            SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
        }
        GetNpcStateByKind(0x89);
        return;
    case W8_FACT_FACTION_RAPAX_TEMPLAR_FRIENDLY:
        if (value == 0) {
            SetFactionDispositionBand(0x10, W8_FACTION_HOSTILE);
            SetFactionDispositionBand(0xf, W8_FACTION_HOSTILE);
            return;
        }
        SetFactionDispositionBand(0x10, W8_FACTION_FRIENDLY);
        SetFactionDispositionBand(0xf, W8_FACTION_FRIENDLY);
        return;
    case W8_FACT_ENDGAME_QUE_CONGRAT_TWO:
    case W8_FACT_ENDGAME_QUE_CONGRAT_THREE:
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::begin_endgame);
        return;
    case W8_FACT_ENDGAME_QUE_CONGRAT_FOUR:
        EndNpcDialogueSession(false);
        ResetLevelDataVectors();
        group = FindFirstMonsterByID(0xc2);
        if (group != 0) {
            monster_info = FindMonster(group->leader_location_id);
            monster_info->entity->SetScript("MoveSavantBoffo.msf", true);
        }
        wiz8::sound::stream("Data\\Sound\\Misc\\Earthquake End.wav");
        {
            ShakeDefinition shake(6.0f, false, 1.0f, 0, nullptr);
            shake.fade_in = true;
            shake.completion_callback = ReplayEarthquakeShake;
            CameraShakes().Start(shake);
        }
        return;
    default:
        return;
    }
}

void HandleScriptedNpcDeath(const ScriptContext& context, unsigned int monster_list_index)
{
    W8MonsterInfo* monster_info = GetMonsterListEntry(monster_list_index);
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    if (record == 0) {
        return;
    }
    if ((record->flags & W8_MONSTER_FLAG_NPC) != 0) {
        W8NpcState* npc = GetNpcStateByKind(record->npc_kind);
        if (npc != 0) {
            npc->spawned = 1;
            if (npc->name_style == W8_NPC_VI_DOMINA) {
                unsigned char fact_ok = GetFact(W8_FACT_VI_RESCUED);
                if (fact_ok == 0) {
                    fact_ok = GetFact(W8_FACT_MYLES_MISSION_RESCUE_VI_ASSIGNED);
                    if (fact_ok == 0) {
                        npc->spawned = 0;
                    }
                }
            }
        }
    }
    if (record->record_id != 0x234) {
        return;
    }
    int lead_index = -1;
    if (NpcLeadHasNameStyle(W8_NPC_VI_DOMINA)) {
        W8NpcState* lead = GetNpcStateByKind(W8_NPC_VI_DOMINA);
        if (lead != 0) {
            lead_index = lead->group_index;
        }
    }
    BeginScriptedWorldAction();
    int eligible_slots[8];
    /* 0x00508F08 and 0x00508F58 test the eligible count unsigned. */
    unsigned int eligible_count = 0;
    int slot;
    unsigned int index;
    int pick;
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &context.party.characters[slot];
        if (context.party.members[slot].has_value() && character->hp_current != 0 &&
            character->highest_condition < W8_CONDITION_ASLEEP) {
            eligible_slots[eligible_count] = slot;
            ++eligible_count;
        }
    }
    for (index = 0; index < eligible_count; ++index) {
        if (eligible_slots[index] == lead_index) {
            QueueCharacterEvent(&context.party.characters[eligible_slots[index]], 36,
                                kCharacterEventNoNpcDefer, kCharacterEventFullVolume);
        }
    }
    if (eligible_count > 2) {
        do {
            pick = context.game.rng.below(eligible_count);
        } while (eligible_slots[pick] == lead_index);
        QueueCharacterEvent(&context.party.characters[eligible_slots[pick]], 36,
                            kCharacterEventNoNpcDefer, kCharacterEventFullVolume);
    }
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &context.party.characters[slot];
        if (context.party.members[slot].has_value() && character->hp_current != 0 &&
            character->highest_condition < W8_CONDITION_ASLEEP) {
            QueueCharacterEvent(character, 42, kCharacterEventNoNpcDefer,
                                kCharacterEventFullVolume);
        }
    }
    if (context.status.endgame2_queued != 0 ||
        (context.status.endgame3_queued != 0 && FindNpcOfKind(W8_NPC_PHOONZANG) != 0)) {
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::turn_to_book);
    } else if (context.status.endgame3_queued != 0) {
        CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::phoonzang_notice);
    }
    for (unsigned int entry_index = 0;
         entry_index < CurrentLevelPopulation().monster_roster.live().size(); ++entry_index) {
        W8MonsterInfo* entry = GetMonsterListEntry(entry_index);
        if (entry->fActive && entry->ubDisposition == W8_DISPOSITION_HOSTILE &&
            !entry->entity->IsDying()) {

            MonsterStartsDying(entry, true);
        }
    }
}

/* Scripted kill reactions keyed by monster record id: facts and faction
   changes for the special kills, the Rattkin breeder location-variable count,
   and the 0x22b cleanup that clears the victim's condition and queues the
   death event on the recorded party slot. */
void MonsterKilled(const ScriptContext& context, int record_id, int killer_party_slot)
{
    unsigned char value;

    if (record_id == 0x131) {
        SetFact(W8_FACT_CROCK_BREKEK_DEAD, 1, false);
        return;
    }
    if (record_id == 0x1a9) {
        SetFact(W8_FACT_QUEEN_IS_DEAD, 1, false);
        ApplyFactionChange(2, 1, 0x10, 0x14);
        return;
    }
    if (record_id == 0x14f || record_id == 0xdd) {
        if (GetLocationVarIDByName("NumberRattkinBreedersKilled") == -1) {
            CreateLocationVar("NumberRattkinBreedersKilled", 1);
            return;
        }
        SetTriggerVariableByName("NumberRattkinBreedersKilled", 2);
        SetFact(W8_FACT_TRYNNIE_KILL_RATS_DONE, 1, false);
        SetFactionDispositionBand(7, W8_FACTION_HOSTILE);
    } else {
        if (record_id == 0x22b) {
            value = GetFact(W8_FACT_ALSEDEXUS_ATTACK);
            if (value != 0) {
                SetFact(W8_FACT_QUEST_KILL_ALSEDEXUS, 0, false);
            }
            if (context.status.rpc_active) {
                if (context.party.characters[context.status.sedexus_party_slot]
                        .uiCondition[W8_CONDITION_INFATUATED] > 0) {
                    LiftCondition(CurrentGame(), context.status.sedexus_party_slot,
                                  W8_CONDITION_INFATUATED, false);
                }
                QueueCharacterEvent(&context.party.characters[context.status.sedexus_party_slot],
                                    131, 0, kCharacterEventFullVolume);
            }
            SetFact(W8_FACT_RAPAX_ALSEDEXUS_DEAD, 1, false);
            return;
        }
        if (record_id == 0x181) {
            SetFact(W8_FACT_KING_IS_DEAD, 1, false);
            return;
        }
        if (record_id == 0x175 || record_id == 0x222) {
            SetFact(W8_FACT_LAVALORD_DIE, 1, false);
            return;
        }
    }
}

/* Original translation-unit ownership of these helpers is unknown. */

unsigned char GetFact(W8FactId fact_id)
{
    unsigned char value;
    std::string display_value;

    if (static_cast<unsigned>(fact_id) >= W8_FACT_SLOT_COUNT) {
        return 0;
    }

    value = EvaluateFact(fact_id);
    if (CurrentGame().status.log_fact_checks &&
        static_cast<unsigned>(fact_id) < Data().facts.size()) {
        if (value) {
            display_value = "TRUE";
        } else {
            display_value = "FALSE";
        }
        ShowNotice(W8_FONT_PALETTE_YELLOW,
                   fmt::format("Checking fact {} which is {}", Data().facts[fact_id].symbolic_name,
                               display_value));
    }
    return value;
}

void SetFact(W8FactId fact_id, unsigned char value, bool suppress_side_effects)
{
    unsigned char previous_value;
    std::string display_value;

    if (static_cast<unsigned>(fact_id) >= W8_FACT_SLOT_COUNT) {
        return;
    }

    Game& game = CurrentGame();
    auto& facts = game.story.facts;
    previous_value = facts[fact_id];
    facts[fact_id] = value;

    if (fact_id < static_cast<int>(Data().facts.size())) {
        if (value) {
            display_value = "TRUE";
        } else {
            display_value = "FALSE";
        }
    }

    if (!suppress_side_effects) {
        if (facts[fact_id] != previous_value) {
            RecordFactChangeForJournal(fact_id);
        }
        HandleFactChange(MakeScriptContext(game), fact_id, value);

        if (game.status.log_fact_checks && static_cast<unsigned>(fact_id) < Data().facts.size()) {
            if (value) {
                display_value = "TRUE";
            } else {
                display_value = "FALSE";
            }
            ShowNotice(
                W8_FONT_PALETTE_YELLOW,
                fmt::format("{} set to {}", Data().facts[fact_id].symbolic_name, display_value));
        }
    }
}

/* The opening sequence consumes only the imported story choices. The decoded
   Wizardry 7 records have already been discarded after character conversion. */
void SeedNewGameFacts(Story& story)
{
    g_fact_notifications_suppressed = true;
    if (story.imported_origin) {
        SetFact(W8_FACT_IMPORT, 1, false);
        switch (story.imported_origin->alignment) {
        case Story::ImportedOrigin::Alignment::Umpani:
            SetFact(W8_FACT_IMPORT_UMPANI, 1, false);
            break;
        case Story::ImportedOrigin::Alignment::Trang:
            SetFact(W8_FACT_IMPORT_TRANG, 1, false);
            break;
        default:
            SetFact(W8_FACT_IMPORT_NOALIGN, 1, false);
            break;
        }
        if (story.imported_origin->barlone_dead) {
            SetFact(W8_FACT_BARLONE_WAS_DEAD, 1, false);
        }
        if (story.imported_origin->rodan_dead) {
            SetFact(W8_FACT_RODAN_WAS_DEAD, 1, false);
        }
    } else {
        SetFact(W8_FACT_VIRGIN, 1, false);
        SetFact(W8_FACT_QUEST_VIRGIN_1, 1, false);
        SetFact(W8_FACT_QUEST_VIRGIN_2, 1, false);
        SetFact(W8_FACT_QUEST_VIRGIN_3, 1, false);
    }
    story.imported_origin.reset();
    g_fact_notifications_suppressed = false;
}
