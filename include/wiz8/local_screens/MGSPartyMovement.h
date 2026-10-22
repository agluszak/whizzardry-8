#pragma once

/* Creates the combat party-movement panel and its two buttons; returns false
   when an allocation fails. */
bool CreatePartyMovementPanel(void);
/* Releases the party-movement panels and clears the
   combat-UI teardown flag; called when the party regains movement. */
void ReleasePartyMovement(void);
void UpdatePartyMovementPanel(void);
/* Before the draw: sync the two buttons and step the gauge's running animation,
   requesting the panel's repaint when either changed. */
void UpdatePartyMovementGauge(void);
// Paint the panel when its repaint was requested or its live button changed.
void DrawPartyMovementPanel(bool requested);
void DisablePartyMovementRegions(void);

// Request the movement panel's repaint.
void InvalidatePartyMovementPanel(void);
/* Repaint the movement gauge when the party's remaining move changed since it was last
   shown, or always with `force`. */
void ShowPartyMovementGauge(bool force);
void DisableFreeTurnButton(void);
void EnableFreeTurnButton(void);
