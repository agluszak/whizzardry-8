#include "wiz8/level_specific_code/MasterFunctionList.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/sound3d.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
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
#include "wiz8/level_specific_code/Trynnie1.h"
#include "wiz8/level_specific_code/Trynnie2.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/location_variables.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/story.h"
#include "wiz8/string_database.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

namespace {

constexpr float kSoundNodeFullVolume = 127.0f;

/* The character events the world-cursor nodes queue: a random member's reaction
   to the node's effect, the relic reaction, and the Sedexus pick's male event. */
constexpr int kCursorNodeEffectEvent = 0x1b;
constexpr int kSedexusMaleEvent = 0x2d;
constexpr int kCursorNodeRelicEvent = 0x40;

std::string LevelMessagePath(int level_id)
{
    const std::string_view folder = GetLevelFolderName(level_id);
    return fmt::format("Data\\Messages\\{}.msg", folder.empty() ? "Test" : folder);
}

} // namespace

LevelScript::LevelScript(const ScriptContext& context) : ctx(context) {}

LevelScript::~LevelScript() = default;

void LevelScript::addMaster(Update update, Capture capture)
{
    masters_.push_back({std::move(update), std::move(capture)});
}

void LevelScript::bindTrigger(std::string_view trigger, Callback callback)
{
    FindRequiredTriggerByName(trigger)->activation_callback = std::move(callback);
}

// Capture the batch size: callbacks may append work for the next frame.
void LevelScript::runMasters()
{
    int count = static_cast<int>(masters_.size());

    for (int index = 0; index < count; ++index) {
        // A copy: the update may arm masters, which moves the stored ones.
        const auto update = masters_[index].update;
        if (update(0)) {
            masters_.erase(masters_.begin() + index);
            --count;
            --index;
        }
    }
}

W8SavedLocationState LevelScript::captureState() const
{
    W8SavedLocationState state;
    for (const auto& master : masters_)
        if (master.capture) master.capture(state);
    return state;
}

/* The level callbacks' scripted spawn: settle the requested point onto the
   ground when asked, create the group, apply hostility, refresh its centre
   cache and re-run outward sight for the new arrivals. */
W8MonsterGroup* SpawnMonsters(int monster_id, int count, glm::vec3* position, int hostility,
                              bool settle, bool use_alternate_name)
{
    W8MonsterGroup* group;
    glm::vec3 position_copy;

    if (settle) {
        position->y = probeGround((*position)).position.y;
    }
    position_copy = *position;
    group = CreateGroup(monster_id, count, &position_copy, use_alternate_name, settle);
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
Sound3D* CreateAndPlaySoundNode(World& world, const char* sound_name, glm::vec3 position,
                                float volume, float scale, bool play_flag)
{
    if (volume > 1.0f) {
        volume = 1.0f;
    } else if (volume < 0.0f) {
        return 0;
    }

    auto sound = std::make_unique<Sound3D>(sound_name);
    sound->position = position;
    sound->volume = static_cast<int>(volume * kSoundNodeFullVolume);
    sound->falloff = scale * kWorldScale;
    sound->Play(play_flag);
    auto* result = sound.get();
    world.audio.script_sounds.push_back(std::move(sound));
    return result;
}

bool Game::partyCastingBlocked()
{
    if (!level.primary) return false;
    const auto messages = level.primary->castingBlockMessagesAt(level.primary->party.eye());
    if (messages.empty()) return false;
    const auto path = LevelMessagePath(level.primary->level_id);
    for (int id : messages) {
        auto text = GetStringFromStringDatabase(path, id, nullptr, nullptr);
        if (!text.empty()) events.push(CastingBlockedNotice{std::move(text)});
    }
    return true;
}

bool DispatchWorldCursorNodeCommand(int command)
{
    World* world = GetWorld();
    if (world == nullptr || !world->script) return false;
    return world->script->dispatchCursorNodeCommand(command);
}

/* Command zero is the movement sweep: the tracked node's handler gets 2 (leave)
   and each newly entered node's handler gets 1 (enter); commands 3 and 8
   deliver themselves to nodes of type 3 and 7 respectively. */
bool LevelScript::dispatchCursorNodeCommand(int command)
{
    glm::vec3 position;
    W8WorldCursorNode* node;
    bool handled = false;
    bool result = false;

    position = cameraLocation();
    position.y = probeGround(position).position.y + 125.0f;
    node = FindWorldCursorNodeAtPoint(0, &position);
    if (node == 0) {
        return handled;
    }
    do {
        switch (command) {
        case 0:
            if (active_cursor_node_ != 0 && active_cursor_node_ != node) {
                result = runCursorNodeHandler(2, active_cursor_node_);
            }
            if (active_cursor_node_ != node) {
                result = runCursorNodeHandler(1, node);
            }
            active_cursor_node_ = node;
            break;
        case 8:
            if (node->parameters[2] == 7) {
                result = runCursorNodeHandler(8, node);
            }
            break;
        case 3:
            if (node->parameters[2] == 3) {
                result = runCursorNodeHandler(3, node);
            }
            break;
        default:
            result = false;
            break;
        }
        if (result) {
            handled = true;
        }
        node = FindWorldCursorNodeAtPoint(node, &position);
    } while (node != 0);
    return handled;
}

/* The cursor-node handlers, by the node's type parameter (numbers[2]). */
bool LevelScript::runCursorNodeHandler(int command, W8WorldCursorNode* node)
{
    switch (node->parameters[2]) {
    case 0:
    case 1:
        return showMessageOnce(command, node);
    case 3:
        return showMessage(command, node);
    case 4:
        // Entering applies the node's item effect to a random party member.
        if (command == 1) {
            ctx.game.speech.QueueRandom(kCursorNodeEffectEvent, -1, 0);
        }
        return true;
    case 5:
        return seenBodies(command);
    case 6:
        if (command == 1) {
            ctx.game.speech.QueueRandom(kCursorNodeRelicEvent, -1, 0);
        }
        return true;
    case 7:
        return command == 8;
    case 8:
        return partyVoice(command, node);
    case 9:
        return maleCharacterEvent(command);
    default:
        return false;
    }
}

/* Type-0/1 nodes: on the enter command, show the node's message-database
   string once - the first byte of the node's userdata is the shown flag.
   Parameter 1 additionally gates the message on a living party member with
   trait 0x0c while the party is neither searching nor under effect 0x11. */
bool LevelScript::showMessageOnce(int command, W8WorldCursorNode* node)
{
    const int message_id = node->parameters[0];
    const int type = node->parameters[2];
    const auto enabled = static_cast<unsigned char>(node->parameters[1]);
    if (enabled != 0 && !ctx.game.search.active() && ctx.party.modifiers.detect_secrets == 0) {
        bool searcher = false;
        for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
            if (ctx.party.members[slot].has_value() &&
                CharacterHasTrait(ctx.party.characters + slot, W8_TRAIT_SEARCH)) {
                searcher = true;
                break;
            }
        }
        if (!searcher) {
            return false;
        }
    }
    if (command != 1) {
        return false;
    }
    if (type != 0) {
        return false;
    }
    if (node->userdata.empty()) node->userdata.resize(1);
    std::byte* shown = node->userdata.data();
    if (*shown != std::byte{}) {
        return false;
    }
    *shown = std::byte{1};
    const std::string text =
        GetStringFromStringDatabase(LevelMessagePath(world().level_id), message_id, 0, 0);
    if (!text.empty()) {
        ctx.game.queueWorldMessage(text);
    }
    return true;
}

/* Type-3 nodes: command 3 shows the node's message unconditionally. */
bool LevelScript::showMessage(int command, W8WorldCursorNode* node)
{
    if (command != 3) {
        return false;
    }
    const std::string text =
        GetStringFromStringDatabase(LevelMessagePath(world().level_id), node->parameters[0], 0, 0);
    if (!text.empty()) {
        ctx.game.queueWorldMessage(text);
    }
    return true;
}

/* Type-9 nodes: entering while the fact-0x14c gate holds queues the male
   event for the indexed party member when that member is male. */
bool LevelScript::maleCharacterEvent(int command)
{
    if (command == 1 && ctx.status.rpc_active) {
        W8Character* character = ctx.party.characters + ctx.status.sedexus_party_slot;
        if (character->gender == W8_GENDER_MALE) {
            QueueCharacterEvent(character, kSedexusMaleEvent, 0, kCharacterEventFullVolume);
        }
    }
    return true;
}

/* Type-5 nodes: the Ascension Peak "seen bodies" check. Once all three relic
   items are on the party and neither blocking fact holds, queue the reaction
   and latch the AP_SeenBodies location variable. Retail runs the fact-0x133
   clear twice. */
bool LevelScript::seenBodies(int command)
{
    if (command == 1) {
        if (GetLocationVarIDByName("AP_SeenBodies") == -1) {
            if (CountAscensionPeakItems(ctx.inventory) == 3) {
                if (ctx.game.testFact(W8_FACT_TMISSION_MEET_ZANT_AT_AP) != 0 ||
                    ctx.game.testFact(W8_FACT_UMISSION_MEET_AP_ASSIGN) != 0) {
                    ctx.game.speech.QueueRandom(kCursorNodeRelicEvent, -1, 0);
                    CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "AP_SeenBodies",
                                                               1);
                    if (ctx.game.testFact(W8_FACT_QUEST_PEACE_GOTO_AP) != 0) {
                        ctx.game.setFact(W8_FACT_QUEST_PEACE_GOTO_AP, 0);
                    }
                    if (ctx.game.testFact(W8_FACT_QUEST_PEACE_GOTO_AP) != 0) {
                        ctx.game.setFact(W8_FACT_QUEST_PEACE_GOTO_AP, 0);
                    }
                }
            }
        }
    }
    return true;
}

/* Type-8 nodes: entering queues a random party member's voice line for the
   node's event type, once - the first byte of the node's userdata is the
   shown flag. */
bool LevelScript::partyVoice(int command, W8WorldCursorNode* node)
{
    const int event_type = node->parameters[0];

    if (command == 1) {
        if (node->userdata.empty()) node->userdata.resize(1);
        std::byte* shown = node->userdata.data();
        if (*shown == std::byte{}) {
            *shown = std::byte{1};
            const int slot = PickRandomPartySpeaker(event_type, 0xff);
            if (slot != -1) {
                QueueCharacterEvent(ctx.party.characters + slot, event_type, 0,
                                    kCharacterEventFullVolume);
            }
        }
    }
    return true;
}

/* Master-function values 0x10 and 0x26 select the same path once either of
   the two enabling facts has been set. */

/* Format the current level's message-database path, fetch the indexed string
   and queue it when present. */
void Game::queueLevelMessage(int message_id)
{
    const std::string text = GetStringFromStringDatabase(
        LevelMessagePath(level.primary ? level.primary->level_id : -1), message_id, 0, 0);
    if (!text.empty()) queueWorldMessage(text);
}

std::unique_ptr<LevelScript> MakeLevelScript(int level, const ScriptContext& context)
{
    switch (level) {
    case 0:
        return MakeArnikaScript(context);
    case 1:
        return MakeAscensionScript(context);
    case 4:
        return MakeCosmicCircleScript(context);
    case 5:
        return MakeMartensBluff1Script(context);
    case 6:
        return MakeMartensBluff2Script(context);
    case 8:
        return MakeMonastery1Script(context);
    case 9:
        return MakeMonastery2Script(context);
    case 0xc:
        return MakeMtGigas1Script(context);
    case 0xd:
        return MakeMtGigas2Script(context);
    case 0xe:
        return MakeMtGigasOuterScript(context);
    case 0xf:
        return MakeMtGigasTopScript(context);
    case 0x10:
        return MakeCampScript(context);
    case 0x12:
        return MakeRapaxMainFloorScript(context);
    case 0x13:
        return MakeRapaxUpperFloorScript(context);
    case 0x15:
        return MakeRift1Script(context);
    case 0x16:
        return MakeSeaCavesScript(context);
    case 0x18:
        return MakeSwampScript(context);
    case 0x19:
        return MakeTrynnie1Script(context);
    case 0x1a:
        return MakeTrynnie2Script(context);
    case 0x1b:
        return MakeConnectiveTissueScript(context);
    case 0x24:
        return MakeSavantTowerScript(context);
    default:
        return std::make_unique<LevelScript>(context);
    }
}
