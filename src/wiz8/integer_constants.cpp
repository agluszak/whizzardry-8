#include <cstdint>
#include "wiz8/integer_constants.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"

/* Definitions for integer_constants.h: this unit reads none of them, so no
   user sees an initializer. */

// GLOBAL: WIZ8 0x005ED8C8
const std::int32_t g_character_event_no_flags = 0;

// GLOBAL: WIZ8 0x005ed8cc
const std::int32_t g_effect_argument0 = 1;

// GLOBAL: WIZ8 0x005ED8D0
const std::int32_t g_effect_argument1 = 2;

// GLOBAL: WIZ8 0x005ED8D4
const std::int32_t g_effect_argument2 = 1;

// GLOBAL: WIZ8 0x005ED8D8
const std::int32_t g_effect_argument3 = 2;

// GLOBAL: WIZ8 0x005ED8E0
const std::uint32_t g_character_event_no_npc_defer = 8;

// GLOBAL: WIZ8 0x005ED8E4
const std::uint8_t g_character_event_flags_mask = 16;

// GLOBAL: WIZ8 0x005ED8E8
const std::uint32_t g_character_event_no_preempt = 0x20;

// GLOBAL: WIZ8 0x005ED8EC
const std::uint32_t g_character_event_npc_script = 0x40;

// GLOBAL: WIZ8 0x005ED8F8
const std::uint32_t g_flee_hp_fraction = 50;

// GLOBAL: WIZ8 0x005ED8FC
const std::uint32_t g_portrait_low_health_percent = 20;

// GLOBAL: WIZ8 0x005ed900
const std::uint32_t g_effect_threshold0 = 70;

/* Two effects the party is holding that a wounded character can no longer
   sustain, and the third that only the deeper threshold breaks. */
// GLOBAL: WIZ8 0x005ed904
const std::uint32_t g_effect_threshold1 = 50;

/* 0x004C62C0 */
// GLOBAL: WIZ8 0x005ed908
const std::uint32_t g_flee_chance = 15;

// GLOBAL: WIZ8 0x005ED914
const std::int32_t g_character_event_full_volume = 127;

// GLOBAL: WIZ8 0x005ee588
const std::int32_t g_effect0 = 0;

// GLOBAL: WIZ8 0x005EE58C
const std::int32_t g_effect1 = 1;

// GLOBAL: WIZ8 0x005EE590
const std::int32_t g_effect2 = 2;

// GLOBAL: WIZ8 0x005EE594
const std::int32_t g_effect3 = 3;

// GLOBAL: WIZ8 0x005ee598
const std::int32_t g_effect4 = 4;

// GLOBAL: WIZ8 0x005ee59c
const std::int32_t g_condition_reaction = 5;

// GLOBAL: WIZ8 0x005ee5a0
const std::int32_t g_condition_reaction_alt = 6;

// GLOBAL: WIZ8 0x005EE5A4
const std::int32_t g_effect5 = 7;

/* The "carrying too much" camp event id. Outside camp the live special
   event slot supplies it instead. */
// GLOBAL: WIZ8 0x005ee5a8
const std::int32_t g_camp_overload_event_id = 8;

// GLOBAL: WIZ8 0x005EE5AC
const std::int32_t g_effect6 = 9;

// GLOBAL: WIZ8 0x005EE5B4
const std::int32_t g_effect7 = 11;

// GLOBAL: WIZ8 0x005EE5B8
const std::int32_t g_effect8 = 12;

// GLOBAL: WIZ8 0x005EE5BC
const std::int32_t g_effect9 = 13;

// GLOBAL: WIZ8 0x005EE5C8
const std::int32_t g_item_message0 = 16;

// GLOBAL: WIZ8 0x005EE5CC
const std::int32_t g_item_message1 = 17;

// GLOBAL: WIZ8 0x005EE5D0
const std::int32_t g_effect10 = 18;

// GLOBAL: WIZ8 0x005EE5D4
const std::int32_t g_effect11 = 19;

// GLOBAL: WIZ8 0x005EE5D8
const std::int32_t g_effect12 = 20;

// GLOBAL: WIZ8 0x005EE5DC
const std::int32_t g_effect13 = 0x15;

// GLOBAL: WIZ8 0x005EE5E0
const std::int32_t g_effect14 = 0x16;

/* Search-pulse event ids: the two found-item variants queued to the
   searcher. */
// GLOBAL: WIZ8 0x005EE5E4
const std::int32_t g_search_found_item_event = 0x17;

// GLOBAL: WIZ8 0x005EE5E8
const std::int32_t g_search_found_item_event_alt = 0x18;

// GLOBAL: WIZ8 0x005EE5EC
const std::int32_t g_effect15 = 0x19;

// GLOBAL: WIZ8 0x005EE5F0
const std::int32_t g_effect16 = 0x1a;

// GLOBAL: WIZ8 0x005EE5F8
const std::int32_t g_effect17 = 28;

// GLOBAL: WIZ8 0x005EE5FC
const std::int32_t g_effect18 = 29;

// GLOBAL: WIZ8 0x005EE600
const std::int32_t g_effect19 = 30;

// GLOBAL: WIZ8 0x005EE604
const std::int32_t g_effect20 = 31;

// GLOBAL: WIZ8 0x005EE60C
const std::int32_t g_effect21 = 33;

// GLOBAL: WIZ8 0x005ee610
const std::int32_t g_effect22 = 34;

// GLOBAL: WIZ8 0x005EE614
const std::int32_t g_effect23 = 35;

// GLOBAL: WIZ8 0x005EE618
const std::int32_t g_effect24 = 36;

// GLOBAL: WIZ8 0x005EE61C
const std::int32_t g_effect25 = 37;

// GLOBAL: WIZ8 0x005ee620
const std::int32_t g_sight_effect0 = 0x26;

/* 0x005EE624: the character event an item use queues when the attempt ends
   without casting anything. */
// GLOBAL: WIZ8 0x005EE624
const std::int32_t g_effect26 = 39;

// GLOBAL: WIZ8 0x005EE628
const std::int32_t g_effect27 = 40;

// GLOBAL: WIZ8 0x005EE630
const std::int32_t g_effect28 = 42;

// GLOBAL: WIZ8 0x005EE634
const std::int32_t g_effect29 = 43;

// GLOBAL: WIZ8 0x005EE638
const std::int32_t g_effect30 = 44;

// GLOBAL: WIZ8 0x005ee640
const std::int32_t g_item_message2 = 46;

// GLOBAL: WIZ8 0x005ee644
const std::int32_t g_item_message3 = 47;

// GLOBAL: WIZ8 0x005ee648
const std::int32_t g_item_message4 = 48;

// GLOBAL: WIZ8 0x005ee64c
const std::int32_t g_item_message5 = 49;

// GLOBAL: WIZ8 0x005EE654
const std::int32_t g_effect31 = 51;

// GLOBAL: WIZ8 0x005EE658
const std::int32_t g_effect32 = 52;

// GLOBAL: WIZ8 0x005EE65C
const std::int32_t g_character_event_kind2 = 0x35;

// GLOBAL: WIZ8 0x005ee664
const std::int32_t g_item_message6 = 55;

// GLOBAL: WIZ8 0x005EE668
const std::int32_t g_item_message7 = 56;

// GLOBAL: WIZ8 0x005ee66c
const std::int32_t g_sight_effect1 = 0x39;

// GLOBAL: WIZ8 0x005ee68c
const std::int32_t g_item_message8 = 65;

// GLOBAL: WIZ8 0x005ee690
const std::int32_t g_item_message9 = 66;

/* The effect ids the two "someone noticed you" notices post. Their slots are
   the four consecutive dwords the producer reads. */
// GLOBAL: WIZ8 0x005ee694
const std::int32_t g_sight_effect2 = 0x43;

// GLOBAL: WIZ8 0x005ee698
const std::int32_t g_sight_effect3 = 0x44;

// GLOBAL: WIZ8 0x005ee69c
const std::int32_t g_effect33 = 69;

/* Quote indices 'F'..'R' fall outside the scripted world-action dispatch in
   W8NpcDialogue::Speak's kind-0x0d/0x14 entries. */
// GLOBAL: WIZ8 0x005EE6A0
const std::int32_t g_world_action_quote_min = 'F';

// GLOBAL: WIZ8 0x005EE6D0
const std::int32_t g_world_action_quote_max = 'R';

// GLOBAL: WIZ8 0x005EE6D8
const std::int32_t g_effect34 = 0x54;

// GLOBAL: WIZ8 0x005EE6DC
const std::int32_t g_effect35 = 0x55;

// GLOBAL: WIZ8 0x005ee6ec
const std::int32_t g_effect36 = 109;

// GLOBAL: WIZ8 0x005ee6f8
const std::int32_t g_effect37 = 131;

// GLOBAL: WIZ8 0x005ee6fc
const std::int32_t g_item_message10 = 132;

// GLOBAL: WIZ8 0x005EE710
const std::uint32_t g_remapped_event_count = 31;

// GLOBAL: WIZ8 0x005EE718
const std::uint32_t g_first_remapped_event = 500;

// GLOBAL: WIZ8 0x005EF958
const std::int32_t g_info_dialog_x = 0x80;

// GLOBAL: WIZ8 0x005EF95C
const std::int32_t g_info_dialog_y = 0x61;

/* The split-amount dialog's confirm code and its default origin; only the
   0x572780/0x572870 pair reads them. */
// GLOBAL: WIZ8 0x005EF9D4
const W8SplitDialogResult g_amount_split_confirm_result = W8_SPLIT_RESULT_CONFIRMED;

// GLOBAL: WIZ8 0x005EF9DC
const std::int32_t g_split_dialog_origin_x = 0x9f;

// GLOBAL: WIZ8 0x005EF9E0
const std::int32_t g_split_dialog_origin_y = 0xb8;

// GLOBAL: WIZ8 0x005EFB44
const W8SplitDialogResult g_item_split_confirm_result = W8_SPLIT_RESULT_CONFIRMED;

// GLOBAL: WIZ8 0x005EFB4C
const std::int32_t g_split_dialog_x = 0xa1;

// GLOBAL: WIZ8 0x005EFB50
const std::int32_t g_split_dialog_y = 0x94;

// GLOBAL: WIZ8 0x005EFB64
const W8ItemSplitMode g_item_split_inventory_mode = W8_ITEM_SPLIT_INVENTORY;

/* The trade-mode split-dialog kinds OpenNpcTradeQuantityDialog selects:
   1 while the party sells to the NPC, 2 while buying or shoplifting. */
// GLOBAL: WIZ8 0x005EFB68
const W8ItemSplitMode g_item_split_sell_mode = W8_ITEM_SPLIT_SELL;

// GLOBAL: WIZ8 0x005EFB6C
const W8ItemSplitMode g_item_split_buy_mode = W8_ITEM_SPLIT_BUY;
