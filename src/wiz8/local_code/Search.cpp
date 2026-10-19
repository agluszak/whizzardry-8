#include "wiz8/local_code/Search.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "compat/native.h"
#include "conditions.h"
#include "gameplay_modifiers.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/item_instance.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/notices.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/picking.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

constexpr int kSearchFoundItemEvent = 0x17;
constexpr int kSearchFoundItemEventAlt = 0x18;

/* Borrowed candidate for one search pulse, derived from hidden items and unsearched triggers. */
struct W8Searchable {

    /* Resolve this searchable's world position. */
    void GetPosition(glm::vec3* position);
    // Apply discovery to the item or trigger itself.
    void Reveal();
    /* The best searching party member within range of this
       searchable, or -1. Practices the searching skill on a qualified pick. */
    int PickBestSearcher();

    W8WorldItem* world_item = nullptr;
    Trigger* trigger = nullptr;
};

static uint64_t g_search_pulse_clock;

/* 10000.0: the unit search radius PickBestSearcher scales by score, and the
   collector's outer range gate. */
constexpr float g_search_radius = 10000.0f;
constexpr float g_search_cone_angle = 3.1415925f;

/* The view's own constructor: seeds the cursor ahead of the first element and
   builds the item vector with capacity five. */
/* Discovery changes the same state used to select the next pulse's candidates. */
void W8Searchable::Reveal()
{
    if (world_item ? !ItemHasFlags(world_item, W8_WORLD_ITEM_HIDDEN) : trigger == nullptr) return;
    if (world_item) {
        SetItemFlags(world_item, W8_WORLD_ITEM_HIDDEN, false);
        SetItemFlags(world_item, W8_WORLD_ITEM_RADAR_BLIP_LIT, true);
        SetItemAndEntityFlags(world_item, W8_ITEM_ENTITY_HIGHLIGHT_BLUE, false);
    } else {
        if ((trigger->flags & W8_TRIGGER_SEARCHED) != 0) return;
        trigger->Run(-1);
        trigger->flags |= W8_TRIGGER_SEARCHED;
    }
}

/* Gather eligible candidates inside the radius and sight cone for one pulse. */
static std::vector<W8Searchable> CollectSearchablesInView(World& world)
{
    std::vector<W8Searchable> found;
    const glm::vec3 camera = cameraLocation();
    const auto collect = [&](W8Searchable candidate) {
        glm::vec3 position;
        candidate.GetPosition(&position);
        if (glm::length(camera - position) >= g_search_radius) return;
        if (candidate.world_item) {
            const float half_cone = g_search_cone_angle * 0.5f;
            auto from = camera;
            const float heading = GetHeadingAngle(&from, &position);
            const float yaw = GetCameraYawRadians();
            if ((half_cone <= NormalizeAngle(yaw - heading) &&
                 half_cone <= NormalizeAngle(heading - yaw)) ||
                !world.compiledIndex() ||
                !world.compiledIndex()->HasLineOfSight(&camera, &position, true))
                return;
        }
        found.push_back(candidate);
    };
    // Trigger definitions precede item records in level loading. Keep that search order.
    for (const auto& trigger : world.triggers)
        if (trigger->trigger_kind == W8TriggerKind::item && trigger->searchable == 1 &&
            (trigger->flags & W8_TRIGGER_SEARCHED) == 0)
            collect({nullptr, trigger.get()});
    for (const auto& group : CurrentLevelPopulation().world_items) {
        if (ItemHasFlags(group.get(), W8_WORLD_ITEM_HIDDEN)) collect({group.get(), nullptr});
        for (const auto& item : group->contents)
            if (ItemHasFlags(item.get(), W8_WORLD_ITEM_HIDDEN)) collect({item.get(), nullptr});
    }
    return found;
}

/* Resolve this searchable's world position: the item's rep midpoint when it
   has a live owner, the recorded position otherwise; prop searchables
   resolve through their trigger's prop. */
void W8Searchable::GetPosition(glm::vec3* position)
{
    (*position) = glm::vec3(0);
    if (world_item != 0) {
        if (world_item->p3D != 0) {
            *position = world_item->p3D->GetSearchPosition();
            return;
        }
        (*position) =
            glm::vec3(world_item->position.x, world_item->position.y + g_octree_cell_scale,
                      world_item->position.z);
        return;
    }
    if (trigger != 0) {
        Prop* prop = trigger->GetProp();
        if (prop != 0) {
            if (const auto center = prop->GetCenterPosition()) *position = *center;
        }
    }
}

/* The 500ms search sweep: while nothing is interacting or surprising the
   party, walk the in-view searchables, pick the best searcher for each, queue
   the found event and turn the camera toward the find. */
void RunSearchPulse(void)
{
    if (TimeUntilDeadline(g_search_pulse_clock) == 0) {
        g_search_pulse_clock = w8_get_ticks() + (500);
        if (g_environment_time_enabled != 0 && !gXStatus.world_update_blocked &&
            !gXStatus.fSurprisePossible && !IsLockInteractionActive() &&
            !LockInteractionSuspended() && !IsTrapInteractionActive() &&
            !TrapInteractionSuspended()) {
            if (IsLevelFastMovement() && g_status.search_mode) {
                ShowNotice(W8_FONT_PALETTE_BEIGE,
                           g_localized_strings[W8_NOTICE_SEARCH_SPECIAL_LEVEL]);
            }
            auto candidates = CollectSearchablesInView(*GetWorld());
            bool found = false;
            {
                for (auto& candidate : candidates) {
                    auto* searchable = &candidate;
                    int slot = searchable->PickBestSearcher();
                    if (slot != -1) {
                        W8Character* character = &g_status.party.characters[slot];
                        if (searchable->world_item == 0) {
                            if (searchable->trigger == 0) {
                                QueueCharacterEvent(character, 0x1a, 0, kCharacterEventFullVolume);
                                found = true;
                            } else {
                                QueueCharacterEvent(character, 0x1a, 0, kCharacterEventFullVolume);
                                int message = searchable->trigger->m_lData1;
                                found = true;
                                if (message >= 0) {
                                    const std::string_view level_folder =
                                        GetLevelFolderName(GetLoadedLevelID());
                                    const std::string folder =
                                        level_folder.empty() ? "Test" : std::string(level_folder);
                                    const std::string path =
                                        fmt::format("Data\\Messages\\{}.msg", folder);
                                    std::string text;
                                    if ((!(text = GetStringFromStringDatabase(path, message, 0, 0))
                                              .empty()) != 0) {
                                        ShowString(text);
                                    }
                                }
                            }
                        } else {
                            PartyAttemptsToIdentifyItem(&searchable->world_item->item, 0);
                            unsigned int event_type = kSearchFoundItemEvent;
                            if (Random(2) != 0) {
                                event_type = kSearchFoundItemEventAlt;
                            }
                            auto event = std::make_unique<W8CharacterEvent>(
                                character, event_type, 0, kCharacterEventFullVolume);
                            if (searchable->world_item != 0) {
                                event->item = searchable->world_item->item;
                            }
                            gXStatus.character_event_queue->QueueEntry(std::move(event));
                        }
                        glm::vec3 position;
                        searchable->GetPosition(&position);
                        ResetInactiveLevelDataVectors();
                        PointCameraAtTarget(&position, true, true);
                        searchable->Reveal();
                    }
                }
            }
            if (!g_status.search_mode || IsLevelFastMovement()) {
                /* Retail scans the party for a live member carrying the
                   Scouting skill and then discards the result. */
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    W8Character* character = &g_status.party.characters[slot];
                    if (g_status.party.rows[slot].fOccupied && character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_TURNCOAT &&
                        character->skills[W8_SKILL_SCOUTING].level != 0) {
                        break;
                    }
                }
            }
            if (g_status.search_mode && !IsLevelFastMovement() && !found && Random(100) == 0) {
                ApplyItemEffectToRandomCharacter(g_container_event, -1, 0);
            }
        }
    }
}

/* Pick the best searching party member for this searchable: eligible members
   score their Scouting skill (halved, quartered in the special level state)
   plus a Senses-derived bonus; the detect-secrets party effect instead
   force-picks random eligible members without practicing the skill. The pick
   must also reach this searchable's position. */
int W8Searchable::PickBestSearcher()
{
    unsigned int best_score = 0;
    int best_slot = -1;
    bool earned = false;
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        W8Character* character = &g_status.party.characters[slot];
        if (!g_status.party.rows[slot].fOccupied || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_TURNCOAT) {
            continue;
        }
        if (CharacterHasTrait(character, W8_TRAIT_SEARCH) || g_status.search_mode) {
            unsigned int level = character->skills[W8_SKILL_SCOUTING].level;
            unsigned int base = level >> 1;
            if (IsLevelFastMovement()) {
                base = level >> 2;
            }
            unsigned int score = 0;
            if (base != 0 || (g_status.search_mode && !IsLevelFastMovement())) {
                unsigned int attribute = character->attributes[W8_ATTRIBUTE_SENSES].effective;
                if (attribute < 0x33) {
                    score = base + attribute / 5;
                } else if (attribute < 0x51) {
                    score = base + 10 + (attribute - 0x32) / 3;
                } else if (attribute < 0x5b) {
                    score = base - 0x3c + attribute;
                } else if (attribute < 100) {
                    score = base - 0x96 + attribute * 2;
                } else if (attribute < 0x65) {
                    score = base + 0x32;
                } else {
                    score = base + 0x32 + (attribute * 10 - 1000) / 0x19;
                }
            }
            if (score > 99) {
                score = 100;
            }
            if (best_score < score) {
                earned = true;
                best_score = score;
                best_slot = slot;
            }
        }
        if (g_status.party.modifiers.detect_secrets != 0) {
            if (best_slot != -1 && Random(2) == 0) {
                continue;
            }
            best_score = 100;
            earned = false;
            best_slot = slot;
        }
    }
    float range = best_score * g_search_radius * kMovementSpeedStep;
    glm::vec3 camera = cameraLocation();
    glm::vec3 position;
    GetPosition(&position);
    glm::vec3 delta = camera - position;
    if (glm::length(delta) < range) {
        if (earned) {
            PracticeCharacterSkill(&g_status.party.characters[best_slot], W8_SKILL_SCOUTING, 5,
                                   false);
        }
        return best_slot;
    }
    return -1;
}

/* Toggle the party's search mode: leaving it posts the off notice, entering it
   posts the on notice and arms the 500ms pulse that sweeps the searchable
   registry; combat blocks the toggle outright. */
void ToggleSearchMode(void)
{
    if (g_status.search_mode) {
        g_status.search_mode = false;
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[W8_NOTICE_SEARCH_MODE_OFF]);
        return;
    }
    if (!g_combat.active()) {
        g_status.search_mode = true;
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[W8_NOTICE_SEARCH_MODE_ON]);
        g_search_pulse_clock = w8_get_ticks() + (0x1f4);
        ClearActiveWorldCursorNode();
    } else {
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[W8_NOTICE_SEARCH_BLOCKED_COMBAT]);
    }
}
