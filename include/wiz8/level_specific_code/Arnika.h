#pragma once

class Trigger;

enum W8ArnikaElevatorId : int {
    W8_ARNIKA_ELEVATOR_ONE = 1,
    W8_ARNIKA_ELEVATOR_TWO = 2
};

enum W8ArnikaElevatorState : int {
    W8_ARNIKA_ELEVATOR_STEP_1 = 1,
    W8_ARNIKA_ELEVATOR_STEP_2 = 2,
    W8_ARNIKA_ELEVATOR_STEP_3 = 3,
    W8_ARNIKA_ELEVATOR_STEP_4 = 4,
    W8_ARNIKA_ELEVATOR_STEP_5 = 5,
    W8_ARNIKA_ELEVATOR_STEP_6 = 6,
    W8_ARNIKA_ELEVATOR_STEP_7 = 7,
    W8_ARNIKA_ELEVATOR_STEP_8 = 8
};

/* Level 0 (Arnika) elevator masters and activation callbacks, registered by
   the case-0 block of InitializeLevelMasterFunctions. */
void ArnikaLevelSetup(void);
bool ArnikaLazerScanner(Trigger* pTrigger);
bool ArnikaLaserScanMaster(int command);
bool ArnikaScannerDoor(Trigger* pTrigger);
bool ArnikaWarningSound(int command);
bool ArnikaMookholo(Trigger* pTrigger);
bool ArnikaMookholoWatch(int command);
bool ArnikaMookFrontDoor(Trigger* pTrigger);
bool ArnikaYellowButton(Trigger* pTrigger);
bool ArnikaVaultAlarmDoor(Trigger* pTrigger);
bool ArnikaExitButton(Trigger* pTrigger);
bool ArnikaTeleportWatch(int command);
bool ArnikaGenVaultDoor(Trigger* pTrigger);
void ArnikaElevator1Setup(void);
bool ArnikaRedButton(Trigger* pTrigger);
bool ArnikaEl1Button(int command);
bool ArnikaEl1TopButtons(Trigger* pTrigger);
bool ArnikaEl1BottomButtons(Trigger* pTrigger);
bool ArnikaEl1Moving(int command);
void ArnikaElevator2Setup(void);
bool ArnikaGreenButton(Trigger* pTrigger);
bool ArnikaEl2Button(int command);
bool ArnikaElevator02Trigger(Trigger* pTrigger);
bool ArnikaEl2Moving(int command);
void ArnikaElevatorAdvance(W8ArnikaElevatorId which);
bool ArnikaChaosMolori(Trigger* pTrigger);
bool ArnikaMaddmook(Trigger* pTrigger);
bool ArnikaAstralDominae(Trigger* pTrigger);
bool ArnikaCMbox(Trigger* pTrigger);
int ArnikaPedestalItem(int* previous_item);
bool ArnikaBallSlot(Trigger* pTrigger);
bool ArnikaFlightRecorder(Trigger* pTrigger);
