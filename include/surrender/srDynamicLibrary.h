#pragma once


#define SR_DYNAMIC_LIBRARY_API

class SR_DYNAMIC_LIBRARY_API srDynamicLibrary {
public:
    enum Compatibility { COMPATIBILITY_0 = 0, COMPATIBILITY_1 = 1, COMPATIBILITY_2 = 2 };

    static Compatibility checkCompatibility(const char* name);
    static int free(void* library);
    static void* getFunction(void* library, const char* function_name);
    static w8_ulong getVersion(const char* name);
    static void* load(const char* name);
    static int testDependencies(const char* name);
};

static_assert(sizeof(srDynamicLibrary) == 0x01, "srDynamicLibrary_must_be_stateless");

#undef SR_DYNAMIC_LIBRARY_API
