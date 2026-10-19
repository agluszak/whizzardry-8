#include "wiz8/level_specific_code/MasterFunctionList.h"

#include <cstdarg>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "gameplay_modifiers.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "item_spawning.h"
#include "layouts/gameplay_databases.h"
#include "surrender/srDebug.h"
#include "wiz8/character_skills.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/fact_state.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/layouts/world.h"
#include "wiz8/level_specific_code/Arnika.h"
#include "wiz8/level_specific_code/Ascension.h"
#include "wiz8/level_specific_code/Camp.h"
#include "wiz8/level_specific_code/ConnectiveTissue.h"
#include "wiz8/level_specific_code/CosmicCircle.h"
#include "wiz8/level_specific_code/MartensBluff1.h"
#include "wiz8/level_specific_code/MartensBluff2.h"
#include "wiz8/level_specific_code/Monastery1.h"
#include "wiz8/level_specific_code/Monastery2.h"
#include "wiz8/level_specific_code/MtGigas1.h"
#include "wiz8/level_specific_code/MtGigas2.h"
#include "wiz8/level_specific_code/MtGigasOuter.h"
#include "wiz8/level_specific_code/MtGigasTop.h"
#include "wiz8/level_specific_code/RapaxMainFloor.h"
#include "wiz8/level_specific_code/RapaxUpperFloor.h"
#include "wiz8/level_specific_code/Rift1.h"
#include "wiz8/level_specific_code/SavantTower.h"
#include "wiz8/level_specific_code/SeaCaves.h"
#include "wiz8/level_specific_code/Swamp.h"
#include "wiz8/level_specific_code/Trynnie2.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/location_variables.h"
#include "wiz8/save_game.h"
#include "wiz8/string_database.h"

constexpr float kSoundNodeFullVolume = 127.0f;

/* The world-cursor node the party is standing in, tracked across the
   command-0 sweep so enter/leave commands fire once per crossing. */
// GLOBAL: WIZ8 0x006834d4
static W8WorldCursorNode* g_active_cursor_node;

// GLOBAL: WIZ8 0x006834d8
std::vector<W8LevelMaster> g_master_functions;

// Capture the batch size: callbacks may append work for the next frame.
// FUNCTION: WIZ8 0x004D8E40
void RunMasterFunctions(void)
{
    int count = static_cast<int>(g_master_functions.size());

    for (int index = 0; index < count; ++index) {
        const auto update = g_master_functions[index].update;
        if (update(0)) {
            g_master_functions.erase(g_master_functions.begin() + index);
            --count;
            --index;
        }
    }
}

W8SavedLocationState CaptureMasterFunctionState()
{
    W8SavedLocationState state;
    for (const auto& master : g_master_functions)
        if (master.capture) master.capture(state);
    return state;
}

/* The level callbacks' scripted spawn: settle the requested point onto the
   ground when asked, create the group, apply hostility, refresh its centre
   cache and re-run outward sight for the new arrivals. */
// FUNCTION: WIZ8 0x004D8F00
W8MonsterGroup* SpawnMonsters(int monster_id, int count, glm::vec3* position, int hostility,
                              bool settle, bool a, bool b)
{
    W8MonsterGroup* group;
    glm::vec3 position_copy;

    if (settle) {
        position->y = probeGround((*position)).position.y;
    }
    position_copy = *position;
    group = CreateGroup(monster_id, count, &position_copy, a, b, settle);
    if (group != 0) {
        SetMonsterGroupHostility(group, static_cast<W8Disposition>(hostility), false);
        GetMonsterGroupCentre(group, 0);
        RefreshOutwardSightForAllMonsters();
    }
    return group;
}

/* The level callbacks' one-shot positional sound: reject a negative
   loudness fraction, clamp anything above one, then create the node at the
   requested spot, scale its base volume and falloff and start playback. */
// FUNCTION: WIZ8 0x004D8F80
stSound3D* CreateAndPlaySoundNode(const char* sound_name, glm::vec3 position, float volume,
                                  float scale, bool play_flag)
{
    if (volume > 1.0f) {
        volume = 1.0f;
    } else if (volume < 0.0f) {
        return 0;
    }

    auto sound = std::make_unique<stSound3D>(sound_name, nullptr);
    sound->setLocation(glm::dvec3(position));
    sound->volume = static_cast<int>(volume * kSoundNodeFullVolume);
    sound->falloff = scale * kWorldScale;
    sound->Play(play_flag);
    return Retain3DSound(std::move(sound));
}

/* One row of the cursor-node dispatch table at 0x006109F4, indexed by the
   node's type parameter (numbers[2]). The dispatcher invokes a row as
   (command, node, context); the context slot normally carries the address of
   the command's argument byte, while command 4 smuggles the byte itself
   through it. */
typedef unsigned char (*W8WorldCursorNodeHandler)(int command, W8WorldCursorNode* node,
                                                  int context);

static unsigned char WorldCursorNodeShowMessageOnce(int command, W8WorldCursorNode* node,
                                                    int context);
static unsigned char WorldCursorNodeShowContextMessage(int command, W8WorldCursorNode* node,
                                                       int context);
static unsigned char WorldCursorNodeShowMessage(int command, W8WorldCursorNode* node, int context);
static unsigned char WorldCursorNodeApplyItemEffect(int command, W8WorldCursorNode* node,
                                                    int context);
static unsigned char WorldCursorNodeMaleCharacterEvent(int command, W8WorldCursorNode* node,
                                                       int context);
static unsigned char IsMasterFunctionTypeEight(int command, W8WorldCursorNode* node, int context);
static unsigned char WorldCursorNodeSeenBodies(int command, W8WorldCursorNode* node, int context);
static unsigned char WorldCursorNodeApplyType6ItemEffect(int command, W8WorldCursorNode* node,
                                                         int context);
static unsigned char WorldCursorNodePartyVoice(int command, W8WorldCursorNode* node, int context);

/* The character-event kind constants the cursor-node handlers queue. */
// GLOBAL: WIZ8 0x005EE5F4
const int g_character_event_kind0 = 0x1b;
// GLOBAL: WIZ8 0x005EE63C
const int g_character_event_kind1 = 0x2d;
// GLOBAL: WIZ8 0x005EE688
const int g_character_event_kind3 = 0x40;

// GLOBAL: WIZ8 0x006109F4
static W8WorldCursorNodeHandler g_world_cursor_node_handlers[10] = {
    WorldCursorNodeShowMessageOnce,      /* type 0 */
    WorldCursorNodeShowMessageOnce,      /* type 1 */
    WorldCursorNodeShowContextMessage,   /* type 2 */
    WorldCursorNodeShowMessage,          /* type 3 */
    WorldCursorNodeApplyItemEffect,      /* type 4 */
    WorldCursorNodeSeenBodies,           /* type 5 */
    WorldCursorNodeApplyType6ItemEffect, /* type 6 */
    IsMasterFunctionTypeEight,           /* type 7 */
    WorldCursorNodePartyVoice,           /* type 8 */
    WorldCursorNodeMaleCharacterEvent    /* type 9 */
};

/* Dispatch one world-cursor-node command against the nodes covering `info`'s
   ground-settled position (the camera's when info is null). Command zero is
   the movement sweep: the tracked node's handler gets 2 (leave) and each
   newly entered node's handler gets 1 (enter); commands 3, 4 and 8 deliver
   themselves to nodes of type 3, 2 and 7 respectively. Answers whether any
   handler reported the command handled. */
// FUNCTION: WIZ8 0x004D9080
unsigned char DispatchWorldCursorNodeCommand(W8MonsterInfo* info, int command, ...)
{
    glm::vec3 position;
    W8WorldCursorNode* node;
    W8WorldCursorNode* previous;
    int context;
    bool handled;
    unsigned char result;

    result = 0;
    handled = false;
    // Retail callers use both two and three arguments. Non-4 commands only
    // forward the optional argument slot's address; handlers do not read it.
    va_list arguments;
    va_start(arguments, command);
    // Only command 4 reads context, from va_arg below. Other handlers ignore
    // the historical address of the optional argument slot.
    context = 0;

    if (info == 0) {
        position = cameraLocation();
    } else {
        position = info->p3D->GetPosition();
    }
    position.y = probeGround(position).position.y + 125.0f;
    node = FindWorldCursorNodeAtPoint(0, &position);
    if (command == 4) {
        context = va_arg(arguments, int);
    }
    va_end(arguments);
    if (node == 0) {
        return handled;
    }
    do {
        previous = g_active_cursor_node;
        switch (command) {
        case 0:
            if (previous != 0 && previous != node) {
                result =
                    g_world_cursor_node_handlers[previous->parameters[2]](2, previous, context);
            }
            previous = g_active_cursor_node;
            if (node != 0 && previous != node) {
                result = g_world_cursor_node_handlers[node->parameters[2]](1, node, context);
            }
            g_active_cursor_node = node;
            break;
        case 4:
            if (node != 0 && node->parameters[2] == 2) {
                result = g_world_cursor_node_handlers[2](4, node, context);
            }
            break;
        case 8:
            if (node != 0 && node->parameters[2] == 7) {
                result = g_world_cursor_node_handlers[7](8, node, context);
            }
            break;
        case 3:
            if (node != 0) {
                int type = node->parameters[2];
                if (type == 3) {
                    result = g_world_cursor_node_handlers[3](type, node, context);
                }
            }
            break;
        default:
            result = 0;
            break;
        }
        if (result != 0) {
            handled = true;
        }
        node = FindWorldCursorNodeAtPoint(node, &position);
    } while (node != 0);
    return handled;
}

/* Type-0/1 nodes: on the enter command, show the node's message-database
   string once - the first byte of the node's userdata is the shown flag.
   Parameter 1 additionally gates the message on a living party member with
   trait 0x0c while the party is neither searching nor under effect 0x11. */
// FUNCTION: WIZ8 0x004D9260
static unsigned char WorldCursorNodeShowMessageOnce(int command, W8WorldCursorNode* node,
                                                    int context)
{
    int message_id;
    const char* folder;
    int type;
    unsigned char enabled;
    bool result;
    std::byte* shown;
    int slot;
    std::string path;
    std::string text;

    result = false;
    message_id = node->parameters[0];
    folder = GetLevelFolderName(GetLoadedLevelID());
    type = node->parameters[2];
    enabled = static_cast<unsigned char>(node->parameters[1]);
    if (enabled != 0 && g_status.search_mode == 0 && g_status.party_modifiers.detect_secrets == 0) {
        for (slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
            if (g_status.buffers.XChar[slot].fOccupied &&
                CharacterHasTrait(g_status.buffers.Char + slot, W8_TRAIT_SEARCH)) {
                goto command_check;
            }
        }
        return 0;
    }
command_check:
    if (command != 1) {
        return 0;
    }
    if (type == 0) {
        result = true;
        if (node->userdata.empty()) node->userdata.resize(1);
        shown = node->userdata.data();
        if (*shown != std::byte{}) {
            return 0;
        }
        *shown = std::byte{1};
        if (folder == 0) {
            folder = "Test";
        }
        path = fmt::format("Data\\Messages\\{}.msg", folder);
        if ((!(text = GetStringFromStringDatabase(path, message_id, 0, 0)).empty()) != 0) {
            ShowString(text);
        }
    }
    return result;
}

/* Type-2 nodes: command 4 with a nonzero context flag shows the node's
   message while the main game screen is up. */
// FUNCTION: WIZ8 0x004D93E0
static unsigned char WorldCursorNodeShowContextMessage(int command, W8WorldCursorNode* node,
                                                       int context)
{
    const char* folder;
    int message_id;
    std::string path;
    std::string text;

    folder = GetLevelFolderName(GetLoadedLevelID());
    if (command != 4) {
        return 0;
    }
    if (static_cast<unsigned char>(context) != 0) {
        message_id = node->parameters[0];
        if (folder == 0) {
            folder = "Test";
        }
        path = fmt::format("Data\\Messages\\{}.msg", folder);
        if ((!(text = GetStringFromStringDatabase(path, message_id, 0, 0)).empty()) != 0) {
            if (text.size() != 0 && g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
                ShowString(text);
            }
        }
    }
    return 1;
}

/* Type-3 nodes: command 3 shows the node's message unconditionally. */
// FUNCTION: WIZ8 0x004D94B0
static unsigned char WorldCursorNodeShowMessage(int command, W8WorldCursorNode* node, int context)
{
    const char* folder;
    int message_id;
    std::string path;
    std::string text;

    folder = GetLevelFolderName(GetLoadedLevelID());
    if (command != 3) {
        return 0;
    }
    message_id = node->parameters[0];
    if (folder == 0) {
        folder = "Test";
    }
    path = fmt::format("Data\\Messages\\{}.msg", folder);
    if ((!(text = GetStringFromStringDatabase(path, message_id, 0, 0)).empty()) != 0) {
        if (text.size() != 0) {
            ShowString(text);
        }
    }
    return 1;
}

/* Type-4 nodes: entering applies the node's item effect to a random party
   member. */
// FUNCTION: WIZ8 0x004D9560
static unsigned char WorldCursorNodeApplyItemEffect(int command, W8WorldCursorNode* node,
                                                    int context)
{
    if (command == 1) {
        ApplyItemEffectToRandomCharacter(g_character_event_kind0, -1, 0);
    }
    return 1;
}

/* Type-9 nodes: entering while the fact-0x14c gate holds queues the male
   event for the indexed party member when that member is male. */
// FUNCTION: WIZ8 0x004D9590
static unsigned char WorldCursorNodeMaleCharacterEvent(int command, W8WorldCursorNode* node,
                                                       int context)
{
    W8Character* character;

    if (command == 1 && g_status.rpc_active) {
        character = g_status.buffers.Char + g_status.sedexus_party_slot;
        if (character->gender == W8_GENDER_MALE) {
            QueueCharacterEvent(character, g_character_event_kind1, 0, kCharacterEventFullVolume);
        }
    }
    return 1;
}

/* Predicate installed in the master-function callback table. */
// FUNCTION: WIZ8 0x004D95F0
static unsigned char IsMasterFunctionTypeEight(int command, W8WorldCursorNode* node, int context)
{
    return command == 8;
}

/* Type-5 nodes: the Ascension Peak "seen bodies" check. Once all three relic
   items are on the party and neither blocking fact holds, queue the reaction
   and latch the AP_SeenBodies location variable. Retail runs the fact-0x133
   clear twice. */
// FUNCTION: WIZ8 0x004D9600
static unsigned char WorldCursorNodeSeenBodies(int command, W8WorldCursorNode* node, int context)
{
    if (command == 1) {
        if (GetLocationVarIDByName("AP_SeenBodies") == -1) {
            if (CountAscensionPeakItems() == 3) {
                if (GetFact(W8_FACT_TMISSION_MEET_ZANT_AT_AP) != 0 ||
                    GetFact(W8_FACT_UMISSION_MEET_AP_ASSIGN) != 0) {
                    ApplyItemEffectToRandomCharacter(g_character_event_kind3, -1, 0);
                    CreateLocationVar("AP_SeenBodies", 1);
                    if (GetFact(W8_FACT_QUEST_PEACE_GOTO_AP) != 0) {
                        SetFact(W8_FACT_QUEST_PEACE_GOTO_AP, 0, false);
                    }
                    if (GetFact(W8_FACT_QUEST_PEACE_GOTO_AP) != 0) {
                        SetFact(W8_FACT_QUEST_PEACE_GOTO_AP, 0, false);
                    }
                }
            }
        }
    }
    return 1;
}

/* Type-6 nodes: entering applies the node's item effect to a random party
   member. */
// FUNCTION: WIZ8 0x004D96C0
static unsigned char WorldCursorNodeApplyType6ItemEffect(int command, W8WorldCursorNode* node,
                                                         int context)
{
    if (command == 1) {
        ApplyItemEffectToRandomCharacter(g_character_event_kind3, -1, 0);
    }
    return 1;
}

// FUNCTION: WIZ8 0x004D96F0
void ClearActiveWorldCursorNode(void)
{
    g_active_cursor_node = 0;
}

/* Master-function values 0x10 and 0x26 select the same path once either of
   the two enabling facts has been set. */
// FUNCTION: WIZ8 0x004D9700
int NormalizeMasterFunctionValue(int value)
{
    if (value == 0x10 || value == 0x26) {
        if (GetFact(W8_FACT_RAPAX_AWAY_CAMP_EXISTS) == 0 && GetFact(W8_FACT_PARTY_AT_RAC) == 0) {
            return 0x26;
        }
        value = 0x10;
    }
    return value;
}

// GLOBAL: WIZ8 0x006834DD
bool g_running_trigger_from_script;
// GLOBAL: WIZ8 0x006109F0
bool g_npc_dialogue_closed = true;
// GLOBAL: WIZ8 0x00652DA5
bool g_sea_caves_slope_override_enabled;

/* Fill the away camp chest from the level-0x26 item records stored in the
   current save. Records that are not world-persistent move into the chest
   trigger's container item; persistent records are flattened back into the
   load vector (so their own groups are visited too) and then freed. */
// FUNCTION: WIZ8 0x004D9740
void LoadAwayCampChest(void)
{
    std::vector<std::unique_ptr<W8WorldItem>> items;
    W8Prop* pChest;
    Trigger* pTrigger;
    W8WorldItem* pContainer;
    int index;

    pChest = FindPropByName(GetWorld(), "AwayCampChest");
    if (pChest == 0) {
        srAssertFail("pChest");
    }
    pTrigger = pChest->GetTrigger();
    if (pTrigger == 0) {
        srAssertFail("pTrigger");
    }
    pContainer = pTrigger->GetOrCreateItemGroup(true);
    if (pContainer == 0) {
        srAssertFail("pContainer");
    }
    LoadSavedLevelItems(0x26, items);
    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        auto item = std::move(items[index]);
        if (ItemInfoIsWorldPersistent(item.get())) {
            for (auto& contained : item->contents) items.push_back(std::move(contained));
        } else {
            pContainer->contents.push_back(std::move(item));
        }
    }
}

/* Type-8 nodes: entering queues a random party member's voice line for the
   node's event type, once - the first byte of the node's userdata is the
   shown flag. */
// FUNCTION: WIZ8 0x004D98C0
static unsigned char WorldCursorNodePartyVoice(int command, W8WorldCursorNode* node, int context)
{
    int event_type;
    std::byte* shown;
    int slot;

    event_type = node->parameters[0];
    if (command == 1) {
        if (node->userdata.empty()) node->userdata.resize(1);
        shown = node->userdata.data();
        if (*shown == std::byte{}) {
            *shown = std::byte{1};
            slot = PickRandomPartySpeaker(event_type, 0xff);
            if (slot != -1) {
                QueueCharacterEvent(g_status.buffers.Char + slot, event_type, 0,
                                    kCharacterEventFullVolume);
            }
        }
    }
    return 1;
}

/* Format the current level's message-database path, fetch the indexed string
   and show it. Answers whether the string existed. */
// FUNCTION: WIZ8 0x004D9960
unsigned char ShowLevelMessage(int message_id)
{
    const char* folder;
    std::string path;
    std::string text;

    folder = GetLevelFolderName(GetLoadedLevelID());
    if (folder == 0) {
        folder = "Test";
    }
    path = fmt::format("Data\\Messages\\{}.msg", folder);
    if ((!(text = GetStringFromStringDatabase(path, message_id, 0, 0)).empty()) != 0) {
        ShowString(text);
        return 1;
    }
    return 0;
}

/* The level-specific callbacks and master-function helpers this unit
   installs. Their bodies live in the other level-specific units, so they
   are declared here rather than in the published header. */
void EnsureTrynnie1KilledVar(void);
bool Trynnie2GoodaVineA(Trigger* trigger);
bool Trynnie2GoodaVineB(Trigger* trigger);
bool Trynnie2GiveZulu(Trigger* trigger);
bool Trynnie2MeatMaker(Trigger* trigger);
bool Trynnie2MeatBox(Trigger* trigger);
bool Trynnie2UrnTrigger(Trigger* trigger);
bool Trynnie1FountRandomFX(Trigger* trigger);

/* Activation callbacks that only let the trigger run or stop it. */
static bool AllowTriggerActivation(Trigger* trigger)
{
    return true;
}

static bool BlockTriggerActivation(Trigger* trigger)
{
    return false;
}

static void BindLevelTrigger(const char* name, Trigger::ActivationCallback callback)
{
    FindRequiredTriggerByName(name)->activation_callback = callback;
}

/* Install the level's trigger callbacks and master-function helpers. */
// FUNCTION: WIZ8 0x004D6C50
void InitializeLevelMasterFunctions(int level)
{
    Trigger* pTrigger;

    g_master_functions.clear();
    g_running_trigger_from_script = false;
    g_npc_dialogue_closed = true;
    g_sea_caves_slope_override_enabled = false;
    switch (level) {
    case 0:
        BindLevelTrigger("ChaosMolori", ArnikaChaosMolori);
        BindLevelTrigger("Maddmook", ArnikaMaddmook);
        BindLevelTrigger("CMbox", ArnikaCMbox);
        BindLevelTrigger("AstralDominae", ArnikaAstralDominae);
        BindLevelTrigger("BallSlot", ArnikaBallSlot);
        BindLevelTrigger("Flightrecordertrigger", ArnikaFlightRecorder);
        BindLevelTrigger("ULLspawn", AllowTriggerActivation);
        BindLevelTrigger("Mookholo", ArnikaMookholo);
        BindLevelTrigger("MookFrontDoor", ArnikaMookFrontDoor);
        BindLevelTrigger("YellowButton", ArnikaYellowButton);
        BindLevelTrigger("Vaultalarmdoor", ArnikaVaultAlarmDoor);
        BindLevelTrigger("Exitbutton", ArnikaExitButton);
        BindLevelTrigger("GenVault-2-door", ArnikaGenVaultDoor);
        BindLevelTrigger("ARN11", CosmicCircleReturnFalse);
        BindLevelTrigger("RedButton", ArnikaRedButton);
        BindLevelTrigger("El1-TopButtons", ArnikaEl1TopButtons);
        BindLevelTrigger("El1-BottomButtons", ArnikaEl1BottomButtons);
        BindLevelTrigger("GreenButton", ArnikaGreenButton);
        BindLevelTrigger("Elevator-02", ArnikaElevator02Trigger);
        BindLevelTrigger("LazerScanner", ArnikaLazerScanner);
        pTrigger = FindRequiredTriggerByName("ScannerDoor");
        pTrigger->m_lData1 = 0;
        pTrigger->activation_callback = ArnikaScannerDoor;
        ArnikaLevelSetup();
        return;
    case 1:
        BindLevelTrigger("RampUp", AscensionRampUp);
        BindLevelTrigger("ChaosATrigger", AscensionChaosATrigger);
        BindLevelTrigger("ChaosBTrigger", AscensionChaosBTrigger);
        BindLevelTrigger("LifeATrigger", AscensionLifeATrigger);
        BindLevelTrigger("LifeBTrigger", AscensionLifeBTrigger);
        BindLevelTrigger("KnowATrigger", AscensionKnowATrigger);
        BindLevelTrigger("KnowBTrigger", AscensionKnowBTrigger);
        BindLevelTrigger("RampUp", AscensionDarkSavantSpawn);
        BindLevelTrigger("Path1Camera", AscensionPath1Camera);
        BindLevelTrigger("Shaker", AscensionShaker);
        AscensionPeakInit();
        return;
    case 4:
        CosmicCircleSetup();
        BindLevelTrigger("CC_TRIGGERPLANE1HEDRA", CosmicCircleTriggerPlane1Hedra);
        break;
    case 5:
        MartensBluff1Setup();
        BindLevelTrigger("MR109", MartensBluff1Teleporter);
        pTrigger = FindTriggerByName("MR110");
        if (pTrigger != 0) {
            pTrigger->activation_callback = BlockTriggerActivation;
        }
        pTrigger = FindTriggerByName("MR111");
        if (pTrigger != 0) {
            pTrigger->activation_callback = BlockTriggerActivation;
        }
        pTrigger = FindTriggerByName("MR112");
        if (pTrigger != 0) {
            pTrigger->activation_callback = BlockTriggerActivation;
        }
        BindLevelTrigger("F-Handlock", MartensBluff1FHandlock);
        BindLevelTrigger("ButtonGigas", MartensBluff1ButtonGigas);
        BindLevelTrigger("ButtonTrang", MartensBluff1ButtonTrang);
        BindLevelTrigger("ButtonRift", MartensBluff1ButtonRift);
        BindLevelTrigger("ButtonMaten", MartensBluff1ButtonMaten);
        BindLevelTrigger("WireTrigger", MartensBluff1WireTrigger);
        BindLevelTrigger("ButtonGigas", MartensBluff1ButtonGigas);
        BindLevelTrigger("Controller", MartensBluff1Controller);
        BindLevelTrigger("Dial-A", MartensBluff1DialA);
        BindLevelTrigger("Dial-B", MartensBluff1DialB);
        BindLevelTrigger("Dial-C", MartensBluff1DialC);
        BindLevelTrigger("Gas-Switch", MartensBluff1GasSwitch);
        BindLevelTrigger("J-Doorcontroller", MartensBluff1JDoorController);
        BindLevelTrigger("MartenBook", MartensBluff1MartenBook);
        return;
    case 6:
        MartensBluff2Setup();
        BindLevelTrigger("Arrowtraptrigger", TriggerArrowTrap);
        BindLevelTrigger("Spikeballtrigger", MartensBluff2Spikeballtrigger);
        BindLevelTrigger("DoorBolt", MartensBluff2DoorBolt);
        BindLevelTrigger("DummyLever", MartensBluff2DummyLever);
        BindLevelTrigger("Dummy", MartensBluff2Dummy);
        BindLevelTrigger("PerfumeBox", MartensBluff2PerfumeBox);
        BindLevelTrigger("StoneIdol", MartensBluff2StoneIdol);
        BindLevelTrigger("BlueFlowers", MartensBluff2BlueFlowers);
        BindLevelTrigger("SquisherControls", MartensBluff2SquisherControls);
        BindLevelTrigger("DoorControls", MartensBluff2DoorControls);
        return;
    case 8:
        ClearTextForBarTrigger();
        BindLevelTrigger("roach_trigger", OnRoachTriggerActivated);
        BindLevelTrigger("spider_trigger", OnSpiderTriggerActivated);
        BindLevelTrigger("Bartrigger", OnBarTriggerActivated);
        BindLevelTrigger("Coffinlide", OnCoffinlideActivated);
        BindLevelTrigger("Coffinlidg", OnCoffinlidgActivated);
        BindLevelTrigger("wheel_star", OnWheelStarActivated);
        return;
    case 9:
        BindLevelTrigger("bell_button", Monastery2BellButton);
        BindLevelTrigger("micro_door2", Monastery2MicroDoor2);
        return;
    case 0xc:
        MtGigas1Setup();
        BindLevelTrigger("_VOC_EWAXXLIFT1", MtGigas1Lift1);
        BindLevelTrigger("_VOC_EWAXXLIFT2", MtGigas1Lift2);
        BindLevelTrigger("PRESSUREPLATE", MtGigas1PressurePlate);
        BindLevelTrigger("mudWallTrigger", MtGigas1MudWall);
        return;
    case 0xd:
        MtGigas2Setup();
        BindLevelTrigger("_VOC_EWAXXTRAIN", MtGigas2Train);
        BindLevelTrigger("redwire", MtGigas2RedWire);
        BindLevelTrigger("bluewire", MtGigas2BlueWire);
        BindLevelTrigger("yellowwire", MtGigas2YellowWire);
        BindLevelTrigger("_VOC_EWAXXLIFT3", MtGigas2Lift3);
        BindLevelTrigger("_VOC_EWAXXTOPDOOR1", MtGigas2TopDoor1);
        BindLevelTrigger("_VOC_EWAXXOFFICER1", MtGigas2Officer1);
        BindLevelTrigger("_VOC_EWAXXOFFICER2", MtGigas2Officer2);
        BindLevelTrigger("triggerPlaneLaserAlarm", MtGigas2LaserAlarm);
        BindLevelTrigger("triggerPlaneLaserAlarm01", MtGigas2LaserAlarm);
        BindLevelTrigger("accessHatch", MtGigas2AccessHatch);
        BindLevelTrigger("wiringMalfunction", MtGigas2WiringMalfunction);
        return;
    case 0xe:
        ProcessFlagPosition();
        BindLevelTrigger("_VOC_EWAXXLIFT1", MtGigas1Lift1);
        BindLevelTrigger("crank", OnCrankTriggerActivated);
        BindLevelTrigger("Security Button", OnSecurityButtonActivated);
        BindLevelTrigger("VOC_EWAXXSENTRYtrig", OnSentryTriggerActivated);
        BindLevelTrigger("ewaxxdoortrigger03", OnEwaxxDoor03Activated);
        BindLevelTrigger("dummytrigger", OnDummyTriggerActivated);
        return;
    case 0xf:
        BindLevelTrigger("_VOC_EWAXXCANNON1", OnEwaxxCannon1Activated);
        BindLevelTrigger("EwaxxLanding", OnEwaxxLandingActivated);
        BindLevelTrigger("catchCord", OnCatchCordActivated);
        BindLevelTrigger("painActivatorTrigger", OnPainActivatorActivated);
        BindLevelTrigger("_VOC_EWAXXTOPDOOR2", OnEwaxxTopDoor2Activated);
        return;
    case 0x10:
        if (GetFact(W8_FACT_PARTY_AT_RAC) == 0) {
            LoadAwayCampChest();
            SetFact(W8_FACT_PARTY_AT_RAC, 0, false);
        }
        BindLevelTrigger("prisondoor06", CampPrisonDoor06);
        BindLevelTrigger("prisondoor04", CampPrisonDoor04);
        BindLevelTrigger("prisondoor03", CampPrisonDoor03);
        return;
    case 0x12:
        BindLevelTrigger("AltarBox", RapaxMainFloorAltarBox);
        BindLevelTrigger("platformtrigger", RapaxMainFloorPlatform);
        BindLevelTrigger("platformtrigger01", RapaxMainFloorPlatform01);
        BindLevelTrigger("platformtrigger02", RapaxMainFloorPlatform02);
        return;
    case 0x13:
        BindLevelTrigger("AirBox", RapaxUpperFloorAirBox);
        BindLevelTrigger("DoorDone", RapaxUpperFloorDoorDone);
        return;
    case 0x15:
        BindLevelTrigger("Fireantspawn", Rift1Fireantspawn);
        BindLevelTrigger("Sexspawn", Rift1Sexspawn);
        BindLevelTrigger("Hotstuff", Rift1Hotstuff);
        BindLevelTrigger("Gate", Rift1Gate);
        BindLevelTrigger("AshLock", Rift1AshLock);
        BindLevelTrigger("TimeDorado", Rift1TimeDorado);
        return;
    case 0x16:
        g_sea_caves_slope_override_enabled = true;
        g_party_has_slope_override_item = FindItemOnParty(0x254, 0, 0, 0, 0);
        BindLevelTrigger("HigardiChest01", SeaCavesHigardiChest01);
        BindLevelTrigger("HigardiChest02", SeaCavesHigardiChest02);
        BindLevelTrigger("HigardiChest03", SeaCavesHigardiChest03);
        BindLevelTrigger("HigardiChest04", SeaCavesHigardiChest04);
        BindLevelTrigger("HigardiChest05", SeaCavesHigardiChest05);
        BindLevelTrigger("doortomb", SeaCavesDoorTomb);
        return;
    case 0x18:
        BindLevelTrigger("gas_trig_plane01", SwampGasPlane);
        BindLevelTrigger("gas_trig_plane02", SwampGasPlane);
        BindLevelTrigger("gas_trig_plane03", SwampGasPlane);
        BindLevelTrigger("gas_trig_plane04", SwampGasPlane);
        BindLevelTrigger("gas_trig_plane05", SwampGasPlane);
        BindLevelTrigger("gas_trig_plane06", SwampGasPlane);
        BindLevelTrigger("gas_trig_plane07", SwampGasPlane);
        BindLevelTrigger("oil_pool", SwampOilPool);
        BindLevelTrigger("onelid", SwampOnelid);
        pTrigger = FindRequiredTriggerByName("fire_trig_plane01");
        pTrigger->activation_callback = SwampFirePlane;
        pTrigger = FindRequiredTriggerByName("fire_trig_plane02");
        pTrigger->activation_callback = SwampFirePlane;
        pTrigger = FindRequiredTriggerByName("fire_trig_plane03");
        pTrigger->activation_callback = SwampFirePlane;
        pTrigger = FindRequiredTriggerByName("fire_trig_plane04");
        pTrigger->activation_callback = SwampFirePlane;
        pTrigger = FindRequiredTriggerByName("fire_trig_plane05");
        pTrigger->activation_callback = SwampFirePlane;
        pTrigger = FindRequiredTriggerByName("fire_trig_plane06");
        pTrigger->activation_callback = SwampFirePlane;
        pTrigger = FindRequiredTriggerByName("fire_trig_plane07");
        pTrigger->activation_callback = SwampFirePlane;
        return;
    case 0x19:
        BindLevelTrigger("Fount_randomFX", Trynnie1FountRandomFX);
        EnsureTrynnie1KilledVar();
        return;
    case 0x1a:
        BindLevelTrigger("GoodaVine_A", Trynnie2GoodaVineA);
        BindLevelTrigger("GoodaVine_B", Trynnie2GoodaVineB);
        BindLevelTrigger("Meat_Maker", Trynnie2MeatMaker);
        BindLevelTrigger("Meat_Box", Trynnie2MeatBox);
        BindLevelTrigger("Give_Zulu", Trynnie2GiveZulu);
        BindLevelTrigger("URN_Trigger_01", Trynnie2UrnTrigger);
        BindLevelTrigger("URN_Trigger_02", Trynnie2UrnTrigger);
        BindLevelTrigger("URN_Trigger_03", Trynnie2UrnTrigger);
        BindLevelTrigger("URN_Trigger_04", Trynnie2UrnTrigger);
        EnsureTrynnie2KilledVar();
        return;
    case 0x1b:
        BindLevelTrigger("Liche", ConnectiveTissueLiche);
        return;
    case 0x24:
        SavantTowerSyncButtonBlocker();
        BindLevelTrigger("Triangle", SavantTowerShape);
        BindLevelTrigger("Circle", SavantTowerShape);
        BindLevelTrigger("Square", SavantTowerShape);
        BindLevelTrigger("Star", SavantTowerShape);
        BindLevelTrigger("ButtonBlocker", SavantTowerButtonBlocker);
        return;
    }
    return;
}
