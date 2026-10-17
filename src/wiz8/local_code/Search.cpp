#include "wiz8/local_code/Search.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "compat/native.h"
#include "conditions.h"
#include "gameplay_modifiers.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/item_instance.h"
#include "random.h"
#include "timer.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/notices.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

constexpr int kSearchFoundItemEvent = 0x17;
constexpr int kSearchFoundItemEventAlt = 0x18;

/* Local Code\search.cpp. The assertion in RegisterSearchableTrigger at source
   line 337 establishes the original translation unit. */

// GLOBAL: WIZ8 0x00689fa8
std::vector<std::unique_ptr<W8Searchable>> g_searchables;
// GLOBAL: WIZ8 0x00689fb8
W8SearchableView g_search_view;
// GLOBAL: WIZ8 0x00689fcc
uint64_t g_search_pulse_clock;

/* 10000.0: the unit search radius PickBestSearcher scales by score, and the
   collector's outer range gate. */
// GLOBAL: WIZ8 0x0061a364
float g_search_radius = 10000.0f;
// GLOBAL: WIZ8 0x0061a368
float g_search_cone_angle = 3.1415925f;

/* The view's own constructor: seeds the cursor ahead of the first element and
   builds the item vector with capacity five. */
// FUNCTION: WIZ8 0x00517900
W8SearchableView::W8SearchableView() : cursor(-1), items() {}

// Remove borrowed entries before their registry owner is destroyed. Keeping the
// cursor before the next entry lets an active search pulse continue after removal.
static void InvalidateSearchView(W8Searchable* searchable)
{
    auto& items = g_search_view.items;
    const auto entry = std::find(items.begin(), items.end(), searchable);
    if (entry == items.end()) return;
    if (entry - items.begin() <= g_search_view.cursor) --g_search_view.cursor;
    items.erase(entry);
}

/* Searchable was detected: items shed their hidden flag and become findable,
   triggers run; either way the record leaves the registry. */
// FUNCTION: WIZ8 0x00516ad0
void W8Searchable::Reveal()
{
    if (world_item ? !ItemHasFlags(world_item, W8_WORLD_ITEM_HIDDEN) : trigger == nullptr) return;
    const auto entry = std::find_if(g_searchables.begin(), g_searchables.end(),
                                    [this](const auto& owner) { return owner.get() == this; });
    if (entry == g_searchables.end()) return;
    // Retain this record across trigger callbacks, which may change the registry.
    auto owner = std::move(*entry);
    InvalidateSearchView(this);
    g_searchables.erase(entry);
    if (world_item) {
        SetItemFlags(world_item, W8_WORLD_ITEM_HIDDEN, false);
        SetItemFlags(world_item, W8_WORLD_ITEM_RADAR_BLIP_LIT, true);
        SetItemAndEntityFlags(world_item, W8_ITEM_ENTITY_HIGHLIGHT_BLUE, false);
    } else {
        trigger->Run(-1);
        trigger->flags |= W8_TRIGGER_SEARCHED;
    }
}

/* Refill the view with the registered searchables inside the search radius
   that either face the party or have a clear line of sight. Returns null when
   nothing qualified. */
// FUNCTION: WIZ8 0x00516ba0
W8SearchableView* CollectSearchablesInView(void)
{
    int total = static_cast<int>(g_searchables.size());
    glm::vec3 camera = cameraLocation();
    g_search_view.items.clear();
    g_search_view.cursor = -1;
    for (int index = 0; index < total; ++index) {
        W8Searchable* searchable = g_searchables[index].get();
        glm::vec3 position;
        searchable->GetPosition(&position);
        glm::vec3 delta = camera - position;
        if (glm::length(delta) < g_search_radius) {
            if (searchable->world_item == 0 && searchable->trigger != 0) {
                g_search_view.items.push_back(searchable);
            } else {
                float half_cone = g_search_cone_angle * 0.5f;
                glm::vec3 from = camera;
                glm::vec3 to = position;
                float yaw = GetCameraYawRadians();
                float heading = GetHeadingAngle(&from, &to);
                float ahead = NormalizeAngle(yaw - heading);
                float behind = NormalizeAngle(heading - yaw);
                if ((half_cone <= ahead && half_cone <= behind) || g_octree == 0 ||
                    !g_octree->HasLineOfSight(&camera, &position, true)) {
                    continue;
                }
                g_search_view.items.push_back(searchable);
            }
        }
    }
    return !g_search_view.items.empty() ? &g_search_view : 0;
}

/* Register one searchable world item. The item pointer lands in the record's
   first slot, which is what distinguishes the item entries from the trigger
   entries registered by the sibling below. */
// FUNCTION: WIZ8 0x00516e20
void RegisterSearchableWorldItem(W8WorldItem* item)
{
    auto searchable = std::make_unique<W8Searchable>();
    searchable->world_item = item;
    g_searchables.push_back(std::move(searchable));
}

// FUNCTION: WIZ8 0x00516f00
void RegisterSearchableTrigger(Trigger* trigger)
{
    auto searchable = std::make_unique<W8Searchable>();
    searchable->trigger = trigger;
    g_searchables.push_back(std::move(searchable));
}

// FUNCTION: WIZ8 0x00516fe0
void UnregisterSearchableTrigger(Trigger* trigger)
{
    for (int index = 0; index < static_cast<int>(g_searchables.size()); ++index) {
        W8Searchable* searchable = g_searchables[index].get();
        if (searchable->trigger == trigger) {
            InvalidateSearchView(searchable);
            g_searchables.erase(g_searchables.begin() + index);
            return;
        }
    }
}

/* Resolve this searchable's world position: the item's rep midpoint when it
   has a live owner, the recorded position otherwise; prop searchables
   resolve through their trigger's prop. */
// FUNCTION: WIZ8 0x00517080
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
        W8Prop* prop = trigger->GetProp();
        if (prop != 0) {
            if (const auto center = prop->GetCenterPosition()) *position = *center;
        }
    }
}

// FUNCTION: WIZ8 0x005171b0
void ClearSearchables()
{
    g_search_view.items.clear();
    g_searchables.clear();
    g_search_view.cursor = -1;
}

/* The 500ms search sweep: while nothing is interacting or surprising the
   party, walk the in-view searchables, pick the best searcher for each, queue
   the found event and turn the camera toward the find. */
// FUNCTION: WIZ8 0x005171c0
void RunSearchPulse(void)
{
    if (TimeUntilDeadline(g_search_pulse_clock) == 0) {
        g_search_pulse_clock = w8_get_ticks() + (500);
        if (g_environment_time_enabled != 0 && !gXStatus.world_update_blocked &&
            !gXStatus.fSurprisePossible && !gXStatus.fLockInteractMode &&
            !LockInteractionSuspended() && !gXStatus.fTrapInteractMode &&
            !TrapInteractionSuspended()) {
            if ((g_level_data->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) != 0 &&
                g_status.search_mode != 0) {
                ShowNotice(W8_FONT_PALETTE_BEIGE,
                           g_localized_strings[W8_NOTICE_SEARCH_SPECIAL_LEVEL]);
            }
            W8SearchableView* view = CollectSearchablesInView();
            bool found = false;
            if (view != 0) {
                for (;;) {
                    ++view->cursor;
                    if (static_cast<int>(view->items.size()) <= view->cursor) {
                        --view->cursor;
                        break;
                    }
                    W8Searchable* searchable = view->items[view->cursor];
                    if (searchable == 0) {
                        break;
                    }
                    int slot = searchable->PickBestSearcher();
                    if (slot != -1) {
                        W8Character* character = &g_status.buffers.Char[slot];
                        if (searchable->world_item == 0) {
                            if (searchable->trigger == 0) {
                                QueueCharacterEvent(character, 0x1a, 0, 1,
                                                    kCharacterEventFullVolume);
                                found = true;
                            } else {
                                QueueCharacterEvent(character, 0x1a, 0, 1,
                                                    kCharacterEventFullVolume);
                                int message = searchable->trigger->m_lData1;
                                found = true;
                                if (message >= 0) {
                                    const char* folder = GetLevelFolderName(g_status.current_level);
                                    if (folder == 0) {
                                        folder = "Test";
                                    }
                                    std::string path;
                                    path = fmt::format("Data\\Messages\\{}.msg", folder);
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
                                character, event_type, 0, 1, kCharacterEventFullVolume);
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
            if (g_status.search_mode == 0 ||
                (g_level_data->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) != 0) {
                /* Retail scans the party for a live member carrying the
                   Scouting skill and then discards the result. */
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    W8Character* character = &g_status.buffers.Char[slot];
                    if (g_status.buffers.XChar[slot].fOccupied && character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_TURNCOAT &&
                        character->skills[W8_SKILL_SCOUTING].level != 0) {
                        break;
                    }
                }
            }
            if (g_status.search_mode != 0 &&
                (g_level_data->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0 && !found &&
                Random(100) == 0) {
                ApplyItemEffectToRandomCharacter(g_container_event, -1, 0, kCharacterEventNoFlags);
            }
        }
    }
}

/* Pick the best searching party member for this searchable: eligible members
   score their Scouting skill (halved, quartered in the special level state)
   plus a Senses-derived bonus; the detect-secrets party effect instead
   force-picks random eligible members without practicing the skill. The pick
   must also reach this searchable's position. */
// FUNCTION: WIZ8 0x00517560
int W8Searchable::PickBestSearcher()
{
    unsigned int best_score = 0;
    int best_slot = -1;
    bool earned = false;
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        W8Character* character = &g_status.buffers.Char[slot];
        if (!g_status.buffers.XChar[slot].fOccupied || character->hp_current == 0 ||
            character->highest_condition >= W8_CONDITION_TURNCOAT) {
            continue;
        }
        if (CharacterHasTrait(character, W8_TRAIT_SEARCH) || g_status.search_mode != 0) {
            unsigned int level = character->skills[W8_SKILL_SCOUTING].level;
            unsigned int base = level >> 1;
            if ((g_level_data->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) != 0) {
                base = level >> 2;
            }
            unsigned int score = 0;
            if (base != 0 || (g_status.search_mode != 0 &&
                              (g_level_data->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0)) {
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
        if (g_status.party_modifiers.detect_secrets != 0) {
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
            PracticeCharacterSkill(&g_status.buffers.Char[best_slot], W8_SKILL_SCOUTING, 5, false);
        }
        return best_slot;
    }
    return -1;
}

/* Toggle the party's search mode: leaving it posts the off notice, entering it
   posts the on notice and arms the 500ms pulse that sweeps the searchable
   registry; combat blocks the toggle outright. */
// FUNCTION: WIZ8 0x00517780
void ToggleSearchMode(void)
{
    if (g_status.search_mode != 0) {
        g_status.search_mode = 0;
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[W8_NOTICE_SEARCH_MODE_OFF]);
        return;
    }
    if (!gXStatus.fCombatMode) {
        g_status.search_mode = 1;
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[W8_NOTICE_SEARCH_MODE_ON]);
        g_search_pulse_clock = w8_get_ticks() + (0x1f4);
        ClearActiveWorldCursorNode();
    } else {
        ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[W8_NOTICE_SEARCH_BLOCKED_COMBAT]);
    }
}
