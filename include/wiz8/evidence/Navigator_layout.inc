/* Layout evidence for include/wiz8/engine_code/Navigator.h. Included after its declarations. */

W8_ABI_ASSERT(sizeof(W8NavigatorAttachment) == 0x60, "W8NavigatorAttachment_size_must_be_0x60");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_cursor) == 0x04,
              "W8NavigatorAttachment_path_cursor");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, position_cursor) == 0x06,
              "W8NavigatorAttachment_position_cursor");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_position_index) == 0x08,
              "W8NavigatorAttachment_path_position_index");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, capacity) == 0x0a,
              "W8NavigatorAttachment_capacity_0a");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, segment_start) == 0x10,
              "W8NavigatorAttachment_position_10");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_destination) == 0x1c,
              "W8NavigatorAttachment_position_1c");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, start_waypoint) == 0x28,
              "W8NavigatorAttachment_position_28");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_length_origin) == 0x34,
              "W8NavigatorAttachment_position_34");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, recorded_position) == 0x40,
              "W8NavigatorAttachment_position_40");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_positions) == 0x4c,
              "W8NavigatorAttachment_position_4c");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_values) == 0x50,
              "W8NavigatorAttachment_path_values");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, separation) == 0x54,
              "W8NavigatorAttachment_separation_54");
W8_ABI_ASSERT(offsetof(W8NavigatorAttachment, path_length) == 0x58,
              "W8NavigatorAttachment_path_length");
W8_ABI_ASSERT(sizeof(W8NavigatorMovementState) == 0xcc,
              "W8NavigatorMovementState_size_must_be_0xcc");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, location_id) == 0x04,
              "W8NavigatorMovementState_location_id_004");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, yaw) == 0x14, "W8NavigatorMovementState_yaw");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, target_yaw) == 0x18,
              "W8NavigatorMovementState_target_yaw");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, pitch) == 0x20,
              "W8NavigatorMovementState_pitch_020");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, target_pitch) == 0x24,
              "W8NavigatorMovementState_target_pitch");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, roll) == 0x28,
              "W8NavigatorMovementState_roll_028");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, target_roll) == 0x2c,
              "W8NavigatorMovementState_target_roll");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, velocity) == 0x34,
              "W8NavigatorMovementState_velocity_034");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, position) == 0x40,
              "W8NavigatorMovementState_position_040");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, target_position) == 0x4c,
              "W8NavigatorMovementState_target_position");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, callback_threshold) == 0x58,
              "W8NavigatorMovementState_callback_threshold");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, callback_progress) == 0x5c,
              "W8NavigatorMovementState_callback_progress");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, movement_scale) == 0x60,
              "W8NavigatorMovementState_movement_scale");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, movement_speed) == 0x64,
              "W8NavigatorMovementState_movement_speed");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, turn_rate) == 0x68,
              "W8NavigatorMovementState_turn_rate_068");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, flags) == 0x6c,
              "W8NavigatorMovementState_flags_06c");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, pitch_enabled) == 0x74,
              "W8NavigatorMovementState_pitch_enabled");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, roll_enabled) == 0x75,
              "W8NavigatorMovementState_roll_enabled");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, vertical_velocity) == 0x78,
              "W8NavigatorMovementState_vertical_velocity");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, vertical_base) == 0x7c,
              "W8NavigatorMovementState_vertical_base");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, vertical_amplitude) == 0x80,
              "W8NavigatorMovementState_vertical_amplitude");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, vertical_phase) == 0x84,
              "W8NavigatorMovementState_vertical_phase");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, basis) == 0x88,
              "W8NavigatorMovementState_basis");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, attachment) == 0xac,
              "W8NavigatorMovementState_attachment_0ac");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, collision_radius) == 0xb0,
              "W8NavigatorMovementState_collision_radius");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, alternate_radius) == 0xb4,
              "W8NavigatorMovementState_alternate_radius");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, height_offset) == 0xb8,
              "W8NavigatorMovementState_height_offset");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, secondary_height_offset) == 0xbc,
              "W8NavigatorMovementState_secondary_height_offset");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, vertical_offset) == 0xc0,
              "W8NavigatorMovementState_vertical_offset");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, scale) == 0xc4,
              "W8NavigatorMovementState_scale_0c4");
W8_ABI_ASSERT(offsetof(W8NavigatorMovementState, position_adjusted) == 0xc8,
              "W8NavigatorMovementState_position_adjusted");
W8_ABI_ASSERT(sizeof(W8Navigator) == 0x190, "W8Navigator_size_must_be_0x190");
W8_ABI_ASSERT(offsetof(W8Navigator, navigation_mode) == 0x08,
              "W8Navigator_navigation_mode_008");
W8_ABI_ASSERT(offsetof(W8Navigator, flags) == 0x0c, "W8Navigator_flags_00c");
W8_ABI_ASSERT(offsetof(W8Navigator, collision_margin) == 0x10,
              "W8Navigator_collision_margin");
W8_ABI_ASSERT(offsetof(W8Navigator, movement) == 0xc0, "W8Navigator_movement_0c0");
W8_ABI_ASSERT(offsetof(W8Navigator, node) == 0x18c, "W8Navigator_node");
