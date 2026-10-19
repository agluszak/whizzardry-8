#pragma once

class Trigger;

enum class MartensBluff1TeleporterPad : int {
    none = 0,
    gigas = 1,
    trang = 2,
    rift = 3,
    maten = 4,
};

/* Level 5 (MartensBluff1) master function and activation callbacks,
   registered by the case-5 block of InitializeLevelMasterFunctions. */
void MartensBluff1Setup(void);
bool MartensBluff1FHandlock(Trigger* pTrigger);
bool MartensBluff1DialA(Trigger* pTrigger);
bool MartensBluff1DialB(Trigger* pTrigger);
bool MartensBluff1DialC(Trigger* pTrigger);
bool MartensBluff1GasSwitch(Trigger* pTrigger);
bool MartensBluff1JDoorController(Trigger* pTrigger);
bool MartensBluff1Controller(Trigger* pTrigger);
bool MartensBluff1TransportSpawn(void);
bool MartensBluff1Transporter(int command);
bool MartensBluff1Teleporter(Trigger* pTrigger);
bool MartensBluff1ButtonGigas(Trigger* pTrigger);
bool MartensBluff1ButtonTrang(Trigger* pTrigger);
bool MartensBluff1ButtonRift(Trigger* pTrigger);
bool MartensBluff1ButtonMaten(Trigger* pTrigger);
bool MartensBluff1TeleportState(MartensBluff1TeleporterPad new_state);
bool MartensBluff1WireTrigger(Trigger* pTrigger);
bool MartensBluff1MartenBook(Trigger* pTrigger);
