#pragma once

// Shared gameplay parameters; numeric representation is independent of retail addresses.
inline constexpr float kCameraAnglePeriod = 6.283185005187988f;
inline constexpr float kCameraSnapEpsilon = 0.009999999776482582f;
inline constexpr double kCameraViewFactor0 = 0.005555555555555556;
inline constexpr unsigned char kCharacterEventFlagsMask = 16;
inline constexpr int kCharacterEventFullVolume = 127;
inline constexpr int kCharacterEventNoFlags = 0;
inline constexpr unsigned int kCharacterEventNoNpcDefer = 8;
inline constexpr unsigned int kCharacterEventNoPreempt = 0x20;
inline constexpr double kColorByteScale = 255.0;
inline constexpr int kConditionReaction = 5;
inline constexpr int kConditionReactionAlt = 6;
inline constexpr float kEnvironmentNearScale = 2.0f;
inline constexpr unsigned int kFirstRemappedEvent = 500;
inline constexpr unsigned int kFleeHpFraction = 50;
inline constexpr int kInfoDialogX = 0x80;
inline constexpr double kInverseFullTurnDegrees = 1.0 / 360.0;
inline constexpr float kItemWeightDisplayScale = 0.1f;
inline constexpr float kMonsterMotionPush = 1.05f;
inline constexpr double kMotionFullTurnRadians = 6.2831852;
inline constexpr float kMovementSpeedStep = 0.009999999776482582f;
inline constexpr float kNavigatorMode3Scale = 0.4000000059604645f;
inline constexpr float kQuarterTurn = 1.57079625f;
inline constexpr double kSlerpEpsilon = 1.1920928955078125e-07;
inline constexpr int kSplitDialogX = 0xa1;
inline constexpr int kSplitDialogY = 0x94;
inline constexpr float kStartupNearLimit = 250.0f;
inline constexpr float kTargetingQuarterPi = 0.7853981256484985f;
inline constexpr float kVectorLengthSquaredEpsilon = 1.0000000116860974e-07f;
inline constexpr float kWorldScale = 500.0f;
