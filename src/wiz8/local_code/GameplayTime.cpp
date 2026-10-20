#include "wiz8/local_code/GameplayTime.h"

#include <memory>
#include <string>
#include <vector>

#include <fmt/printf.h>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/monster_generators.h"
#include "wiz8/monster_runtime.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

constexpr float kFastMagicRecoveryScale = 1.25f;

using SurprisePhase = SurpriseSequence::Phase;

void ApplyWorldAdvancement(Game& game, WorldHold hold)
{
    auto& advancement = game.advancement;
    advancement.hold = hold;
    const bool held = advancement.paused || hold.covered || hold.world;
    const bool time_stopped = held || game.combat.active();
    const World* world = GetWorld();
    const bool hold_changed = held != advancement.held;
    const bool time_changed = world != advancement.world || time_stopped == advancement.time_passes;
    advancement.held = held;
    advancement.world = world;
    if (advancement.timer_paused != advancement.paused) {
        advancement.timer_paused = advancement.paused;
        MoveTimer(advancement.timer_paused ? W8_MOVE_TIMER_SUSPEND : W8_MOVE_TIMER_RESUME);
    }
    if (!world) return;
    if (time_changed) {
        SetEnvironmentTimeEnabled(!time_stopped);
        if (time_stopped) {
            StopAllNavigators();
            ResetLevelDataVectors();
        } else {
            ResumeAllNavigators();
            if (!hold.party) ClearLevelMovementStopped();
        }
    }
    if (hold_changed || time_changed)
        game.events.push(WorldAdvancementChanged{.held = held, .hold_changed = hold_changed});
}

bool SurpriseFadingIn()
{
    return CurrentGame().surprise.phase == SurprisePhase::fade_in;
}

// Camping lifts the player's pause; whatever the interface holds stays held.
static void BeginPartyCamping()
{
    auto& game = CurrentGame();
    auto& surprise = game.surprise;
    game.advancement.paused = false;
    if (game.advancement.held) {
        ApplyWorldAdvancement(game, game.advancement.hold);
    }
    if (!AnyCharacterEngaged()) {
        surprise.unengaged = true;
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x794]);
    } else {
        surprise.unengaged = false;
        ShowNotice(W8_FONT_PALETTE_BEIGE, fmt::sprintf(Data().strings[0x790], 8));
    }
    SetNpcQuoteBubbleVisible(false, {}, 0, -1, 0xffffffff);
    BeginSurprise();
    game.surprise_possible = true;
    SetWorldResumeInput(true);
    surprise.deadline_turns = 0;
    CreateSurpriseFade();
    surprise.phase = SurprisePhase::fade_in;
    StartMusicResource("Camping.MPL", 0, 1);
    ForgetRecentCombat();
}

/* The per-frame game-time driver: folds the elapsed milliseconds into the
   world clock, fires the 120-second aging tick, the hourly item recharge,
   the 2500ms camping-fatigue and 1400ms stamina ticks, and runs the surprise
   sequence entry/transition while the party is eligible. */
void UpdateGameClock(int elapsed)
{
    CurrentGame().status.world_clock +=
        static_cast<int>((CurrentGame().status.world_clock_ms + elapsed) / 1000);
    CurrentGame().status.world_clock_ms = (CurrentGame().status.world_clock_ms + elapsed) % 1000;

    CurrentGame().status.aging_accumulator += elapsed;
    unsigned int aging_ticks = 0;
    if (!CurrentGame().aging_held) {
        aging_ticks = CurrentGame().status.aging_accumulator / 120000;
        if (aging_ticks != 0) {
            CurrentGame().status.aging_accumulator %= 120000;
            AdvanceTimedEffects(aging_ticks);
        }
    }

    CurrentGame().status.item_recharge_ms += elapsed;
    unsigned int hours = CurrentGame().status.item_recharge_ms / 3600000;
    if (hours != 0) {
        CurrentGame().status.item_recharge_ms %= 3600000;
        for (unsigned int slot = 0; slot < 8; ++slot) {
            char uses = static_cast<char>(hours);
            if (!CurrentGame().party.rows[slot].fOccupied) {
                continue;
            }
            W8Character* character = &CurrentGame().party.characters[slot];
            int i;
            for (i = 0; i < 0xc; ++i) {
                if (character->EquippedItem[i].iItemNo == 0x266) {
                    AddItemUses(&character->EquippedItem[i], uses);
                }
            }
            for (i = 0; i < 8; ++i) {
                if (character->backpack[i].iItemNo == 0x266) {
                    AddItemUses(&character->backpack[i], uses);
                }
            }
        }
        for (unsigned int index = 0; index < CurrentGame().party.item_count; ++index) {
            W8ItemInstance* item = &CurrentGame().party.item_pool[index];
            if (item->iItemNo == 0x266) {
                AddItemUses(item, static_cast<char>(hours));
            }
        }
    }

    if ((!CurrentGame().combat.active() || aging_ticks != 0) && g_camera_sway_active) {
        if (CurrentGame().combat.active()) {
            if (aging_ticks == 0) {
                Fail("uiTurnsElapsed > 0");
            }
            CurrentGame().status.camp_tick_ms -= 4 * 0x9c4;
            UpdateCampFatigue(4);
        } else {
            CurrentGame().status.camp_tick_ms += elapsed;
            unsigned int camp_ticks = CurrentGame().status.camp_tick_ms / 0x9c4;
            if (camp_ticks != 0) {
                CurrentGame().status.camp_tick_ms -= camp_ticks * 0x9c4;
                UpdateCampFatigue(static_cast<int>(camp_ticks));
            }
        }
    } else {
        CurrentGame().status.camp_tick_ms = 0;
        CurrentGame().status.party_fatigued = false;
        CurrentGame().status.camp_fatigue_count = 0;
    }

    if (CurrentGame().combat.active()) {
        CurrentGame().status.stamina_tick_ms = 0;
    } else {
        CurrentGame().status.stamina_tick_ms += elapsed;
        unsigned int stamina_ticks = CurrentGame().status.stamina_tick_ms / 0x578;
        if (stamina_ticks != 0) {
            CurrentGame().status.stamina_tick_ms %= 0x578;
            UpdatePartyStamina(static_cast<int>(stamina_ticks));
        }

        if (!CurrentGame().surprise_possible && !AnyCharacterEngaged() && AnyCharacterActive() &&
            !CurrentGame().status.party_fatigued && !HasLevelDataVector() &&
            HasLevelWalkableContact() && IsScreenIdle()) {
            BeginPartyCamping();
        }
    }

    if (!CurrentGame().surprise_possible) {
        return;
    }
    auto& surprise = CurrentGame().surprise;
    if (surprise.unengaged && AnyCharacterEngaged() && surprise.phase == SurprisePhase::hold) {
        ResetSight();
        ReverseSurpriseFade();
        surprise.phase = SurprisePhase::fade_out_and_resolve;
        ReleaseMarkedNpcBindings();
        StartLevelMusic(1, 1);
    }
}

/* The Camp key/button requests the surprise-possible camp state. Camping is
   refused outright in combat and while a level vector or a level flag blocks
   it; a blocking world-cursor node or a busy screen declines silently. */
void RequestCamp(void)
{
    if (CurrentGame().surprise_possible) {
        return;
    }
    if (CurrentGame().combat.active()) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x774]);
        return;
    }
    if (!HasLevelDataVector() && HasLevelWalkableContact()) {
        if (DispatchWorldCursorNodeCommand(0, 3) != 0) {
            return;
        }
        if (!IsScreenIdle()) {
            return;
        }
        BeginPartyCamping();
        return;
    }
    ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x796]);
}

/* Complete the pending character events, reset the occupied slots' portrait
   poses to the startled frame, take the menu button banks down and request a
   full redraw. The scripted ambush triggers call this to open the surprise
   sequence. */
void BeginSurprise(void)
{
    unsigned int i;

    CurrentGame().speech.CompleteAllActiveEvents();
    for (i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
        if (CurrentGame().party.rows[i].fOccupied) {
            SetPortraitTargetPose(&CurrentGame().monster_manager_entries[i], 2);
        }
    }
    DisableMenuButtonBanks();
    RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
}

/* Advance the surprise fade/hold/resolve sequence while the party is locked
   into fSurprisePossible. Phase 0 waits for the fade-in helper, widens sight,
   and scales time from the level's +0x54 float; phase 1 holds until the
   world clock reaches the deadline (unless surprise started unengaged);
   phase 2 waits for the fade-out helper, then ends surprise and forwards
   combat if needed. */
void UpdateSurpriseMode(void)
{
    float scale;
    float level_scale;

    if (!CurrentGame().surprise_possible) {
        return;
    }
    auto& surprise = CurrentGame().surprise;
    switch (surprise.phase) {
    case SurprisePhase::fade_in:
        if (UpdateSurpriseFade() != 0) {
            surprise.deadline_turns = CurrentGame().status.world_clock + 0x7080;
            DestroyUngroupedMonsters();
            SetViewDistance(2880.0f);
            SetNavigatorLinkMode(W8_NAVIGATOR_LINK_MODE_STOPPED);
            level_scale = Data().levels[GetLoadedLevelID()].gameplay_time_scale;
            scale = 1.0f / level_scale;
            GameTimeAccumulator().SetTimeScale(scale);
            SetMonsterGeneratorDurationScale(scale);
            surprise.phase = SurprisePhase::hold;
        }
        break;
    case SurprisePhase::hold:
        if (!surprise.unengaged && static_cast<unsigned int>(CurrentGame().status.world_clock) >=
                                       surprise.deadline_turns) {
            ResetSight();
            UpdateEnvironmentLight();
            RefreshEnvironment();
            ReverseSurpriseFade();
            surprise.phase = SurprisePhase::fade_out_and_resolve;
            ReleaseMarkedNpcBindings();
            StartLevelMusic(1, 1);
        }
        break;
    case SurprisePhase::fade_out_and_resolve:
        if (UpdateSurpriseFade() != 0) {
            EndSurprise();
            if (CurrentGame().combat.active()) {
                StopAllNavigators();
            }
        }
        break;
    }
}

/* The cancel command while surprise runs: while the ambush is unengaged and
   the party is out of combat it reposts the surprise notice; once the fade
   has reached phase 1 it performs the same restore as UpdateSurpriseMode's
   deadline path - without the environment-light refresh - and moves the
   sequence to phase 2. */
void AcknowledgeSurprise(void)
{
    if (CurrentGame().surprise.unengaged && !CurrentGame().combat.active()) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x794]);
        return;
    }
    ResolveSurpriseHold();
}

/* While a surprise sequence is holding, end it for combat: restore the view,
   navigator and time-scale overrides, reverse the fade and restart the level
   music. */
void ResolveSurpriseHold(void)
{
    if (CurrentGame().surprise.phase == SurprisePhase::hold) {
        ResetSight();
        ReverseSurpriseFade();
        CurrentGame().surprise.phase = SurprisePhase::fade_out_and_resolve;
        ReleaseMarkedNpcBindings();
        StartLevelMusic(1, 1);
    }
}

/* End the surprise sequence: post the outcome notice and, if the condition-13
   rest event armed condition13_clock more than a world-clock day ago, clear the
   condition and queue the rest-benefit event for that character. */
void EndSurprise(void)
{
    CurrentGame().surprise_possible = false;
    ResolveSurpriseWake();
    SetWorldResumeInput(false);

    std::string text;
    auto& surprise = CurrentGame().surprise;
    if (!surprise.unengaged) {
        if (surprise.deadline_turns == 0) {
            text = Data().strings[0x793];
        } else if (surprise.deadline_turns <
                   static_cast<unsigned int>(CurrentGame().status.world_clock)) {
            text = Data().strings[0x791];
        } else {
            text = Data().strings[0x792];
        }
    } else {
        surprise.unengaged = false;
        text = AnyCharacterEngaged() ? Data().strings[0x795] : Data().strings[0x792];
    }
    ShowNotice(W8_FONT_PALETTE_BEIGE, text);

    if (CurrentGame().status.condition13_clock != 0 &&
        0x15180 < static_cast<unsigned int>(CurrentGame().status.world_clock) -
                      CurrentGame().status.condition13_clock) {
        CurrentGame().status.condition13_clock = 0;
        CurrentGame().status.skip_next_condition_reaction = true;
        int party_slot = CurrentGame().status.pending_condition_party_slot;
        RemoveCharacterCondition(party_slot, W8_CONDITION_MISSING, false);
        QueueCharacterEvent(&CurrentGame().party.characters[party_slot], 52, 0,
                            kCharacterEventFullVolume);
        SetFact(W8_FACT_MOOK_MOOK_PC_SWAP_COMPLETE, 1, false);
    }
}

/* Tear down the world-state overrides the surprise sequence installed:
   restore default view distance and navigator link mode, reset the time
   scale, restart the monster generator timers and release the fade overlay. */
void RestoreSurpriseView(void)
{
    CurrentGame().surprise_possible = false;
    CurrentGame().surprise.unengaged = false;
    ResetSight();
    DestroySurpriseFade();
}

/* The camp-wake resolution inside the surprise teardown: each surviving
   character rolls against their senses attribute - failure applies the
   groggy condition, success posts a notice. Then every occupied portrait is
   posed awake with a randomized idle clock and the button banks re-enable. */
void ResolveSurpriseWake(void)
{
    if (CurrentGame().combat.state() != 0 && CurrentGame().combat.state()->party_surprised) {
        for (unsigned int slot = 0; slot < 8; ++slot) {
            W8Character* character = &CurrentGame().party.characters[slot];
            if (!CurrentGame().party.rows[slot].fOccupied ||
                character->highest_condition >= W8_CONDITION_DEAD) {
                continue;
            }
            int roll =
                static_cast<int>(Random(100)) - 0x14 -
                static_cast<int>(character->attributes[W8_ATTRIBUTE_SENSES].effective * 0x46 / 100);
            if (roll < 1) {
                PostCharacterNotice(
                    slot,
                    fmt::sprintf(Data().strings[0x243],
                                 Data().strings[g_condition_notices[W8_CONDITION_ASLEEP].name]));
            } else {
                SetCharacterCondition(slot, W8_CONDITION_ASLEEP, roll / 0x1e + 1, 0, 0, 0);
            }
        }
    }

    for (unsigned int slot = 0; slot < 8; ++slot) {
        if (!CurrentGame().party.rows[slot].fOccupied) {
            continue;
        }
        W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[slot];
        SetPortraitTargetPose(entry, 1);
        if (Random(2) != 0) {
            entry->portrait.idle_clock = MonotonicMilliseconds() + (Random(5000) + 5000);
        }
        entry->portrait.pose_dirty = true;
        RequestRedraw(1 << slot);
    }
    EnableMenuButtonBanks();
}

/* The monster counterpart of the character regen rebuild: the same pool-share
   rates plus the modifier block's two regen channels, raised by half while the corresponding
   regeneration-boost flags are set. */
void RebuildMonsterRegenRates(W8MonsterInfo* monster_info)
{
    float rate;

    rate = (static_cast<unsigned int>(monster_info->uiHPMax) * kNavigatorMode3Scale +
            g_monster_record_float_scale) *
               0.0041666669f +
           monster_info->modifiers.health_regen_adjustment;
    monster_info->hp_regen_rate = rate;
    if (monster_info->modifiers.boost_health_regen != 0) {
        monster_info->hp_regen_rate = rate * 1.5f;
    }

    rate = ((static_cast<unsigned int>(monster_info->stamina_max)) * 0.9f +
            g_monster_record_float_scale) *
               0.0041666669f +
           monster_info->modifiers.stamina_regen_adjustment;
    monster_info->stamina_regen_rate = rate;
    if (monster_info->modifiers.boost_stamina_regen != 0) {
        monster_info->stamina_regen_rate = rate * 1.5f;
    }
}

/* The 120-second aging tick: advances the wait-state machine while the
   real/frame elapsed accumulators hold at zero, ages each living character
   and monster, expires party and combat effect slots with expiry notices,
   ticks queued spell effects once per minute and runs the NPC-side passes. */
static bool AgePartyEffectSlot(W8EffectSlot* slot, unsigned int minutes)
{
    if (!slot->active) {
        return false;
    }
    if (minutes < slot->duration) {
        slot->duration -= minutes;
        return false;
    }
    ShowNotice(W8_FONT_PALETTE_BEIGE,
               fmt::sprintf(Data().strings[0x1b4], Data().spells[slot->effect_id].display_name));
    if (GetViewDistance() != g_encounter_culling_rate) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Expiry.wav");
    }
    ResetPartyEffectBlock(slot);
    return true;
}

void AdvanceTimedEffects(unsigned int minutes)
{
    if (CurrentGame().status.real_elapsed + CurrentGame().status.frame_elapsed == 0.0f) {
        if (CurrentGame().status.wait_state == W8_GAME_WAIT_STATE_FAST_MOVEMENT ||
            CurrentGame().status.wait_state == W8_GAME_WAIT_STATE_MOVING) {
            CurrentGame().status.wait_state = W8_GAME_WAIT_STATE_INITIAL_REST;
            for (unsigned int slot = 0; slot < 8; ++slot) {
                CurrentGame().party.rows[slot].movement_fatigue = 0;
            }
        } else if (CurrentGame().status.wait_state == W8_GAME_WAIT_STATE_INITIAL_REST) {
            CurrentGame().status.wait_state = W8_GAME_WAIT_STATE_RESTING;
        }
    } else {
        CurrentGame().status.wait_state = CurrentGame().status.frame_elapsed != 0.0f
                                              ? W8_GAME_WAIT_STATE_FAST_MOVEMENT
                                              : W8_GAME_WAIT_STATE_MOVING;
        CurrentGame().status.real_elapsed = 0.0f;
        CurrentGame().status.frame_elapsed = 0.0f;
    }

    for (unsigned int slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.rows[slot].fOccupied &&
            (character->highest_condition < W8_CONDITION_DEAD ||
             (character->uiCondition[W8_CONDITION_DEAD] == 0 &&
              GetConditionRecordFlag(slot, W8_DEPENDENCE_SWALLOWED)))) {
            GameTurnsPassedChar(slot, minutes);
        }
    }

    if (CurrentGame().combat.active()) {
        W8CombatSlot target;
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_PARTY;
        TickCombatEffectSlots(CurrentGame().combat.state()->effect_slots, &target);
        TickRadiusBlastEffectSlots(CurrentGame().combat.state()->effect_slots0);
    }

    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);
        if (monster_info->highest_condition < W8_CONDITION_DEAD) {
            AgeMonsterSight(monster_info, minutes, 0);
        }
    }

    bool party_changed = false;
    bool combat_changed = false;
    unsigned int i;
    for (i = 0; i < 12; ++i) {
        W8EffectSlot* slot = &CurrentGame().status.effect_slots[i];
        if (AgePartyEffectSlot(slot, minutes)) {
            party_changed = true;
        }
    }

    if (CurrentGame().combat.active()) {
        for (i = 0; i < 9; ++i) {
            W8EffectSlot* slot = &CurrentGame().combat.state()->effect_slots[i];
            if (AgePartyEffectSlot(slot, minutes)) {
                combat_changed = true;
            }
        }
        for (i = 0; i < 6; ++i) {
            W8EffectSlot* slot = &CurrentGame().combat.state()->effect_slots0[i];
            if (AgePartyEffectSlot(slot, minutes)) {
                combat_changed = true;
            }
        }
    }

    if (party_changed) {
        RequestRedrawCombatBar();
    }
    if (combat_changed) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }
    for (unsigned int remaining = minutes; remaining != 0; --remaining) {
        TickSpellEffects();
    }
    W8SpellEffectEntry* control = FindMonsterControlSpellEffect();
    if (control != 0 && control->turns_remaining != 0) {
        ApplyMonsterControlToNearbyMonsters(control);
    }
    if (party_changed || combat_changed) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
    }
    if (!CurrentGame().surprise_possible) {
        DetectMonsterGroups();
    }
    AdvanceNpcTimers(minutes * 10);
    ProcessNpcPendingEvents();
}

/* Advance one character's whole aging cycle by the elapsed minutes: the timed
   damage/heal modifier bytes, the disease deterioration rolls, the
   item-0x243/0x239 madness pair, the fractional health/stamina/spell-point
   regeneration accumulators, and the finite condition and enchantment
   countdowns. */
void GameTurnsPassedChar(int party_slot, unsigned int minutes)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    bool diseased = false;
    unsigned int realm;

    unsigned int damage = character->bonus.damage_per_minute;
    if (damage != 0) {
        damage *= minutes;
        if (CurrentGame().status.wait_state != W8_GAME_WAIT_STATE_RESTING ||
            CurrentGame().combat.active()) {
            damage += damage >> 1;
        }
        ApplyDamageToCharacter(party_slot, damage, true, true, false,
                               static_cast<W8SpellEffectResult*>(0), false);
    }

    if (character->uiCondition[W8_CONDITION_DISEASED] != 0) {
        diseased = true;
        if (Random(character->attributes[W8_ATTRIBUTE_VITALITY].effective * 10) < minutes) {
            switch (Random(4)) {
            case 0: {
                unsigned int condition;
                switch (Random(5)) {
                case 0:
                    condition = W8_CONDITION_IRRITATED;
                    break;
                case 1:
                    condition = W8_CONDITION_NAUSEATED;
                    break;
                case 2:
                    condition = W8_CONDITION_SLOWED;
                    break;
                case 3:
                    condition = W8_CONDITION_BLIND;
                    break;
                case 4:
                    condition = W8_CONDITION_INSANE;
                    break;
                case 5:
                    condition = W8_CONDITION_PARALYZED;
                    break;
                default:
                    Fail("FALSE", "GameTurnsPassedChar: ERROR - Invalid condition");
                    condition = minutes;
                    break;
                }
                SetCharacterCondition(party_slot, static_cast<W8Condition>(condition),
                                      W8_CONDITION_INDEFINITE, 0, 0, 1);
                break;
            }
            case 1: {
                unsigned int attribute = Random(7);
                if (1 < character->attributes[attribute].base) {
                    character->attributes[attribute].base =
                        character->attributes[attribute].base - 1;
                    ++character->attributes[attribute].change_counter;
                    ApplyAttributeChange(CurrentGame(), character,
                                         static_cast<W8Attribute>(attribute));
                    PostCharacterNotice(
                        party_slot,
                        fmt::sprintf(Data().strings[0x270],
                                     Data().strings[g_character_description_first_ids[attribute]]));
                }
                break;
            }
            case 2:
                if (static_cast<unsigned int>(character->uiHPMax) < 5) {
                    break;
                }
                {
                    unsigned int loss = Random(4) + 1;
                    character->uiHPMax -= loss;
                    character->hp_adjustment -= loss;
                    if (loss == 1) {
                        PostCharacterNotice(party_slot, Data().strings[0x26e]);
                    } else {
                        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x26f], loss));
                    }
                    unsigned int current = character->hp_current;
                    if (1 < current) {
                        if (loss >= current - 1) {
                            loss = current - 1;
                        }
                        character->hp_current = current - loss;
                    }
                    RequestPartySlotRedraw(party_slot);
                }
                break;
            case 3:
                if (static_cast<unsigned int>(character->uiStaminaMax) < 5) {
                    break;
                }
                {
                    unsigned int loss = Random(4) + 1;
                    character->uiStaminaMax -= loss;
                    character->fatigue_penalty += loss;
                    PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x271], loss));
                    int stamina = character->stamina;
                    if (1 < stamina) {
                        if (loss >= stamina - 1U) {
                            loss = stamina - 1U;
                        }
                        character->stamina = stamina - loss;
                    }
                    RequestPartySlotRedraw(party_slot);
                }
                break;
            }
        }
    }

    if (character->uiCondition[W8_CONDITION_INFATUATED] != 0) {
        if (GetLevelBand(GetLoadedLevelID()) == 9 || GetLevelBand(GetLoadedLevelID()) == 0xa) {
            if (character->uiCondition[W8_CONDITION_HEXED] == W8_CONDITION_INDEFINITE) {
                RemoveCharacterCondition(party_slot, W8_CONDITION_HEXED, true);
            }
        } else {
            unsigned int hits = 0;
            for (unsigned int roll = minutes; roll != 0; --roll) {
                if (Random(4) == 0) {
                    ++hits;
                }
            }
            if (hits != 0) {
                ApplyDamageToCharacter(party_slot, hits, false, true, false,
                                       static_cast<W8SpellEffectResult*>(0), false);
            }
            if (character->uiCondition[W8_CONDITION_HEXED] == 0) {
                SetCharacterCondition(party_slot, W8_CONDITION_HEXED, W8_CONDITION_INDEFINITE, 0, 0,
                                      1);
            }
        }
    }

    if (const auto found =
            FindPartyItem(CurrentGame().party, ItemWithId(0x243), W8ItemReach::Party);
        found.storage != W8ItemStorage::None && found.storage != W8ItemStorage::Hand) {
        auto* holder = CurrentGame().party.holderOf(found);
        if (!holder || !FindCharacterItem(*holder, ItemWithId(0x239), W8ItemReach::Worn)) {
            if (character->uiCondition[W8_CONDITION_INSANE] < W8_CONDITION_INDEFINITE) {
                SetCharacterCondition(party_slot, W8_CONDITION_INSANE, W8_CONDITION_INDEFINITE, 0,
                                      0, 1);
            }
        } else if (character->uiCondition[W8_CONDITION_INSANE] == W8_CONDITION_INDEFINITE) {
            RemoveCharacterCondition(party_slot, W8_CONDITION_INSANE, true);
        }
    }

    signed char health_mod = character->bonus.health_regen_adjustment;
    if (health_mod > 0) {
        if (character->hp_current < static_cast<unsigned int>(character->uiHPMax)) {
            HealCharacter(party_slot, static_cast<int>(health_mod) * static_cast<int>(minutes),
                          false);
        }
    } else if (health_mod < 0) {
        ApplyDamageToCharacter(party_slot,
                               -static_cast<int>(health_mod) * static_cast<int>(minutes), false,
                               true, false, static_cast<W8SpellEffectResult*>(0), false);
    }

    signed char stamina_mod = character->bonus.stamina_regen_adjustment;
    if (stamina_mod > 0) {
        if (character->stamina < character->uiStaminaMax) {
            RestoreCharacterStamina(party_slot, stamina_mod * static_cast<int>(minutes), false);
        }
    } else if (stamina_mod < 0) {
        FatigueCharacter(party_slot, -static_cast<int>(stamina_mod * minutes), false,
                         static_cast<W8SpellEffectResult*>(0));
    }

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        signed char spell_mod = character->bonus.spell_regen_adjustment;
        if (spell_mod > 0) {
            if (character->iSPLeft[realm] < character->sp_max[realm]) {
                RestoreCharacterRealmSpellPoints(party_slot, static_cast<W8SpellRealm>(realm),
                                                 static_cast<int>(spell_mod) *
                                                     static_cast<int>(minutes));
            }
        } else if (spell_mod < 0) {
            DrainCharacterRealmSpellPoints(party_slot, static_cast<W8SpellRealm>(realm),
                                           -static_cast<int>(spell_mod), true);
        }
    }

    float health_scale;
    if (CurrentGame().surprise_possible) {
        health_scale = 1.0f;
    } else if (CurrentGame().status.wait_state == W8_GAME_WAIT_STATE_RESTING &&
               !CurrentGame().combat.active()) {
        health_scale = 0.5f;
    } else {
        health_scale = 0.0f;
    }
    if (diseased) {
        health_scale *= g_navigator_vertical_phase_step;
    }
    float spell_scale = health_scale;
    if (health_scale < 0.5f) {
        spell_scale = 0.5f;
    }
    float stamina_scale = 0.0f;

    if (CharacterHasTrait(character, W8_TRAIT_HEALTH_REGENERATION)) {
        if (health_scale == 0.0f) {
            health_scale =
                ScaleValueByProfessionLevel(character, W8_TRAIT_HEALTH_REGENERATION, 16.67f) * 0.5f;
        } else {
            health_scale =
                ScaleValueByProfessionLevel(character, W8_TRAIT_HEALTH_REGENERATION, 16.67f) *
                health_scale;
        }
    }
    if (CharacterHasTrait(character, W8_TRAIT_STAMINA_REGENERATION) &&
        CurrentGame().combat.active()) {
        stamina_scale = ScaleValueByProfessionLevel(character, W8_TRAIT_STAMINA_REGENERATION, 3.3f);
    }
    if (CharacterHasTrait(character, static_cast<W8Trait>(0x1a)) && spell_scale > 0.0f) {
        spell_scale *= kFastMagicRecoveryScale;
    }
    if (CharacterHasTrait(character, W8_TRAIT_LIZARDMAN_SLOW_MAGIC_RECOVERY) &&
        spell_scale > 0.0f) {
        spell_scale *= 0.75f;
    }

    if (health_scale > 0.0f &&
        character->hp_current < static_cast<unsigned int>(character->uiHPMax)) {
        character->health_regen_accumulator =
            minutes * character->health_regen_rate * health_scale +
            character->health_regen_accumulator;
        HealCharacter(party_slot, static_cast<int>(character->health_regen_accumulator), false);
        character->health_regen_accumulator =
            character->health_regen_accumulator -
            static_cast<unsigned int>(character->health_regen_accumulator);
    }
    if (stamina_scale > 0.0f && character->stamina < character->uiStaminaMax) {
        character->stamina_regen_accumulator =
            minutes * character->stamina_regen_rate * stamina_scale +
            character->stamina_regen_accumulator;
        RestoreCharacterStamina(party_slot, static_cast<int>(character->stamina_regen_accumulator),
                                false);
        character->stamina_regen_accumulator =
            character->stamina_regen_accumulator -
            static_cast<unsigned int>(character->stamina_regen_accumulator);
    }
    if (spell_scale > 0.0f) {
        for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
            if (character->iSPLeft[realm] < character->sp_max[realm]) {
                character->spell_regen_rates[realm * 2 + 1] =
                    minutes * character->spell_regen_rates[realm * 2] * spell_scale +
                    character->spell_regen_rates[realm * 2 + 1];
                RestoreCharacterRealmSpellPoints(
                    party_slot, static_cast<W8SpellRealm>(realm),
                    static_cast<int>(character->spell_regen_rates[realm * 2 + 1]));
                character->spell_regen_rates[realm * 2 + 1] =
                    character->spell_regen_rates[realm * 2 + 1] -
                    static_cast<unsigned int>(character->spell_regen_rates[realm * 2 + 1]);
            }
        }
    }

    if (character->hp_current >= static_cast<unsigned int>(character->uiHPMax)) {
        character->health_regen_accumulator = 0.0f;
    }
    if (character->stamina >= character->uiStaminaMax) {
        character->stamina_regen_accumulator = 0.0f;
    }
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        if (character->iSPLeft[realm] >= character->sp_max[realm]) {
            character->spell_regen_rates[realm * 2 + 1] = 0.0f;
        }
    }

    for (unsigned int condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
        if (character->uiCondition[condition] != 0 &&
            character->uiCondition[condition] < W8_CONDITION_INDEFINITE) {
            TickCharacterCondition(party_slot, static_cast<W8Condition>(condition), minutes);
        }
    }
    for (unsigned int slot = 0; slot < 8; ++slot) {
        unsigned int turns = character->enchantments[slot].turns;
        if (turns != 0 && turns < W8_CONDITION_INDEFINITE) {
            if (turns > minutes) {
                character->enchantments[slot].turns = turns - minutes;
            } else {
                ClearCharacterEnchantmentSlot(party_slot, static_cast<W8EnchantmentSlot>(slot));
            }
        }
    }

    if (CharacterHasTrait(character, W8_TRAIT_MAKE_POTIONS)) {
        if (CurrentGame().surprise_possible) {
            if (character->potion_brew_cooldown == 0) {
                BrewAlchemistPotion(CurrentGame(), character);
            }
        } else if (character->potion_brew_cooldown != 0) {
            if (minutes >= character->potion_brew_cooldown) {
                character->potion_brew_cooldown = 0;
            } else {
                character->potion_brew_cooldown = character->potion_brew_cooldown - minutes;
            }
        }
    }
}

/* Moves a monster group to a position, then each allied group that exists. */
static void MoveMonsterGroupAndAllies(W8MonsterGroup* group, glm::vec3* position, float yaw)
{
    MoveMonsterGroupToPosition(group, position, yaw, false, false, false, false);
    for (const int ally_id : group->allied_group_ids) {
        if (ally_id != 0) {
            auto* ally = FindMonsterGroup(ally_id);
            MoveMonsterGroupToPosition(ally, position, yaw, false, false, false, false);
        }
    }
}

/* Out-of-combat movement aging: stay-home monsters drift back to their
   formation point, wanderers advance their look/pause timers. Combat only
   ticks the combat effect slots. */
static void AgeMonsterMovement(W8MonsterInfo* monster_info, unsigned int minutes)
{
    Monster* monster;

    if (monster_info->pCombat) {
        W8CombatSlot target;
        target.iType = static_cast<W8TargetKind>(3);
        target.iMonsterID = monster_info->location_id;
        TickCombatEffectSlots(monster_info->pCombat->combat_effects, &target);
        return;
    }
    monster = monster_info->p3D;
    if (monster->stay_home) {
        glm::vec3 location;
        glm::vec3 last_seen;
        glm::vec3 delta;

        location = monster->GetRepresentationLocation();
        if (monster->formation.x == 0.0f && monster->formation.y == 0.0f &&
            monster->formation.z == 0.0f) {
            monster->formation = monster->nav.GetPosition();
        }
        last_seen = monster->formation;
        delta = last_seen - location;
        if (glm::length(delta) > 500.0f) {
            glm::vec3 probe = last_seen;
            glm::vec3 camera;

            probe.y += 1e+03f;
            camera = cameraLocation();
            if (projectsToCamera(probe) == 0 &&
                !GetWorldSpatialIndex()->hasLineOfSight(camera, probe)) {
                glm::vec3 notify_position = last_seen;
                W8MonsterGroup* group = FindMonsterGroup(monster_info->monster_group_id);

                MoveMonsterGroupAndAllies(group, &notify_position, monster->nav.GetYaw());
            }
        }
        return;
    }
    if (GetViewDistance() != g_sight_default) {
        return;
    }
    if ((monster->nav.linked_navigator == 0 && !monster->nav.halted) &&
        (monster_info->movement_stall_ticks > 1 || !monster->nav.movement_stopped)) {
        glm::vec3 location;
        glm::vec3 previous;
        glm::vec3 delta;
        unsigned char cycle;

        location = monster->GetRepresentationLocation();
        previous = monster_info->movement_watch_position;
        monster_info->position.y = location.y;
        cycle = monster_info->movement_stall_ticks;
        delta = location - previous;
        monster_info->position.x = location.x;
        monster_info->position.z = location.z;
        if (static_cast<signed char>(cycle) > 1) {
            bool cycle_cleared = false;
            bool flags_cleared = false;

            if (static_cast<signed char>(cycle) < 4) {
                bool cleared = false;

                if (glm::length(delta) < 500.0f) {
                    glm::vec3 navigator_position = monster->nav.GetPosition();

                    /* Retail resets the AI mode and pathing cooldown only once the
                       short-move test passes; a long move leaves both alone. */
                    flags_cleared = true;

                    if (!GetWorldPathing()->SnapWaypointPosition(&navigator_position, false) &&
                        !monster_info->party_threat.visible_to_player) {
                        glm::vec3 next_position;

                        next_position = monster->nav.movement.attachment->GetNextPosition();
                        if (glm::length(next_position) == static_cast<float>(0.0)) {
                            next_position = monster->nav.GetPosition();
                        }
                        {
                            glm::vec3 camera;
                            glm::vec3 probe = next_position;

                            camera = cameraLocation();
                            if (projectsToCamera(probe) == 0 &&
                                !GetWorldSpatialIndex()->hasLineOfSight(camera, probe)) {
                                glm::vec3 notify_position = next_position;
                                W8MonsterGroup* group =
                                    FindMonsterGroup(monster_info->monster_group_id);

                                MoveMonsterGroupAndAllies(group, &notify_position,
                                                          monster->nav.GetYaw());
                                cleared = true;
                            }
                        }
                    }
                }
                cycle_cleared = cleared;
            } else {
                glm::vec3 camera;
                glm::vec3 probe;
                glm::vec3 location2;

                camera = cameraLocation();
                location2 = monster->nav.GetPosition();
                probe = location2;
                probe.y += 1e+03f;
                /* Both on-screen tests take the representation location read at the top of
                   the watch block, not the navigator position (0x00503e90, 0x00503f40). */
                if (projectsToCamera(location) == 0 &&
                    !GetWorldSpatialIndex()->hasLineOfSight(camera, probe)) {
                    if (monster->formation.x == 0.0f && monster->formation.y == 0.0f &&
                        monster->formation.z == 0.0f) {
                        monster->formation = monster->nav.GetPosition();
                    }
                    probe = monster->formation;
                    probe.y += 1e+03f;
                    if (projectsToCamera(location) == 0 &&
                        !GetWorldSpatialIndex()->hasLineOfSight(camera, probe)) {
                        if (monster->formation.x == 0.0f && monster->formation.y == 0.0f &&
                            monster->formation.z == 0.0f) {
                            monster->formation = monster->nav.GetPosition();
                        }
                        {
                            glm::vec3 notify_position = monster->formation;
                            W8MonsterGroup* group =
                                FindMonsterGroup(monster_info->monster_group_id);

                            MoveMonsterGroupAndAllies(group, &notify_position,
                                                      monster->nav.GetYaw());
                        }
                        cycle_cleared = true;
                        flags_cleared = true;
                    }
                }
            }
            if (cycle_cleared) {
                monster_info->movement_stall_ticks = 0;
            }
            if (flags_cleared) {
                monster_info->ai_mode = W8_RT_AI_IDLE;
                monster_info->pathing_cooldown = 0;
            }
        }
        if (monster_info->movement_stall_ticks > 0) {
            ++monster_info->movement_stall_ticks;
        }
        monster_info->movement_watch_position = monster_info->position;
    }

    /* The monster alternates between a looking spell and a pause; each timer
       counts down in minutes and, on expiry, rolls the other around half the
       monster's configured duration or frequency (never 0). */
    if (monster_info->look_time != 0) {
        if (minutes < monster_info->look_time) {
            monster_info->look_time -= static_cast<unsigned char>(minutes);
        } else {
            int duration = monster_info->p3D->look_duration;

            monster_info->look_time = 0;
            monster_info->pause_time = static_cast<unsigned char>(
                Random(static_cast<unsigned int>(duration)) + duration / 2);
            if (monster_info->pause_time == 0) {
                monster_info->pause_time = 1;
            }
        }
    } else if (monster_info->pause_time != 0) {
        if (minutes < monster_info->pause_time) {
            monster_info->pause_time -= static_cast<unsigned char>(minutes);
        } else {
            int frequency = monster_info->p3D->look_frequency;

            monster_info->pause_time = 0;
            monster_info->look_time = static_cast<unsigned char>(
                Random(static_cast<unsigned int>(frequency)) + frequency / 2);
            if (monster_info->look_time == 0) {
                monster_info->look_time = 1;
            }
        }
    }
}

/* Advance one monster's whole aging cycle by the elapsed minutes: the
   look-around timers, the sight bookkeeping around its last-seen position,
   the per-turn regeneration and fatigue bookkeeping, condition, enchantment
   and effect countdowns, and finally the combat effect slots. */
void AgeMonsterSight(W8MonsterInfo* monster_info, unsigned int minutes,
                     unsigned char full_health_regeneration)
{
    bool frost_condition = false;

    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    AgeMonsterMovement(monster_info, minutes);
    {
        unsigned int amount = monster_info->modifiers.damage_per_minute;

        if (amount != 0) {
            W8TargetSource source;

            amount *= minutes;
            if (monster_info->pCombat) {
                amount += amount >> 1;
            }
            ResetTargetSource(&source);
            ApplyDamageToMonster(monster_info, amount, &source, true, CurrentGame().combat.active(),
                                 0, 0, false);
        }
    }
    if (monster_info->uiCondition[W8_CONDITION_DISEASED] != 0) {
        frost_condition = true;
    }
    {
        W8MonsterRecord* data = GetMonsterDataForInfo(monster_info);
        int amount = (static_cast<int>(data->hp_regeneration) +
                      monster_info->modifiers.health_regen_adjustment) *
                     static_cast<int>(minutes);

        if (amount < 1) {
            if (amount < 0) {
                W8TargetSource source;

                ResetTargetSource(&source);
                ApplyDamageToMonster(monster_info, -amount, &source, false, 0, 0, 0, false);
            }
        } else if (monster_info->hp_current < static_cast<unsigned int>(monster_info->uiHPMax)) {
            HealMonster(monster_info, amount, false);
        }
    }
    {
        int amount = (monster_info->modifiers.stamina_regen_adjustment +
                      static_cast<int>(record->stamina_regeneration)) *
                     static_cast<int>(minutes);

        if (amount < 1) {
            if (amount < 0) {
                FatigueMonster(monster_info, -amount, 0);
            }
        } else if (monster_info->stamina < monster_info->stamina_max) {
            RestoreMonsterStamina(monster_info, amount, false);
        }
    }
    {
        float heal_scale;

        if (!CurrentGame().surprise_possible && full_health_regeneration == 0) {
            if (!monster_info->pCombat) {
                heal_scale = 0.5f;
            } else {
                heal_scale = 0.0f;
            }
        } else {
            heal_scale = 1.0f;
        }
        if (frost_condition) {
            heal_scale *= g_navigator_vertical_phase_step;
        }
        if (heal_scale > 0.0f) {
            if (monster_info->hp_current < static_cast<unsigned int>(monster_info->uiHPMax)) {
                monster_info->hp_regen_accumulator =
                    minutes * monster_info->hp_regen_rate * heal_scale +
                    monster_info->hp_regen_accumulator;
                int healed = static_cast<int>(monster_info->hp_regen_accumulator);

                HealMonster(monster_info, healed, false);
                monster_info->hp_regen_accumulator -= static_cast<float>(healed);
            }
            if (monster_info->stamina < monster_info->stamina_max) {
                monster_info->stamina_regen_accumulator =
                    minutes * monster_info->stamina_regen_rate * heal_scale +
                    monster_info->stamina_regen_accumulator;
                int restored = static_cast<int>(monster_info->stamina_regen_accumulator);

                RestoreMonsterStamina(monster_info, restored, false);
                monster_info->stamina_regen_accumulator -= static_cast<float>(restored);
            }
        }
    }
    for (int condition = 0; condition < 0x14; ++condition) {
        if (monster_info->uiCondition[condition] != 0 &&
            monster_info->uiCondition[condition] < W8_CONDITION_INDEFINITE) {
            TickMonsterCondition(monster_info->location_id, static_cast<W8Condition>(condition),
                                 minutes);
        }
    }
    for (int enchant = 0; enchant < 8; ++enchant) {
        float remaining = static_cast<float>(monster_info->enchantments[enchant].turns);

        if (remaining != 0.0f && static_cast<unsigned int>(remaining) < W8_CONDITION_INDEFINITE) {
            if (minutes < static_cast<unsigned int>(remaining)) {
                monster_info->enchantments[enchant].turns =
                    static_cast<int>(remaining) - static_cast<int>(minutes);
            } else {
                ClearMonsterEnchantmentSlot(monster_info->location_id,
                                            static_cast<W8EnchantmentSlot>(enchant));
            }
        }
    }
    if (static_cast<unsigned int>(monster_info->uiHPMax) <= monster_info->hp_current) {
        monster_info->hp_regen_accumulator = 0.0f;
    }
    if (monster_info->stamina_max <= monster_info->stamina) {
        monster_info->stamina_regen_accumulator = 0.0f;
    }
    {
        W8EffectSlot* slot = monster_info->effect_slots;

        for (int owned_index = 0; owned_index < 0xc; ++owned_index) {
            if (slot->active) {
                if (minutes < slot->duration) {
                    slot->duration -= minutes;
                } else if (monster_info == 0) {
                    ResetPartyEffectBlock(slot);
                } else {
                    ClearEffectSlot(monster_info, slot);
                }
            }
            ++slot;
        }
    }
    if (monster_info->pCombat) {
        for (auto& combat_effect : monster_info->pCombat->combat_effects) {
            W8EffectSlot* slot = &combat_effect;

            if (slot->active) {
                if (minutes < slot->duration) {
                    slot->duration -= minutes;
                } else if (monster_info == 0) {
                    ResetPartyEffectBlock(slot);
                } else {
                    ClearEffectSlot(monster_info, slot);
                }
            }
        }
        for (auto& d7_index : monster_info->pCombat->combat_effects_2) {
            W8EffectSlot* slot = &d7_index;

            if (slot->active) {
                if (minutes < slot->duration) {
                    slot->duration -= minutes;
                } else if (monster_info == 0) {
                    ResetPartyEffectBlock(slot);
                } else {
                    ClearEffectSlot(monster_info, slot);
                }
            }
        }
    }
}

/* The camping fatigue tick: while a camp is active each character without
   the rest item rolls fatigue dice whose sides grow with camp_fatigue_count;
   stamina absorbs the roll first, overflow becomes damage. The first roll
   raises the fatigued flag and posts the notice once. */
void UpdateCampFatigue(int ticks)
{
    if (CurrentGame().status.world_suspended) {
        return;
    }
    bool any_rolled = false;
    for (unsigned int slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CanPartySlotParticipate(slot) && character->iRace != W8_RACE_ANDROID &&
            !FindCharacterItem(*character, ItemWithId(0x1e5), W8ItemReach::Worn)) {
            W8Dice dice;
            dice.count = static_cast<unsigned char>(ticks);
            dice.sides =
                static_cast<unsigned char>(CurrentGame().status.camp_fatigue_count / 6) + 2;
            any_rolled = true;
            dice.base = 0;
            unsigned int amount = static_cast<unsigned int>(RollDice(&dice));
            if (amount != 0) {
                int stamina = character->stamina;
                if (stamina < static_cast<int>(amount)) {
                    if (stamina > 0) {
                        amount -= stamina;
                        FatigueCharacter(slot, stamina, false,
                                         static_cast<W8SpellEffectResult*>(0));
                    }
                    bool announce =
                        !CurrentGame().combat.active() || g_settings.verbose_combat_messages == 0
                            ? 0
                            : 1;
                    ApplyDamageToCharacter(slot, amount, false, announce, false,
                                           static_cast<W8SpellEffectResult*>(0), false);
                } else {
                    FatigueCharacter(slot, static_cast<int>(amount), false,
                                     static_cast<W8SpellEffectResult*>(0));
                }
            }
            if (!CurrentGame().status.party_fatigued) {
                CurrentGame().status.party_fatigued = true;
                ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x1da]);
            }
        }
    }
    if (any_rolled) {
        CurrentGame().status.camp_fatigue_count += ticks;
        return;
    }
    CurrentGame().status.camp_fatigue_count = 0;
    CurrentGame().status.party_fatigued = false;
    CurrentGame().status.camp_tick_ms = 0;
}

/* The stamina-tick driver: refreshes the wait state from fast movement and
   motion in the last update, then ticks each eligible character. While the party is
   fatigued, characters lacking the rest item are skipped. */
void UpdatePartyStamina(int ticks)
{
    if (IsLevelFastMovement()) {
        CurrentGame().status.wait_state = W8_GAME_WAIT_STATE_FAST_MOVEMENT;
    } else {
        CurrentGame().status.wait_state =
            LevelMovedThisUpdate() ? W8_GAME_WAIT_STATE_MOVING : W8_GAME_WAIT_STATE_RESTING;
    }

    for (unsigned int slot = 0; slot < 8; ++slot) {
        if (!CurrentGame().party.rows[slot].fOccupied) {
            continue;
        }
        W8Character* character = &CurrentGame().party.characters[slot];
        if (character->highest_condition >= W8_CONDITION_DEAD &&
            (character->uiCondition[W8_CONDITION_DEAD] != 0 ||
             !GetConditionRecordFlag(slot, W8_DEPENDENCE_SWALLOWED))) {
            continue;
        }
        if (CurrentGame().status.party_fatigued &&
            !FindCharacterItem(*character, ItemWithId(0x1e5), W8ItemReach::Worn)) {
            continue;
        }
        RegenCharacterStamina(slot, static_cast<unsigned int>(ticks));
    }
}

/* One character's stamina tick: the bonus block's signed regen modifier is
   applied directly, then the fractional regen accumulator is scaled by the
   wait state (camp halved, resting states reduced, surprise full), frost
   quarters it and the trait rerolls it through the profession level. */
void RegenCharacterStamina(int party_slot, unsigned int elapsed)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int frost = character->uiCondition[W8_CONDITION_DISEASED];
    signed char stamina_mod = character->bonus.stamina_regen_adjustment;
    if (stamina_mod < 1) {
        if (stamina_mod < 0) {
            FatigueCharacter(party_slot, -static_cast<int>(stamina_mod * elapsed), false,
                             static_cast<W8SpellEffectResult*>(0));
        }
    } else if (character->stamina < character->uiStaminaMax) {
        RestoreCharacterStamina(party_slot, stamina_mod * static_cast<int>(elapsed), false);
    }

    float scale = 1.0f;
    if (!CurrentGame().surprise_possible) {
        scale = 0.5f;
        if (CurrentGame().status.wait_state != W8_GAME_WAIT_STATE_RESTING) {
            scale = 0.1f;
            if (CurrentGame().status.wait_state != W8_GAME_WAIT_STATE_MOVING &&
                CurrentGame().status.wait_state != W8_GAME_WAIT_STATE_INITIAL_REST) {
                scale = 0.0f;
            }
        }
    }
    if (frost != 0) {
        scale *= g_navigator_vertical_phase_step;
    }
    if (CharacterHasTrait(character, W8_TRAIT_STAMINA_REGENERATION)) {
        if (scale == 0.0f) {
            if (CurrentGame().combat.active()) {
                scale = ScaleValueByProfessionLevel(character, W8_TRAIT_STAMINA_REGENERATION, 3.3f);
            }
        } else {
            scale *= 1.5f;
        }
    }
    if (scale > 0.0f && character->stamina < character->uiStaminaMax) {
        character->stamina_regen_accumulator += elapsed * character->stamina_regen_rate * scale;
        int amount = static_cast<int>(character->stamina_regen_accumulator);
        RestoreCharacterStamina(party_slot, amount, false);
        character->stamina_regen_accumulator -=
            static_cast<float>(static_cast<int>(character->stamina_regen_accumulator));
    }
    if (character->stamina >= character->uiStaminaMax) {
        character->stamina_regen_accumulator = 0.0f;
    }
}
