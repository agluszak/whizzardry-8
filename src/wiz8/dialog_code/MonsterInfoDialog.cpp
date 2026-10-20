#include "wiz8/dialog_code/MonsterInfoDialog.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "dialog_code/DialogBase.h"
#include "dialog_code/DialogScrollBar.h"
#include "dialog_code/DialogTextArea.h"
#include "local_code/Factions.h"
#include "monster_actions.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/SpellInfoDialog.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/script.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"

/* The background and scroll track the monster and statistic info dialogs
   load, read through this pointer. */
const char* g_info_dialog_background = "Data\\Dialogs\\popup_monsterinfo.sti";

W8MonsterInfoDialog::W8MonsterInfoDialog(int location_id) : m_location_id(location_id)
{
    SetOrigin(0x9c, 0x31);
    SetExtent(0x14a, 0x10e);
    SetBackground(g_info_dialog_background, 0);
    UseInfoPopup({m_text_area, m_scroll_bar, &m_button});
    if (!PopulateText()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }

    int x = m_x;
    m_scroll_bar.SetLayout(x + 0x12b, m_y + 0x26, m_text_area.GetTotalLineCount(), 0,
                           m_text_area.GetLineHeight(), 0xb9);

    CreateCloseButton(m_button, 0x11a, 0xe6);
}

/* GppStringList indices naming the seven classes the monster's
   name line falls into by how far its level sits above the party average. */
static int g_monster_level_name_ids[7] = {301, 302, 303, 304, 305, 306, 307};

/* Knowledge threshold and Data().strings label id pairs gating the
   record's six resistance entries. */
static int g_monster_resistance_label_gates[6][2] = {
    {70, 308}, {60, 309}, {40, 310}, {50, 311}, {80, 312}, {90, 313},
};

bool W8MonsterInfoDialog::PopulateText()
{
    W8ControlsRect bounds;
    std::string text;
    bool is_npc = false;
    unsigned int knowledge;
    int leader_location_id;
    int leader_group_id;
    int count;
    int index;
    int slot;
    bool has_unknown;
    bool any_shown;
    unsigned char max_values[0x10]{};
    int range_category;
    std::string entry_text;
    std::string prefix;
    float combat_range;
    float average_level;
    int monster_level;
    int best_party_slot;
    int sight;
    W8MonsterGroup* group;
    W8MonsterInfo* leader_info;
    Navigator* linked;
    Script* script;
    W8NpcState* npc;
    const W8MonsterAttack* attack;
    W8ConditionImmunity* immunity;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;

    bounds.left = m_x + 0x11;
    bounds.right = m_x + 0x11f;
    bounds.top = m_y + 0x26;
    bounds.bottom = m_y + 0xdf;

    monster_info = FindMonster(m_location_id);
    record = GetMonsterDataForInfo(monster_info);
    average_level = GetAveragePartyMemberLevel();
    monster_level = record->display_level;
    if (monster_info->ubDisposition != W8_DISPOSITION_HOSTILE &&
        (record->flags & W8_MONSTER_FLAG_NPC) != 0 &&
        (npc = GetNpcStateByKind(record->npc_kind)) != 0 && npc->record->has_group != 0) {
        is_npc = true;
    }
    if (monster_info->summoned == W8_MONSTER_SUMMON_FRIENDLY) {
        knowledge = 0x7d;
    } else {
        knowledge = GetBestPartySkillLevel(W8_SKILL_MYTHOLOGY, &best_party_slot);
        if (static_cast<int>(average_level) < monster_level) {
            float adjusted_knowledge = knowledge - (monster_level - average_level) * 3.0f + 0.5f;
            if (adjusted_knowledge < 0.0f) {
                adjusted_knowledge = 0.0f;
            }
            knowledge = static_cast<unsigned int>(adjusted_knowledge);
        }
    }
    combat_range = monster_info->p3D->GetDistanceToPlayer();
    m_text_area.Configure(&bounds, g_wiz_text_font_secondary, 0);
    m_text_area.SetEntrySpacing(1);

    if (g_dev_mode) {
        group = FindMonsterGroup(monster_info->monster_group_id);
        linked = monster_info->p3D->nav.linked_navigator;
        if (linked == 0) {
            leader_location_id = m_location_id;
        } else {
            leader_location_id = linked->movement.location_id;
        }
        leader_group_id = group->leader_group_id;
        if (leader_group_id == 0) {
            leader_group_id = monster_info->monster_group_id;
        }
        m_text_area.AddEntry("ID/Group (Leader)",
                             fmt::format("{:d}/{:d} ({:d}/{:d}) {}", m_location_id,
                                         monster_info->monster_group_id, leader_location_id,
                                         leader_group_id,
                                         group->encounter_registered ? "random" : "placed"),
                             5, 0xf, 0);
    }

    if (is_npc) {
        prefix = Data().strings[0x13b];
        entry_text = Data().strings[0x13a];
    } else {
        int level_class = static_cast<int>(monster_level / average_level * 5.0f - 1.5f);
        level_class = std::clamp(level_class, 0, 6);
        entry_text = fmt::format("{:d} ({})", monster_level,
                                 Data().strings[g_monster_level_name_ids[level_class]]);
        prefix = Data().strings[0x13b];
    }
    m_text_area.AddEntry(prefix, entry_text, 10, 0xf, 0);

    if (CurrentGame().status.status_ints[monster_info->monster_species] == 2) {
        text = FormatUnsignedIntegerWithCommas(GetMonsterExperience(record));
    } else {
        text = Data().strings[0x13a];
    }
    m_text_area.AddEntry(Data().strings[0x13c], text, 10, 0xf, 0);

    if (knowledge < 10 || is_npc) {
        text = Data().strings[0x13a];
    } else {
        text = fmt::format("{:d}", monster_info->hp_current);
    }
    text += " / ";
    if (knowledge < 5 || is_npc) {
        text = Data().strings[0x13a];
    } else {
        text += fmt::format("{:d}", monster_info->uiHPMax);
    }
    m_text_area.AddEntry(Data().strings[0x13d], text, 10, 0xf, 0);
    if (g_dev_mode && knowledge < 10) {
        text = fmt::format("{:d} / {:d}", monster_info->hp_current, monster_info->uiHPMax);
        m_text_area.AddEntry(Data().strings[0x13d], text, 5, 0xf, 0);
    }

    if (knowledge < 0x14 || is_npc) {
        text = Data().strings[0x13a];
    } else {
        text = fmt::format("{:d}", monster_info->stamina);
    }
    text += " / ";
    if (knowledge < 0xf || is_npc) {
        text = Data().strings[0x13a];
    } else {
        text += fmt::format("{:d}", monster_info->stamina_max);
    }
    m_text_area.AddEntry(Data().strings[0x13e], text, 10, 0xf, 0);
    if (g_dev_mode && knowledge < 0x14) {
        text = fmt::format("{:d} / {:d}", monster_info->stamina, monster_info->stamina_max);
        m_text_area.AddEntry(Data().strings[0x13e], text, 5, 0xf, 0);
    }

    text.clear();
    count = 0;
    for (index = 0; index < W8_CONDITION_COUNT; ++index) {
        if (monster_info->uiCondition[index] != 0) {
            if (count > 0) {
                text += g_comma_space;
            }
            text += Data().strings[g_condition_notices[index].name];
            ++count;
        }
    }
    if (monster_info->pCombat) {
        for (index = 0; index < 9; ++index) {
            if (monster_info->pCombat->combat_effects[index].active) {
                if (count > 0) {
                    text += g_comma_space;
                }
                text += Data()
                            .spells[monster_info->pCombat->combat_effects[index].effect_id]
                            .display_name;
                ++count;
            }
        }
    }
    if (monster_info->control_state == W8_MONSTER_CONTROL_LURED) {
        if (count > 0) {
            text += g_comma_space;
        }
        text += Data().strings[0x144];
        ++count;
    }
    if (monster_info->charm_strength != 0) {
        if (count > 0) {
            text += g_comma_space;
        }
        text += Data().strings[0x145];
        ++count;
    }
    if (monster_info->summoned != W8_MONSTER_SUMMON_NONE) {
        if (count > 0) {
            text += g_comma_space;
        }
        text += Data().strings[0x146];
        ++count;
    }
    if (count > 0) {
        m_text_area.AddEntry(Data().strings[0x147], text, 10, 0xf, 0);
    }

    text.clear();
    count = 0;
    for (index = 0; index < 8; ++index) {
        if (monster_info->enchantments[index].turns != 0) {
            if (count > 0) {
                text += g_comma_space;
            }
            text += Data().strings[g_enchantment_notices[index]];
            ++count;
        }
    }
    for (index = 0; index < 12; ++index) {
        if (monster_info->effect_slots[index].active) {
            if (count > 0) {
                text += g_comma_space;
            }
            text += Data().spells[monster_info->effect_slots[index].effect_id].display_name;
            ++count;
        }
    }
    if (monster_info->pCombat) {
        for (index = 0; index < 6; ++index) {
            if (monster_info->pCombat->combat_effects_2[index].active) {
                if (count > 0) {
                    text += g_comma_space;
                }
                text += Data()
                            .spells[monster_info->pCombat->combat_effects_2[index].effect_id]
                            .display_name;
                ++count;
            }
        }
    }
    if (count > 0) {
        m_text_area.AddEntry(Data().strings[0x148], text, 10, 0xf, 0);
    }

    for (range_category = W8_RANGE_TOUCH; range_category < 4; ++range_category) {
        if (combat_range <= CalcRangeDistance(static_cast<W8RangeCategory>(range_category))) {
            text = Data().strings[g_spell_range_name_ids[range_category]];
            break;
        }
    }
    if (range_category == 4) {
        text = Data().strings[0x140];
    }
    m_text_area.AddEntry(Data().strings[0x13f], text, 10, 0xf, 0);

    if (0x13 < knowledge) {
        W8RangeCategory best_range = GetMonsterBestRangeCategory(monster_info, true, &sight);
        if (best_range != W8_RANGE_NONE) {
            m_text_area.AddEntry(Data().strings[0x141],
                                 Data().strings[g_spell_range_name_ids[best_range]], 10, 0xf, 0);
        }
    }
    if (0x31 < knowledge && record->special_attack_kind != 0) {
        m_text_area.AddEntry(
            Data().strings[0x142],
            Data().strings[g_monster_special_attack_name_ids[record->special_attack_kind]], 10, 0xf,
            0);
    }

    if (9 < knowledge) {
        has_unknown = false;
        any_shown = false;
        for (index = 0; index < 3; ++index) {
            attack = &record->attacks[index];
            if (attack->fHasAttack != 0) {
                for (slot = 0; slot < 0x10; ++slot) {
                    if (max_values[slot] < attack->missile_values[slot]) {
                        max_values[slot] = attack->missile_values[slot];
                    }
                }
            }
        }
        text.clear();
        count = 0;
        for (index = 0; index < 0x10; ++index) {
            unsigned char value = max_values[index];
            if (value != 0) {
                if (value < 0x4b && (knowledge < 0x1e || value < 0x32) &&
                    (knowledge < 0x3c || value < 0x19) && (knowledge < 0x50 || value < 10) &&
                    (knowledge < 0x5a || value < 5) && knowledge < 100) {
                    has_unknown = true;
                } else {
                    if (count > 0) {
                        text += g_comma_space;
                    }
                    text += Data().strings[g_attack_effect_name_ids[index]];
                    ++count;
                    any_shown = true;
                }
            }
        }
        if (any_shown && has_unknown) {
            if (count > 0) {
                text += g_comma_space;
            }
            text += "???";
            ++count;
        }
        if (count > 0) {
            m_text_area.AddEntry(Data().strings[0x143], text, 10, 0xf, 0);
        }
    }

    if (0x1d < knowledge) {
        text.clear();
        count = 0;
        for (index = 0; index < 3; ++index) {
            immunity = &g_condition_immunities[index];
            if (record->kind == immunity->kind) {
                for (slot = 0; slot < 0x14 && immunity->conditions[slot] != 0; ++slot) {
                    if (count > 0) {
                        text += g_comma_space;
                    }
                    text += Data().strings[g_condition_notices[immunity->conditions[slot]].noun];
                    ++count;
                }
                if (immunity->immune_all != 0) {
                    if (count > 0) {
                        text += g_comma_space;
                    }
                    text += Data().strings[0x14b];
                    ++count;
                }
                if (count > 0) {
                    m_text_area.AddEntry(Data().strings[0x14a], text, 10, 0xf, 0);
                }
                break;
            }
        }
    }

    for (index = 0; index < 6; ++index) {
        if (g_monster_resistance_label_gates[index][0] <= static_cast<int>(knowledge)) {
            m_text_area.AddEntry(Data().strings[g_monster_resistance_label_gates[index][1]],
                                 fmt::format("{:d}", record->resistances[index]), 10, 0xf, 0);
        }
    }

    if (g_dev_mode) {
        m_text_area.AddEntry("Range (Combat/Ground)",
                             fmt::format("{:5.2f} / {:5.2f} M", combat_range * 0.002f,
                                         glm::length((monster_info->p3D->nav.GetPosition() -
                                                      PartyNavigator()->GetPosition())) *
                                             g_world_cursor_scale),
                             5, 0xf, 0);
        leader_info = FindMonster(leader_location_id);
        script = leader_info->p3D->script.get();
        m_text_area.AddEntry("Leader's Current Script",
                             fmt::format("<{}>", script != 0 ? script->getName().c_str() : ""), 5,
                             0xf, 0);
        m_text_area.AddEntry("Leader's AI Mode", fmt::format("{:d}", leader_info->ai_mode), 5, 0xf,
                             0);
        const char* strategy = "Close";
        if (record->prefer_ranged_actions != 0) {
            strategy = "Ranged";
        }
        m_text_area.AddEntry("Combat Strategy", strategy, 5, 0xf, 0);
    }
    return true;
}

void W8MonsterInfoDialog::Draw(SDL_Surface& target)
{

    W8DialogBase::Draw(target);
    g_wiz_text_font_secondary.resetPalette();
    W8MonsterInfo* monster_info = FindMonster(m_location_id);
    auto name = GetMonsterName(monster_info, 0, 0);
    std::int16_t width = g_wiz_text_font_secondary.textWidth(name);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0xe + (0x112 - width) / 2, m_y + 0x11},
             name);

    m_text_area.Paint(target);
    m_scroll_bar.Draw(target);
    m_button.Draw(target);
}
