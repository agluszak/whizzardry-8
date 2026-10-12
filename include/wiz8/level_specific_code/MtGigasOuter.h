#pragma once


#include <cstdint>
/* Level Specific Code\MtGigasOuter.cpp helpers, run by
   InitializeLevelMasterFunctions under case 0x0e. */

class Trigger;

void ProcessFlagPosition(void);                   /* 0x004DBE70 */
bool OnCrankTriggerActivated(Trigger* trigger);   /* 0x004DBEC0 */
void ControlLiftGate(std::int32_t command);                /* 0x004DC080 */
bool OnSecurityButtonActivated(Trigger* trigger); /* 0x004DC390 */
void ControlCampAlarm(std::int32_t command);               /* 0x004DC3D0 */
bool OnSentryTriggerActivated(Trigger* trigger);  /* 0x004DC600 */
bool OnEwaxxDoor03Activated(Trigger* trigger);    /* 0x004DC640 */
bool OnDummyTriggerActivated(Trigger* trigger);   /* 0x004DC670 */

class W8IntervalGate;
// Initialize the saved countdown; return true when the save command is handled.
bool PrepareUmpaniAlarmCommand(std::int32_t command, const char* variable, W8IntervalGate* gate);
