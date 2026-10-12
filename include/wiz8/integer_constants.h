#ifndef WIZ8_INTEGER_CONSTANTS_H
#define WIZ8_INTEGER_CONSTANTS_H


#include <cstdint>
/* Integer globals the image keeps in read-only data rather than as immediate
   operands. A body reads one of these from its address: with the initializer
   in view, C++ would fold the value into the instruction, and would initialize
   a global computed from it statically where retail computes it at startup.
   So the definitions live in their own translation unit, which reads none of
   them. */

extern const std::int32_t g_character_event_no_flags;
extern const std::int32_t g_effect_argument0;
extern const std::int32_t g_effect_argument1;
extern const std::int32_t g_effect_argument2;
extern const std::int32_t g_effect_argument3;
extern const std::uint32_t g_character_event_no_npc_defer;
extern const std::uint8_t g_character_event_flags_mask; /* Gates the portrait quote flag. */
extern const std::uint32_t g_character_event_no_preempt;
extern const std::uint32_t g_character_event_npc_script;
extern const std::uint32_t g_flee_hp_fraction;
extern const std::uint32_t g_portrait_low_health_percent;
extern const std::uint32_t g_effect_threshold0;
extern const std::uint32_t g_effect_threshold1;
extern const std::uint32_t g_flee_chance;
extern const std::int32_t g_character_event_full_volume;
extern const std::int32_t g_effect0;
extern const std::int32_t g_effect1;
extern const std::int32_t g_effect2;
extern const std::int32_t g_effect3;
extern const std::int32_t g_effect4;
extern const std::int32_t g_condition_reaction;
extern const std::int32_t g_condition_reaction_alt;
extern const std::int32_t g_effect5;
extern const std::int32_t g_camp_overload_event_id;
extern const std::int32_t g_effect6;
extern const std::int32_t g_effect7;
extern const std::int32_t g_effect8;
extern const std::int32_t g_effect9;
extern const std::int32_t g_item_message0;
extern const std::int32_t g_item_message1;
extern const std::int32_t g_effect10;
extern const std::int32_t g_effect11;
extern const std::int32_t g_effect12;
extern const std::int32_t g_effect13;
extern const std::int32_t g_effect14;
extern const std::int32_t g_search_found_item_event;
extern const std::int32_t g_search_found_item_event_alt;
extern const std::int32_t g_effect15;
extern const std::int32_t g_effect16;
extern const std::int32_t g_effect17;
extern const std::int32_t g_effect18;
extern const std::int32_t g_effect19;
extern const std::int32_t g_effect20;
extern const std::int32_t g_effect21;
extern const std::int32_t g_effect22;
extern const std::int32_t g_effect23;
extern const std::int32_t g_effect24; /* Queued for the allied NPC's death. */
extern const std::int32_t g_effect25;
extern const std::int32_t g_sight_effect0;
extern const std::int32_t g_effect26;
extern const std::int32_t g_effect27;
extern const std::int32_t g_effect28; /* Queued for eligible party members in that pass. */
extern const std::int32_t g_effect29;
extern const std::int32_t g_effect30;
extern const std::int32_t g_item_message2;
extern const std::int32_t g_item_message3;
extern const std::int32_t g_item_message4;
extern const std::int32_t g_item_message5;
extern const std::int32_t g_effect31;
extern const std::int32_t g_effect32; /* Rest benefit after the surprise sequence. */
extern const std::int32_t g_character_event_kind2;
extern const std::int32_t g_item_message6;
extern const std::int32_t g_item_message7;
extern const std::int32_t g_sight_effect1;
extern const std::int32_t g_item_message8;
extern const std::int32_t g_item_message9;
extern const std::int32_t g_sight_effect2;
extern const std::int32_t g_sight_effect3;
extern const std::int32_t g_effect33;
extern const std::int32_t g_world_action_quote_min;
extern const std::int32_t g_world_action_quote_max;
extern const std::int32_t g_effect34;
extern const std::int32_t g_effect35;
extern const std::int32_t g_effect36;
extern const std::int32_t g_effect37;
extern const std::int32_t g_item_message10;
extern const std::uint32_t g_remapped_event_count;
extern const std::uint32_t g_first_remapped_event;
extern const std::int32_t g_info_dialog_x;
extern const std::int32_t g_info_dialog_y;
extern const std::int32_t g_split_dialog_origin_x;
extern const std::int32_t g_split_dialog_origin_y;
extern const std::int32_t g_split_dialog_x;
extern const std::int32_t g_split_dialog_y;

#endif
