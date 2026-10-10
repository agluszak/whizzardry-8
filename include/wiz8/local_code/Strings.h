#pragma once



void ReleaseLocalizedStrings();
void LoadLocalizedStrings(const char* path);

/* Local Code\Strings.cpp owns the decoded game string table. */
extern char** gppStringList;
extern int giStringListLen;
