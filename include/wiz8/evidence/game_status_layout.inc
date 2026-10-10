/* Layout evidence for include/wiz8/layouts/game_status.h. Included after its declarations. */

W8_ABI_ASSERT(sizeof(W8StatusBuffers) == 0x0c, "W8StatusBuffers_must_be_0x0c");
W8_ABI_ASSERT(sizeof(W8ItemSpellUsageRecord) == 0x10, "W8ItemSpellUsageRecord_must_be_0x10");
W8_ABI_ASSERT(sizeof(W8CharacterSpellUsageRecord) == 0x10,
              "W8CharacterSpellUsageRecord_must_be_0x10");
W8_ABI_ASSERT(offsetof(W8CharacterSpellUsageRecord, cast_count) == 0x4,
              "W8CharacterSpellUsageRecord_cast_count_offset");
W8_ABI_ASSERT(offsetof(W8CharacterSpellUsageRecord, usable_cast_count) == 0x8,
              "W8CharacterSpellUsageRecord_usable_cast_count_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, party_gold) == 0x19, "W8GlobalStatus_party_gold_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, selected_character) == 0x1d,
              "W8GlobalStatus_selected_character_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, party_item_pool) == 0x21,
              "W8GlobalStatus_party_item_pool_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, party_item_count) == 0x1791,
              "W8GlobalStatus_party_item_count_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, text_line_cursor) == 0x1795,
              "W8GlobalStatus_text_line_cursor_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, flags) == 0x2367, "W8GlobalStatus_flags_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, party_facing) == 0x18d0,
              "W8GlobalStatus_party_facing_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, current_level) == 0x1900,
              "W8GlobalStatus_current_level_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, status_header_prefix) == 0x1904,
              "W8GlobalStatus_status_header_prefix_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, uiTurnsElapsed) == 0x19d8,
              "W8GlobalStatus_uiTurnsElapsed_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, status_header_suffix) == 0x19dc,
              "W8GlobalStatus_status_header_suffix_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, level_progress) == 0x1a04,
              "W8GlobalStatus_level_progress_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, iron_man) == 0x2429, "W8GlobalStatus_iron_man_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, world_cursor_gate) == 0x2435,
              "W8GlobalStatus_value_2435_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, pending_move_location) == 0x22a7,
              "W8GlobalStatus_pending_move_location_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, formation) == 0x23a1, "W8GlobalStatus_formation_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, world_suspended) == 0x2390,
              "W8GlobalStatus_world_suspended_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, skip_loose_character_check) == 0x2444,
              "W8GlobalStatus_skip_loose_character_check_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, use_item_latch) == 0x2445,
              "W8GlobalStatus_use_item_latch_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, selected_party_member) == 0x2434,
              "W8GlobalStatus_selected_party_member_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, rpc_races) == 0x243a,
              "W8GlobalStatus_rpc_races_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, monster_name_buffer) == 0x2453,
              "W8GlobalStatus_monster_name_buffer_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, sedexus_party_slot) == 0x247f,
              "W8GlobalStatus_sedexus_party_slot_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, greeting_pending) == 0x2497,
              "W8GlobalStatus_flag_2497_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, next_group_id) == 0x234a,
              "W8GlobalStatus_next_group_id_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, log_fact_checks) == 0x3120,
              "W8GlobalStatus_log_fact_checks_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, text_box_lines_shown) == 0x49a7,
              "W8GlobalStatus_text_box_lines_shown_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, intro_shown) == 0x49bc,
              "W8GlobalStatus_intro_shown_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, text_box_lines_used) == 0x4997,
              "W8GlobalStatus_migrated_values_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, rpc_active) == 0x2489,
              "W8GlobalStatus_flag_2489_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, status_ints) == 0x3121,
              "W8GlobalStatus_status_ints_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, fact_88_latch) == 0x40c1,
              "W8GlobalStatus_fact_88_latch_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, spell_usage) == 0x40ce,
              "W8GlobalStatus_spell_usage_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, vi_event_stage) == 0x498b,
              "W8GlobalStatus_value_498b_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, endgame2_queued) == 0x498f,
              "W8GlobalStatus_value_498f_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, endgame3_queued) == 0x4993,
              "W8GlobalStatus_value_4993_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, party_slot) == 0x249c,
              "W8GlobalStatus_party_slot_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, regular_member_count) == 0x000d,
              "W8GlobalStatus_regular_member_count_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, auxiliary_member_count) == 0x0011,
              "W8GlobalStatus_auxiliary_member_count_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, total_member_count) == 0x0015,
              "W8GlobalStatus_total_member_count_offset");
W8_ABI_ASSERT(offsetof(W8GlobalStatus, party_gold) == 0x0019, "W8GlobalStatus_party_gold_offset");
W8_ABI_ASSERT(sizeof(W8GlobalStatus) == 0x49c2, "W8GlobalStatus_must_be_0x49c2");
