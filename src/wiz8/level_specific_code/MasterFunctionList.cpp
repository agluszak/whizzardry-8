#include <fmt/format.h>
#include <string>
#include <cstdarg>
#include <cstdlib>
#include <vector>

#include "wiz8/integer_constants.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/Arnika.h"
#include "wiz8/level_specific_code/Monastery1.h"
#include "wiz8/level_specific_code/Monastery2.h"
#include "wiz8/level_specific_code/Trynnie2.h"
#include "wiz8/level_specific_code/Swamp.h"
#include "wiz8/level_specific_code/SeaCaves.h"
#include "wiz8/level_specific_code/Rift1.h"
#include "wiz8/level_specific_code/Camp.h"
#include "wiz8/level_specific_code/RapaxMainFloor.h"
#include "wiz8/level_specific_code/RapaxUpperFloor.h"
#include "wiz8/level_specific_code/Ascension.h"
#include "wiz8/level_specific_code/MtGigas1.h"
#include "wiz8/level_specific_code/MtGigas2.h"
#include "wiz8/level_specific_code/MtGigasOuter.h"
#include "wiz8/level_specific_code/MtGigasTop.h"
#include "wiz8/level_specific_code/CosmicCircle.h"
#include "wiz8/level_specific_code/MartensBluff1.h"
#include "wiz8/level_specific_code/MartensBluff2.h"
#include "wiz8/level_specific_code/SavantTower.h"
#include "wiz8/level_specific_code/ConnectiveTissue.h"
#include "wiz8/fact_state.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/layouts/world.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/save_game.h"
#include "wiz8/sr_api.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_screens/mipe.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/location_variables.h"
#include "wiz8/character_skills.h"
#include "wiz8/string_database.h"
#include "wiz8/float_constants.h"
#include "gameplay_modifiers.h"
#include "glm/ext/vector_double3.hpp"
#include "item_spawning.h"
#include "layouts/gameplay_databases.h"
#include "surrender/srMath.h"


/* The world-cursor node the party is standing in, tracked across the
   command-0 sweep so enter/leave commands fire once per crossing. */
// GLOBAL: WIZ8 0x006834d4
static W8WorldCursorNode* g_active_cursor_node;

// GLOBAL: WIZ8 0x006834d8
std::vector<W8MasterFunction> g_master_functions;
// GLOBAL: WIZ8 0x006834dc
bool g_remove_current_master_function;

/* SGP full-volume scale: CreateAndPlaySoundNode multiplies its clamped
   loudness fraction by this to get the node's base volume. */
// GLOBAL: WIZ8 0x005EC510
const float g_sound_node_full_volume = 127.0f;

/* Run every registered master function once with argument zero, dropping the
   ones that set the removal flag while it runs. */
// FUNCTION: WIZ8 0x004D8E40
void RunMasterFunctions(void)
{
    int count = static_cast<int>(g_master_functions.size());

    for (int index = 0; index < count; ++index) {
        g_master_functions[index](0);
        if (g_remove_current_master_function) {
            g_master_functions.erase(g_master_functions.begin() + index);
            --count;
            --index;
        }
    }
}

/* Run every registered master function once with argument -1, the persist
   command the level masters answer by writing their live state into the
   location variables. */
// FUNCTION: WIZ8 0x004D8EC0
void SaveMasterFunctions(void)
{
    int count = static_cast<int>(g_master_functions.size());

    for (int index = 0; index < count; ++index) {
        g_master_functions[index](-1);
    }
}

/* The level callbacks' scripted spawn: settle the requested point onto the
   ground when asked, create the group, apply hostility, refresh its centre
   cache and re-run outward sight for the new arrivals. */
// FUNCTION: WIZ8 0x004D8F00
W8MonsterGroup* SpawnMonsters(int monster_id, int count, srVector3T<float>* position, int hostility,
                              bool settle, bool a, bool b)
{
    W8MonsterGroup* group;
    srVector3T<float> position_copy;

    if (settle) {
        position->y = probeGround(position->glmValue()).position.y;
    }
    position_copy = *position;
    group = CreateGroup(monster_id, count, &position_copy, a, b, settle);
    if (group != 0) {
        SetMonsterGroupHostility(group, hostility, false);
        GetMonsterGroupCentre(group, 0);
        RefreshOutwardSightForAllMonsters();
    }
    return group;
}

/* The level callbacks' one-shot positional sound: reject a negative
   loudness fraction, clamp anything above one, then create the node at the
   requested spot, scale its base volume and falloff and start playback. */
// FUNCTION: WIZ8 0x004D8F80
stSound3D* CreateAndPlaySoundNode(const char* sound_name, srVector3T<float> position, float volume,
                                  float scale, bool play_flag)
{
    if (volume > g_float_one) {
        volume = 1.0f;
    } else if (volume < g_float_zero) {
        return 0;
    }

    stSound3D* sound = new stSound3D(sound_name, 0);
    if (sound != 0) {
        glm::dvec3 sound_position;
        sound_position = glm::dvec3(position.glmValue());
        sound->setLocation(sound_position);
        sound->volume = static_cast<int>(volume * g_sound_node_full_volume);
        sound->falloff = scale * g_world_scale;
        sound->Play(play_flag, true);
    }
    return sound;
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
    srVector3T<float> position;
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
    if (IsMipeActive()) {
        va_end(arguments);
        return handled;
    }
    if (info == 0) {
        position = cameraLocation();
    } else {
        position = info->p3D->GetPosition();
    }
    position.y = probeGround(position.glmValue()).position.y + g_float_one_hundred_twenty_five;
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
                result = g_world_cursor_node_handlers[GetWorldCursorNodeParameter(previous, 2)](
                    2, previous, context);
            }
            previous = g_active_cursor_node;
            if (node != 0 && previous != node) {
                result = g_world_cursor_node_handlers[GetWorldCursorNodeParameter(node, 2)](
                    1, node, context);
            }
            g_active_cursor_node = node;
            break;
        case 4:
            if (node != 0 && GetWorldCursorNodeParameter(node, 2) == 2) {
                result = g_world_cursor_node_handlers[2](4, node, context);
            }
            break;
        case 8:
            if (node != 0 && GetWorldCursorNodeParameter(node, 2) == 7) {
                result = g_world_cursor_node_handlers[7](8, node, context);
            }
            break;
        case 3:
            if (node != 0) {
                int type = GetWorldCursorNodeParameter(node, 2);
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
    char* shown;
    int slot;
    std::string path;
    std::string text;

    result = false;
    message_id = GetWorldCursorNodeParameter(node, 0);
    folder = GetLevelFolderName(GetLoadedLevelID());
    type = GetWorldCursorNodeParameter(node, 2);
    enabled = static_cast<unsigned char>(GetWorldCursorNodeParameter(node, 1));
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
        shown = static_cast<char*>(node->pUserdata);
        if (shown == 0) {
            SetWorldCursorNodeUserdataSize(node, 1);
            shown = static_cast<char*>(node->pUserdata);
        }
        if (*shown != 0) {
            return 0;
        }
        *shown = 1;
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
        message_id = GetWorldCursorNodeParameter(node, 0);
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
    message_id = GetWorldCursorNodeParameter(node, 0);
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
        ApplyItemEffectToRandomCharacter(g_character_event_kind0, -1, 0,
                                         g_character_event_no_flags);
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
            QueueCharacterEvent(character, g_character_event_kind1, 0, g_character_event_no_flags,
                                g_character_event_full_volume);
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
                    ApplyItemEffectToRandomCharacter(g_character_event_kind3, -1, 0,
                                                     g_character_event_no_flags);
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
        ApplyItemEffectToRandomCharacter(g_character_event_kind3, -1, 0,
                                         g_character_event_no_flags);
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

#define MASTER_FUNCTION_CPP "C:\\Projects\\Wizardry 8\\Level Specific Code\\MasterFunctionList.cpp"

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
    std::vector<W8WorldItem*> items;
    W8Prop* pChest;
    Trigger* pTrigger;
    W8WorldItem* pContainer;
    int index;

    pChest = FindPropByName(g_world, "AwayCampChest");
    if (pChest == 0) {
        srAssertFail("pChest", MASTER_FUNCTION_CPP, 0x5a4, 0);
    }
    pTrigger = pChest->GetTrigger();
    if (pTrigger == 0) {
        srAssertFail("pTrigger", MASTER_FUNCTION_CPP, 0x5a6, 0);
    }
    pContainer = pTrigger->GetOrCreateItemGroup(true);
    if (pContainer == 0) {
        srAssertFail("pContainer", MASTER_FUNCTION_CPP, 0x5a8, 0);
    }
    LoadSavedLevelItems(0x26, items);
    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        W8WorldItem* item = items[index];

        if (ItemInfoIsWorldPersistent(item)) {
            if (ItemInfoGetNumInGroup(item) != 0) {
                W8WorldItem* next = ItemInfoGroupGetNext(item);

                if (next != 0) {
                    ItemInfoMakeGroupList(next, items);
                }
            }
            free(item);
        } else {
            ItemInfoAddToGroup(pContainer, item);
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
    char* shown;
    int slot;

    event_type = GetWorldCursorNodeParameter(node, 0);
    if (command == 1) {
        shown = static_cast<char*>(node->pUserdata);
        if (shown == 0) {
            SetWorldCursorNodeUserdataSize(node, 1);
            shown = static_cast<char*>(node->pUserdata);
        }
        if (*shown == 0) {
            *shown = 1;
            slot = PickRandomPartySpeaker(event_type, 0xff);
            if (slot != -1) {
                QueueCharacterEvent(g_status.buffers.Char + slot, event_type, 0,
                                    g_character_event_no_flags, g_character_event_full_volume);
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

/* Activation callbacks that only let the trigger run or stop it. Retail's
   linker folded them into ScreenLifecycleSuccess and IgnoreSpellCastingInput,
   which compile to the same bytes. */
static bool AllowTriggerActivation(Trigger* trigger)
{
    return true;
}

static bool BlockTriggerActivation(Trigger* trigger)
{
    return false;
}

/* Repeated level setup owns the find/assert/bind operation. Diagnostics keep
   their original line and literal spelling, including the inconsistent names. */
static void BindLevelTrigger(const char* name, Trigger::ActivationCallback callback, int line,
                             const char* diagnostic)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger == 0) {
        srAssertFail("pTrigger", MASTER_FUNCTION_CPP, line, diagnostic);
    }
    trigger->activation_callback = callback;
}

static void BindLevelTrigger(const char* name, Trigger::ActivationCallback callback, int line)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger == 0) {
        srAssertFail("pTrigger", MASTER_FUNCTION_CPP, line,
                     fmt::format("Missing trigger '{}'! It's not in the LVL file!", name));
    }
    trigger->activation_callback = callback;
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
        BindLevelTrigger("ChaosMolori", ArnikaChaosMolori, 0x74);
        BindLevelTrigger("Maddmook", ArnikaMaddmook, 0x78);
        BindLevelTrigger("CMbox", ArnikaCMbox, 0x7c);
        BindLevelTrigger("AstralDominae", ArnikaAstralDominae, 0x80);
        BindLevelTrigger("BallSlot", ArnikaBallSlot, 0x84);
        BindLevelTrigger("Flightrecordertrigger", ArnikaFlightRecorder, 0x88);
        BindLevelTrigger("ULLspawn", AllowTriggerActivation, 0x8c);
        BindLevelTrigger("Mookholo", ArnikaMookholo, 0x90);
        BindLevelTrigger("MookFrontDoor", ArnikaMookFrontDoor, 0x9c);
        BindLevelTrigger("YellowButton", ArnikaYellowButton, 0xa0);
        BindLevelTrigger("Vaultalarmdoor", ArnikaVaultAlarmDoor, 0xa4);
        BindLevelTrigger("Exitbutton", ArnikaExitButton, 0xa8);
        BindLevelTrigger("GenVault-2-door", ArnikaGenVaultDoor, 0xac);
        BindLevelTrigger("ARN11", CosmicCircleReturnFalse, 0xb8);
        BindLevelTrigger("RedButton", ArnikaRedButton, 0xbd);
        BindLevelTrigger("El1-TopButtons", ArnikaEl1TopButtons, 0xc1);
        BindLevelTrigger("El1-BottomButtons", ArnikaEl1BottomButtons, 0xc5);
        BindLevelTrigger("GreenButton", ArnikaGreenButton, 0xca);
        BindLevelTrigger("Elevator-02", ArnikaElevator02Trigger, 0xce);
        BindLevelTrigger("LazerScanner", ArnikaLazerScanner, 0xd8);
        pTrigger = FindRequiredTriggerByName("ScannerDoor");
        pTrigger->m_lData1 = 0;
        pTrigger->activation_callback = ArnikaScannerDoor;
        ArnikaLevelSetup();
        return;
    case 1:
        BindLevelTrigger("RampUp", AscensionRampUp, 0x116);
        BindLevelTrigger("ChaosATrigger", AscensionChaosATrigger, 0x11a);
        BindLevelTrigger("ChaosBTrigger", AscensionChaosBTrigger, 0x11e);
        BindLevelTrigger("LifeATrigger", AscensionLifeATrigger, 0x122);
        BindLevelTrigger("LifeBTrigger", AscensionLifeBTrigger, 0x126);
        BindLevelTrigger("KnowATrigger", AscensionKnowATrigger, 0x12a);
        BindLevelTrigger("KnowBTrigger", AscensionKnowBTrigger, 0x12e);
        BindLevelTrigger("RampUp", AscensionDarkSavantSpawn, 0x132);
        BindLevelTrigger("Path1Camera", AscensionPath1Camera, 0x136);
        BindLevelTrigger("Shaker", AscensionShaker, 0x5ee);
        AscensionPeakInit();
        return;
    case 4:
        CosmicCircleSetup();
        BindLevelTrigger("CC_TRIGGERPLANE1HEDRA", CosmicCircleTriggerPlane1Hedra, 0x33a);
        break;
    case 5:
        MartensBluff1Setup();
        BindLevelTrigger("MR109", MartensBluff1Teleporter, 0x144);
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
        BindLevelTrigger("F-Handlock", MartensBluff1FHandlock, 0x154);
        BindLevelTrigger("ButtonGigas", MartensBluff1ButtonGigas, 0x158);
        BindLevelTrigger("ButtonTrang", MartensBluff1ButtonTrang, 0x15c);
        BindLevelTrigger("ButtonRift", MartensBluff1ButtonRift, 0x160);
        BindLevelTrigger("ButtonMaten", MartensBluff1ButtonMaten, 0x164);
        BindLevelTrigger("WireTrigger", MartensBluff1WireTrigger, 0x168);
        BindLevelTrigger("ButtonGigas", MartensBluff1ButtonGigas, 0x16c);
        BindLevelTrigger("Controller", MartensBluff1Controller, 0x170);
        BindLevelTrigger("Dial-A", MartensBluff1DialA, 0x174);
        BindLevelTrigger("Dial-B", MartensBluff1DialB, 0x178);
        BindLevelTrigger("Dial-C", MartensBluff1DialC, 0x17c);
        BindLevelTrigger("Gas-Switch", MartensBluff1GasSwitch, 0x180);
        BindLevelTrigger("J-Doorcontroller", MartensBluff1JDoorController, 0x184);
        BindLevelTrigger("MartenBook", MartensBluff1MartenBook, 0x188,
                         "Missing trigger 'trigger16254'! It's not in the LVL file!");
        return;
    case 6:
        MartensBluff2Setup();
        BindLevelTrigger("Arrowtraptrigger", TriggerArrowTrap, 0x192);
        BindLevelTrigger("Spikeballtrigger", MartensBluff2Spikeballtrigger, 0x196);
        BindLevelTrigger("DoorBolt", MartensBluff2DoorBolt, 0x19a);
        BindLevelTrigger("DummyLever", MartensBluff2DummyLever, 0x19e);
        BindLevelTrigger("Dummy", MartensBluff2Dummy, 0x1a2);
        BindLevelTrigger("PerfumeBox", MartensBluff2PerfumeBox, 0x1a6);
        BindLevelTrigger("StoneIdol", MartensBluff2StoneIdol, 0x1aa);
        BindLevelTrigger("BlueFlowers", MartensBluff2BlueFlowers, 0x1ae);
        BindLevelTrigger("SquisherControls", MartensBluff2SquisherControls, 0x1b2);
        BindLevelTrigger("DoorControls", MartensBluff2DoorControls, 0x1b6);
        return;
    case 8:
        ClearTextForBarTrigger();
        BindLevelTrigger("roach_trigger", OnRoachTriggerActivated, 0x1c0);
        BindLevelTrigger("spider_trigger", OnSpiderTriggerActivated, 0x1c4);
        BindLevelTrigger("Bartrigger", OnBarTriggerActivated, 0x1c8);
        BindLevelTrigger("Coffinlide", OnCoffinlideActivated, 0x1cc);
        BindLevelTrigger("Coffinlidg", OnCoffinlidgActivated, 0x1d0);
        BindLevelTrigger("wheel_star", OnWheelStarActivated, 0x5ee);
        return;
    case 9:
        BindLevelTrigger("bell_button", Monastery2BellButton, 0x1d8);
        BindLevelTrigger("micro_door2", Monastery2MicroDoor2, 0x1dc);
        return;
    case 0xc:
        MtGigas1Setup();
        BindLevelTrigger("_VOC_EWAXXLIFT1", MtGigas1Lift1, 0x219);
        BindLevelTrigger("_VOC_EWAXXLIFT2", MtGigas1Lift2, 0x21d);
        BindLevelTrigger("PRESSUREPLATE", MtGigas1PressurePlate, 0x221,
                         "Missing trigger '_VOC_EWAXXLIFT2'! It's not in the LVL file!");
        BindLevelTrigger("mudWallTrigger", MtGigas1MudWall, 0x225);
        return;
    case 0xd:
        MtGigas2Setup();
        BindLevelTrigger("_VOC_EWAXXTRAIN", MtGigas2Train, 0x22e);
        BindLevelTrigger("redwire", MtGigas2RedWire, 0x232);
        BindLevelTrigger("bluewire", MtGigas2BlueWire, 0x236);
        BindLevelTrigger("yellowwire", MtGigas2YellowWire, 0x23a);
        BindLevelTrigger("_VOC_EWAXXLIFT3", MtGigas2Lift3, 0x23e);
        BindLevelTrigger("_VOC_EWAXXTOPDOOR1", MtGigas2TopDoor1, 0x242);
        BindLevelTrigger("_VOC_EWAXXOFFICER1", MtGigas2Officer1, 0x246,
                         "Missing trigger '_VOC_EWAXXTOPDOOR1'! It's not in the LVL file!");
        BindLevelTrigger("_VOC_EWAXXOFFICER2", MtGigas2Officer2, 0x24a,
                         "Missing trigger '_VOC_EWAXXTOPDOOR1'! It's not in the LVL file!");
        BindLevelTrigger("triggerPlaneLaserAlarm", MtGigas2LaserAlarm, 0x24e);
        BindLevelTrigger("triggerPlaneLaserAlarm01", MtGigas2LaserAlarm, 0x252);
        BindLevelTrigger("accessHatch", MtGigas2AccessHatch, 0x256);
        BindLevelTrigger("wiringMalfunction", MtGigas2WiringMalfunction, 0x25a);
        return;
    case 0xe:
        ProcessFlagPosition();
        BindLevelTrigger("_VOC_EWAXXLIFT1", MtGigas1Lift1, 0x1ff);
        BindLevelTrigger("crank", OnCrankTriggerActivated, 0x203);
        BindLevelTrigger("Security Button", OnSecurityButtonActivated, 0x207);
        BindLevelTrigger("VOC_EWAXXSENTRYtrig", OnSentryTriggerActivated, 0x20b);
        BindLevelTrigger("ewaxxdoortrigger03", OnEwaxxDoor03Activated, 0x20f);
        BindLevelTrigger("dummytrigger", OnDummyTriggerActivated, 0x5ee);
        return;
    case 0xf:
        BindLevelTrigger("_VOC_EWAXXCANNON1", OnEwaxxCannon1Activated, 0x1e2);
        BindLevelTrigger("EwaxxLanding", OnEwaxxLandingActivated, 0x1ea);
        BindLevelTrigger("catchCord", OnCatchCordActivated, 0x1ee);
        BindLevelTrigger("painActivatorTrigger", OnPainActivatorActivated, 0x1f2);
        BindLevelTrigger("_VOC_EWAXXTOPDOOR2", OnEwaxxTopDoor2Activated, 0x1f6);
        return;
    case 0x10:
        if (GetFact(W8_FACT_PARTY_AT_RAC) == 0) {
            LoadAwayCampChest();
            SetFact(W8_FACT_PARTY_AT_RAC, 0, false);
        }
        BindLevelTrigger("prisondoor06", CampPrisonDoor06, 0x308);
        BindLevelTrigger("prisondoor04", CampPrisonDoor04, 0x30c);
        BindLevelTrigger("prisondoor03", CampPrisonDoor03, 0x310);
        return;
    case 0x12:
        BindLevelTrigger("AltarBox", RapaxMainFloorAltarBox, 0x29c);
        BindLevelTrigger("platformtrigger", RapaxMainFloorPlatform, 0x2a0);
        BindLevelTrigger("platformtrigger01", RapaxMainFloorPlatform01, 0x2a4);
        BindLevelTrigger("platformtrigger02", RapaxMainFloorPlatform02, 0x2a8);
        return;
    case 0x13:
        BindLevelTrigger("AirBox", RapaxUpperFloorAirBox, 0x290);
        BindLevelTrigger("DoorDone", RapaxUpperFloorDoorDone, 0x294);
        return;
    case 0x15:
        BindLevelTrigger("Fireantspawn", Rift1Fireantspawn, 0x27a);
        BindLevelTrigger("Sexspawn", Rift1Sexspawn, 0x27e);
        BindLevelTrigger("Hotstuff", Rift1Hotstuff, 0x282);
        BindLevelTrigger("Gate", Rift1Gate, 0x286);
        BindLevelTrigger("AshLock", Rift1AshLock, 0x5ee);
        BindLevelTrigger("TimeDorado", Rift1TimeDorado, 0x5ee);
        return;
    case 0x16:
        g_sea_caves_slope_override_enabled = true;
        g_party_has_slope_override_item = FindItemOnParty(0x254, 0, 0, 0, 0);
        BindLevelTrigger("HigardiChest01", SeaCavesHigardiChest01, 0x2b1);
        BindLevelTrigger("HigardiChest02", SeaCavesHigardiChest02, 0x2b5);
        BindLevelTrigger("HigardiChest03", SeaCavesHigardiChest03, 0x2b9);
        BindLevelTrigger("HigardiChest04", SeaCavesHigardiChest04, 0x2bd);
        BindLevelTrigger("HigardiChest05", SeaCavesHigardiChest05, 0x2c1);
        BindLevelTrigger("doortomb", SeaCavesDoorTomb, 0x5ee);
        return;
    case 0x18:
        BindLevelTrigger("gas_trig_plane01", SwampGasPlane, 0x2ca);
        BindLevelTrigger("gas_trig_plane02", SwampGasPlane, 0x2ce);
        BindLevelTrigger("gas_trig_plane03", SwampGasPlane, 0x2d2);
        BindLevelTrigger("gas_trig_plane04", SwampGasPlane, 0x2d6);
        BindLevelTrigger("gas_trig_plane05", SwampGasPlane, 0x2da);
        BindLevelTrigger("gas_trig_plane06", SwampGasPlane, 0x2de);
        BindLevelTrigger("gas_trig_plane07", SwampGasPlane, 0x2e2);
        BindLevelTrigger("oil_pool", SwampOilPool, 0x2e6);
        BindLevelTrigger("onelid", SwampOnelid, 0x2ea);
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
        BindLevelTrigger("Fount_randomFX", Trynnie1FountRandomFX, 0x2fa);
        EnsureTrynnie1KilledVar();
        return;
    case 0x1a:
        BindLevelTrigger("GoodaVine_A", Trynnie2GoodaVineA, 0x31a);
        BindLevelTrigger("GoodaVine_B", Trynnie2GoodaVineB, 0x31e);
        BindLevelTrigger("Meat_Maker", Trynnie2MeatMaker, 0x322,
                         "Missing trigger 'Meat maker'! It's not in the LVL file!");
        BindLevelTrigger("Meat_Box", Trynnie2MeatBox, 0x326,
                         "Missing trigger 'Meat_Box! It's not in the LVL file!");
        BindLevelTrigger("Give_Zulu", Trynnie2GiveZulu, 0x32a,
                         "Missing trigger 'Give Zulu'! It's not in the LVL file!");
        BindLevelTrigger("URN_Trigger_01", Trynnie2UrnTrigger, 0x5ee);
        BindLevelTrigger("URN_Trigger_02", Trynnie2UrnTrigger, 0x5ee);
        BindLevelTrigger("URN_Trigger_03", Trynnie2UrnTrigger, 0x5ee);
        BindLevelTrigger("URN_Trigger_04", Trynnie2UrnTrigger, 0x5ee);
        EnsureTrynnie2KilledVar();
        return;
    case 0x1b:
        BindLevelTrigger("Liche", ConnectiveTissueLiche, 0xe7);
        return;
    case 0x24:
        SavantTowerSyncButtonBlocker();
        BindLevelTrigger("Triangle", SavantTowerShape, 0xf2);
        BindLevelTrigger("Circle", SavantTowerShape, 0xf6);
        BindLevelTrigger("Square", SavantTowerShape, 0xfa);
        BindLevelTrigger("Star", SavantTowerShape, 0xfe);
        BindLevelTrigger("ButtonBlocker", SavantTowerButtonBlocker, 0x102);
        return;
    }
    return;
}
