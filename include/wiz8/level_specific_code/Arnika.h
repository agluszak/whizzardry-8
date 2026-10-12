#pragma once


#include <cstdint>
class Trigger;

/* Level 0 (Arnika) elevator masters and activation callbacks, registered by
   the case-0 block of InitializeLevelMasterFunctions. */
void ArnikaLevelSetup(void);
bool ArnikaLazerScanner(Trigger* pTrigger);
void ArnikaLaserScanMaster(std::int32_t command);
bool ArnikaScannerDoor(Trigger* pTrigger);
void ArnikaWarningSound(std::int32_t command);
bool ArnikaMookholo(Trigger* pTrigger);
void ArnikaMookholoWatch(std::int32_t command);
bool ArnikaMookFrontDoor(Trigger* pTrigger);
bool ArnikaYellowButton(Trigger* pTrigger);
bool ArnikaVaultAlarmDoor(Trigger* pTrigger);
bool ArnikaExitButton(Trigger* pTrigger);
void ArnikaTeleportWatch(std::int32_t command);
bool ArnikaGenVaultDoor(Trigger* pTrigger);
void ArnikaElevator1Setup(void);
bool ArnikaRedButton(Trigger* pTrigger);
void ArnikaEl1Button(std::int32_t command);
bool ArnikaEl1TopButtons(Trigger* pTrigger);
bool ArnikaEl1BottomButtons(Trigger* pTrigger);
void ArnikaEl1Moving(std::int32_t command);
void ArnikaElevator2Setup(void);
bool ArnikaGreenButton(Trigger* pTrigger);
void ArnikaEl2Button(std::int32_t command);
bool ArnikaElevator02Trigger(Trigger* pTrigger);
void ArnikaEl2Moving(std::int32_t command);
void ArnikaElevatorAdvance(std::int32_t which);
bool ArnikaChaosMolori(Trigger* pTrigger);
bool ArnikaMaddmook(Trigger* pTrigger);
bool ArnikaAstralDominae(Trigger* pTrigger);
bool ArnikaCMbox(Trigger* pTrigger);
std::int32_t ArnikaPedestalItem(std::int32_t* previous_item);
bool ArnikaBallSlot(Trigger* pTrigger);
bool ArnikaFlightRecorder(Trigger* pTrigger);
