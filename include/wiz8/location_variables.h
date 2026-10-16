#pragma once

struct W8Chunk;

namespace wiz8 {
class File;
}

int GetLocationVarIDByName(const char* name);
void SetTriggerVariableByName(const char* name, int value);
void CreateLocationVar(const char* name, int value); /* 0x00443DC0 */
int GetLocationVarValueByName(const char* name);     /* 0x004440D0 */
void SaveLocationVariables(wiz8::File& handle);
bool LoadLocationVariables(W8Chunk& chunk);
bool HasLocationVariables();
void ClearLocationVariables();
