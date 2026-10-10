#pragma once

#include "Types.h"

// SGP text is UTF-8, using char storage throughout.
inline UINT8* Wiz8ToSgpText(char* text)
{
    // reinterpret-ok: SGP byte-text ABI uses UINT8* for char storage
    return reinterpret_cast<UINT8*>(text);
}

inline UINT8* Wiz8ToSgpText(const char* text)
{
    // reinterpret-ok: SGP byte-text ABI uses mutable UINT8* for input text
    return reinterpret_cast<UINT8*>(const_cast<char*>(text));
}

inline char* Wiz8ToSgpTextBuffer(char* text) { return text; }
inline char* Wiz8ToSgpTextBuffer(const char* text) { return const_cast<char*>(text); }
