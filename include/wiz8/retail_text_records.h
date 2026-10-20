#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_records.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/unicode.h"

namespace wiz8::retail {
enum class Encoding : unsigned char { utf8, utf16 };
template<class T> struct Record;

/* A disk pointer slot is a 32-bit presence word: writers store whether the
   live pointer is set, readers never trust it and leave the pointer null.
   Loaders that need the presence word read it from the disk image. */
template<class T> void pointer_slot(T* const& live, std::span<std::byte> bytes)
{
    const std::uint32_t present = live != nullptr;
    std::memcpy(bytes.data(), &present, sizeof(present));
}
template<class T> void pointer_slot(T*& live, std::span<const std::byte>)
{
    live = nullptr;
}

/* Runtime-exclusive resources use the same on-disk pointer-presence word.
   The decoder never adopts saved process addresses. */
template<class T> void pointer_slot(const std::unique_ptr<T>& live, std::span<std::byte> bytes)
{
    const std::uint32_t present = static_cast<bool>(live);
    std::memcpy(bytes.data(), &present, sizeof(present));
}
template<class T> void pointer_slot(std::unique_ptr<T>& live, std::span<const std::byte>)
{
    live.reset();
}

/* A disk pointer slot whose payload a live record owns by value (an
   optional or a container): writers store whether it is present. */
template<class Owner> void presence_slot(const Owner& live, std::span<std::byte> bytes)
{
    std::uint32_t present;
    if constexpr (requires { live.has_value(); })
        present = live.has_value();
    else
        present = !live.empty();
    std::memcpy(bytes.data(), &present, sizeof(present));
}
template<class Owner> void presence_slot(Owner&, std::span<const std::byte>) {}

template<class Raw, class Enum, class Bytes> void enum_value(Enum* value, Bytes bytes)
{
    static_assert(std::is_enum_v<std::remove_const_t<Enum>>);
    if constexpr (std::is_const_v<Enum>) {
        const Raw raw = static_cast<Raw>(*value);
        std::memcpy(bytes.data(), &raw, sizeof(raw));
    } else {
        Raw raw{};
        std::memcpy(&raw, bytes.data(), sizeof(raw));
        *value = static_cast<std::remove_const_t<Enum>>(raw);
    }
}

template<class Boolean, class Bytes> void byte_boolean(Boolean* value, Bytes bytes)
{
    static_assert(std::is_same_v<std::remove_const_t<Boolean>, bool>);
    if constexpr (std::is_const_v<Boolean>) {
        const unsigned char raw = *value ? 1 : 0;
        std::memcpy(bytes.data(), &raw, sizeof(raw));
    } else {
        unsigned char raw{};
        std::memcpy(&raw, bytes.data(), sizeof(raw));
        *value = raw != 0;
    }
}

/* A nested record with its own codec. */
template<class Runtime, class Bytes, class Value, class Text>
void nested(Runtime& runtime, Bytes bytes, Value value, Text text)
{
    Record<std::remove_const_t<Runtime>>::fields(runtime, bytes, value, text);
}

template<> struct Record<glm::vec3> {
    using Disk = Vec3f;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text)
    {
        value(&runtime.x, bytes.subspan(0, 4));
        value(&runtime.y, bytes.subspan(4, 4));
        value(&runtime.z, bytes.subspan(8, 4));
    }
};

template<> struct Record<CameraPose> {
    using Disk = CameraState;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        nested(runtime.position, bytes.subspan(offsetof(Disk, position), sizeof(Disk::position)),
               value, text);
        value(&runtime.orientation.pitch, bytes.subspan(offsetof(Disk, pitch), 4));
        value(&runtime.retail.pitch, bytes.subspan(offsetof(Disk, pitch) + 4, 20));
        value(&runtime.orientation.yaw, bytes.subspan(offsetof(Disk, yaw), 4));
        value(&runtime.retail.yaw, bytes.subspan(offsetof(Disk, yaw) + 4, 20));
    }
};
template<> struct Record<W8TargetSource> {
    using Disk = TargetSource;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        enum_value<std::int32_t>(&runtime.iType,
                                 bytes.subspan(offsetof(Disk, iType), sizeof(Disk::iType)));
        value(&runtime.iChar, bytes.subspan(offsetof(Disk, iChar), sizeof(Disk::iChar)));
        value(&runtime.iMonsterID,
              bytes.subspan(offsetof(Disk, iMonsterID), sizeof(Disk::iMonsterID)));
        nested(runtime.point, bytes.subspan(offsetof(Disk, point), sizeof(Disk::point)), value,
               text);
        byte_boolean(&runtime.auto_cast,
                     bytes.subspan(offsetof(Disk, auto_cast), sizeof(Disk::auto_cast)));
        byte_boolean(&runtime.name_known,
                     bytes.subspan(offsetof(Disk, name_known), sizeof(Disk::name_known)));
        value(&runtime.aim_resolved,
              bytes.subspan(offsetof(Disk, aim_resolved), sizeof(Disk::aim_resolved)));
        value(&runtime.fReflection,
              bytes.subspan(offsetof(Disk, fReflection), sizeof(Disk::fReflection)));
        value(&runtime.fBackfire,
              bytes.subspan(offsetof(Disk, fBackfire), sizeof(Disk::fBackfire)));
        byte_boolean(&runtime.point_source,
                     bytes.subspan(offsetof(Disk, point_source), sizeof(Disk::point_source)));
        byte_boolean(&runtime.target_diverted,
                     bytes.subspan(offsetof(Disk, target_diverted), sizeof(Disk::target_diverted)));
        value(&runtime.spell_difficulty,
              bytes.subspan(offsetof(Disk, spell_difficulty), sizeof(Disk::spell_difficulty)));
        value(&runtime.unknown_20,
              bytes.subspan(offsetof(Disk, unknown_20), sizeof(Disk::unknown_20)));
        byte_boolean(&runtime.item_cast,
                     bytes.subspan(offsetof(Disk, item_cast), sizeof(Disk::item_cast)));
        value(&runtime.unknown_22,
              bytes.subspan(offsetof(Disk, unknown_22), sizeof(Disk::unknown_22)));
    }
};
template<> struct Record<W8CombatSlot> {
    using Disk = CombatTarget;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text)
    {
        enum_value<std::int32_t>(&runtime.iType, bytes.subspan(offsetof(Disk, iType), 4));
        value(&runtime.iChar, bytes.subspan(offsetof(Disk, iChar), 4));
        value(&runtime.iMonsterID, bytes.subspan(offsetof(Disk, iMonsterID), 4));
        value(&runtime.iGroupID, bytes.subspan(offsetof(Disk, iGroupID), 4));
        nested(runtime.point, bytes.subspan(offsetof(Disk, point), sizeof(Disk::point)), value,
               nullptr);
        // The item marker is a saved address; loaders rebuild the item.
        if constexpr (std::is_const_v<Runtime>)
            std::fill_n(bytes.begin() + offsetof(Disk, item_marker), 4, std::byte{});
        else
            runtime.item = {};
    }
};
template<> struct Record<W8PartySlotRow> {
    using Disk = PartySlot;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text)
    {
        if constexpr (!std::is_const_v<Runtime>)
            runtime.remembered.spell.kind = W8_ACTION_CAST_SPELL;
        value(&runtime.fOccupied,
              bytes.subspan(offsetof(Disk, fOccupied), sizeof(Disk::fOccupied)));
        value(&runtime.executing.kind,
              bytes.subspan(offsetof(Disk, pending_action), sizeof(Disk::pending_action)));
        value(&runtime.executing.attack_mode,
              bytes.subspan(offsetof(Disk, attack_mode), sizeof(Disk::attack_mode)));
        value(&runtime.committed.kind, bytes.subspan(offsetof(Disk, action), sizeof(Disk::action)));
        value(&runtime.committed.detail,
              bytes.subspan(offsetof(Disk, action_detail0), sizeof(Disk::action_detail0)));
        value(&runtime.remembered.basic_kind,
              bytes.subspan(offsetof(Disk, action_kind), sizeof(Disk::action_kind)));
        value(&runtime.remembered.basic_detail,
              bytes.subspan(offsetof(Disk, action_detail), sizeof(Disk::action_detail)));
        value(&runtime.remembered.spell.detail,
              bytes.subspan(offsetof(Disk, spell_id), sizeof(Disk::spell_id)));
        value(&runtime.remembered.item_id,
              bytes.subspan(offsetof(Disk, item_id), sizeof(Disk::item_id)));
        constexpr W8ItemStorage storage_kinds[] = {W8ItemStorage::Backpack,
                                                   W8ItemStorage::Equipment, W8ItemStorage::Pool};
        auto& location = runtime.remembered.item_detail.item_use.item;
        if constexpr (std::is_const_v<Runtime>) {
            const auto found = std::ranges::find(storage_kinds, location.storage);
            const unsigned char origin =
                found == std::end(storage_kinds) ? 0xff : found - std::begin(storage_kinds);
            const unsigned short slot = location.slot;
            value(&origin, bytes.subspan(offsetof(Disk, item_origin), 1));
            value(&slot, bytes.subspan(offsetof(Disk, item_slot), 2));
        } else {
            unsigned char origin;
            unsigned short slot;
            value(&origin, bytes.subspan(offsetof(Disk, item_origin), 1));
            value(&slot, bytes.subspan(offsetof(Disk, item_slot), 2));
            location = {origin < 3 ? storage_kinds[origin] : W8ItemStorage::None, 0, slot};
        }
        enum_value<signed char>(
            &runtime.remembered.repeat_action,
            bytes.subspan(offsetof(Disk, queued_action), sizeof(Disk::queued_action)));
        value(&runtime.party_order_index,
              bytes.subspan(offsetof(Disk, party_order_index), sizeof(Disk::party_order_index)));
        value(&runtime.item_action_pending, bytes.subspan(offsetof(Disk, item_action_pending),
                                                          sizeof(Disk::item_action_pending)));
        value(&runtime.movement_fatigue,
              bytes.subspan(offsetof(Disk, movement_fatigue), sizeof(Disk::movement_fatigue)));
        value(&runtime.npc_index,
              bytes.subspan(offsetof(Disk, npc_index), sizeof(Disk::npc_index)));
        value(&runtime.npc_bound,
              bytes.subspan(offsetof(Disk, npc_bound), sizeof(Disk::npc_bound)));
        value(&runtime.pending_event_type,
              bytes.subspan(offsetof(Disk, pending_event_type), sizeof(Disk::pending_event_type)));
        value(&runtime.portrait_advance,
              bytes.subspan(offsetof(Disk, portrait_advance), sizeof(Disk::portrait_advance)));
        value(&runtime.remembered.wants_berserk,
              bytes.subspan(offsetof(Disk, action_is_berserk), sizeof(Disk::action_is_berserk)));
        value(&runtime.weapon_swap_pending, bytes.subspan(offsetof(Disk, weapon_swap_pending),
                                                          sizeof(Disk::weapon_swap_pending)));
        auto detail = [&](auto& live, auto field, bool item_use) {
            if (item_use) {
                value(&live.item_use.kind, field.first(4));
                // Retail stores an address here; locations are reconstructed from provenance.
                if constexpr (std::is_const_v<Runtime>)
                    std::fill(field.begin() + 4, field.end(), std::byte{});
            } else {
                value(&live.spell.power_level, field.first(4));
                value(&live.spell.unused, field.subspan(4, 4));
            }
        };
        detail(runtime.executing.detail_block,
               bytes.subspan(offsetof(Disk, pending_action_detail), 8),
               runtime.executing.kind == W8_ACTION_USE_ITEM);
        detail(runtime.committed.detail_block, bytes.subspan(offsetof(Disk, action_detail1), 8),
               runtime.committed.kind == W8_ACTION_USE_ITEM);
        detail(runtime.remembered.spell.detail_block,
               bytes.subspan(offsetof(Disk, spell_detail), 8), false);
        detail(runtime.remembered.item_detail, bytes.subspan(offsetof(Disk, item_detail), 8), true);
        auto target = [&](auto& live, auto field) { nested(live, field, value, nullptr); };
        target(runtime.executing.target,
               bytes.subspan(offsetof(Disk, target_out_of_combat), sizeof(CombatTarget)));
        target(runtime.committed.target,
               bytes.subspan(offsetof(Disk, target_in_combat), sizeof(CombatTarget)));
        target(runtime.remembered.spell.target,
               bytes.subspan(offsetof(Disk, spell_target), sizeof(CombatTarget)));
        target(runtime.remembered.item_target,
               bytes.subspan(offsetof(Disk, item_target), sizeof(CombatTarget)));
        target(runtime.remembered.breath_target,
               bytes.subspan(offsetof(Disk, breath_target), sizeof(CombatTarget)));
    }
};

template<> struct Record<W8MissileTableRecord> {
    using Disk = MissileTableRecord;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        text(runtime.display_name,
             bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)),
             Encoding::utf16);
        text(runtime.cycle_name,
             bytes.subspan(offsetof(Disk, cycle_name), sizeof(Disk::cycle_name)), Encoding::utf8);
        value(&runtime.radius, bytes.subspan(offsetof(Disk, radius), sizeof(Disk::radius)));
        value(&runtime.attack_mode,
              bytes.subspan(offsetof(Disk, attack_mode), sizeof(Disk::attack_mode)));
        value(&runtime.unknown_148,
              bytes.subspan(offsetof(Disk, unknown_148), sizeof(Disk::unknown_148)));
        value(&runtime.magnitude_base,
              bytes.subspan(offsetof(Disk, magnitude_base), sizeof(Disk::magnitude_base)));
        value(&runtime.spell_missile,
              bytes.subspan(offsetof(Disk, spell_missile), sizeof(Disk::spell_missile)));
        value(&runtime.condition_chances,
              bytes.subspan(offsetof(Disk, condition_chances), sizeof(Disk::condition_chances)));
        value(&runtime.weapon_sound_class,
              bytes.subspan(offsetof(Disk, weapon_sound_class), sizeof(Disk::weapon_sound_class)));
        value(&runtime.unknown_169,
              bytes.subspan(offsetof(Disk, unknown_169), sizeof(Disk::unknown_169)));
    }
};

template<> struct Record<W8ItemDatabaseRecord> {
    using Disk = ItemDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        using Properties =
            std::conditional_t<std::is_const_v<R>, const ItemProperties, ItemProperties>;
        using Effects = std::conditional_t<std::is_const_v<R>, const ItemEffects, ItemEffects>;
        value(static_cast<Properties*>(&runtime),
              bytes.subspan(offsetof(Disk, properties), sizeof(ItemProperties)));
        value(static_cast<Effects*>(&runtime),
              bytes.subspan(offsetof(Disk, effects), sizeof(ItemEffects)));
        text(runtime.display_name,
             bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)),
             Encoding::utf16);
        text(runtime.internal_name,
             bytes.subspan(offsetof(Disk, internal_name), sizeof(Disk::internal_name)),
             Encoding::utf8);
        text(runtime.video_object_name,
             bytes.subspan(offsetof(Disk, video_object_name), sizeof(Disk::video_object_name)),
             Encoding::utf8);
        text(runtime.sound_name,
             bytes.subspan(offsetof(Disk, sound_name), sizeof(Disk::sound_name)), Encoding::utf8);
    }
};
template<> struct Record<W8SpellRuntimeRecord> {
    using Disk = SpellRuntimeRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.database_name,
             bytes.subspan(offsetof(Disk, database_name), sizeof(Disk::database_name)),
             Encoding::utf8);
        value(&runtime.serialized.unknown_040,
              bytes.subspan(offsetof(Disk, unknown_040), sizeof(Disk::unknown_040)));
        value(&runtime.duration, bytes.subspan(offsetof(Disk, duration), sizeof(Disk::duration)));
        value(&runtime.alchemy_spell,
              bytes.subspan(offsetof(Disk, alchemy_spell), sizeof(Disk::alchemy_spell)));
        value(&runtime.spell_point_cost,
              bytes.subspan(offsetof(Disk, spell_point_cost), sizeof(Disk::spell_point_cost)));
        value(&runtime.duration_per_level,
              bytes.subspan(offsetof(Disk, duration_per_level), sizeof(Disk::duration_per_level)));
        value(&runtime.effect_dice,
              bytes.subspan(offsetof(Disk, effect_dice), sizeof(Disk::effect_dice)));
        value(&runtime.serialized.unknown_055,
              bytes.subspan(offsetof(Disk, unknown_055), sizeof(Disk::unknown_055)));
        value(&runtime.spell_level,
              bytes.subspan(offsetof(Disk, spell_level), sizeof(Disk::spell_level)));
        value(&runtime.wizardry_spell,
              bytes.subspan(offsetof(Disk, wizardry_spell), sizeof(Disk::wizardry_spell)));
        text(runtime.resource_name,
             bytes.subspan(offsetof(Disk, resource_name), sizeof(Disk::resource_name)),
             Encoding::utf8);
        text(runtime.display_name,
             bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)),
             Encoding::utf16);
        value(&runtime.serialized.unknown_11b,
              bytes.subspan(offsetof(Disk, unknown_11b), sizeof(Disk::unknown_11b)));
        value(&runtime.divinity_spell,
              bytes.subspan(offsetof(Disk, divinity_spell), sizeof(Disk::divinity_spell)));
        value(&runtime.psionics_spell,
              bytes.subspan(offsetof(Disk, psionics_spell), sizeof(Disk::psionics_spell)));
        value(&runtime.effect_radius,
              bytes.subspan(offsetof(Disk, effect_radius), sizeof(Disk::effect_radius)));
        value(&runtime.serialized.unknown_125,
              bytes.subspan(offsetof(Disk, unknown_125), sizeof(Disk::unknown_125)));
        value(&runtime.monster_castable,
              bytes.subspan(offsetof(Disk, monster_castable), sizeof(Disk::monster_castable)));
        value(&runtime.radius_per_level,
              bytes.subspan(offsetof(Disk, radius_per_level), sizeof(Disk::radius_per_level)));
        value(&runtime.power_class,
              bytes.subspan(offsetof(Disk, power_class), sizeof(Disk::power_class)));
        value(&runtime.range_category,
              bytes.subspan(offsetof(Disk, range_category), sizeof(Disk::range_category)));
        value(&runtime.realm, bytes.subspan(offsetof(Disk, realm), sizeof(Disk::realm)));
        value(&runtime.target_type,
              bytes.subspan(offsetof(Disk, target_type), sizeof(Disk::target_type)));
        value(&runtime.usable_when,
              bytes.subspan(offsetof(Disk, usable_when), sizeof(Disk::usable_when)));
        value(&runtime.needs_aim,
              bytes.subspan(offsetof(Disk, needs_aim), sizeof(Disk::needs_aim)));
        value(&runtime.missile_index,
              bytes.subspan(offsetof(Disk, missile_index), sizeof(Disk::missile_index)));
        value(&runtime.missile_delivered,
              bytes.subspan(offsetof(Disk, missile_delivered), sizeof(Disk::missile_delivered)));
        value(&runtime.serialized.unknown_145,
              bytes.subspan(offsetof(Disk, unknown_145), sizeof(Disk::unknown_145)));
        value(&runtime.show_effect_dice,
              bytes.subspan(offsetof(Disk, show_effect_dice), sizeof(Disk::show_effect_dice)));
        text(runtime.sound_name,
             bytes.subspan(offsetof(Disk, sound_name), sizeof(Disk::sound_name)), Encoding::utf8);
    }
};
template<> struct Record<W8FactDatabaseRecord> {
    using Disk = FactDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.identifier,
              bytes.subspan(offsetof(Disk, identifier), sizeof(Disk::identifier)));
        text(runtime.symbolic_name,
             bytes.subspan(offsetof(Disk, symbolic_name), sizeof(Disk::symbolic_name)),
             Encoding::utf8);
        value(&runtime.highlight_when_true, bytes.subspan(offsetof(Disk, highlight_when_true),
                                                          sizeof(Disk::highlight_when_true)));
        value(&runtime.visibility,
              bytes.subspan(offsetof(Disk, visibility), sizeof(Disk::visibility)));
        text(runtime.alternate_description,
             bytes.subspan(offsetof(Disk, alternate_description),
                           sizeof(Disk::alternate_description)),
             Encoding::utf16);
        text(runtime.description,
             bytes.subspan(offsetof(Disk, description), sizeof(Disk::description)),
             Encoding::utf16);
    }
};
template<> struct Record<W8NpcCharacterTemplate> {
    using Disk = NpcCharacterTemplate;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.name, bytes.subspan(offsetof(Disk, name), sizeof(Disk::name)),
             Encoding::utf16);
        text(runtime.name_part_2,
             bytes.subspan(offsetof(Disk, name_part_2), sizeof(Disk::name_part_2)),
             Encoding::utf16);
        value(&runtime.unknown_01a,
              bytes.subspan(offsetof(Disk, unknown_01a), sizeof(Disk::unknown_01a)));
        value(&runtime.profession,
              bytes.subspan(offsetof(Disk, profession), sizeof(Disk::profession)));
        value(&runtime.race, bytes.subspan(offsetof(Disk, race), sizeof(Disk::race)));
        value(&runtime.table_value,
              bytes.subspan(offsetof(Disk, table_value), sizeof(Disk::table_value)));
        value(&runtime.level, bytes.subspan(offsetof(Disk, level), sizeof(Disk::level)));
        value(&runtime.attributes,
              bytes.subspan(offsetof(Disk, attributes), sizeof(Disk::attributes)));
        value(&runtime.skills, bytes.subspan(offsetof(Disk, skills), sizeof(Disk::skills)));
        value(&runtime.spells, bytes.subspan(offsetof(Disk, spells), sizeof(Disk::spells)));
        value(&runtime.equipment_present,
              bytes.subspan(offsetof(Disk, equipment_present), sizeof(Disk::equipment_present)));
        value(&runtime.equipment_ids,
              bytes.subspan(offsetof(Disk, equipment_ids), sizeof(Disk::equipment_ids)));
        value(&runtime.backpack_present,
              bytes.subspan(offsetof(Disk, backpack_present), sizeof(Disk::backpack_present)));
        value(&runtime.backpack_ids,
              bytes.subspan(offsetof(Disk, backpack_ids), sizeof(Disk::backpack_ids)));
        value(&runtime.gender, bytes.subspan(offsetof(Disk, gender), sizeof(Disk::gender)));
        value(&runtime.wanted_item_ids,
              bytes.subspan(offsetof(Disk, wanted_item_ids), sizeof(Disk::wanted_item_ids)));
        value(&runtime.unknown_1fd,
              bytes.subspan(offsetof(Disk, unknown_1fd), sizeof(Disk::unknown_1fd)));
    }
};
template<> struct Record<W8NpcDatabaseRecord> {
    using Disk = NpcDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.version, bytes.subspan(offsetof(Disk, version), sizeof(Disk::version)));
        value(&runtime.trade_pool,
              bytes.subspan(offsetof(Disk, trade_pool), sizeof(Disk::trade_pool)));
        text(runtime.source_name,
             bytes.subspan(offsetof(Disk, source_name), sizeof(Disk::source_name)),
             Encoding::utf16);
        value(&runtime.monster_bound,
              bytes.subspan(offsetof(Disk, monster_bound), sizeof(Disk::monster_bound)));
        value(&runtime.owns_stock,
              bytes.subspan(offsetof(Disk, owns_stock), sizeof(Disk::owns_stock)));
        value(&runtime.merchant, bytes.subspan(offsetof(Disk, merchant), sizeof(Disk::merchant)));
        value(&runtime.has_group,
              bytes.subspan(offsetof(Disk, has_group), sizeof(Disk::has_group)));
        value(&runtime.kind, bytes.subspan(offsetof(Disk, kind), sizeof(Disk::kind)));
        value(&runtime.disposition,
              bytes.subspan(offsetof(Disk, disposition), sizeof(Disk::disposition)));
        value(&runtime.charm_scale,
              bytes.subspan(offsetof(Disk, charm_scale), sizeof(Disk::charm_scale)));
        value(&runtime.talk_scale,
              bytes.subspan(offsetof(Disk, talk_scale), sizeof(Disk::talk_scale)));
        value(&runtime.faction, bytes.subspan(offsetof(Disk, faction), sizeof(Disk::faction)));
        value(&runtime.name_alias_mask,
              bytes.subspan(offsetof(Disk, name_alias_mask), sizeof(Disk::name_alias_mask)));
        value(&runtime.item_table_id,
              bytes.subspan(offsetof(Disk, item_table_id), sizeof(Disk::item_table_id)));
        value(&runtime.service_flags,
              bytes.subspan(offsetof(Disk, service_flags), sizeof(Disk::service_flags)));
        value(&runtime.unknown_06c,
              bytes.subspan(offsetof(Disk, unknown_06c), sizeof(Disk::unknown_06c)));
        value(&runtime.allied_faction,
              bytes.subspan(offsetof(Disk, allied_faction), sizeof(Disk::allied_faction)));
        value(&runtime.min_party_level,
              bytes.subspan(offsetof(Disk, min_party_level), sizeof(Disk::min_party_level)));
        value(&runtime.restore_level,
              bytes.subspan(offsetof(Disk, restore_level), sizeof(Disk::restore_level)));
        value(&runtime.unknown_071,
              bytes.subspan(offsetof(Disk, unknown_071), sizeof(Disk::unknown_071)));
        text(runtime.restore_entity_name,
             bytes.subspan(offsetof(Disk, restore_entity_name), sizeof(Disk::restore_entity_name)),
             Encoding::utf8);
        value(&runtime.no_item_stock,
              bytes.subspan(offsetof(Disk, no_item_stock), sizeof(Disk::no_item_stock)));
        text(runtime.display_name,
             bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)),
             Encoding::utf8);
        Record<W8NpcCharacterTemplate>::fields(
            runtime.character, bytes.subspan(offsetof(Disk, character), sizeof(Disk::character)),
            value, text);
        presence_slot(runtime.item_stock_rules, bytes.subspan(offsetof(Disk, item_stock_rules),
                                                              sizeof(Disk::item_stock_rules)));
        value(&runtime.buy_price_factor,
              bytes.subspan(offsetof(Disk, buy_price_factor), sizeof(Disk::buy_price_factor)));
        value(&runtime.sell_price_factor,
              bytes.subspan(offsetof(Disk, sell_price_factor), sizeof(Disk::sell_price_factor)));
        value(&runtime.trade_item_class_mask, bytes.subspan(offsetof(Disk, trade_item_class_mask),
                                                            sizeof(Disk::trade_item_class_mask)));
        value(&runtime.unknown_2da,
              bytes.subspan(offsetof(Disk, unknown_2da), sizeof(Disk::unknown_2da)));
        value(&runtime.voice_script,
              bytes.subspan(offsetof(Disk, voice_script), sizeof(Disk::voice_script)));
        value(&runtime.gold, bytes.subspan(offsetof(Disk, gold), sizeof(Disk::gold)));
        value(&runtime.combat_script_notice_enabled,
              bytes.subspan(offsetof(Disk, combat_script_notice_enabled),
                            sizeof(Disk::combat_script_notice_enabled)));
        value(&runtime.allow_dismissed_departure_dialogue,
              bytes.subspan(offsetof(Disk, allow_dismissed_departure_dialogue),
                            sizeof(Disk::allow_dismissed_departure_dialogue)));
        value(&runtime.unknown_2f1,
              bytes.subspan(offsetof(Disk, unknown_2f1), sizeof(Disk::unknown_2f1)));
    }
};
template<> struct Record<W8LevelDatabaseRecord> {
    using Disk = LevelDatabaseRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.display_name,
             bytes.subspan(offsetof(Disk, display_name), sizeof(Disk::display_name)),
             Encoding::utf16);
        value(&runtime.maximum_random_encounters,
              bytes.subspan(offsetof(Disk, maximum_random_encounters),
                            sizeof(Disk::maximum_random_encounters)));
        value(&runtime.minimum_random_encounters,
              bytes.subspan(offsetof(Disk, minimum_random_encounters),
                            sizeof(Disk::minimum_random_encounters)));
        value(&runtime.maximum_encounter_budget,
              bytes.subspan(offsetof(Disk, maximum_encounter_budget),
                            sizeof(Disk::maximum_encounter_budget)));
        value(&runtime.minimum_encounter_budget,
              bytes.subspan(offsetof(Disk, minimum_encounter_budget),
                            sizeof(Disk::minimum_encounter_budget)));
        value(&runtime.encounter_budget_period,
              bytes.subspan(offsetof(Disk, encounter_budget_period),
                            sizeof(Disk::encounter_budget_period)));
        value(&runtime.encounter_culling_seconds,
              bytes.subspan(offsetof(Disk, encounter_culling_seconds),
                            sizeof(Disk::encounter_culling_seconds)));
        value(&runtime.gameplay_time_scale, bytes.subspan(offsetof(Disk, gameplay_time_scale),
                                                          sizeof(Disk::gameplay_time_scale)));
        value(&runtime.unknown_058,
              bytes.subspan(offsetof(Disk, unknown_058), sizeof(Disk::unknown_058)));
    }
};
template<> struct Record<W8MonsterRecord> {
    using Disk = MonsterRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        text(runtime.name0, bytes.subspan(offsetof(Disk, name0), sizeof(Disk::name0)),
             Encoding::utf16);
        text(runtime.name1, bytes.subspan(offsetof(Disk, name1), sizeof(Disk::name1)),
             Encoding::utf16);
        text(runtime.name2, bytes.subspan(offsetof(Disk, name2), sizeof(Disk::name2)),
             Encoding::utf16);
        text(runtime.name3, bytes.subspan(offsetof(Disk, name3), sizeof(Disk::name3)),
             Encoding::utf16);
        byte_boolean(&runtime.can_open_doors,
                     bytes.subspan(offsetof(Disk, can_open_doors), sizeof(Disk::can_open_doors)));
        value(&runtime.group_size_dice,
              bytes.subspan(offsetof(Disk, group_size_dice), sizeof(Disk::group_size_dice)));
        value(&runtime.companions,
              bytes.subspan(offsetof(Disk, companions), sizeof(Disk::companions)));
        value(&runtime.kind, bytes.subspan(offsetof(Disk, kind), sizeof(Disk::kind)));
        value(&runtime.name_group,
              bytes.subspan(offsetof(Disk, name_group), sizeof(Disk::name_group)));
        value(&runtime.npc_kind, bytes.subspan(offsetof(Disk, npc_kind), sizeof(Disk::npc_kind)));
        value(&runtime.stamina_regeneration, bytes.subspan(offsetof(Disk, stamina_regeneration),
                                                           sizeof(Disk::stamina_regeneration)));
        value(&runtime.damage_reduction,
              bytes.subspan(offsetof(Disk, damage_reduction), sizeof(Disk::damage_reduction)));
        value(&runtime.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.attribute_values,
              bytes.subspan(offsetof(Disk, attribute_values), sizeof(Disk::attribute_values)));
        value(&runtime.hit_points,
              bytes.subspan(offsetof(Disk, hit_points), sizeof(Disk::hit_points)));
        value(&runtime.stamina_dice,
              bytes.subspan(offsetof(Disk, stamina_dice), sizeof(Disk::stamina_dice)));
        value(&runtime.unknown_0de,
              bytes.subspan(offsetof(Disk, unknown_0de), sizeof(Disk::unknown_0de)));
        value(&runtime.spell_chance,
              bytes.subspan(offsetof(Disk, spell_chance), sizeof(Disk::spell_chance)));
        value(&runtime.flee_chance,
              bytes.subspan(offsetof(Disk, flee_chance), sizeof(Disk::flee_chance)));
        value(&runtime.advance_chance,
              bytes.subspan(offsetof(Disk, advance_chance), sizeof(Disk::advance_chance)));
        value(&runtime.special_attack_kind, bytes.subspan(offsetof(Disk, special_attack_kind),
                                                          sizeof(Disk::special_attack_kind)));
        value(&runtime.initiative,
              bytes.subspan(offsetof(Disk, initiative), sizeof(Disk::initiative)));
        value(&runtime.attacks_per_round,
              bytes.subspan(offsetof(Disk, attacks_per_round), sizeof(Disk::attacks_per_round)));
        value(&runtime.swings_per_round,
              bytes.subspan(offsetof(Disk, swings_per_round), sizeof(Disk::swings_per_round)));
        value(&runtime.attacks, bytes.subspan(offsetof(Disk, attacks), sizeof(Disk::attacks)));
        value(&runtime.spells, bytes.subspan(offsetof(Disk, spells), sizeof(Disk::spells)));
        value(&runtime.attack_body_part_chances,
              bytes.subspan(offsetof(Disk, attack_body_part_chances),
                            sizeof(Disk::attack_body_part_chances)));
        value(&runtime.special_attack_cooldown,
              bytes.subspan(offsetof(Disk, special_attack_cooldown),
                            sizeof(Disk::special_attack_cooldown)));
        value(&runtime.evasion, bytes.subspan(offsetof(Disk, evasion), sizeof(Disk::evasion)));
        value(&runtime.constitution,
              bytes.subspan(offsetof(Disk, constitution), sizeof(Disk::constitution)));
        value(&runtime.hit_location_chances, bytes.subspan(offsetof(Disk, hit_location_chances),
                                                           sizeof(Disk::hit_location_chances)));
        value(&runtime.armor_class_by_location,
              bytes.subspan(offsetof(Disk, armor_class_by_location),
                            sizeof(Disk::armor_class_by_location)));
        value(&runtime.armor_class_by_attack_mode,
              bytes.subspan(offsetof(Disk, armor_class_by_attack_mode),
                            sizeof(Disk::armor_class_by_attack_mode)));
        value(&runtime.resistances,
              bytes.subspan(offsetof(Disk, resistances), sizeof(Disk::resistances)));
        value(&runtime.hp_regeneration,
              bytes.subspan(offsetof(Disk, hp_regeneration), sizeof(Disk::hp_regeneration)));
        value(&runtime.unknown_17d,
              bytes.subspan(offsetof(Disk, unknown_17d), sizeof(Disk::unknown_17d)));
        value(&runtime.experience,
              bytes.subspan(offsetof(Disk, experience), sizeof(Disk::experience)));
        value(&runtime.unknown_185,
              bytes.subspan(offsetof(Disk, unknown_185), sizeof(Disk::unknown_185)));
        value(&runtime.record_id,
              bytes.subspan(offsetof(Disk, record_id), sizeof(Disk::record_id)));
        text(runtime.cycle_name,
             bytes.subspan(offsetof(Disk, cycle_name), sizeof(Disk::cycle_name)), Encoding::utf8);
        value(&runtime.prefer_ranged_actions, bytes.subspan(offsetof(Disk, prefer_ranged_actions),
                                                            sizeof(Disk::prefer_ranged_actions)));
        value(&runtime.combat_move_range,
              bytes.subspan(offsetof(Disk, combat_move_range), sizeof(Disk::combat_move_range)));
        value(&runtime.instant_death_immune, bytes.subspan(offsetof(Disk, instant_death_immune),
                                                           sizeof(Disk::instant_death_immune)));
        value(&runtime.combat_behavior,
              bytes.subspan(offsetof(Disk, combat_behavior), sizeof(Disk::combat_behavior)));
        value(&runtime.combat_morale,
              bytes.subspan(offsetof(Disk, combat_morale), sizeof(Disk::combat_morale)));
        value(&runtime.editor_index,
              bytes.subspan(offsetof(Disk, editor_index), sizeof(Disk::editor_index)));
        text(runtime.remains_model_name,
             bytes.subspan(offsetof(Disk, remains_model_name), sizeof(Disk::remains_model_name)),
             Encoding::utf8);
        value(&runtime.treasure, bytes.subspan(offsetof(Disk, treasure), sizeof(Disk::treasure)));
        value(&runtime.attack_multiple_targets,
              bytes.subspan(offsetof(Disk, attack_multiple_targets),
                            sizeof(Disk::attack_multiple_targets)));
        value(&runtime.camouflage0,
              bytes.subspan(offsetof(Disk, camouflage0), sizeof(Disk::camouflage0)));
        value(&runtime.camouflage1,
              bytes.subspan(offsetof(Disk, camouflage1), sizeof(Disk::camouflage1)));
        value(&runtime.untargetable,
              bytes.subspan(offsetof(Disk, untargetable), sizeof(Disk::untargetable)));
        value(&runtime.unknown_24b,
              bytes.subspan(offsetof(Disk, unknown_24b), sizeof(Disk::unknown_24b)));
        value(&runtime.effective_level,
              bytes.subspan(offsetof(Disk, effective_level), sizeof(Disk::effective_level)));
        value(&runtime.unknown_250,
              bytes.subspan(offsetof(Disk, unknown_250), sizeof(Disk::unknown_250)));
        value(&runtime.display_level,
              bytes.subspan(offsetof(Disk, display_level), sizeof(Disk::display_level)));
        value(&runtime.unknown_252,
              bytes.subspan(offsetof(Disk, unknown_252), sizeof(Disk::unknown_252)));
        value(&runtime.model_index,
              bytes.subspan(offsetof(Disk, model_index), sizeof(Disk::model_index)));
        value(&runtime.alternate_model_index, bytes.subspan(offsetof(Disk, alternate_model_index),
                                                            sizeof(Disk::alternate_model_index)));
        value(&runtime.hostility_radius,
              bytes.subspan(offsetof(Disk, hostility_radius), sizeof(Disk::hostility_radius)));
        enum_value<std::int32_t>(&runtime.faction_id, bytes.subspan(offsetof(Disk, faction_id),
                                                                    sizeof(Disk::faction_id)));
        value(&runtime.material, bytes.subspan(offsetof(Disk, material), sizeof(Disk::material)));
        value(&runtime.deleted, bytes.subspan(offsetof(Disk, deleted), sizeof(Disk::deleted)));
        value(&runtime.significant_kill,
              bytes.subspan(offsetof(Disk, significant_kill), sizeof(Disk::significant_kill)));
        value(&runtime.unknown_269,
              bytes.subspan(offsetof(Disk, unknown_269), sizeof(Disk::unknown_269)));
        value(&runtime.unborn, bytes.subspan(offsetof(Disk, unborn), sizeof(Disk::unborn)));
        value(&runtime.experience_override, bytes.subspan(offsetof(Disk, experience_override),
                                                          sizeof(Disk::experience_override)));
        value(&runtime.sp_budget,
              bytes.subspan(offsetof(Disk, sp_budget), sizeof(Disk::sp_budget)));
        value(&runtime.unknown_273,
              bytes.subspan(offsetof(Disk, unknown_273), sizeof(Disk::unknown_273)));
    }
};
template<> struct Record<W8CharacterSkill> {
    using Disk = CharacterSkill;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.active, bytes.subspan(offsetof(Disk, active), sizeof(Disk::active)));
        value(&runtime.reset_flag,
              bytes.subspan(offsetof(Disk, reset_flag), sizeof(Disk::reset_flag)));
        value(&runtime.points, bytes.subspan(offsetof(Disk, points), sizeof(Disk::points)));
        value(&runtime.level, bytes.subspan(offsetof(Disk, level), sizeof(Disk::level)));
        value(&runtime.base_level,
              bytes.subspan(offsetof(Disk, base_level), sizeof(Disk::base_level)));
        value(&runtime.practice_count,
              bytes.subspan(offsetof(Disk, practice_count), sizeof(Disk::practice_count)));
        value(&runtime.improved, bytes.subspan(offsetof(Disk, improved), sizeof(Disk::improved)));
        value(&runtime.available,
              bytes.subspan(offsetof(Disk, available), sizeof(Disk::available)));
        value(&runtime.unknown_14,
              bytes.subspan(offsetof(Disk, unknown_14), sizeof(Disk::unknown_14)));
    }
};
template<> struct Record<W8HandAttack> {
    using Disk = HandAttack;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.uiHolds, bytes.subspan(offsetof(Disk, uiHolds), sizeof(Disk::uiHolds)));
        value(&runtime.in_play, bytes.subspan(offsetof(Disk, in_play), sizeof(Disk::in_play)));
        value(&runtime.weapon_skill,
              bytes.subspan(offsetof(Disk, weapon_skill), sizeof(Disk::weapon_skill)));
        value(&runtime.combat_skill,
              bytes.subspan(offsetof(Disk, combat_skill), sizeof(Disk::combat_skill)));
        value(&runtime.combined_skill,
              bytes.subspan(offsetof(Disk, combined_skill), sizeof(Disk::combined_skill)));
        value(&runtime.attack_score,
              bytes.subspan(offsetof(Disk, attack_score), sizeof(Disk::attack_score)));
        value(&runtime.attacks, bytes.subspan(offsetof(Disk, attacks), sizeof(Disk::attacks)));
        value(&runtime.swings, bytes.subspan(offsetof(Disk, swings), sizeof(Disk::swings)));
        value(&runtime.damage_bonus,
              bytes.subspan(offsetof(Disk, damage_bonus), sizeof(Disk::damage_bonus)));
        value(&runtime.hit_bonus,
              bytes.subspan(offsetof(Disk, hit_bonus), sizeof(Disk::hit_bonus)));
        value(&runtime.attack_bonus,
              bytes.subspan(offsetof(Disk, attack_bonus), sizeof(Disk::attack_bonus)));
        value(&runtime.damage_percent,
              bytes.subspan(offsetof(Disk, damage_percent), sizeof(Disk::damage_percent)));
        value(&runtime.damage_dice,
              bytes.subspan(offsetof(Disk, damage_dice), sizeof(Disk::damage_dice)));
        value(&runtime.attack_flags,
              bytes.subspan(offsetof(Disk, attack_flags), sizeof(Disk::attack_flags)));
        value(&runtime.condition_chances,
              bytes.subspan(offsetof(Disk, condition_chances), sizeof(Disk::condition_chances)));
        value(&runtime.unknown_43,
              bytes.subspan(offsetof(Disk, unknown_43), sizeof(Disk::unknown_43)));
    }
};
template<> struct Record<W8CharacterConditionRecord> {
    using Disk = CharacterConditionRecord;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.level_acquired,
              bytes.subspan(offsetof(Disk, level_acquired), sizeof(Disk::level_acquired)));
        value(&runtime.source_monster,
              bytes.subspan(offsetof(Disk, source_monster), sizeof(Disk::source_monster)));
        value(&runtime.active, bytes.subspan(offsetof(Disk, active), sizeof(Disk::active)));
        value(&runtime.unknown_09,
              bytes.subspan(offsetof(Disk, unknown_09), sizeof(Disk::unknown_09)));
    }
};
template<> struct Record<W8EffectSlot> {
    using Disk = EffectSlot;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.active, bytes.subspan(offsetof(Disk, active), sizeof(Disk::active)));
        value(&runtime.effect_id,
              bytes.subspan(offsetof(Disk, effect_id), sizeof(Disk::effect_id)));
        value(&runtime.amount, bytes.subspan(offsetof(Disk, amount), sizeof(Disk::amount)));
        value(&runtime.percent, bytes.subspan(offsetof(Disk, percent), sizeof(Disk::percent)));
        value(&runtime.duration, bytes.subspan(offsetof(Disk, duration), sizeof(Disk::duration)));
    }
};
template<> struct Record<W8Character> {
    using Disk = Character;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.record_version,
              bytes.subspan(offsetof(Disk, record_version), sizeof(Disk::record_version)));
        value(&runtime.fInParty, bytes.subspan(offsetof(Disk, fInParty), sizeof(Disk::fInParty)));
        text(runtime.name, bytes.subspan(offsetof(Disk, name), sizeof(Disk::name)),
             Encoding::utf16);
        text(runtime.name_part_2,
             bytes.subspan(offsetof(Disk, name_part_2), sizeof(Disk::name_part_2)),
             Encoding::utf16);
        value(&runtime.unknown_0025,
              bytes.subspan(offsetof(Disk, unknown_0025), sizeof(Disk::unknown_0025)));
        value(&runtime.iProfession,
              bytes.subspan(offsetof(Disk, iProfession), sizeof(Disk::iProfession)));
        value(&runtime.original_profession, bytes.subspan(offsetof(Disk, original_profession),
                                                          sizeof(Disk::original_profession)));
        value(&runtime.iRace, bytes.subspan(offsetof(Disk, iRace), sizeof(Disk::iRace)));
        value(&runtime.gender, bytes.subspan(offsetof(Disk, gender), sizeof(Disk::gender)));
        value(&runtime.portrait_index,
              bytes.subspan(offsetof(Disk, portrait_index), sizeof(Disk::portrait_index)));
        value(&runtime.unknown_007d,
              bytes.subspan(offsetof(Disk, unknown_007d), sizeof(Disk::unknown_007d)));
        value(&runtime.personality,
              bytes.subspan(offsetof(Disk, personality), sizeof(Disk::personality)));
        value(&runtime.voice, bytes.subspan(offsetof(Disk, voice), sizeof(Disk::voice)));
        value(&runtime.uiExpLevel,
              bytes.subspan(offsetof(Disk, uiExpLevel), sizeof(Disk::uiExpLevel)));
        value(&runtime.profession_levels,
              bytes.subspan(offsetof(Disk, profession_levels), sizeof(Disk::profession_levels)));
        value(&runtime.unknown_00c9,
              bytes.subspan(offsetof(Disk, unknown_00c9), sizeof(Disk::unknown_00c9)));
        value(&runtime.level_band,
              bytes.subspan(offsetof(Disk, level_band), sizeof(Disk::level_band)));
        value(&runtime.level_band_base,
              bytes.subspan(offsetof(Disk, level_band_base), sizeof(Disk::level_band_base)));
        value(&runtime.attributes,
              bytes.subspan(offsetof(Disk, attributes), sizeof(Disk::attributes)));
        value(&runtime.unknown_0171,
              bytes.subspan(offsetof(Disk, unknown_0171), sizeof(Disk::unknown_0171)));
        value(&runtime.attribute_point_deficit,
              bytes.subspan(offsetof(Disk, attribute_point_deficit),
                            sizeof(Disk::attribute_point_deficit)));
        for (std::size_t index = 0; index < std::size(runtime.skills); ++index)
            Record<W8CharacterSkill>::fields(
                runtime.skills[index],
                bytes.subspan(offsetof(Disk, skills) + index * sizeof(CharacterSkill),
                              sizeof(CharacterSkill)),
                value, text);
        value(&runtime.unknown_07b3,
              bytes.subspan(offsetof(Disk, unknown_07b3), sizeof(Disk::unknown_07b3)));
        value(&runtime.experience,
              bytes.subspan(offsetof(Disk, experience), sizeof(Disk::experience)));
        value(&runtime.experience_goal,
              bytes.subspan(offsetof(Disk, experience_goal), sizeof(Disk::experience_goal)));
        value(&runtime.experience_previous_goal,
              bytes.subspan(offsetof(Disk, experience_previous_goal),
                            sizeof(Disk::experience_previous_goal)));
        value(&runtime.kill_count,
              bytes.subspan(offsetof(Disk, kill_count), sizeof(Disk::kill_count)));
        value(&runtime.death_count,
              bytes.subspan(offsetof(Disk, death_count), sizeof(Disk::death_count)));
        value(&runtime.uiCondition,
              bytes.subspan(offsetof(Disk, uiCondition), sizeof(Disk::uiCondition)));
        value(&runtime.unknown_0a51,
              bytes.subspan(offsetof(Disk, unknown_0a51), sizeof(Disk::unknown_0a51)));
        value(&runtime.enchantments,
              bytes.subspan(offsetof(Disk, enchantments), sizeof(Disk::enchantments)));
        value(&runtime.unknown_0ac5,
              bytes.subspan(offsetof(Disk, unknown_0ac5), sizeof(Disk::unknown_0ac5)));
        value(&runtime.highest_condition,
              bytes.subspan(offsetof(Disk, highest_condition), sizeof(Disk::highest_condition)));
        value(&runtime.enchantment_top,
              bytes.subspan(offsetof(Disk, enchantment_top), sizeof(Disk::enchantment_top)));
        value(&runtime.condition_argument,
              bytes.subspan(offsetof(Disk, condition_argument), sizeof(Disk::condition_argument)));
        value(&runtime.uiHPMax, bytes.subspan(offsetof(Disk, uiHPMax), sizeof(Disk::uiHPMax)));
        value(&runtime.hp_current,
              bytes.subspan(offsetof(Disk, hp_current), sizeof(Disk::hp_current)));
        value(&runtime.hp_adjustment,
              bytes.subspan(offsetof(Disk, hp_adjustment), sizeof(Disk::hp_adjustment)));
        value(&runtime.uiStaminaMax,
              bytes.subspan(offsetof(Disk, uiStaminaMax), sizeof(Disk::uiStaminaMax)));
        value(&runtime.stamina, bytes.subspan(offsetof(Disk, stamina), sizeof(Disk::stamina)));
        value(&runtime.fatigue_penalty,
              bytes.subspan(offsetof(Disk, fatigue_penalty), sizeof(Disk::fatigue_penalty)));
        value(&runtime.sp_max, bytes.subspan(offsetof(Disk, sp_max), sizeof(Disk::sp_max)));
        value(&runtime.unknown_0b3d,
              bytes.subspan(offsetof(Disk, unknown_0b3d), sizeof(Disk::unknown_0b3d)));
        value(&runtime.iSPLeft, bytes.subspan(offsetof(Disk, iSPLeft), sizeof(Disk::iSPLeft)));
        value(&runtime.unknown_0b5d,
              bytes.subspan(offsetof(Disk, unknown_0b5d), sizeof(Disk::unknown_0b5d)));
        value(&runtime.potion_brew_cooldown, bytes.subspan(offsetof(Disk, potion_brew_cooldown),
                                                           sizeof(Disk::potion_brew_cooldown)));
        value(&runtime.health_regen_rate,
              bytes.subspan(offsetof(Disk, health_regen_rate), sizeof(Disk::health_regen_rate)));
        value(&runtime.health_regen_accumulator,
              bytes.subspan(offsetof(Disk, health_regen_accumulator),
                            sizeof(Disk::health_regen_accumulator)));
        value(&runtime.stamina_regen_rate,
              bytes.subspan(offsetof(Disk, stamina_regen_rate), sizeof(Disk::stamina_regen_rate)));
        value(&runtime.stamina_regen_accumulator,
              bytes.subspan(offsetof(Disk, stamina_regen_accumulator),
                            sizeof(Disk::stamina_regen_accumulator)));
        value(&runtime.spell_regen_rates,
              bytes.subspan(offsetof(Disk, spell_regen_rates), sizeof(Disk::spell_regen_rates)));
        value(&runtime.unknown_0ba9,
              bytes.subspan(offsetof(Disk, unknown_0ba9), sizeof(Disk::unknown_0ba9)));
        value(&runtime.inventory_weight,
              bytes.subspan(offsetof(Disk, inventory_weight), sizeof(Disk::inventory_weight)));
        value(&runtime.party_weight_share,
              bytes.subspan(offsetof(Disk, party_weight_share), sizeof(Disk::party_weight_share)));
        value(&runtime.total_carried_weight, bytes.subspan(offsetof(Disk, total_carried_weight),
                                                           sizeof(Disk::total_carried_weight)));
        value(&runtime.carrying_capacity,
              bytes.subspan(offsetof(Disk, carrying_capacity), sizeof(Disk::carrying_capacity)));
        value(&runtime.load_category,
              bytes.subspan(offsetof(Disk, load_category), sizeof(Disk::load_category)));
        value(&runtime.spell_learned,
              bytes.subspan(offsetof(Disk, spell_learned), sizeof(Disk::spell_learned)));
        value(&runtime.skill_unlocks,
              bytes.subspan(offsetof(Disk, skill_unlocks), sizeof(Disk::skill_unlocks)));
        value(&runtime.initiative,
              bytes.subspan(offsetof(Disk, initiative), sizeof(Disk::initiative)));
        value(&runtime.armor_class_total,
              bytes.subspan(offsetof(Disk, armor_class_total), sizeof(Disk::armor_class_total)));
        value(&runtime.armor_class_average, bytes.subspan(offsetof(Disk, armor_class_average),
                                                          sizeof(Disk::armor_class_average)));
        value(&runtime.armor_class_components, bytes.subspan(offsetof(Disk, armor_class_components),
                                                             sizeof(Disk::armor_class_components)));
        value(&runtime.armor_class_by_location,
              bytes.subspan(offsetof(Disk, armor_class_by_location),
                            sizeof(Disk::armor_class_by_location)));
        value(&runtime.unknown_0ed5,
              bytes.subspan(offsetof(Disk, unknown_0ed5), sizeof(Disk::unknown_0ed5)));
        value(&runtime.damage_reduction,
              bytes.subspan(offsetof(Disk, damage_reduction), sizeof(Disk::damage_reduction)));
        value(&runtime.resistances,
              bytes.subspan(offsetof(Disk, resistances), sizeof(Disk::resistances)));
        value(&runtime.flags0, bytes.subspan(offsetof(Disk, flags0), sizeof(Disk::flags0)));
        value(&runtime.EquippedItem,
              bytes.subspan(offsetof(Disk, EquippedItem), sizeof(Disk::EquippedItem)));
        value(&runtime.flags1, bytes.subspan(offsetof(Disk, flags1), sizeof(Disk::flags1)));
        value(&runtime.backpack, bytes.subspan(offsetof(Disk, backpack), sizeof(Disk::backpack)));
        value(&runtime.unknown_1089,
              bytes.subspan(offsetof(Disk, unknown_1089), sizeof(Disk::unknown_1089)));
        for (std::size_t index = 0; index < std::size(runtime.Hand); ++index)
            Record<W8HandAttack>::fields(
                runtime.Hand[index],
                bytes.subspan(offsetof(Disk, Hand) + index * sizeof(HandAttack),
                              sizeof(HandAttack)),
                value, text);
        value(&runtime.unknown_11ff,
              bytes.subspan(offsetof(Disk, unknown_11ff), sizeof(Disk::unknown_11ff)));
        value(&runtime.dual_wielding,
              bytes.subspan(offsetof(Disk, dual_wielding), sizeof(Disk::dual_wielding)));
        value(&runtime.monster_awareness,
              bytes.subspan(offsetof(Disk, monster_awareness), sizeof(Disk::monster_awareness)));
        value(&runtime.fatigue_band,
              bytes.subspan(offsetof(Disk, fatigue_band), sizeof(Disk::fatigue_band)));
        value(&runtime.condition_modifiers, bytes.subspan(offsetof(Disk, condition_modifiers),
                                                          sizeof(Disk::condition_modifiers)));
        value(&runtime.equipment_bonus,
              bytes.subspan(offsetof(Disk, equipment_bonus), sizeof(Disk::equipment_bonus)));
        value(&runtime.bonus, bytes.subspan(offsetof(Disk, bonus), sizeof(Disk::bonus)));
        nested(runtime.saved_location,
               bytes.subspan(offsetof(Disk, saved_location), sizeof(Disk::saved_location)), value,
               nullptr);
        value(&runtime.saved_level,
              bytes.subspan(offsetof(Disk, saved_level), sizeof(Disk::saved_level)));
        for (std::size_t index = 0; index < std::size(runtime.conditions); ++index)
            Record<W8CharacterConditionRecord>::fields(
                runtime.conditions[index],
                bytes.subspan(offsetof(Disk, conditions) + index * sizeof(CharacterConditionRecord),
                              sizeof(CharacterConditionRecord)),
                value, text);
        value(&runtime.deep_fatigue_applied, bytes.subspan(offsetof(Disk, deep_fatigue_applied),
                                                           sizeof(Disk::deep_fatigue_applied)));
        value(&runtime.skill_costs,
              bytes.subspan(offsetof(Disk, skill_costs), sizeof(Disk::skill_costs)));
        value(&runtime.magic_bonus_pool,
              bytes.subspan(offsetof(Disk, magic_bonus_pool), sizeof(Disk::magic_bonus_pool)));
        value(&runtime.has_saved_location,
              bytes.subspan(offsetof(Disk, has_saved_location), sizeof(Disk::has_saved_location)));
    }
};
template<> struct Record<W8LevelProgressRow> {
    using Disk = LevelProgress;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.visited, bytes.subspan(offsetof(Disk, visited), sizeof(Disk::visited)));
        value(&runtime.combat_end_count,
              bytes.subspan(offsetof(Disk, combat_end_count), sizeof(Disk::combat_end_count)));
        value(&runtime.monster_kill_count,
              bytes.subspan(offsetof(Disk, monster_kill_count), sizeof(Disk::monster_kill_count)));
        value(&runtime.experience_gained,
              bytes.subspan(offsetof(Disk, experience_gained), sizeof(Disk::experience_gained)));
        value(&runtime.gold_collected,
              bytes.subspan(offsetof(Disk, gold_collected), sizeof(Disk::gold_collected)));
        value(&runtime.sight_clock,
              bytes.subspan(offsetof(Disk, sight_clock), sizeof(Disk::sight_clock)));
        value(&runtime.unknown_11,
              bytes.subspan(offsetof(Disk, unknown_11), sizeof(Disk::unknown_11)));
    }
};
template<> struct Record<W8SavedGameStatus> {
    using Disk = GlobalStatus;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.status.save_version, bytes.subspan(offsetof(Disk, buffers.save_version),
                                                          sizeof(Disk::buffers.save_version)));
        std::uint32_t party_present = 1;
        value(&party_present, bytes.subspan(offsetof(Disk, buffers.characters), 4));
        party_present = 1;
        value(&party_present, bytes.subspan(offsetof(Disk, buffers.party_rows), 4));
        value(&runtime.status.game_started,
              bytes.subspan(offsetof(Disk, game_started), sizeof(Disk::game_started)));
        // Retail stores three counters; runtime membership has one source of truth.
        if constexpr (std::is_const_v<R>) {
            int regular = 0, auxiliary = 0;
            for (const auto& row : runtime.party.rows) {
                if (!row.fOccupied) continue;
                if (row.npc_index == -1)
                    ++regular;
                else
                    ++auxiliary;
            }
            const int total_count = regular + auxiliary;
            value(&regular, bytes.subspan(offsetof(Disk, regular_member_count), 4));
            value(&auxiliary, bytes.subspan(offsetof(Disk, auxiliary_member_count), 4));
            value(&total_count, bytes.subspan(offsetof(Disk, total_member_count), 4));
        }
        value(&runtime.party.gold,
              bytes.subspan(offsetof(Disk, party_gold), sizeof(Disk::party_gold)));
        value(&runtime.status.selected_character,
              bytes.subspan(offsetof(Disk, selected_character), sizeof(Disk::selected_character)));
        value(&runtime.party.item_pool,
              bytes.subspan(offsetof(Disk, party_item_pool), sizeof(Disk::party_item_pool)));
        value(&runtime.party.item_count,
              bytes.subspan(offsetof(Disk, party_item_count), sizeof(Disk::party_item_count)));
        value(&runtime.status.text_line_cursor,
              bytes.subspan(offsetof(Disk, text_line_cursor), sizeof(Disk::text_line_cursor)));
        value(&runtime.status.legacy_text_box_lines,
              bytes.subspan(offsetof(Disk, legacy_text_box_lines),
                            sizeof(Disk::legacy_text_box_lines)));
        for (std::size_t index = 0; index < std::size(runtime.status.effect_slots); ++index)
            Record<W8EffectSlot>::fields(
                runtime.status.effect_slots[index],
                bytes.subspan(offsetof(Disk, effect_slots) + index * sizeof(EffectSlot),
                              sizeof(EffectSlot)),
                value, text);
        value(&runtime.status.unknown_187b,
              bytes.subspan(offsetof(Disk, unknown_187b), sizeof(Disk::unknown_187b)));
        value(&runtime.status.party_facing,
              bytes.subspan(offsetof(Disk, party_facing), sizeof(Disk::party_facing)));
        value(&runtime.status.party_heading,
              bytes.subspan(offsetof(Disk, party_heading), sizeof(Disk::party_heading)));
        value(&runtime.status.world_clock,
              bytes.subspan(offsetof(Disk, world_clock), sizeof(Disk::world_clock)));
        value(&runtime.status.world_clock_ms,
              bytes.subspan(offsetof(Disk, world_clock_ms), sizeof(Disk::world_clock_ms)));
        value(&runtime.party.order_slots,
              bytes.subspan(offsetof(Disk, party_order_slots), sizeof(Disk::party_order_slots)));
        value(&runtime.status.resume_level,
              bytes.subspan(offsetof(Disk, current_level), sizeof(Disk::current_level)));
        value(&runtime.status.status_header_prefix,
              bytes.subspan(offsetof(Disk, status_header_prefix),
                            sizeof(Disk::status_header_prefix)));
        value(&runtime.status.uiTurnsElapsed,
              bytes.subspan(offsetof(Disk, uiTurnsElapsed), sizeof(Disk::uiTurnsElapsed)));
        value(&runtime.status.status_header_suffix,
              bytes.subspan(offsetof(Disk, status_header_suffix),
                            sizeof(Disk::status_header_suffix)));
        for (std::size_t level = 0; level < W8_LEVEL_COUNT; ++level)
            nested(runtime.status.level_progress[level],
                   bytes.subspan(offsetof(Disk, level_progress) + level * sizeof(LevelProgress),
                                 sizeof(LevelProgress)),
                   value, text);
        value(&runtime.status.unknown_2013,
              bytes.subspan(offsetof(Disk, unknown_2013), sizeof(Disk::unknown_2013)));
        value(&runtime.status.pending_move_location,
              bytes.subspan(offsetof(Disk, pending_move_location),
                            sizeof(Disk::pending_move_location)));
        value(&runtime.party.modifiers,
              bytes.subspan(offsetof(Disk, party_modifiers), sizeof(Disk::party_modifiers)));
        value(&runtime.status.next_group_id,
              bytes.subspan(offsetof(Disk, next_group_id), sizeof(Disk::next_group_id)));
        value(&runtime.status.next_monster_location_id,
              bytes.subspan(offsetof(Disk, next_monster_location_id),
                            sizeof(Disk::next_monster_location_id)));
        value(&runtime.status.next_world_item_id,
              bytes.subspan(offsetof(Disk, next_world_item_id), sizeof(Disk::next_world_item_id)));
        value(&runtime.status.next_trigger_id,
              bytes.subspan(offsetof(Disk, next_trigger_id), sizeof(Disk::next_trigger_id)));
        bool held_present = runtime.party.held.item.iItemNo != -1;
        value(&held_present,
              bytes.subspan(offsetof(Disk, item_in_cursor), sizeof(Disk::item_in_cursor)));
        value(&runtime.party.held.item,
              bytes.subspan(offsetof(Disk, item_in_hand), sizeof(Disk::item_in_hand)));
        value(&runtime.status.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.status.game_time_ms,
              bytes.subspan(offsetof(Disk, game_time_ms), sizeof(Disk::game_time_ms)));
        value(&runtime.status.aging_accumulator,
              bytes.subspan(offsetof(Disk, aging_accumulator), sizeof(Disk::aging_accumulator)));
        byte_boolean(&runtime.status.search_mode,
                     bytes.subspan(offsetof(Disk, search_mode), sizeof(Disk::search_mode)));
        value(&runtime.status.world_suspended,
              bytes.subspan(offsetof(Disk, world_suspended), sizeof(Disk::world_suspended)));
        value(&runtime.status.real_elapsed,
              bytes.subspan(offsetof(Disk, real_elapsed), sizeof(Disk::real_elapsed)));
        value(&runtime.status.frame_elapsed,
              bytes.subspan(offsetof(Disk, frame_elapsed), sizeof(Disk::frame_elapsed)));
        enum_value<std::uint32_t>(
            &runtime.status.wait_state,
            bytes.subspan(offsetof(Disk, wait_state), sizeof(Disk::wait_state)));
        value(&runtime.status.item_recharge_ms,
              bytes.subspan(offsetof(Disk, item_recharge_ms), sizeof(Disk::item_recharge_ms)));
        value(&runtime.party.formation,
              bytes.subspan(offsetof(Disk, formation), sizeof(Disk::formation)));
        value(&runtime.status.game_time_days,
              bytes.subspan(offsetof(Disk, game_time_days), sizeof(Disk::game_time_days)));
        value(&runtime.status.iron_man,
              bytes.subspan(offsetof(Disk, iron_man), sizeof(Disk::iron_man)));
        value(
            &runtime.status.binding_reset_clock,
            bytes.subspan(offsetof(Disk, binding_reset_clock), sizeof(Disk::binding_reset_clock)));
        value(&runtime.status.binding_reset_pending,
              bytes.subspan(offsetof(Disk, binding_reset_pending),
                            sizeof(Disk::binding_reset_pending)));
        value(&runtime.status.serialized.padding_242f,
              bytes.subspan(offsetof(Disk, padding_242f), sizeof(Disk::padding_242f)));
        value(
            &runtime.status.npc_restore_pending,
            bytes.subspan(offsetof(Disk, npc_restore_pending), sizeof(Disk::npc_restore_pending)));
        value(&runtime.status.quote_audit,
              bytes.subspan(offsetof(Disk, quote_audit), sizeof(Disk::quote_audit)));
        value(&runtime.status.long_quote,
              bytes.subspan(offsetof(Disk, long_quote), sizeof(Disk::long_quote)));
        value(&runtime.status.party_fatigued,
              bytes.subspan(offsetof(Disk, party_fatigued), sizeof(Disk::party_fatigued)));
        value(&runtime.status.selected_party_member,
              bytes.subspan(offsetof(Disk, selected_party_member),
                            sizeof(Disk::selected_party_member)));
        value(&runtime.status.world_cursor_gate,
              bytes.subspan(offsetof(Disk, world_cursor_gate), sizeof(Disk::world_cursor_gate)));
        value(&runtime.status.camp_tick_ms,
              bytes.subspan(offsetof(Disk, camp_tick_ms), sizeof(Disk::camp_tick_ms)));
        value(&runtime.status.rpc_races,
              bytes.subspan(offsetof(Disk, rpc_races), sizeof(Disk::rpc_races)));
        value(&runtime.status.unknown_243f,
              bytes.subspan(offsetof(Disk, unknown_243f), sizeof(Disk::unknown_243f)));
        value(&runtime.status.skip_loose_character_check,
              bytes.subspan(offsetof(Disk, skip_loose_character_check),
                            sizeof(Disk::skip_loose_character_check)));
        value(&runtime.status.use_item_latch,
              bytes.subspan(offsetof(Disk, use_item_latch), sizeof(Disk::use_item_latch)));
        value(
            &runtime.status.infatuation_pending,
            bytes.subspan(offsetof(Disk, infatuation_pending), sizeof(Disk::infatuation_pending)));
        value(&runtime.status.difficulty,
              bytes.subspan(offsetof(Disk, difficulty), sizeof(Disk::difficulty)));
        value(&runtime.status.save_filetime_xor,
              bytes.subspan(offsetof(Disk, save_filetime_xor), sizeof(Disk::save_filetime_xor)));
        text(runtime.status.monster_name_buffer,
             bytes.subspan(offsetof(Disk, monster_name_buffer), sizeof(Disk::monster_name_buffer)),
             Encoding::utf16);
        value(&runtime.status.sedexus_party_slot,
              bytes.subspan(offsetof(Disk, sedexus_party_slot), sizeof(Disk::sedexus_party_slot)));
        value(&runtime.status.stamina_tick_ms,
              bytes.subspan(offsetof(Disk, stamina_tick_ms), sizeof(Disk::stamina_tick_ms)));
        value(&runtime.status.condition13_clock,
              bytes.subspan(offsetof(Disk, condition13_clock), sizeof(Disk::condition13_clock)));
        byte_boolean(&runtime.status.skip_next_condition_reaction,
                     bytes.subspan(offsetof(Disk, skip_next_condition_reaction),
                                   sizeof(Disk::skip_next_condition_reaction)));
        value(&runtime.status.rpc_active,
              bytes.subspan(offsetof(Disk, rpc_active), sizeof(Disk::rpc_active)));
        value(&runtime.status.fact_b8_pending,
              bytes.subspan(offsetof(Disk, fact_b8_pending), sizeof(Disk::fact_b8_pending)));
        value(&runtime.status.condition13_stamp,
              bytes.subspan(offsetof(Disk, condition13_stamp), sizeof(Disk::condition13_stamp)));
        value(&runtime.status.pending_condition_party_slot,
              bytes.subspan(offsetof(Disk, pending_condition_party_slot),
                            sizeof(Disk::pending_condition_party_slot)));
        value(&runtime.status.fact_b8_clock,
              bytes.subspan(offsetof(Disk, fact_b8_clock), sizeof(Disk::fact_b8_clock)));
        value(&runtime.status.greeting_pending,
              bytes.subspan(offsetof(Disk, greeting_pending), sizeof(Disk::greeting_pending)));
        value(&runtime.status.camp_fatigue_count,
              bytes.subspan(offsetof(Disk, camp_fatigue_count), sizeof(Disk::camp_fatigue_count)));
        value(&runtime.status.party_slot,
              bytes.subspan(offsetof(Disk, party_slot), sizeof(Disk::party_slot)));
        value(&runtime.status.item_spell_usage,
              bytes.subspan(offsetof(Disk, item_spell_usage), sizeof(Disk::item_spell_usage)));
        value(&runtime.status.log_fact_checks,
              bytes.subspan(offsetof(Disk, log_fact_checks), sizeof(Disk::log_fact_checks)));
        value(&runtime.status.status_ints,
              bytes.subspan(offsetof(Disk, status_ints), sizeof(Disk::status_ints)));
        value(&runtime.status.fact_88_latch,
              bytes.subspan(offsetof(Disk, fact_88_latch), sizeof(Disk::fact_88_latch)));
        value(&runtime.status.unknown_40c2,
              bytes.subspan(offsetof(Disk, unknown_40c2), sizeof(Disk::unknown_40c2)));
        value(&runtime.status.spell_usage,
              bytes.subspan(offsetof(Disk, spell_usage), sizeof(Disk::spell_usage)));
        value(&runtime.status.unknown_47de,
              bytes.subspan(offsetof(Disk, unknown_47de), sizeof(Disk::unknown_47de)));
        value(&runtime.status.cc_arena_spawned,
              bytes.subspan(offsetof(Disk, cc_arena_spawned), sizeof(Disk::cc_arena_spawned)));
        value(&runtime.status.savant_hack_tick,
              bytes.subspan(offsetof(Disk, savant_hack_tick), sizeof(Disk::savant_hack_tick)));
        value(&runtime.status.bela_cycle_tick,
              bytes.subspan(offsetof(Disk, bela_cycle_tick), sizeof(Disk::bela_cycle_tick)));
        value(&runtime.status.unknown_497b,
              bytes.subspan(offsetof(Disk, unknown_497b), sizeof(Disk::unknown_497b)));
        value(&runtime.status.combat_difficulty_counts,
              bytes.subspan(offsetof(Disk, combat_difficulty_counts),
                            sizeof(Disk::combat_difficulty_counts)));
        value(&runtime.status.vi_event_stage,
              bytes.subspan(offsetof(Disk, vi_event_stage), sizeof(Disk::vi_event_stage)));
        value(&runtime.status.endgame2_queued,
              bytes.subspan(offsetof(Disk, endgame2_queued), sizeof(Disk::endgame2_queued)));
        value(&runtime.status.endgame3_queued,
              bytes.subspan(offsetof(Disk, endgame3_queued), sizeof(Disk::endgame3_queued)));
        value(
            &runtime.status.text_box_lines_used,
            bytes.subspan(offsetof(Disk, text_box_lines_used), sizeof(Disk::text_box_lines_used)));
        value(&runtime.status.text_box_lines_shown,
              bytes.subspan(offsetof(Disk, text_box_lines_shown),
                            sizeof(Disk::text_box_lines_shown)));
        value(&runtime.status.trang_check_clock,
              bytes.subspan(offsetof(Disk, trang_check_clock), sizeof(Disk::trang_check_clock)));
        value(
            &runtime.status.trang_check_pending,
            bytes.subspan(offsetof(Disk, trang_check_pending), sizeof(Disk::trang_check_pending)));
        value(&runtime.status.intro_shown,
              bytes.subspan(offsetof(Disk, intro_shown), sizeof(Disk::intro_shown)));
        value(&runtime.status.flag, bytes.subspan(offsetof(Disk, flag), sizeof(Disk::flag)));
        value(&runtime.status.serialized.padding_49be,
              bytes.subspan(offsetof(Disk, padding_49be), sizeof(Disk::padding_49be)));
        value(&runtime.status.endgame_started,
              bytes.subspan(offsetof(Disk, endgame_started), sizeof(Disk::endgame_started)));
        value(&runtime.status.dev_flagged,
              bytes.subspan(offsetof(Disk, dev_flagged), sizeof(Disk::dev_flagged)));
    }
};

template<class T> void decode(std::span<const std::byte> disk, T& runtime)
{
    if (disk.size() > sizeof(typename Record<T>::Disk))
        throw std::runtime_error("Oversized retail record");
    // Old saves omit trailing fields. Zero-extend the disk image, never the
    // native object (which owns strings and has a different layout).
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    if (!disk.empty()) std::memcpy(image.data(), disk.data(), disk.size());
    T decoded{};
    Record<T>::fields(
        decoded, std::span<const std::byte>(image),
        [](auto* destination, auto bytes) { std::memcpy(destination, bytes.data(), bytes.size()); },
        [](std::string& destination, auto bytes, Encoding encoding) {
            if (encoding == Encoding::utf16)
                destination = text::from_utf16le(bytes);
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
    Record<T>::fields(
        runtime, std::span<std::byte>(image),
        [](const auto* source, auto bytes) { std::memcpy(bytes.data(), source, bytes.size()); },
        [](const std::string& source, auto bytes, Encoding encoding) {
            if (encoding == Encoding::utf16)
                text::to_utf16le(source, bytes);
            else
                text::write_utf8_field(
                    source, std::span(reinterpret_cast<char*>(bytes.data()), bytes.size()));
        });
    return image;
}
template<class T> typename Record<T>::Disk encode_image(const T& runtime)
{
    return std::bit_cast<typename Record<T>::Disk>(encode(runtime));
}
template<class T> void read_exact(File& file, T& runtime,
                                  std::size_t bytes = sizeof(typename Record<T>::Disk))
{
    if (bytes > sizeof(typename Record<T>::Disk))
        throw std::runtime_error("Oversized retail record");
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    file.read_exact(image.data(), bytes);
    decode<T>(std::span(image).first(bytes), runtime);
}
// Read a count-prefixed table without publishing partially decoded records.
template<class T>
std::vector<T> read_database(File& file,
                             std::size_t maximum = std::numeric_limits<unsigned int>::max())
{
    unsigned int count;
    file.read_exact(&count, sizeof(count));
    const auto remaining = file.size() - file.tell();
    if (count > maximum || remaining < 0 ||
        count > static_cast<std::uint64_t>(remaining) / sizeof(typename Record<T>::Disk)) {
        throw std::runtime_error("Invalid retail database record count");
    }
    std::vector<T> records(count);
    for (auto& record : records) read_exact(file, record);
    return records;
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
// Chunk-bounded record transfer: a record never reads past its chunk.
template<class T> void read_exact(W8Chunk& chunk, T& runtime,
                                  std::size_t bytes = sizeof(typename Record<T>::Disk))
{
    if (bytes > sizeof(typename Record<T>::Disk))
        throw std::runtime_error("Oversized retail record");
    std::array<std::byte, sizeof(typename Record<T>::Disk)> image{};
    chunk.ReadBytes(image.data(), bytes);
    decode<T>(std::span(image).first(bytes), runtime);
}
template<class T> void write(W8Chunk& chunk, const T& runtime)
{
    auto image = encode(runtime);
    chunk.WriteBytes(image.data(), image.size());
}
/* Read a record's disk image whole, for loaders that inspect its presence
   words before decoding it. */
template<class T> typename Record<T>::Disk read_image(File& file)
{
    typename Record<T>::Disk image{};
    file.read_exact(&image, sizeof(image));
    return image;
}
template<class T> typename Record<T>::Disk read_image(W8Chunk& chunk)
{
    typename Record<T>::Disk image{};
    chunk.ReadBytes(&image, sizeof(image));
    return image;
}
template<class T> void decode_image(const typename Record<T>::Disk& image, T& runtime)
{
    decode<T>(std::as_bytes(std::span(&image, 1)), runtime);
}
// Size-prefixed records: a four-byte length, then that many bytes of the
// record. Older writers may store a prefix of today's layout.
template<class T> void write_sized(File& file, const T& runtime)
{
    auto image = encode(runtime);
    const auto size = static_cast<std::uint32_t>(image.size());
    file.write(&size, sizeof(size));
    file.write(image.data(), image.size());
}
template<class T> void read_sized(File& file, T& runtime)
{
    std::uint32_t size;
    file.read_exact(&size, sizeof(size));
    read_exact(file, runtime, size);
}
template<class T> void write_sized(W8Chunk& chunk, const T& runtime)
{
    auto image = encode(runtime);
    chunk.WriteValue(static_cast<std::uint32_t>(image.size()));
    chunk.WriteBytes(image.data(), image.size());
}
template<class T> void read_sized(W8Chunk& chunk, T& runtime)
{
    read_exact(chunk, runtime, chunk.ReadValue<std::uint32_t>());
}
} // namespace wiz8::retail
