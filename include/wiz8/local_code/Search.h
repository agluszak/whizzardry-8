#pragma once

// Run the periodic search sweep, or toggle the player's search mode.
void RunSearchPulse();
void ToggleSearchMode();
/* The party starts searching (search mode turned on, or a fight ended with it on): post
   the on notice, arm the 500ms sweep and drop the active cursor node. */
void ResumeSearching();
