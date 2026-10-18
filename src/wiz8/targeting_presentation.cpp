#include "wiz8/targeting_presentation.h"

#include <vector>

#include "wiz8/engine_code/Cursor3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

namespace {
int PreviewPower(const W8Action& action)
{
    const int requested =
        action.kind == W8_ACTION_CAST_SPELL ? action.detail_block.spell.power_level : 1;
    // Automatic power is resolved when the action executes. Its preview keeps
    // the historical base-power geometry and never consumes a gameplay roll.
    return requested >= 1 && requested <= 7 ? requested : 1;
}
}

bool CurrentCombatSubjectIsValid()
{
    if (!g_level_block) return false;
    const int actor = g_status.selected_character;
    if (actor == -1) return true;
    const auto context = GetEditedActionContext(actor);
    const auto action = SelectCombatAction(actor, context);
    const bool sighting = context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT;
    if (g_level_block->highlighted_item)
        return AssessMonsterTarget(actor, *g_level_block->highlighted_item, action, true,
                                  sighting);
    if (auto* group = FindMonsterGroup(g_level_block->action_group, false))
        return CanTargetMonsterGroup(actor, group, action, sighting);
    return false;
}

W8TargetingPresentation BuildTargetingPresentation()
{
    W8TargetingPresentation result;
    if (!GetWorld() || !g_level_block) return result;
    const int selected = g_status.selected_character;
    auto tint_owner = g_level_block->highlight.SlotFor(W8_HIGHLIGHT_TARGET);
    if (!tint_owner && selected != -1) tint_owner = selected;
    bool area_preview = false;
    if (selected != -1 &&
        (gXStatus.iTargetingMode == W8_TARGET_NEED_CONE ||
         (gXStatus.iTargetingMode == W8_TARGET_NEED_PLACE && IsWorldCursorVisible()))) {
        const auto action = SelectCombatAction(selected, GetEditedActionContext(selected));
        const auto spell = GetActionSpellLikeId(action);
        if (spell != 0) {
            area_preview = true;
            W8CombatSlot target{W8_TARGET_KIND_PLACE, -1, -1, -1, {}, nullptr};
            if (gXStatus.iTargetingMode == W8_TARGET_NEED_CONE)
                GetCameraForwardPoint(GetGroundTargetRange(), &target.point);
            else
                GetWorldCursorTargetPosition(&target.point);
            W8TargetSource source;
            SetTargetSourceToCharacter(selected, &source);
            std::vector<int> party;
            PopulateSpellTargetMarkers(spell, PreviewPower(action), &source, &target,
                                       result.area_targets, party, W8AreaSightRule::GeometryOnly);
            for (int id : result.area_targets) result.monsters[id].tint = {0, 1, 0, 1};
        }
    }
    const auto add_targets = [&](int actor, W8Action action) {
        std::vector<int> targets;
        const auto spell = GetActionSpellLikeId(action);
        if (actor == selected && area_preview) {
            targets = result.area_targets;
        } else if (spell != 0) {
            if (!IsTargetStillPresent(&action.target)) return;
            W8TargetSource source;
            SetTargetSourceToCharacter(actor, &source);
            std::vector<int> party;
            PopulateSpellTargetMarkers(spell, PreviewPower(action), &source, &action.target,
                                       targets, party, W8AreaSightRule::RequireLineOfSight);
        } else if (action.target.iType == W8_TARGET_KIND_MONSTER) {
            if (FindMonster(action.target.iMonsterID, false))
                targets.push_back(action.target.iMonsterID);
        } else if (action.target.iType == W8_TARGET_KIND_GROUP) {
            if (auto* group = FindMonsterGroup(action.target.iGroupID, false))
                targets.assign(group->MemberIds().begin(), group->MemberIds().end());
        }
        for (int id : targets) {
            auto& mark = result.monsters[id];
            mark.party_targets |= static_cast<unsigned char>(1u << actor);
            if (tint_owner && actor == *tint_owner &&
                gXStatus.iTargetingMode == W8_TARGET_NEED_NONE)
                mark.tint = {0, 1, 0, 1};
        }
    };
    for (int actor = 0; actor < 8; ++actor) {
        if (!CanPartySlotParticipate(actor)) continue;
        const auto context = GetEditedActionContext(actor);
        if (context != W8_TARGETING_CONTEXT_OUT_OF_COMBAT)
            add_targets(actor, SelectCombatAction(actor, context));
    }
    result.subject_valid = CurrentCombatSubjectIsValid();
    const glm::vec4 tint = result.subject_valid ? glm::vec4{0, 1, 0, 1} : glm::vec4{1, 0, 0, 1};
    auto* highlighted = g_level_block->highlighted_item
                            ? FindMonster(*g_level_block->highlighted_item, false)
                            : nullptr;
    if (highlighted) {
        result.monsters[highlighted->location_id].hovered = true;
        const auto action = selected == -1
                                ? W8Action{}
                                : SelectCombatAction(selected, GetEditedActionContext(selected));
        if (selected != -1 && GetTargetNeededForAction(action) == W8_TARGET_NEED_GROUP) {
            if (auto* group = FindMonsterGroup(highlighted->monster_group_id, false))
                for (int id : group->MemberIds()) result.monsters[id].tint = tint;
        } else {
            result.monsters[highlighted->location_id].tint = tint;
        }
    } else if (auto* group = FindMonsterGroup(g_level_block->action_group, false)) {
        const auto action = selected == -1
                                ? W8Action{}
                                : SelectCombatAction(selected, GetEditedActionContext(selected));
        if (selected != -1 && GetTargetNeededForAction(action) == W8_TARGET_NEED_GROUP) {
            for (int id : group->MemberIds()) result.monsters[id].tint = tint;
        } else if (selected != -1) {
            const auto id = PickNextTargetableGroupMember(selected, group, action,
                                                          GetEditedActionContext(selected) ==
                                                              W8_TARGETING_CONTEXT_OUT_OF_COMBAT);
            if (id) result.monsters[*id].tint = tint;
        }
    }
    return result;
}

void ApplyTargetingPresentation(const W8TargetingPresentation& presentation)
{
    if (!GetWorld()) return;
    for (const auto& info : CurrentLevelPopulation().monster_roster.live()) {
        auto* monster = info->p3D;
        if (!monster) continue;
        const auto found = presentation.monsters.find(info->location_id);
        const auto mark =
            found == presentation.monsters.end() ? W8MonsterPresentation{} : found->second;
        for (int actor = 0; actor < 8; ++actor) {
            const bool targeted = (mark.party_targets & (1u << actor)) != 0;
            // Marker objects are a replaceable render cache, never selection input.
            if (targeted != (monster->m_pRep->objects[actor] != nullptr))
                SetMonsterPartySlotMarker(actor, info->location_id, targeted);
        }
        monster->UpdateAttachedObjects(mark.hovered);
        monster->SetPresentationColour(monster->IsDying() ? glm::vec4{0} : mark.tint);
    }
    for (const auto& item : CurrentLevelPopulation().world_items) {
        if (item->fActive && item->p3D) {
            const bool hovered = g_level_block && g_level_block->selected_item == item->runtime_id;
            item->p3D->SetHighlight(true, hovered);
        }
    }
}
