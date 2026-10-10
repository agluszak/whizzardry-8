#include "surrender/srCore.h"
#include "surrender/srDynamicLibrary.h"
#include "surrender/srPlugin.h"

#include <SDL3/SDL_loadso.h>
#include <string>

/* Built-in components register directly; native plug-ins use SDL's loader. */
namespace {
std::string libraryName(const char* name)
{
    std::string filename(name);
    const size_t separator = filename.find_last_of("/\\");
    const size_t dot = filename.rfind('.');
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) {
        filename += SR_LIBRARY_EXTENSION;
    }
    return filename;
}

struct LibraryInitializer
{
    LibraryInitializer() { _srLibraryInit(); }
};
LibraryInitializer library_initializer;
} // namespace

srDynamicLibrary::Compatibility srDynamicLibrary::checkCompatibility(const char* name)
{
    if (name == 0) {
        return COMPATIBILITY_0;
    }
    const w8_ulong version = getVersion(name);
    if (version == 0) {
        return COMPATIBILITY_0;
    }
    return (version & 0xffffff00) == 0x012a0200 ? COMPATIBILITY_2 : COMPATIBILITY_1;
}

void* srDynamicLibrary::load(const char* name)
{
    if (name == 0) {
        return 0;
    }
    return SDL_LoadObject(libraryName(name).c_str());
}

int srDynamicLibrary::free(void* library)
{
    if (library == 0) {
        return 0;
    }
    SDL_UnloadObject(static_cast<SDL_SharedObject*>(library));
    return 1;
}

void* srDynamicLibrary::getFunction(void* library, const char* function_name)
{
    if (library == 0 || function_name == 0) {
        return 0;
    }
    return reinterpret_cast<void*>(
        SDL_LoadFunction(static_cast<SDL_SharedObject*>(library), function_name));
}

int srDynamicLibrary::testDependencies(const char*)
{
    return 1;
}

w8_ulong srDynamicLibrary::getVersion(const char* name)
{
    void* library = load(name);
    if (library == 0) {
        return 0;
    }
    srGetLibraryVersionCdeclFn get_library_version =
        reinterpret_cast<srGetLibraryVersionCdeclFn>(getFunction(library, "srGetLibraryVersion"));
    const w8_ulong version = get_library_version != 0 ? get_library_version() : 0;
    free(library);
    return version;
}
