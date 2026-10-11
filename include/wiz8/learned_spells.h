#pragma once

struct W8Character;
struct W8LearnedSpellState;

/* 0x004F9600: original TU unresolved in the ItemManager/Magic boundary gap. */
void BuildLearnedSpellState(W8LearnedSpellState* scratch, W8Character* character);
