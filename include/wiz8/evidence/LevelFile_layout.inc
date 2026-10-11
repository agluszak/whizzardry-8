/* Layout of the fixed-width level-file disk records. Included by LevelFile.cpp. */

W8_ABI_ASSERT(sizeof(W8LevelFileCompressedFace) == 0x21, "W8LevelFileCompressedFace_must_be_0x21");
W8_ABI_ASSERT(sizeof(W8LevelFilePathNode) == 0x1c, "W8LevelFilePathNode_must_be_0x1c");
W8_ABI_ASSERT(sizeof(W8LevelFileScaledPathNode) == 0x28, "W8LevelFileScaledPathNode_must_be_0x28");
W8_ABI_ASSERT(sizeof(W8LevelFileBounds) == 0x18, "W8LevelFileBounds_must_be_0x18");
W8_ABI_ASSERT(sizeof(W8LevelFileDoor) == 0x99, "W8LevelFileDoor_must_be_0x99");
W8_ABI_ASSERT(sizeof(W8LevelFilePlane) == 0x30, "W8LevelFilePlane_must_be_0x30");
W8_ABI_ASSERT(sizeof(W8LevelFileLightExtra) == 0x3c, "W8LevelFileLightExtra_must_be_0x3c");
W8_ABI_ASSERT(sizeof(W8LevelFileItemRecord) == 0x44, "W8LevelFileItemRecord_must_be_0x44");
W8_ABI_ASSERT(sizeof(W8LevelFileClippingPlaneRecord) == 0x50,
              "W8LevelFileClippingPlaneRecord_must_be_0x50");
W8_ABI_ASSERT(sizeof(W8LevelFileFramePosition) == 4, "W8LevelFileFramePosition_must_be_4");
W8_ABI_ASSERT(sizeof(W8LevelFileTriggerPosition) == 0x1c,
              "W8LevelFileTriggerPosition_must_be_0x1c");
W8_ABI_ASSERT(offsetof(W8LevelFileTriggerPosition, angle) == 0x0c,
              "Level trigger placement angle offset");
W8_ABI_ASSERT(offsetof(W8LevelFileTriggerPosition, direction) == 0x10,
              "Level trigger placement direction offset");
W8_ABI_ASSERT(sizeof(W8LevelFileTriggerHotSpot) == 0x85, "W8LevelFileTriggerHotSpot_must_be_0x85");
W8_ABI_ASSERT(sizeof(W8LevelParticleRecord) == 0x225, "W8LevelParticleRecord_must_be_0x225");
W8_ABI_ASSERT(sizeof(W8LevelFileParticleSystem) == 0x226,
              "W8LevelFileParticleSystem_must_be_0x226");
W8_ABI_ASSERT(offsetof(W8LevelFileParticleSystem, particle.location) == 0x41,
              "W8LevelFileParticleSystem_position_41");
W8_ABI_ASSERT(offsetof(W8LevelFileParticleSystem, particle.attachment_key) == 0x217,
              "W8LevelFileParticleSystem_attachment_key_217");
W8_ABI_ASSERT(sizeof(W8LevelFileNamedPosition) == 0x9d, "W8LevelFileNamedPosition_must_be_0x9d");
W8_ABI_ASSERT(offsetof(W8LevelFileNamedPosition, position) == 0x81,
              "W8LevelFileNamedPosition_position_81");
W8_ABI_ASSERT(offsetof(W8LevelFileNamedPosition, angle) == 0x8d,
              "W8LevelFileNamedPosition_angle_8d");
W8_ABI_ASSERT(offsetof(W8LevelFileNamedPosition, direction) == 0x91,
              "W8LevelFileNamedPosition_direction_91");
W8_ABI_ASSERT(sizeof(W8LevelFileMonsterHeader) == 0x22, "W8LevelFileMonsterHeader_must_be_0x22");
