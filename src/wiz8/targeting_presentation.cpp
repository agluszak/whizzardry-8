#include "wiz8/targeting_presentation.h"

#include <optional>
#include <vector>

#include "wiz8/combat/combat.h"
#include "wiz8/combat/range.h"
#include "wiz8/entities/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/pointer.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

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

bool CurrentCombatSubjectIsValid(const W8LevelRuntimeBlock& hud)
{
    const int actor = CurrentGame().status.selected_character;
    if (actor == -1) return true;
    const auto action = EditedAction(actor);
    const bool sighting = IsSighting(actor);
    if (hud.highlighted_item != -1)
        return AssessMonsterTarget(actor, hud.highlighted_item, action, true, sighting);
    if (auto* group = FindMonsterGroup(hud.action_group, false))
        return CanTargetMonsterGroup(actor, group, action, sighting);
    return false;
}

W8TargetingPresentation BuildTargetingPresentation(const W8LevelRuntimeBlock& hud)
{
    W8TargetingPresentation result;
    if (!GetWorld()) return result;
    const int selected = CurrentGame().status.selected_character;
    int tint_owner = hud.highlight.SlotFor(W8_HIGHLIGHT_TARGET);
    if (tint_owner == -1) tint_owner = selected;
    bool area_preview = false;
    if (selected != -1 &&
        (ScreenPointer().targeting() == W8_TARGET_NEED_CONE ||
         (ScreenPointer().targeting() == W8_TARGET_NEED_PLACE && IsWorldCursorVisible()))) {
        const auto action = EditedAction(selected);
        const auto spell = GetActionSpellLikeId(CurrentGame().inventory, action);
        if (spell != 0) {
            area_preview = true;
            W8CombatSlot target{W8_TARGET_KIND_PLACE, -1, -1, -1, {}, {}};
            if (ScreenPointer().targeting() == W8_TARGET_NEED_CONE)
                GetCameraForwardPoint(GetGroundTargetRange(), &target.point);
            else
                GetWorldCursorTargetPosition(&target.point);
            W8TargetSource source;
            SetTargetSourceToCharacter(selected, &source);
            std::vector<int> party;
            CurrentGame().appendSpellTargets(spell, PreviewPower(action), &source, &target,
                                             result.area_targets, party,
                                             W8AreaSightRule::GeometryOnly);
            for (int id : result.area_targets) result.monsters[id].tint = {0, 1, 0, 1};
        }
    }
    const auto add_targets = [&](int actor, W8Action action) {
        std::vector<int> targets;
        const auto spell = GetActionSpellLikeId(CurrentGame().inventory, action);
        if (actor == selected && area_preview) {
            targets = result.area_targets;
        } else if (spell != 0) {
            if (!CurrentGame().targetPresent(action.target)) return;
            W8TargetSource source;
            SetTargetSourceToCharacter(actor, &source);
            std::vector<int> party;
            CurrentGame().appendSpellTargets(spell, PreviewPower(action), &source, &action.target,
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
            if (actor == tint_owner && ScreenPointer().targeting() == W8_TARGET_NEED_NONE)
                mark.tint = {0, 1, 0, 1};
        }
    };
    for (int actor = 0; actor < 8; ++actor) {
        if (!CurrentGame().party.participates(actor)) continue;
        if (!IsSighting(actor)) add_targets(actor, EditedAction(actor));
    }
    result.subject_valid = CurrentCombatSubjectIsValid(hud);
    result.hovered_item = hud.selected_item;
    const glm::vec4 tint = result.subject_valid ? glm::vec4{0, 1, 0, 1} : glm::vec4{1, 0, 0, 1};
    if (auto* info = FindMonster(hud.highlighted_item, false)) {
        result.monsters[info->location_id].hovered = true;
        const auto action = selected == -1 ? W8Action{} : EditedAction(selected);
        if (selected != -1 && CurrentGame().targetNeed(action) == W8_TARGET_NEED_GROUP) {
            if (auto* group = FindMonsterGroup(info->monster_group_id, false))
                for (int id : group->MemberIds()) result.monsters[id].tint = tint;
        } else
            result.monsters[info->location_id].tint = tint;
    } else if (auto* group = FindMonsterGroup(hud.action_group, false)) {
        const auto action = selected == -1 ? W8Action{} : EditedAction(selected);
        if (selected != -1 && CurrentGame().targetNeed(action) == W8_TARGET_NEED_GROUP) {
            for (int id : group->MemberIds()) result.monsters[id].tint = tint;
        } else if (selected != -1) {
            const auto id =
                PickNextTargetableGroupMember(selected, group, action, IsSighting(selected));
            if (id != -1) result.monsters[id].tint = tint;
        }
    }
    return result;
}

void ApplyTargetingPresentation(const W8TargetingPresentation& presentation)
{
    if (!GetWorld()) return;
    for (const auto& info : CurrentLevelPopulation().monster_roster.live()) {
        auto* monster = info->entity.get();
        if (!monster) continue;
        const auto found = presentation.monsters.find(info->location_id);
        const auto mark =
            found == presentation.monsters.end() ? W8MonsterPresentation{} : found->second;
        for (int actor = 0; actor < 8; ++actor) {
            const bool targeted = (mark.party_targets & (1u << actor)) != 0;
            // Marker objects are a replaceable render cache, never selection input.
            monster->setPartyMarker(
                actor,
                targeted ? std::optional{CurrentGame().party.orderIndexOf(actor)} : std::nullopt);
        }
        monster->UpdateAttachedObjects(mark.hovered);
        monster->visual.SetPresentationColour(monster->IsDying() ? glm::vec4{0} : mark.tint);
    }
    for (const auto& item : CurrentLevelPopulation().world_items) {
        if (item->entity) {
            const bool hovered = presentation.hovered_item == item->runtime_id;
            item->entity->SetHighlight(true, hovered);
        }
    }
}

void RevalidateSelectedTarget(int party_slot)
{
    W8TargetNeed needed;

    if (party_slot != CurrentGame().status.selected_character) {
        return;
    }
    const auto selected = EditedAction(party_slot);
    if (!CurrentGame().canPerformAction(party_slot, selected, true)) {
        CurrentGame().events.push(TargetingRequested{});
        return;
    }
    needed = W8_TARGET_NEED_NONE;
    if (!IsSighting(party_slot)) {
        needed = CurrentGame().targetNeed(selected, MainGameHud() == nullptr);
    }
    CurrentGame().events.push(TargetingRequested{
        CurrentGame().targetMatches(selected.target, needed) ? W8_TARGET_NEED_NONE : needed});
}
