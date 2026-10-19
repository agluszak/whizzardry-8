#pragma once

struct W8Character;
struct InventoryWeightChange;

// Consume results at a live inventory/equipment operation boundary.
void PresentInventoryWeightChange(W8Character& character, InventoryWeightChange change);
void PresentPartyLoadChange(bool redistributed);

// Live operation: capture party rules, redistribute or defer, and present the result.
void RedistributePartyEncumbrance();
