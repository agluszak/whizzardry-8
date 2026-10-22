#pragma once

// Old numeric APST records identify props by the shared construction sequence.
// Other entity creations must still consume a number, although they do not store it.
int AllocateLegacyPropSaveId();
void ResetLegacyPropSaveIds();
