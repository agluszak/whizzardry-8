#include "wiz8/local_code/GameplayCode.h"

#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/printf.h>

#include "layouts/levels.h"
#include "local_code/Factions.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/records.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/message_box.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"

/* One entry per sex, race and profession together. */
static int g_character_table[480] = {
    3,  1,  0,  0,  3,  56, 4,  2,  1,  2,  5,  4,  24, 4,  5,  13, 12, 0,  12, 13, 56, 13, 12, 12,
    12, 14, 14, 14, 14, 14, 19, 19, 0,  19, 19, 56, 20, 20, 20, 20, 18, 18, 18, 18, 18, 24, 24, 0,
    24, 24, 56, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 0,  28, 28, 56, 28, 28, 29, 29, 29, 29,
    29, 29, 29, 32, 32, 0,  32, 32, 56, 32, 32, 33, 33, 33, 33, 33, 33, 33, 36, 36, 0,  36, 36, 56,
    36, 36, 37, 37, 37, 37, 37, 37, 37, 40, 40, 0,  40, 40, 56, 41, 40, 41, 40, 41, 41, 41, 41, 41,
    45, 45, 0,  45, 45, 56, 44, 44, 44, 44, 44, 44, 44, 44, 44, 48, 48, 0,  48, 48, 56, 48, 48, 49,
    49, 49, 49, 49, 49, 49, 52, 52, 0,  52, 52, 56, 52, 52, 53, 53, 53, 53, 53, 53, 53, 0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    9,  11, 9,  10, 9,  57, 10, 11, 7,  7,  7,  8,  8,  8,  8,  15, 15, 15, 17, 15, 57, 17, 17, 16,
    16, 17, 16, 16, 17, 16, 22, 21, 22, 22, 22, 57, 22, 21, 21, 23, 23, 23, 23, 23, 23, 26, 26, 26,
    26, 26, 57, 26, 26, 27, 27, 27, 27, 27, 27, 27, 30, 30, 30, 30, 30, 57, 30, 30, 31, 31, 31, 31,
    31, 31, 31, 35, 35, 35, 35, 35, 57, 35, 35, 34, 34, 34, 34, 34, 34, 34, 38, 38, 38, 38, 38, 57,
    38, 38, 39, 39, 39, 39, 39, 39, 39, 42, 42, 42, 42, 42, 57, 42, 42, 43, 43, 43, 43, 43, 43, 43,
    47, 47, 47, 47, 47, 57, 47, 47, 46, 46, 46, 46, 46, 46, 46, 50, 50, 50, 50, 50, 57, 50, 50, 51,
    51, 51, 51, 51, 51, 51, 55, 55, 55, 55, 55, 57, 55, 55, 52, 52, 52, 52, 52, 52, 52, 0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};

/* Whether any monster is engaged with the party right now: in combat, in the
   engaged state, still alive and not yet on its way out. */
bool AnyMonsterEngaged(void)
{
    unsigned int index;
    const W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat && monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
            monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_WEBBED) {
            return true;
        }
    }
    return false;
}

/* How many party members are still on their feet. */
int CountActiveCharacters(void)
{
    int count = 0;
    int party_slot;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CanPartySlotParticipate(party_slot)) {
            ++count;
        }
    }
    return count;
}

/* Whether anybody is. */
bool AnyCharacterActive(void)
{
    return CountActiveCharacters() != 0;
}

/* Walk a character up to a level from scratch, recomputing the experience goal
   at each step so the goals for the level below are the ones they actually
   passed. */
void AdvanceCharacterToLevel(W8Character* character, unsigned int level)
{
    character->uiExpLevel = 1;
    character->experience_previous_goal = 0;
    CalcXPGoal(character);

    while (character->uiExpLevel < level) {
        ++character->uiExpLevel;
        character->experience_previous_goal = character->experience_goal;
        CalcXPGoal(character);
    }
    character->experience = character->experience_previous_goal;
}

/* The next level's goal: the goal the previous level had plus the profession's
   weight, doubled for every level up to ten and grown twenty percent per level
   above it. */
void CalcXPGoal(W8Character* character)
{
    int weight;
    unsigned int value;

    assert(character != 0);
    assert(character->uiExpLevel != 0);
    assert(character->iProfession >= W8_PROFESSION_FIGHTER &&
           character->iProfession < W8_PROFESSION_COUNT);
    switch (character->iProfession) {
    case W8_PROFESSION_FIGHTER:
    case W8_PROFESSION_ROGUE:
    case W8_PROFESSION_GADGETEER:
    case W8_PROFESSION_BARD:
        weight = 1000;
        break;
    case W8_PROFESSION_PRIEST:
    case W8_PROFESSION_ALCHEMIST:
    case W8_PROFESSION_PSIONIC:
    case W8_PROFESSION_MAGE:
        weight = 0x4b0;
        break;
    case W8_PROFESSION_LORD:
    case W8_PROFESSION_VALKYRIE:
    case W8_PROFESSION_RANGER:
    case W8_PROFESSION_SAMURAI:
    case W8_PROFESSION_MONK:
        weight = 0x578;
        break;
    case W8_PROFESSION_NINJA:
    case W8_PROFESSION_BISHOP:
        weight = 0x640;
        break;
    default:
        return;
    }

    if (character->uiExpLevel == 1) {
        character->experience_goal = character->experience_previous_goal + weight;
        return;
    }
    if (character->uiExpLevel <= 10) {
        character->experience_goal = character->experience_previous_goal +
                                     IntegerPower(2, character->uiExpLevel - 2) * weight;
        return;
    }
    value = IntegerPower(2, 8) * weight;
    for (unsigned int current = 10; current < character->uiExpLevel; ++current) {
        value = value * 12 / 10;
    }
    character->experience_goal = character->experience_previous_goal + value;
}

/* Refresh which party members are ready to level up: raise the pending flag,
   post a notice the first time each ready slot is seen, and clear the marker
   when a slot is no longer ready. Skipped while greeting_pending is set or the saved
   level is in band 0xe. */
void RefreshLevelUpReadyNotices(void)
{
    int party_slot;

    if (CurrentGame().status.greeting_pending || GetLevelBand(GetLoadedLevelID()) == 0xe) {
        return;
    }

    CurrentGame().level_up_check_due = false;
    for (party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        bool* ready_flag = &CurrentGame().monster_manager_entries[party_slot].level_up_ready;
        W8Character* character = &CurrentGame().party.characters[party_slot];

        if (!CurrentGame().party.members[party_slot].has_value()) {
            if (character->hp_current != 0) {
                if (*ready_flag != 0) {
                    CurrentGame().events.push(
                        PartySlotChanged{static_cast<unsigned int>(party_slot)});
                }
                *ready_flag = 0;
            }
        } else if (character->hp_current != 0) {
            if (character->highest_condition > W8_CONDITION_UNCONSCIOUS ||
                character->experience < character->experience_goal) {
                if (character->hp_current != 0) {
                    if (*ready_flag != 0) {
                        CurrentGame().events.push(
                            PartySlotChanged{static_cast<unsigned int>(party_slot)});
                    }
                    *ready_flag = 0;
                    CurrentGame().party.members[party_slot]->level_up_announced = false;
                }
            } else {
                CurrentGame().level_up_check_due = true;
                if (*ready_flag == 0) {
                    if (!CurrentGame().party.members[party_slot]->level_up_announced) {
                        std::string text = " ";
                        wiz8::text::append(text, 0xb4);
                        wiz8::text::append(text,
                                           GetPartyOrderTextColor(static_cast<signed char>(
                                               CurrentGame().party.orderIndexOf(party_slot))));
                        text += " " + character->name + " ";
                        wiz8::text::append(text, 0xb5);
                        text += " ";
                        text = fmt::sprintf(Data().strings[0x773], text);
                        *ready_flag = 1;
                        CurrentGame().npc_dialogue.QueuePortraitMessage(
                            std::move(text), W8LevelUpNotice{party_slot});
                        CurrentGame().npc_dialogue.Queue(W8NpcLevelUpReaction{party_slot});
                    } else {
                        *ready_flag = 1;
                    }
                } else if (CurrentGame().party.members[party_slot]->level_up_announced) {
                    *ready_flag = 1;
                }
            }
        }

        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
            EnablePortraitAdvanceRegions();
        }
    }
}

/* Whether one party slot has earned its next level: occupied, alive, in shape
   to act, and holding at least the experience the next level asks for. */
bool IsCharacterReadyToAdvance(int party_slot)
{
    const W8Character* character = &CurrentGame().party.characters[party_slot];

    if (!CurrentGame().party.members[party_slot].has_value()) {
        return false;
    }
    if (character->hp_current == 0) {
        return false;
    }
    if (character->highest_condition > W8_CONDITION_UNCONSCIOUS) {
        return false;
    }
    return character->experience >= character->experience_goal;
}

/* The first free party slot in a range, or -1 when the range is full. */
unsigned int FindFreePartySlot(unsigned int first, unsigned int last)
{
    unsigned int slot;

    for (slot = first; slot < last; ++slot) {
        if (!CurrentGame().party.members[slot].has_value()) {
            return slot;
        }
    }
    return static_cast<unsigned int>(-1);
}

/* Recompute the eight-band ladder over the character's level in their current
   profession. A character who has changed profession is banded on the whole
   level; one still in their first profession has the starting base taken off
   first. */
void CalcCharacterLevelBand(W8Character* character)
{
    unsigned int level = character->profession_levels[character->iProfession];

    if (character->iProfession == character->original_profession) {
        level -= character->level_band_base;
    }

    if (level == 0) {
        character->level_band = 0;
    } else if (level == 1) {
        character->level_band = 1;
    } else if (level < 4) {
        character->level_band = 2;
    } else if (level < 7) {
        character->level_band = 3;
    } else if (level < 0xb) {
        character->level_band = 4;
    } else if (level < 0x10) {
        character->level_band = 5;
    } else if (level < 0x16) {
        character->level_band = 6;
    } else {
        character->level_band = (level > 0x1b) + 7;
    }
}

/* Look up the value that sex, race and profession together select. */
void CalcCharacterTableValue(W8Character* character)
{
    assert(character->iRace <= W8_RACE_MOOK);
    character->portrait_index =
        g_character_table[(character->gender * 0x10 + character->iRace) * W8_PROFESSION_COUNT +
                          character->iProfession];
}

static char s_fall_impact_wav[] = "Data\\Sound\\Misc\\Fall Impact.wav";

/* Level-motion override landing: the accumulated fall magnitude becomes
   pow(fall + 0.7, 8.0) six-sided dice of damage against the whole party,
   with a notice and the fall-impact sound. The eighth power keeps a short
   drop to a die or two and lets a long one grow steeply. */
void HandleLevelOverride(float fall)
{
    unsigned int count = static_cast<unsigned int>(pow(fall + 0.7, 8.0));
    if (count > 0) {
        ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x252]);
        wiz8::sound::play(s_fall_impact_wav, {});
        W8Dice dice;
        SetDice(&dice, static_cast<unsigned char>(count), 6, 0);
        ApplyRolledHealthChangeToParty(&dice, 0, 1);
    }
}

/* Personality and voice values by sex and profession class,
   two dwords per row. It ends exactly where the sex/race/profession table
   at 0x00616604 begins. */
static int g_character_value_table[34][2] = {
    {0, 2}, {0, 1}, {6, 1}, {0, 1}, {0, 1}, {2, 2}, {0, 1}, {8, 1}, {7, 2}, {7, 2}, {7, 2}, {7, 2},
    {7, 2}, {7, 2}, {4, 1}, {4, 1}, {1, 1}, {5, 2}, {7, 1}, {6, 2}, {6, 2}, {6, 1}, {3, 2}, {3, 2},
    {5, 1}, {1, 1}, {1, 2}, {5, 2}, {1, 1}, {1, 2}, {2, 1}, {2, 1}, {2, 2}, {2, 2}};

/* Derive the character's personality and voice from sex and profession.
   Unaligned characters pick a class through the race shortcut first. */
void DeriveCharacterPersonality(W8Character* character)
{
    int gender = character->gender;
    int value = character->iProfession;
    int index;
    int flag;

    if (gender == 0) {
        switch (value) {
        case 0:
        case 1:
        case 3:
        case 7:
            if (character->iRace == W8_RACE_DWARF) {
                value = 0x10;
            } else if (character->iRace == W8_RACE_LIZARDMAN) {
                value = 0x0f;
            }
            break;
        }
    }
    index = gender + value * 2;
    flag = g_character_value_table[index][1];
    character->personality = g_character_value_table[index][0];
    if (flag == 1) {
        character->voice = 0;
    } else {
        character->voice = 1;
    }
    character->unknown_007d = 0;
}

/* Re-roll the character's voice until no other in-party character shares the
   same gender/personality/voice triple. The first clash only flips the voice;
   further clashes re-roll both personality and voice. */
void EnsureUniquePartyVoice(W8Character* character)
{
    W8Character* other;
    unsigned int slot;
    int attempts = 0;

    for (;;) {
        slot = 0;
        other = CurrentGame().party.characters;
        for (;;) {
            if (other->fInParty && other != character && other->gender == character->gender &&
                other->personality == character->personality && other->voice == character->voice) {
                break;
            }
            ++slot;
            ++other;
            if (slot > 7) {
                return;
            }
        }
        if (attempts == 0) {
            attempts = 1;
            character->voice = (character->voice == 0);
        } else {
            character->personality = CurrentGame().rng.below(9);
            character->voice = CurrentGame().rng.below(2);
            ++attempts;
        }
    }
}

/* Add one character to the party: find a free slot in the requested band,
   copy the record, mark it in party, rebuild its row and formation position
   and enter it in the marching order. The band is 2..7 for a regular member
   and 0..1 for the two auxiliary slots. */
int AddCharacterToParty(W8Character& character, int slot_kind)
{
    unsigned int slot;

    if (slot_kind == -1) {
        slot = 2;
        while (CurrentGame().party.members[slot].has_value()) {
            ++slot;
            if (slot > 7) {
                return -1;
            }
        }
    } else {
        slot = 0;
        while (CurrentGame().party.members[slot].has_value()) {
            ++slot;
            if (slot > 1) {
                return -1;
            }
        }
    }
    if (static_cast<int>(slot) < 0) {
        return -1;
    }

    W8Character* destination = &CurrentGame().party.characters[slot];
    *destination = character;
    destination->fInParty = true;
    ResetPartySlotRow(slot);
    ResetGameplaySlot(slot);

    CurrentGame().party.members[slot]->npc_index = slot_kind;
    for (unsigned int index = 0; index < 8; ++index) {
        if (CurrentGame().party.order_slots[index] == static_cast<unsigned int>(-1)) {
            CurrentGame().party.order_slots[index] = slot;
            CurrentGame().party.members[slot]->order_index = index;
            break;
        }
    }
    PlaceCharacterInFormation(&CurrentGame().party.formation, slot);
    CurrentGame().party.formation.positions[slot].revival_row = -1;

    if (GameStarted()) {
        CurrentGame().planned_formation.positions[slot].revival_row = -1;
        PostCharacterNotice(slot, Data().strings[0x250]);
    }

    RecalculateCharacterDerivedStats(CurrentGame(), *destination);
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        MainGame()->refreshPartySlotRegions();
    }
    return slot;
}

/* Remove one character from the party: drop its queued events, clear the row,
   its formation positions and marching-order entry, and fix the member counts
   and selection. */
void RemoveCharacterFromParty(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    CurrentGame().speech.RemoveCharacterEvents(character);
    character->fInParty = false;
    character->highest_condition = W8_CONDITION_NONE;
    character->enchantment_top = W8_ENCHANTMENT_NONE;
    SetFormationPosition(&CurrentGame().party.formation, party_slot, std::nullopt, false, true,
                         true);
    if (CurrentGame().combat.has_value()) {
        SetFormationPosition(&CurrentGame().planned_formation, party_slot, std::nullopt, false,
                             true, true);
        SetFormationPosition(&CurrentGame().combat->saved_formation, party_slot, std::nullopt,
                             false, true, true);
    }
    if (GameStarted()) {
        PostCharacterNotice(party_slot, Data().strings[0x251]);
    }
    // The departing member's notice still names it in its marching-order colour.
    auto& member = CurrentGame().party.members[party_slot];
    if (member) CurrentGame().party.order_slots[member->order_index] = -1;
    member.reset();
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        MainGame()->refreshPartySlotRegions();
    }
    if (CurrentGame().status.selected_character == party_slot) {
        CurrentGame().status.selected_character = GetNextCharacter(1, 1, -1);
    }
}

/* Install a finished character record over a party slot: everything the old
   record still carries goes to the party pool, the slot is cleared out, the
   record is copied in and the row re-enters the marching order and formation
   as a regular member. When the caller pays for it, the starting equipment is
   bought out of the party gold the way a new recruit would bring it. */
unsigned char RecruitCharacterIntoParty(W8Character* character, W8Character* record,
                                        bool buy_equipment)
{
    unsigned int slot = CharacterPointerToPartySlot(character);
    int index;
    unsigned int order_index;

    for (index = 0; index < 12; ++index) {
        if (character->EquippedItem[index].iItemNo != -1) {
            CurrentGame().inventory.storeInPool(character->EquippedItem[index], false);
        }
    }
    for (index = 0; index < 8; ++index) {
        if (character->backpack[index].iItemNo != -1) {
            CurrentGame().inventory.storeInPool(character->backpack[index], false);
        }
    }
    RemoveCharacterFromParty(slot);
    *character = *record;
    character->fInParty = true;
    ResetPartySlotRow(slot);
    ResetGameplaySlot(slot);
    CurrentGame().party.members[slot]->npc_index = -1;
    for (order_index = 0; order_index < 8; ++order_index) {
        if (CurrentGame().party.order_slots[order_index] == static_cast<unsigned int>(-1)) {
            CurrentGame().party.order_slots[order_index] = slot;
            CurrentGame().party.members[slot]->order_index = order_index;
            break;
        }
    }
    PlaceCharacterInFormation(&CurrentGame().party.formation, slot);
    CurrentGame().party.formation.positions[slot].revival_row = -1;
    RecalculateCharacterDerivedStats(CurrentGame(), *character);
    if (buy_equipment) {
        unsigned int cost = ComputeStartingEquipmentCost(CurrentGame(), character);
        assert(CurrentGame().party.gold >= cost);
        CurrentGame().party.gold -= cost;
        AddCharacterStartingEquipment(CurrentGame(), character);
    }
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        MainGame()->refreshPartySlotRegions();
    }
    return slot;
}

// FUNCTION: WIZ8 0x004EEF10
void AwardPartyExperience(int amount, bool alternate_message)
{
    for (int slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CanPartySlotParticipate(slot) && amount != 0) {
            unsigned int total = character->experience + static_cast<unsigned int>(amount);
            if (total > character->experience) {
                character->experience = total;
            } else {
                character->experience = static_cast<unsigned int>(-1);
            }
            if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT) {
                auto& progress = CurrentGame().status.level_progress[GetLoadedLevelID()];
                const unsigned int gained = progress.experience_gained;
                total = gained + static_cast<unsigned int>(amount);
                if (total > gained) {
                    progress.experience_gained = total;
                }
            }
            CurrentGame().level_up_check_due = true;
        }
    }
    auto text = fmt::sprintf(Data().strings[alternate_message ? 0x231 : 0x232], amount);
    CurrentGame().npc_dialogue.QueuePortraitMessage(
        std::move(text),
        W8ExperienceNoticePayload{static_cast<unsigned int>(amount), alternate_message});
}
