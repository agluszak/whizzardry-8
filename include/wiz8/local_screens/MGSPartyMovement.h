#pragma once

/* Creates the combat party-movement panel and its two buttons; returns zero
   when an allocation fails. */
unsigned char CreatePartyMovementPanel(void);
/* Releases the party-movement panels and clears the
   combat-UI teardown flag; called when the party regains movement. */
void ReleasePartyMovement(void);
void UpdatePartyMovementPanel(void);
void DrawPartyMovementPanel(void);
void DisablePartyMovementRegions(void);
/* Per-frame movement/fatigue processing; the callers reuse unrelated storage
   for the two elapsed-time outputs. */
unsigned char HandlePartyMovement(float* real_elapsed, float* frame_elapsed);

void InvalidatePartyMovementPanel(void);
void DisableFreeTurnButton(void);
void EnableFreeTurnButton(void);
