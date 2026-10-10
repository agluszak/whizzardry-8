#pragma once

#include "wiz8/retail_records.h"
#include "wiz8/unicode.h"
#include "wiz8/filesystem.h"
#include "wiz8/chunk.h"
#include <cstring>
#include <stdexcept>

namespace wiz8::retail {
enum class Encoding { utf8, utf16 };
template<class T> struct Record;
template<> struct Record<W8ItemDatabaseRecord> {
    using Disk = ItemDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.display_name, bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)), Encoding::utf16);
        value(&runtime.legacy_item_number, bytes.subspan(offsetof(Disk, legacy_item_number), sizeof(Disk::legacy_item_number)));
        value(&runtime.equip_class, bytes.subspan(offsetof(Disk, equip_class), sizeof(Disk::equip_class)));
        value(&runtime.unidentified_name_index, bytes.subspan(offsetof(Disk, unidentified_name_index), sizeof(Disk::unidentified_name_index)));
        value(&runtime.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.category, bytes.subspan(offsetof(Disk, category), sizeof(Disk::category)));
        value(&runtime.unknown_043, bytes.subspan(offsetof(Disk, unknown_043), sizeof(Disk::unknown_043)));
        value(&runtime.weapon_skill, bytes.subspan(offsetof(Disk, weapon_skill), sizeof(Disk::weapon_skill)));
        value(&runtime.range_category, bytes.subspan(offsetof(Disk, range_category), sizeof(Disk::range_category)));
        value(&runtime.attack_damage_bonus, bytes.subspan(offsetof(Disk, attack_damage_bonus), sizeof(Disk::attack_damage_bonus)));
        value(&runtime.attack_hit_bonus, bytes.subspan(offsetof(Disk, attack_hit_bonus), sizeof(Disk::attack_hit_bonus)));
        value(&runtime.damage_dice, bytes.subspan(offsetof(Disk, damage_dice), sizeof(Disk::damage_dice)));
        value(&runtime.attack_flags, bytes.subspan(offsetof(Disk, attack_flags), sizeof(Disk::attack_flags)));
        value(&runtime.missile_values, bytes.subspan(offsetof(Disk, missile_values), sizeof(Disk::missile_values)));
        value(&runtime.missile_magnitude, bytes.subspan(offsetof(Disk, missile_magnitude), sizeof(Disk::missile_magnitude)));
        value(&runtime.slays_kind, bytes.subspan(offsetof(Disk, slays_kind), sizeof(Disk::slays_kind)));
        value(&runtime.armor_class_bonus, bytes.subspan(offsetof(Disk, armor_class_bonus), sizeof(Disk::armor_class_bonus)));
        value(&runtime.spell_id, bytes.subspan(offsetof(Disk, spell_id), sizeof(Disk::spell_id)));
        value(&runtime.spell_power, bytes.subspan(offsetof(Disk, spell_power), sizeof(Disk::spell_power)));
        value(&runtime.unknown_065, bytes.subspan(offsetof(Disk, unknown_065), sizeof(Disk::unknown_065)));
        value(&runtime.quantity_kind, bytes.subspan(offsetof(Disk, quantity_kind), sizeof(Disk::quantity_kind)));
        value(&runtime.initial_quantity, bytes.subspan(offsetof(Disk, initial_quantity), sizeof(Disk::initial_quantity)));
        value(&runtime.maximum_quantity, bytes.subspan(offsetof(Disk, maximum_quantity), sizeof(Disk::maximum_quantity)));
        value(&runtime.health_regen_bonus, bytes.subspan(offsetof(Disk, health_regen_bonus), sizeof(Disk::health_regen_bonus)));
        value(&runtime.stamina_regen_bonus, bytes.subspan(offsetof(Disk, stamina_regen_bonus), sizeof(Disk::stamina_regen_bonus)));
        value(&runtime.spell_regen_bonus, bytes.subspan(offsetof(Disk, spell_regen_bonus), sizeof(Disk::spell_regen_bonus)));
        value(&runtime.resistance_bonus, bytes.subspan(offsetof(Disk, resistance_bonus), sizeof(Disk::resistance_bonus)));
        value(&runtime.property, bytes.subspan(offsetof(Disk, property), sizeof(Disk::property)));
        value(&runtime.profession_mask, bytes.subspan(offsetof(Disk, profession_mask), sizeof(Disk::profession_mask)));
        value(&runtime.race_mask, bytes.subspan(offsetof(Disk, race_mask), sizeof(Disk::race_mask)));
        value(&runtime.gender_mask, bytes.subspan(offsetof(Disk, gender_mask), sizeof(Disk::gender_mask)));
        value(&runtime.attribute_requirements, bytes.subspan(offsetof(Disk, attribute_requirements), sizeof(Disk::attribute_requirements)));
        value(&runtime.skill_requirements, bytes.subspan(offsetof(Disk, skill_requirements), sizeof(Disk::skill_requirements)));
        value(&runtime.identify_difficulty, bytes.subspan(offsetof(Disk, identify_difficulty), sizeof(Disk::identify_difficulty)));
        value(&runtime.value, bytes.subspan(offsetof(Disk, value), sizeof(Disk::value)));
        value(&runtime.weight, bytes.subspan(offsetof(Disk, weight), sizeof(Disk::weight)));
        value(&runtime.binds_on_equip, bytes.subspan(offsetof(Disk, binds_on_equip), sizeof(Disk::binds_on_equip)));
        text(runtime.internal_name, bytes.subspan(offsetof(Disk, internal_name), sizeof(Disk::internal_name)), Encoding::utf8);
        value(&runtime.swings_bonus, bytes.subspan(offsetof(Disk, swings_bonus), sizeof(Disk::swings_bonus)));
        value(&runtime.modifier_0b1_index, bytes.subspan(offsetof(Disk, modifier_0b1_index), sizeof(Disk::modifier_0b1_index)));
        value(&runtime.modifier_0b1_value, bytes.subspan(offsetof(Disk, modifier_0b1_value), sizeof(Disk::modifier_0b1_value)));
        value(&runtime.modifier_0b3_index, bytes.subspan(offsetof(Disk, modifier_0b3_index), sizeof(Disk::modifier_0b3_index)));
        value(&runtime.modifier_0b3_value, bytes.subspan(offsetof(Disk, modifier_0b3_value), sizeof(Disk::modifier_0b3_value)));
        value(&runtime.unknown_0b5, bytes.subspan(offsetof(Disk, unknown_0b5), sizeof(Disk::unknown_0b5)));
        value(&runtime.merge_component_a, bytes.subspan(offsetof(Disk, merge_component_a), sizeof(Disk::merge_component_a)));
        value(&runtime.merge_component_b, bytes.subspan(offsetof(Disk, merge_component_b), sizeof(Disk::merge_component_b)));
        value(&runtime.material, bytes.subspan(offsetof(Disk, material), sizeof(Disk::material)));
        value(&runtime.weapon_sound_class, bytes.subspan(offsetof(Disk, weapon_sound_class), sizeof(Disk::weapon_sound_class)));
        value(&runtime.merge_skill, bytes.subspan(offsetof(Disk, merge_skill), sizeof(Disk::merge_skill)));
        value(&runtime.merge_skill_level, bytes.subspan(offsetof(Disk, merge_skill_level), sizeof(Disk::merge_skill_level)));
        value(&runtime.editor_excluded, bytes.subspan(offsetof(Disk, editor_excluded), sizeof(Disk::editor_excluded)));
        value(&runtime.missile_type, bytes.subspan(offsetof(Disk, missile_type), sizeof(Disk::missile_type)));
        text(runtime.video_object_name, bytes.subspan(offsetof(Disk, video_object_name), sizeof(Disk::video_object_name)), Encoding::utf8);
        text(runtime.sound_name, bytes.subspan(offsetof(Disk, sound_name), sizeof(Disk::sound_name)), Encoding::utf8);
    }
};
template<> struct Record<W8SpellRuntimeRecord> {
    using Disk = SpellRuntimeRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.database_name, bytes.subspan(offsetof(Disk, database_name), sizeof(Disk::database_name)), Encoding::utf8);
        value(&runtime.unknown_040, bytes.subspan(offsetof(Disk, unknown_040), sizeof(Disk::unknown_040)));
        value(&runtime.duration, bytes.subspan(offsetof(Disk, duration), sizeof(Disk::duration)));
        value(&runtime.alchemy_spell, bytes.subspan(offsetof(Disk, alchemy_spell), sizeof(Disk::alchemy_spell)));
        value(&runtime.spell_point_cost, bytes.subspan(offsetof(Disk, spell_point_cost), sizeof(Disk::spell_point_cost)));
        value(&runtime.duration_per_level, bytes.subspan(offsetof(Disk, duration_per_level), sizeof(Disk::duration_per_level)));
        value(&runtime.effect_dice, bytes.subspan(offsetof(Disk, effect_dice), sizeof(Disk::effect_dice)));
        value(&runtime.unknown_055, bytes.subspan(offsetof(Disk, unknown_055), sizeof(Disk::unknown_055)));
        value(&runtime.spell_level, bytes.subspan(offsetof(Disk, spell_level), sizeof(Disk::spell_level)));
        value(&runtime.wizardry_spell, bytes.subspan(offsetof(Disk, wizardry_spell), sizeof(Disk::wizardry_spell)));
        text(runtime.resource_name, bytes.subspan(offsetof(Disk, resource_name), sizeof(Disk::resource_name)), Encoding::utf8);
        text(runtime.display_name, bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)), Encoding::utf16);
        value(&runtime.unknown_11b, bytes.subspan(offsetof(Disk, unknown_11b), sizeof(Disk::unknown_11b)));
        value(&runtime.divinity_spell, bytes.subspan(offsetof(Disk, divinity_spell), sizeof(Disk::divinity_spell)));
        value(&runtime.psionics_spell, bytes.subspan(offsetof(Disk, psionics_spell), sizeof(Disk::psionics_spell)));
        value(&runtime.effect_radius, bytes.subspan(offsetof(Disk, effect_radius), sizeof(Disk::effect_radius)));
        value(&runtime.unknown_125, bytes.subspan(offsetof(Disk, unknown_125), sizeof(Disk::unknown_125)));
        value(&runtime.monster_castable, bytes.subspan(offsetof(Disk, monster_castable), sizeof(Disk::monster_castable)));
        value(&runtime.radius_per_level, bytes.subspan(offsetof(Disk, radius_per_level), sizeof(Disk::radius_per_level)));
        value(&runtime.power_class, bytes.subspan(offsetof(Disk, power_class), sizeof(Disk::power_class)));
        value(&runtime.range_category, bytes.subspan(offsetof(Disk, range_category), sizeof(Disk::range_category)));
        value(&runtime.realm, bytes.subspan(offsetof(Disk, realm), sizeof(Disk::realm)));
        value(&runtime.target_type, bytes.subspan(offsetof(Disk, target_type), sizeof(Disk::target_type)));
        value(&runtime.usable_when, bytes.subspan(offsetof(Disk, usable_when), sizeof(Disk::usable_when)));
        value(&runtime.needs_aim, bytes.subspan(offsetof(Disk, needs_aim), sizeof(Disk::needs_aim)));
        value(&runtime.missile_index, bytes.subspan(offsetof(Disk, missile_index), sizeof(Disk::missile_index)));
        value(&runtime.missile_delivered, bytes.subspan(offsetof(Disk, missile_delivered), sizeof(Disk::missile_delivered)));
        value(&runtime.unknown_145, bytes.subspan(offsetof(Disk, unknown_145), sizeof(Disk::unknown_145)));
        value(&runtime.show_effect_dice, bytes.subspan(offsetof(Disk, show_effect_dice), sizeof(Disk::show_effect_dice)));
        text(runtime.sound_name, bytes.subspan(offsetof(Disk, sound_name), sizeof(Disk::sound_name)), Encoding::utf8);
    }
};
template<> struct Record<W8FactDatabaseRecord> {
    using Disk = FactDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.identifier, bytes.subspan(offsetof(Disk, identifier), sizeof(Disk::identifier)));
        text(runtime.symbolic_name, bytes.subspan(offsetof(Disk, symbolic_name), sizeof(Disk::symbolic_name)), Encoding::utf8);
        value(&runtime.highlight_when_true, bytes.subspan(offsetof(Disk, highlight_when_true), sizeof(Disk::highlight_when_true)));
        value(&runtime.visibility, bytes.subspan(offsetof(Disk, visibility), sizeof(Disk::visibility)));
        text(runtime.alternate_description, bytes.subspan(offsetof(Disk, alternate_description), sizeof(Disk::alternate_description)), Encoding::utf16);
        text(runtime.description, bytes.subspan(offsetof(Disk, description), sizeof(Disk::description)), Encoding::utf16);
    }
};
template<> struct Record<W8NpcCharacterTemplate> {
    using Disk = NpcCharacterTemplate;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.name, bytes.subspan(offsetof(Disk, name), sizeof(Disk::name)), Encoding::utf16);
        text(runtime.name_part_2, bytes.subspan(offsetof(Disk, name_part_2), sizeof(Disk::name_part_2)), Encoding::utf16);
        value(&runtime.unknown_01a, bytes.subspan(offsetof(Disk, unknown_01a), sizeof(Disk::unknown_01a)));
        value(&runtime.profession, bytes.subspan(offsetof(Disk, profession), sizeof(Disk::profession)));
        value(&runtime.race, bytes.subspan(offsetof(Disk, race), sizeof(Disk::race)));
        value(&runtime.table_value, bytes.subspan(offsetof(Disk, table_value), sizeof(Disk::table_value)));
        value(&runtime.level, bytes.subspan(offsetof(Disk, level), sizeof(Disk::level)));
        value(&runtime.attributes, bytes.subspan(offsetof(Disk, attributes), sizeof(Disk::attributes)));
        value(&runtime.skills, bytes.subspan(offsetof(Disk, skills), sizeof(Disk::skills)));
        value(&runtime.spells, bytes.subspan(offsetof(Disk, spells), sizeof(Disk::spells)));
        value(&runtime.equipment_present, bytes.subspan(offsetof(Disk, equipment_present), sizeof(Disk::equipment_present)));
        value(&runtime.equipment_ids, bytes.subspan(offsetof(Disk, equipment_ids), sizeof(Disk::equipment_ids)));
        value(&runtime.backpack_present, bytes.subspan(offsetof(Disk, backpack_present), sizeof(Disk::backpack_present)));
        value(&runtime.backpack_ids, bytes.subspan(offsetof(Disk, backpack_ids), sizeof(Disk::backpack_ids)));
        value(&runtime.gender, bytes.subspan(offsetof(Disk, gender), sizeof(Disk::gender)));
        value(&runtime.wanted_item_ids, bytes.subspan(offsetof(Disk, wanted_item_ids), sizeof(Disk::wanted_item_ids)));
        value(&runtime.unknown_1fd, bytes.subspan(offsetof(Disk, unknown_1fd), sizeof(Disk::unknown_1fd)));
    }
};
template<> struct Record<W8NpcDatabaseRecord> {
    using Disk = NpcDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.version, bytes.subspan(offsetof(Disk, version), sizeof(Disk::version)));
        value(&runtime.trade_pool, bytes.subspan(offsetof(Disk, trade_pool), sizeof(Disk::trade_pool)));
        text(runtime.source_name, bytes.subspan(offsetof(Disk, source_name), sizeof(Disk::source_name)), Encoding::utf16);
        value(&runtime.monster_bound, bytes.subspan(offsetof(Disk, monster_bound), sizeof(Disk::monster_bound)));
        value(&runtime.owns_stock, bytes.subspan(offsetof(Disk, owns_stock), sizeof(Disk::owns_stock)));
        value(&runtime.merchant, bytes.subspan(offsetof(Disk, merchant), sizeof(Disk::merchant)));
        value(&runtime.has_group, bytes.subspan(offsetof(Disk, has_group), sizeof(Disk::has_group)));
        value(&runtime.kind, bytes.subspan(offsetof(Disk, kind), sizeof(Disk::kind)));
        value(&runtime.disposition, bytes.subspan(offsetof(Disk, disposition), sizeof(Disk::disposition)));
        value(&runtime.charm_scale, bytes.subspan(offsetof(Disk, charm_scale), sizeof(Disk::charm_scale)));
        value(&runtime.talk_scale, bytes.subspan(offsetof(Disk, talk_scale), sizeof(Disk::talk_scale)));
        value(&runtime.faction, bytes.subspan(offsetof(Disk, faction), sizeof(Disk::faction)));
        value(&runtime.name_alias_mask, bytes.subspan(offsetof(Disk, name_alias_mask), sizeof(Disk::name_alias_mask)));
        value(&runtime.item_table_id, bytes.subspan(offsetof(Disk, item_table_id), sizeof(Disk::item_table_id)));
        value(&runtime.service_flags, bytes.subspan(offsetof(Disk, service_flags), sizeof(Disk::service_flags)));
        value(&runtime.unknown_06c, bytes.subspan(offsetof(Disk, unknown_06c), sizeof(Disk::unknown_06c)));
        value(&runtime.allied_faction, bytes.subspan(offsetof(Disk, allied_faction), sizeof(Disk::allied_faction)));
        value(&runtime.min_party_level, bytes.subspan(offsetof(Disk, min_party_level), sizeof(Disk::min_party_level)));
        value(&runtime.restore_level, bytes.subspan(offsetof(Disk, restore_level), sizeof(Disk::restore_level)));
        value(&runtime.unknown_071, bytes.subspan(offsetof(Disk, unknown_071), sizeof(Disk::unknown_071)));
        text(runtime.restore_entity_name, bytes.subspan(offsetof(Disk, restore_entity_name), sizeof(Disk::restore_entity_name)), Encoding::utf8);
        value(&runtime.no_item_stock, bytes.subspan(offsetof(Disk, no_item_stock), sizeof(Disk::no_item_stock)));
        text(runtime.display_name, bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)), Encoding::utf8);
        Record<W8NpcCharacterTemplate>::fields(runtime.character, bytes.subspan(offsetof(Disk, character), sizeof(Disk::character)), value, text);
        value(&runtime.item_stock_rules, bytes.subspan(offsetof(Disk, item_stock_rules), sizeof(Disk::item_stock_rules)));
        value(&runtime.buy_price_factor, bytes.subspan(offsetof(Disk, buy_price_factor), sizeof(Disk::buy_price_factor)));
        value(&runtime.sell_price_factor, bytes.subspan(offsetof(Disk, sell_price_factor), sizeof(Disk::sell_price_factor)));
        value(&runtime.trade_item_class_mask, bytes.subspan(offsetof(Disk, trade_item_class_mask), sizeof(Disk::trade_item_class_mask)));
        value(&runtime.unknown_2da, bytes.subspan(offsetof(Disk, unknown_2da), sizeof(Disk::unknown_2da)));
        value(&runtime.voice_script, bytes.subspan(offsetof(Disk, voice_script), sizeof(Disk::voice_script)));
        value(&runtime.gold, bytes.subspan(offsetof(Disk, gold), sizeof(Disk::gold)));
        value(&runtime.combat_script_notice_enabled, bytes.subspan(offsetof(Disk, combat_script_notice_enabled), sizeof(Disk::combat_script_notice_enabled)));
        value(&runtime.allow_dismissed_departure_dialogue, bytes.subspan(offsetof(Disk, allow_dismissed_departure_dialogue), sizeof(Disk::allow_dismissed_departure_dialogue)));
        value(&runtime.unknown_2f1, bytes.subspan(offsetof(Disk, unknown_2f1), sizeof(Disk::unknown_2f1)));
    }
};
template<> struct Record<W8LevelDatabaseRecord> {
    using Disk = LevelDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.display_name, bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)), Encoding::utf16);
        value(&runtime.maximum_random_encounters, bytes.subspan(offsetof(Disk, maximum_random_encounters), sizeof(Disk::maximum_random_encounters)));
        value(&runtime.minimum_random_encounters, bytes.subspan(offsetof(Disk, minimum_random_encounters), sizeof(Disk::minimum_random_encounters)));
        value(&runtime.maximum_encounter_budget, bytes.subspan(offsetof(Disk, maximum_encounter_budget), sizeof(Disk::maximum_encounter_budget)));
        value(&runtime.minimum_encounter_budget, bytes.subspan(offsetof(Disk, minimum_encounter_budget), sizeof(Disk::minimum_encounter_budget)));
        value(&runtime.encounter_budget_period, bytes.subspan(offsetof(Disk, encounter_budget_period), sizeof(Disk::encounter_budget_period)));
        value(&runtime.encounter_culling_seconds, bytes.subspan(offsetof(Disk, encounter_culling_seconds), sizeof(Disk::encounter_culling_seconds)));
        value(&runtime.gameplay_time_scale, bytes.subspan(offsetof(Disk, gameplay_time_scale), sizeof(Disk::gameplay_time_scale)));
        value(&runtime.unknown_058, bytes.subspan(offsetof(Disk, unknown_058), sizeof(Disk::unknown_058)));
    }
};
template<> struct Record<W8MonsterRecord> {
    using Disk = MonsterRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.name0, bytes.subspan(offsetof(Disk, name0), sizeof(Disk::name0)), Encoding::utf16);
        text(runtime.name1, bytes.subspan(offsetof(Disk, name1), sizeof(Disk::name1)), Encoding::utf16);
        text(runtime.name2, bytes.subspan(offsetof(Disk, name2), sizeof(Disk::name2)), Encoding::utf16);
        text(runtime.name3, bytes.subspan(offsetof(Disk, name3), sizeof(Disk::name3)), Encoding::utf16);
        value(&runtime.can_open_doors, bytes.subspan(offsetof(Disk, can_open_doors), sizeof(Disk::can_open_doors)));
        value(&runtime.group_size_dice, bytes.subspan(offsetof(Disk, group_size_dice), sizeof(Disk::group_size_dice)));
        value(&runtime.companions, bytes.subspan(offsetof(Disk, companions), sizeof(Disk::companions)));
        value(&runtime.kind, bytes.subspan(offsetof(Disk, kind), sizeof(Disk::kind)));
        value(&runtime.name_group, bytes.subspan(offsetof(Disk, name_group), sizeof(Disk::name_group)));
        value(&runtime.npc_kind, bytes.subspan(offsetof(Disk, npc_kind), sizeof(Disk::npc_kind)));
        value(&runtime.stamina_regeneration, bytes.subspan(offsetof(Disk, stamina_regeneration), sizeof(Disk::stamina_regeneration)));
        value(&runtime.damage_reduction, bytes.subspan(offsetof(Disk, damage_reduction), sizeof(Disk::damage_reduction)));
        value(&runtime.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.attribute_values, bytes.subspan(offsetof(Disk, attribute_values), sizeof(Disk::attribute_values)));
        value(&runtime.hit_points, bytes.subspan(offsetof(Disk, hit_points), sizeof(Disk::hit_points)));
        value(&runtime.stamina_dice, bytes.subspan(offsetof(Disk, stamina_dice), sizeof(Disk::stamina_dice)));
        value(&runtime.unknown_0de, bytes.subspan(offsetof(Disk, unknown_0de), sizeof(Disk::unknown_0de)));
        value(&runtime.spell_chance, bytes.subspan(offsetof(Disk, spell_chance), sizeof(Disk::spell_chance)));
        value(&runtime.flee_chance, bytes.subspan(offsetof(Disk, flee_chance), sizeof(Disk::flee_chance)));
        value(&runtime.advance_chance, bytes.subspan(offsetof(Disk, advance_chance), sizeof(Disk::advance_chance)));
        value(&runtime.special_attack_kind, bytes.subspan(offsetof(Disk, special_attack_kind), sizeof(Disk::special_attack_kind)));
        value(&runtime.initiative, bytes.subspan(offsetof(Disk, initiative), sizeof(Disk::initiative)));
        value(&runtime.attacks_per_round, bytes.subspan(offsetof(Disk, attacks_per_round), sizeof(Disk::attacks_per_round)));
        value(&runtime.swings_per_round, bytes.subspan(offsetof(Disk, swings_per_round), sizeof(Disk::swings_per_round)));
        value(&runtime.attacks, bytes.subspan(offsetof(Disk, attacks), sizeof(Disk::attacks)));
        value(&runtime.spells, bytes.subspan(offsetof(Disk, spells), sizeof(Disk::spells)));
        value(&runtime.attack_body_part_chances, bytes.subspan(offsetof(Disk, attack_body_part_chances), sizeof(Disk::attack_body_part_chances)));
        value(&runtime.special_attack_cooldown, bytes.subspan(offsetof(Disk, special_attack_cooldown), sizeof(Disk::special_attack_cooldown)));
        value(&runtime.evasion, bytes.subspan(offsetof(Disk, evasion), sizeof(Disk::evasion)));
        value(&runtime.constitution, bytes.subspan(offsetof(Disk, constitution), sizeof(Disk::constitution)));
        value(&runtime.hit_location_chances, bytes.subspan(offsetof(Disk, hit_location_chances), sizeof(Disk::hit_location_chances)));
        value(&runtime.armor_class_by_location, bytes.subspan(offsetof(Disk, armor_class_by_location), sizeof(Disk::armor_class_by_location)));
        value(&runtime.armor_class_by_attack_mode, bytes.subspan(offsetof(Disk, armor_class_by_attack_mode), sizeof(Disk::armor_class_by_attack_mode)));
        value(&runtime.resistances, bytes.subspan(offsetof(Disk, resistances), sizeof(Disk::resistances)));
        value(&runtime.hp_regeneration, bytes.subspan(offsetof(Disk, hp_regeneration), sizeof(Disk::hp_regeneration)));
        value(&runtime.unknown_17d, bytes.subspan(offsetof(Disk, unknown_17d), sizeof(Disk::unknown_17d)));
        value(&runtime.experience, bytes.subspan(offsetof(Disk, experience), sizeof(Disk::experience)));
        value(&runtime.unknown_185, bytes.subspan(offsetof(Disk, unknown_185), sizeof(Disk::unknown_185)));
        value(&runtime.record_id, bytes.subspan(offsetof(Disk, record_id), sizeof(Disk::record_id)));
        text(runtime.cycle_name, bytes.subspan(offsetof(Disk, cycle_name), sizeof(Disk::cycle_name)), Encoding::utf8);
        value(&runtime.prefer_ranged_actions, bytes.subspan(offsetof(Disk, prefer_ranged_actions), sizeof(Disk::prefer_ranged_actions)));
        value(&runtime.combat_move_range, bytes.subspan(offsetof(Disk, combat_move_range), sizeof(Disk::combat_move_range)));
        value(&runtime.instant_death_immune, bytes.subspan(offsetof(Disk, instant_death_immune), sizeof(Disk::instant_death_immune)));
        value(&runtime.combat_behavior, bytes.subspan(offsetof(Disk, combat_behavior), sizeof(Disk::combat_behavior)));
        value(&runtime.combat_morale, bytes.subspan(offsetof(Disk, combat_morale), sizeof(Disk::combat_morale)));
        value(&runtime.editor_index, bytes.subspan(offsetof(Disk, editor_index), sizeof(Disk::editor_index)));
        text(runtime.remains_model_name, bytes.subspan(offsetof(Disk, remains_model_name), sizeof(Disk::remains_model_name)), Encoding::utf8);
        value(&runtime.treasure, bytes.subspan(offsetof(Disk, treasure), sizeof(Disk::treasure)));
        value(&runtime.attack_multiple_targets, bytes.subspan(offsetof(Disk, attack_multiple_targets), sizeof(Disk::attack_multiple_targets)));
        value(&runtime.camouflage0, bytes.subspan(offsetof(Disk, camouflage0), sizeof(Disk::camouflage0)));
        value(&runtime.camouflage1, bytes.subspan(offsetof(Disk, camouflage1), sizeof(Disk::camouflage1)));
        value(&runtime.untargetable, bytes.subspan(offsetof(Disk, untargetable), sizeof(Disk::untargetable)));
        value(&runtime.unknown_24b, bytes.subspan(offsetof(Disk, unknown_24b), sizeof(Disk::unknown_24b)));
        value(&runtime.effective_level, bytes.subspan(offsetof(Disk, effective_level), sizeof(Disk::effective_level)));
        value(&runtime.unknown_250, bytes.subspan(offsetof(Disk, unknown_250), sizeof(Disk::unknown_250)));
        value(&runtime.display_level, bytes.subspan(offsetof(Disk, display_level), sizeof(Disk::display_level)));
        value(&runtime.unknown_252, bytes.subspan(offsetof(Disk, unknown_252), sizeof(Disk::unknown_252)));
        value(&runtime.model_index, bytes.subspan(offsetof(Disk, model_index), sizeof(Disk::model_index)));
        value(&runtime.alternate_model_index, bytes.subspan(offsetof(Disk, alternate_model_index), sizeof(Disk::alternate_model_index)));
        value(&runtime.hostility_radius, bytes.subspan(offsetof(Disk, hostility_radius), sizeof(Disk::hostility_radius)));
        value(&runtime.faction_id, bytes.subspan(offsetof(Disk, faction_id), sizeof(Disk::faction_id)));
        value(&runtime.material, bytes.subspan(offsetof(Disk, material), sizeof(Disk::material)));
        value(&runtime.deleted, bytes.subspan(offsetof(Disk, deleted), sizeof(Disk::deleted)));
        value(&runtime.significant_kill, bytes.subspan(offsetof(Disk, significant_kill), sizeof(Disk::significant_kill)));
        value(&runtime.unknown_269, bytes.subspan(offsetof(Disk, unknown_269), sizeof(Disk::unknown_269)));
        value(&runtime.unborn, bytes.subspan(offsetof(Disk, unborn), sizeof(Disk::unborn)));
        value(&runtime.experience_override, bytes.subspan(offsetof(Disk, experience_override), sizeof(Disk::experience_override)));
        value(&runtime.sp_budget, bytes.subspan(offsetof(Disk, sp_budget), sizeof(Disk::sp_budget)));
        value(&runtime.unknown_273, bytes.subspan(offsetof(Disk, unknown_273), sizeof(Disk::unknown_273)));
    }
};
template<> struct Record<W8Character> {
    using Disk = Character;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.record_version, bytes.subspan(offsetof(Disk, record_version), sizeof(Disk::record_version)));
        value(&runtime.fInParty, bytes.subspan(offsetof(Disk, fInParty), sizeof(Disk::fInParty)));
        text(runtime.name, bytes.subspan(offsetof(Disk, name), sizeof(Disk::name)), Encoding::utf16);
        text(runtime.name_part_2, bytes.subspan(offsetof(Disk, name_part_2), sizeof(Disk::name_part_2)), Encoding::utf16);
        value(&runtime.unknown_0025, bytes.subspan(offsetof(Disk, unknown_0025), sizeof(Disk::unknown_0025)));
        value(&runtime.iProfession, bytes.subspan(offsetof(Disk, iProfession), sizeof(Disk::iProfession)));
        value(&runtime.original_profession, bytes.subspan(offsetof(Disk, original_profession), sizeof(Disk::original_profession)));
        value(&runtime.iRace, bytes.subspan(offsetof(Disk, iRace), sizeof(Disk::iRace)));
        value(&runtime.gender, bytes.subspan(offsetof(Disk, gender), sizeof(Disk::gender)));
        value(&runtime.portrait_index, bytes.subspan(offsetof(Disk, portrait_index), sizeof(Disk::portrait_index)));
        value(&runtime.unknown_007d, bytes.subspan(offsetof(Disk, unknown_007d), sizeof(Disk::unknown_007d)));
        value(&runtime.personality, bytes.subspan(offsetof(Disk, personality), sizeof(Disk::personality)));
        value(&runtime.voice, bytes.subspan(offsetof(Disk, voice), sizeof(Disk::voice)));
        value(&runtime.uiExpLevel, bytes.subspan(offsetof(Disk, uiExpLevel), sizeof(Disk::uiExpLevel)));
        value(&runtime.profession_levels, bytes.subspan(offsetof(Disk, profession_levels), sizeof(Disk::profession_levels)));
        value(&runtime.unknown_00c9, bytes.subspan(offsetof(Disk, unknown_00c9), sizeof(Disk::unknown_00c9)));
        value(&runtime.level_band, bytes.subspan(offsetof(Disk, level_band), sizeof(Disk::level_band)));
        value(&runtime.level_band_base, bytes.subspan(offsetof(Disk, level_band_base), sizeof(Disk::level_band_base)));
        value(&runtime.attributes, bytes.subspan(offsetof(Disk, attributes), sizeof(Disk::attributes)));
        value(&runtime.unknown_0171, bytes.subspan(offsetof(Disk, unknown_0171), sizeof(Disk::unknown_0171)));
        value(&runtime.attribute_point_deficit, bytes.subspan(offsetof(Disk, attribute_point_deficit), sizeof(Disk::attribute_point_deficit)));
        value(&runtime.skills, bytes.subspan(offsetof(Disk, skills), sizeof(Disk::skills)));
        value(&runtime.unknown_07b3, bytes.subspan(offsetof(Disk, unknown_07b3), sizeof(Disk::unknown_07b3)));
        value(&runtime.experience, bytes.subspan(offsetof(Disk, experience), sizeof(Disk::experience)));
        value(&runtime.experience_goal, bytes.subspan(offsetof(Disk, experience_goal), sizeof(Disk::experience_goal)));
        value(&runtime.experience_previous_goal, bytes.subspan(offsetof(Disk, experience_previous_goal), sizeof(Disk::experience_previous_goal)));
        value(&runtime.kill_count, bytes.subspan(offsetof(Disk, kill_count), sizeof(Disk::kill_count)));
        value(&runtime.death_count, bytes.subspan(offsetof(Disk, death_count), sizeof(Disk::death_count)));
        value(&runtime.uiCondition, bytes.subspan(offsetof(Disk, uiCondition), sizeof(Disk::uiCondition)));
        value(&runtime.unknown_0a51, bytes.subspan(offsetof(Disk, unknown_0a51), sizeof(Disk::unknown_0a51)));
        value(&runtime.enchantments, bytes.subspan(offsetof(Disk, enchantments), sizeof(Disk::enchantments)));
        value(&runtime.unknown_0ac5, bytes.subspan(offsetof(Disk, unknown_0ac5), sizeof(Disk::unknown_0ac5)));
        value(&runtime.highest_condition, bytes.subspan(offsetof(Disk, highest_condition), sizeof(Disk::highest_condition)));
        value(&runtime.enchantment_top, bytes.subspan(offsetof(Disk, enchantment_top), sizeof(Disk::enchantment_top)));
        value(&runtime.condition_argument, bytes.subspan(offsetof(Disk, condition_argument), sizeof(Disk::condition_argument)));
        value(&runtime.uiHPMax, bytes.subspan(offsetof(Disk, uiHPMax), sizeof(Disk::uiHPMax)));
        value(&runtime.hp_current, bytes.subspan(offsetof(Disk, hp_current), sizeof(Disk::hp_current)));
        value(&runtime.hp_adjustment, bytes.subspan(offsetof(Disk, hp_adjustment), sizeof(Disk::hp_adjustment)));
        value(&runtime.uiStaminaMax, bytes.subspan(offsetof(Disk, uiStaminaMax), sizeof(Disk::uiStaminaMax)));
        value(&runtime.stamina, bytes.subspan(offsetof(Disk, stamina), sizeof(Disk::stamina)));
        value(&runtime.fatigue_penalty, bytes.subspan(offsetof(Disk, fatigue_penalty), sizeof(Disk::fatigue_penalty)));
        value(&runtime.sp_max, bytes.subspan(offsetof(Disk, sp_max), sizeof(Disk::sp_max)));
        value(&runtime.unknown_0b3d, bytes.subspan(offsetof(Disk, unknown_0b3d), sizeof(Disk::unknown_0b3d)));
        value(&runtime.iSPLeft, bytes.subspan(offsetof(Disk, iSPLeft), sizeof(Disk::iSPLeft)));
        value(&runtime.unknown_0b5d, bytes.subspan(offsetof(Disk, unknown_0b5d), sizeof(Disk::unknown_0b5d)));
        value(&runtime.potion_brew_cooldown, bytes.subspan(offsetof(Disk, potion_brew_cooldown), sizeof(Disk::potion_brew_cooldown)));
        value(&runtime.health_regen_rate, bytes.subspan(offsetof(Disk, health_regen_rate), sizeof(Disk::health_regen_rate)));
        value(&runtime.health_regen_accumulator, bytes.subspan(offsetof(Disk, health_regen_accumulator), sizeof(Disk::health_regen_accumulator)));
        value(&runtime.stamina_regen_rate, bytes.subspan(offsetof(Disk, stamina_regen_rate), sizeof(Disk::stamina_regen_rate)));
        value(&runtime.stamina_regen_accumulator, bytes.subspan(offsetof(Disk, stamina_regen_accumulator), sizeof(Disk::stamina_regen_accumulator)));
        value(&runtime.spell_regen_rates, bytes.subspan(offsetof(Disk, spell_regen_rates), sizeof(Disk::spell_regen_rates)));
        value(&runtime.unknown_0ba9, bytes.subspan(offsetof(Disk, unknown_0ba9), sizeof(Disk::unknown_0ba9)));
        value(&runtime.inventory_weight, bytes.subspan(offsetof(Disk, inventory_weight), sizeof(Disk::inventory_weight)));
        value(&runtime.party_weight_share, bytes.subspan(offsetof(Disk, party_weight_share), sizeof(Disk::party_weight_share)));
        value(&runtime.total_carried_weight, bytes.subspan(offsetof(Disk, total_carried_weight), sizeof(Disk::total_carried_weight)));
        value(&runtime.carrying_capacity, bytes.subspan(offsetof(Disk, carrying_capacity), sizeof(Disk::carrying_capacity)));
        value(&runtime.load_category, bytes.subspan(offsetof(Disk, load_category), sizeof(Disk::load_category)));
        value(&runtime.spell_learned, bytes.subspan(offsetof(Disk, spell_learned), sizeof(Disk::spell_learned)));
        value(&runtime.skill_unlocks, bytes.subspan(offsetof(Disk, skill_unlocks), sizeof(Disk::skill_unlocks)));
        value(&runtime.initiative, bytes.subspan(offsetof(Disk, initiative), sizeof(Disk::initiative)));
        value(&runtime.armor_class_total, bytes.subspan(offsetof(Disk, armor_class_total), sizeof(Disk::armor_class_total)));
        value(&runtime.armor_class_average, bytes.subspan(offsetof(Disk, armor_class_average), sizeof(Disk::armor_class_average)));
        value(&runtime.armor_class_components, bytes.subspan(offsetof(Disk, armor_class_components), sizeof(Disk::armor_class_components)));
        value(&runtime.armor_class_by_location, bytes.subspan(offsetof(Disk, armor_class_by_location), sizeof(Disk::armor_class_by_location)));
        value(&runtime.unknown_0ed5, bytes.subspan(offsetof(Disk, unknown_0ed5), sizeof(Disk::unknown_0ed5)));
        value(&runtime.damage_reduction, bytes.subspan(offsetof(Disk, damage_reduction), sizeof(Disk::damage_reduction)));
        value(&runtime.resistances, bytes.subspan(offsetof(Disk, resistances), sizeof(Disk::resistances)));
        value(&runtime.flags0, bytes.subspan(offsetof(Disk, flags0), sizeof(Disk::flags0)));
        value(&runtime.EquippedItem, bytes.subspan(offsetof(Disk, EquippedItem), sizeof(Disk::EquippedItem)));
        value(&runtime.flags1, bytes.subspan(offsetof(Disk, flags1), sizeof(Disk::flags1)));
        value(&runtime.backpack, bytes.subspan(offsetof(Disk, backpack), sizeof(Disk::backpack)));
        value(&runtime.unknown_1089, bytes.subspan(offsetof(Disk, unknown_1089), sizeof(Disk::unknown_1089)));
        value(&runtime.Hand, bytes.subspan(offsetof(Disk, Hand), sizeof(Disk::Hand)));
        value(&runtime.unknown_11ff, bytes.subspan(offsetof(Disk, unknown_11ff), sizeof(Disk::unknown_11ff)));
        value(&runtime.dual_wielding, bytes.subspan(offsetof(Disk, dual_wielding), sizeof(Disk::dual_wielding)));
        value(&runtime.monster_awareness, bytes.subspan(offsetof(Disk, monster_awareness), sizeof(Disk::monster_awareness)));
        value(&runtime.fatigue_band, bytes.subspan(offsetof(Disk, fatigue_band), sizeof(Disk::fatigue_band)));
        value(&runtime.condition_modifiers, bytes.subspan(offsetof(Disk, condition_modifiers), sizeof(Disk::condition_modifiers)));
        value(&runtime.equipment_bonus, bytes.subspan(offsetof(Disk, equipment_bonus), sizeof(Disk::equipment_bonus)));
        value(&runtime.bonus, bytes.subspan(offsetof(Disk, bonus), sizeof(Disk::bonus)));
        value(&runtime.saved_location, bytes.subspan(offsetof(Disk, saved_location), sizeof(Disk::saved_location)));
        value(&runtime.saved_level, bytes.subspan(offsetof(Disk, saved_level), sizeof(Disk::saved_level)));
        value(&runtime.conditions, bytes.subspan(offsetof(Disk, conditions), sizeof(Disk::conditions)));
        value(&runtime.deep_fatigue_applied, bytes.subspan(offsetof(Disk, deep_fatigue_applied), sizeof(Disk::deep_fatigue_applied)));
        value(&runtime.skill_costs, bytes.subspan(offsetof(Disk, skill_costs), sizeof(Disk::skill_costs)));
        value(&runtime.magic_bonus_pool, bytes.subspan(offsetof(Disk, magic_bonus_pool), sizeof(Disk::magic_bonus_pool)));
        value(&runtime.has_saved_location, bytes.subspan(offsetof(Disk, has_saved_location), sizeof(Disk::has_saved_location)));
    }
};
template<> struct Record<W8GlobalStatus> {
    using Disk = GlobalStatus;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.buffers, bytes.subspan(offsetof(Disk, buffers), sizeof(Disk::buffers)));
        value(&runtime.game_started, bytes.subspan(offsetof(Disk, game_started), sizeof(Disk::game_started)));
        value(&runtime.regular_member_count, bytes.subspan(offsetof(Disk, regular_member_count), sizeof(Disk::regular_member_count)));
        value(&runtime.auxiliary_member_count, bytes.subspan(offsetof(Disk, auxiliary_member_count), sizeof(Disk::auxiliary_member_count)));
        value(&runtime.total_member_count, bytes.subspan(offsetof(Disk, total_member_count), sizeof(Disk::total_member_count)));
        value(&runtime.party_gold, bytes.subspan(offsetof(Disk, party_gold), sizeof(Disk::party_gold)));
        value(&runtime.selected_character, bytes.subspan(offsetof(Disk, selected_character), sizeof(Disk::selected_character)));
        value(&runtime.party_item_pool, bytes.subspan(offsetof(Disk, party_item_pool), sizeof(Disk::party_item_pool)));
        value(&runtime.party_item_count, bytes.subspan(offsetof(Disk, party_item_count), sizeof(Disk::party_item_count)));
        value(&runtime.text_line_cursor, bytes.subspan(offsetof(Disk, text_line_cursor), sizeof(Disk::text_line_cursor)));
        value(&runtime.legacy_text_box_lines, bytes.subspan(offsetof(Disk, legacy_text_box_lines), sizeof(Disk::legacy_text_box_lines)));
        value(&runtime.effect_slots, bytes.subspan(offsetof(Disk, effect_slots), sizeof(Disk::effect_slots)));
        value(&runtime.unknown_187b, bytes.subspan(offsetof(Disk, unknown_187b), sizeof(Disk::unknown_187b)));
        value(&runtime.party_facing, bytes.subspan(offsetof(Disk, party_facing), sizeof(Disk::party_facing)));
        value(&runtime.party_heading, bytes.subspan(offsetof(Disk, party_heading), sizeof(Disk::party_heading)));
        value(&runtime.world_clock, bytes.subspan(offsetof(Disk, world_clock), sizeof(Disk::world_clock)));
        value(&runtime.world_clock_ms, bytes.subspan(offsetof(Disk, world_clock_ms), sizeof(Disk::world_clock_ms)));
        value(&runtime.party_order_slots, bytes.subspan(offsetof(Disk, party_order_slots), sizeof(Disk::party_order_slots)));
        value(&runtime.current_level, bytes.subspan(offsetof(Disk, current_level), sizeof(Disk::current_level)));
        value(&runtime.status_header_prefix, bytes.subspan(offsetof(Disk, status_header_prefix), sizeof(Disk::status_header_prefix)));
        value(&runtime.uiTurnsElapsed, bytes.subspan(offsetof(Disk, uiTurnsElapsed), sizeof(Disk::uiTurnsElapsed)));
        value(&runtime.status_header_suffix, bytes.subspan(offsetof(Disk, status_header_suffix), sizeof(Disk::status_header_suffix)));
        value(&runtime.level_progress, bytes.subspan(offsetof(Disk, level_progress), sizeof(Disk::level_progress)));
        value(&runtime.unknown_2013, bytes.subspan(offsetof(Disk, unknown_2013), sizeof(Disk::unknown_2013)));
        value(&runtime.pending_move_location, bytes.subspan(offsetof(Disk, pending_move_location), sizeof(Disk::pending_move_location)));
        value(&runtime.party_modifiers, bytes.subspan(offsetof(Disk, party_modifiers), sizeof(Disk::party_modifiers)));
        value(&runtime.next_group_id, bytes.subspan(offsetof(Disk, next_group_id), sizeof(Disk::next_group_id)));
        value(&runtime.next_monster_location_id, bytes.subspan(offsetof(Disk, next_monster_location_id), sizeof(Disk::next_monster_location_id)));
        value(&runtime.next_world_item_id, bytes.subspan(offsetof(Disk, next_world_item_id), sizeof(Disk::next_world_item_id)));
        value(&runtime.next_trigger_id, bytes.subspan(offsetof(Disk, next_trigger_id), sizeof(Disk::next_trigger_id)));
        value(&runtime.item_in_cursor, bytes.subspan(offsetof(Disk, item_in_cursor), sizeof(Disk::item_in_cursor)));
        value(&runtime.item_in_hand, bytes.subspan(offsetof(Disk, item_in_hand), sizeof(Disk::item_in_hand)));
        value(&runtime.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.game_time_ms, bytes.subspan(offsetof(Disk, game_time_ms), sizeof(Disk::game_time_ms)));
        value(&runtime.aging_accumulator, bytes.subspan(offsetof(Disk, aging_accumulator), sizeof(Disk::aging_accumulator)));
        value(&runtime.search_mode, bytes.subspan(offsetof(Disk, search_mode), sizeof(Disk::search_mode)));
        value(&runtime.world_suspended, bytes.subspan(offsetof(Disk, world_suspended), sizeof(Disk::world_suspended)));
        value(&runtime.real_elapsed, bytes.subspan(offsetof(Disk, real_elapsed), sizeof(Disk::real_elapsed)));
        value(&runtime.frame_elapsed, bytes.subspan(offsetof(Disk, frame_elapsed), sizeof(Disk::frame_elapsed)));
        value(&runtime.wait_state, bytes.subspan(offsetof(Disk, wait_state), sizeof(Disk::wait_state)));
        value(&runtime.item_recharge_ms, bytes.subspan(offsetof(Disk, item_recharge_ms), sizeof(Disk::item_recharge_ms)));
        value(&runtime.formation, bytes.subspan(offsetof(Disk, formation), sizeof(Disk::formation)));
        value(&runtime.game_time_days, bytes.subspan(offsetof(Disk, game_time_days), sizeof(Disk::game_time_days)));
        value(&runtime.iron_man, bytes.subspan(offsetof(Disk, iron_man), sizeof(Disk::iron_man)));
        value(&runtime.binding_reset_clock, bytes.subspan(offsetof(Disk, binding_reset_clock), sizeof(Disk::binding_reset_clock)));
        value(&runtime.binding_reset_pending, bytes.subspan(offsetof(Disk, binding_reset_pending), sizeof(Disk::binding_reset_pending)));
        value(&runtime.padding_242f, bytes.subspan(offsetof(Disk, padding_242f), sizeof(Disk::padding_242f)));
        value(&runtime.npc_restore_pending, bytes.subspan(offsetof(Disk, npc_restore_pending), sizeof(Disk::npc_restore_pending)));
        value(&runtime.quote_audit, bytes.subspan(offsetof(Disk, quote_audit), sizeof(Disk::quote_audit)));
        value(&runtime.long_quote, bytes.subspan(offsetof(Disk, long_quote), sizeof(Disk::long_quote)));
        value(&runtime.party_fatigued, bytes.subspan(offsetof(Disk, party_fatigued), sizeof(Disk::party_fatigued)));
        value(&runtime.selected_party_member, bytes.subspan(offsetof(Disk, selected_party_member), sizeof(Disk::selected_party_member)));
        value(&runtime.world_cursor_gate, bytes.subspan(offsetof(Disk, world_cursor_gate), sizeof(Disk::world_cursor_gate)));
        value(&runtime.camp_tick_ms, bytes.subspan(offsetof(Disk, camp_tick_ms), sizeof(Disk::camp_tick_ms)));
        value(&runtime.rpc_races, bytes.subspan(offsetof(Disk, rpc_races), sizeof(Disk::rpc_races)));
        value(&runtime.unknown_243f, bytes.subspan(offsetof(Disk, unknown_243f), sizeof(Disk::unknown_243f)));
        value(&runtime.skip_loose_character_check, bytes.subspan(offsetof(Disk, skip_loose_character_check), sizeof(Disk::skip_loose_character_check)));
        value(&runtime.use_item_latch, bytes.subspan(offsetof(Disk, use_item_latch), sizeof(Disk::use_item_latch)));
        value(&runtime.infatuation_pending, bytes.subspan(offsetof(Disk, infatuation_pending), sizeof(Disk::infatuation_pending)));
        value(&runtime.difficulty, bytes.subspan(offsetof(Disk, difficulty), sizeof(Disk::difficulty)));
        value(&runtime.save_filetime_xor, bytes.subspan(offsetof(Disk, save_filetime_xor), sizeof(Disk::save_filetime_xor)));
        text(runtime.monster_name_buffer, bytes.subspan(offsetof(Disk, monster_name_buffer), sizeof(Disk::monster_name_buffer)), Encoding::utf16);
        value(&runtime.sedexus_party_slot, bytes.subspan(offsetof(Disk, sedexus_party_slot), sizeof(Disk::sedexus_party_slot)));
        value(&runtime.stamina_tick_ms, bytes.subspan(offsetof(Disk, stamina_tick_ms), sizeof(Disk::stamina_tick_ms)));
        value(&runtime.condition13_clock, bytes.subspan(offsetof(Disk, condition13_clock), sizeof(Disk::condition13_clock)));
        value(&runtime.skip_next_condition_reaction, bytes.subspan(offsetof(Disk, skip_next_condition_reaction), sizeof(Disk::skip_next_condition_reaction)));
        value(&runtime.rpc_active, bytes.subspan(offsetof(Disk, rpc_active), sizeof(Disk::rpc_active)));
        value(&runtime.fact_b8_pending, bytes.subspan(offsetof(Disk, fact_b8_pending), sizeof(Disk::fact_b8_pending)));
        value(&runtime.condition13_stamp, bytes.subspan(offsetof(Disk, condition13_stamp), sizeof(Disk::condition13_stamp)));
        value(&runtime.pending_condition_party_slot, bytes.subspan(offsetof(Disk, pending_condition_party_slot), sizeof(Disk::pending_condition_party_slot)));
        value(&runtime.fact_b8_clock, bytes.subspan(offsetof(Disk, fact_b8_clock), sizeof(Disk::fact_b8_clock)));
        value(&runtime.greeting_pending, bytes.subspan(offsetof(Disk, greeting_pending), sizeof(Disk::greeting_pending)));
        value(&runtime.camp_fatigue_count, bytes.subspan(offsetof(Disk, camp_fatigue_count), sizeof(Disk::camp_fatigue_count)));
        value(&runtime.party_slot, bytes.subspan(offsetof(Disk, party_slot), sizeof(Disk::party_slot)));
        value(&runtime.item_spell_usage, bytes.subspan(offsetof(Disk, item_spell_usage), sizeof(Disk::item_spell_usage)));
        value(&runtime.log_fact_checks, bytes.subspan(offsetof(Disk, log_fact_checks), sizeof(Disk::log_fact_checks)));
        value(&runtime.status_ints, bytes.subspan(offsetof(Disk, status_ints), sizeof(Disk::status_ints)));
        value(&runtime.fact_88_latch, bytes.subspan(offsetof(Disk, fact_88_latch), sizeof(Disk::fact_88_latch)));
        value(&runtime.unknown_40c2, bytes.subspan(offsetof(Disk, unknown_40c2), sizeof(Disk::unknown_40c2)));
        value(&runtime.spell_usage, bytes.subspan(offsetof(Disk, spell_usage), sizeof(Disk::spell_usage)));
        value(&runtime.unknown_47de, bytes.subspan(offsetof(Disk, unknown_47de), sizeof(Disk::unknown_47de)));
        value(&runtime.cc_arena_spawned, bytes.subspan(offsetof(Disk, cc_arena_spawned), sizeof(Disk::cc_arena_spawned)));
        value(&runtime.savant_hack_tick, bytes.subspan(offsetof(Disk, savant_hack_tick), sizeof(Disk::savant_hack_tick)));
        value(&runtime.bela_cycle_tick, bytes.subspan(offsetof(Disk, bela_cycle_tick), sizeof(Disk::bela_cycle_tick)));
        value(&runtime.unknown_497b, bytes.subspan(offsetof(Disk, unknown_497b), sizeof(Disk::unknown_497b)));
        value(&runtime.combat_difficulty_counts, bytes.subspan(offsetof(Disk, combat_difficulty_counts), sizeof(Disk::combat_difficulty_counts)));
        value(&runtime.vi_event_stage, bytes.subspan(offsetof(Disk, vi_event_stage), sizeof(Disk::vi_event_stage)));
        value(&runtime.endgame2_queued, bytes.subspan(offsetof(Disk, endgame2_queued), sizeof(Disk::endgame2_queued)));
        value(&runtime.endgame3_queued, bytes.subspan(offsetof(Disk, endgame3_queued), sizeof(Disk::endgame3_queued)));
        value(&runtime.text_box_lines_used, bytes.subspan(offsetof(Disk, text_box_lines_used), sizeof(Disk::text_box_lines_used)));
        value(&runtime.text_box_lines_shown, bytes.subspan(offsetof(Disk, text_box_lines_shown), sizeof(Disk::text_box_lines_shown)));
        value(&runtime.trang_check_clock, bytes.subspan(offsetof(Disk, trang_check_clock), sizeof(Disk::trang_check_clock)));
        value(&runtime.trang_check_pending, bytes.subspan(offsetof(Disk, trang_check_pending), sizeof(Disk::trang_check_pending)));
        value(&runtime.intro_shown, bytes.subspan(offsetof(Disk, intro_shown), sizeof(Disk::intro_shown)));
        value(&runtime.flag, bytes.subspan(offsetof(Disk, flag), sizeof(Disk::flag)));
        value(&runtime.padding_49be, bytes.subspan(offsetof(Disk, padding_49be), sizeof(Disk::padding_49be)));
        value(&runtime.endgame_started, bytes.subspan(offsetof(Disk, endgame_started), sizeof(Disk::endgame_started)));
        value(&runtime.dev_flagged, bytes.subspan(offsetof(Disk, dev_flagged), sizeof(Disk::dev_flagged)));
    }
};


template<class T> void decode(std::span<const std::byte> disk, T& runtime)
{
    if (disk.size() > sizeof(typename Record<T>::Disk)) throw std::runtime_error("Oversized retail record");
    // Old saves omit trailing fields. Zero-extend the disk image, never the
    // native object (which owns strings and has a different layout).
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    if (!disk.empty()) std::memcpy(image.data(), disk.data(), disk.size());
    T decoded{};
    Record<T>::fields(decoded, std::span<const std::byte>(image),
        [](auto* destination, auto bytes) { std::memcpy(destination, bytes.data(), bytes.size()); },
        [](std::string& destination, auto bytes, Encoding encoding) {
            if (encoding == Encoding::utf16) destination = text::from_utf16le(bytes);
            else {
                const auto* data = reinterpret_cast<const char*>(bytes.data());
                const auto* end = static_cast<const char*>(std::memchr(data, 0, bytes.size()));
                destination.assign(data, end ? end - data : bytes.size());
            }
        });
    runtime = std::move(decoded);
}
template<class T> auto encode(const T& runtime)
{
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    Record<T>::fields(runtime, std::span<std::byte>(image),
        [](const auto* source, auto bytes) { std::memcpy(bytes.data(), source, bytes.size()); },
        [](const std::string& source, auto bytes, Encoding encoding) {
            if (encoding == Encoding::utf16) text::to_utf16le(source, bytes);
            else text::copy(reinterpret_cast<char*>(bytes.data()), bytes.size(), source);
        });
    return image;
}
template<class T> ReadResult read(File& file, T& runtime)
{
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    auto result = file.read(image.data(), image.size());
    if (result.bytes == image.size()) decode<T>(image, runtime);
    return result;
}
template<class T> void write(File& file, const T& runtime)
{
    auto image = encode(runtime);
    file.write(image.data(), image.size());
}
template<class T> bool read(W8Chunk& chunk, T& runtime, std::size_t bytes = sizeof(typename Record<T>::Disk))
{
    if (bytes > sizeof(typename Record<T>::Disk)) return false;
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    if (!chunk.Read(image.data(), bytes, nullptr)) return false;
    decode<T>(std::span(image).first(bytes), runtime);
    return true;
}
template<class T> bool write(W8Chunk& chunk, const T& runtime)
{
    auto image = encode(runtime);
    return chunk.Write(image.data(), image.size(), nullptr);
}
} // namespace wiz8::retail
